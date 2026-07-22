import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../main.dart';
import '../models/book_record.dart';

class BleService {
  static final BleService _instance = BleService._internal();
  factory BleService() => _instance;
  BleService._internal();

  BluetoothDevice? _device;
  BluetoothCharacteristic? _writeCharacteristic;
  BluetoothCharacteristic? _readCharacteristic;

  /// Reports true only when we have a device that is currently connected.
  bool get isConnected {
    final device = _device;
    if (device == null) return false;
    return device.isConnected;
  }

  static const String serviceUuid = "12345678-1234-1234-1234-123456789abc";
  static const String writeUuid = "12345678-1234-1234-1234-123456789001";
  static const String readUuid = "12345678-1234-1234-1234-123456789002";

  static const String _prefsDeviceIdKey = 'last_connected_device_id';

  // Safe ATT MTU payload size for writes without negotiation.
  static const int _mtuPayload = 20;

  /// Returns the last successfully connected device ID, if any.
  Future<String?> _getStoredDeviceId() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getString(_prefsDeviceIdKey);
  }

  /// Stores the device ID so future app launches can reconnect automatically.
  Future<void> _storeDeviceId(String deviceId) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_prefsDeviceIdKey, deviceId);
  }

  /// Clears the stored device ID (e.g. after explicit disconnect).
  Future<void> _clearStoredDeviceId() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_prefsDeviceIdKey);
  }

  Future<List<BluetoothDevice>> scan() async {
    if (!await isBluetoothReady()) {
      throw Exception('Bluetooth is off or not supported. Please enable Bluetooth.');
    }

    List<BluetoothDevice> devices = [];
    var subscription = FlutterBluePlus.scanResults.listen((results) {
      for (ScanResult r in results) {
        if (r.device.advName == 'EPD-Reader' && !devices.any((d) => d.remoteId == r.device.remoteId)) {
          devices.add(r.device);
        }
      }
    });

    try {
      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 4));
      await FlutterBluePlus.isScanning.where((val) => val == false).first;
    } finally {
      await subscription.cancel();
    }
    return devices;
  }

  Future<void> connect(BluetoothDevice device) async {
    await device.connect();
    _device = device;

    List<BluetoothService> services = await device.discoverServices();
    for (BluetoothService service in services) {
      if (service.uuid.toString() == serviceUuid) {
        for (BluetoothCharacteristic characteristic in service.characteristics) {
          if (characteristic.uuid.toString() == writeUuid) {
            _writeCharacteristic = characteristic;
          } else if (characteristic.uuid.toString() == readUuid) {
            _readCharacteristic = characteristic;
          }
        }
      }
    }

    if (_writeCharacteristic == null || _readCharacteristic == null) {
      disconnect();
      throw Exception('Required BLE characteristics not found on EPD-Reader.');
    }

    await _storeDeviceId(device.remoteId.toString());

    // Sync Android's clock to the device immediately on connect so that
    // last-change-wins bookmark comparisons use the correct time.
    try {
      await sendTimeSync();
    } catch (e) {
      // Don't fail the connection if the time sync write doesn't land.
    }
  }

  /// Attempts to reconnect to the last known device without scanning.
  Future<bool> reconnectToLastDevice() async {
    final lastId = await _getStoredDeviceId();
    if (lastId == null || lastId.isEmpty) return false;

    if (!await isBluetoothReady()) return false;

    try {
      final device = BluetoothDevice.fromId(lastId);
      await connect(device);
      return true;
    } catch (e) {
      // Reconnection failed; clear stored ID so we don't keep retrying stale IDs.
      await _clearStoredDeviceId();
      return false;
    }
  }

  /// Tries to reconnect to the last known device; if that fails, scans for
  /// 'EPD-Reader' and connects to the first one found.
  Future<bool> autoConnect() async {
    if (isConnected) return true;
    if (!await isBluetoothReady()) return false;

    if (await reconnectToLastDevice()) return true;

    try {
      final devices = await scan();
      if (devices.isNotEmpty) {
        await connect(devices.first);
        return true;
      }
    } catch (e) {
      // Fall through to false.
    }
    return false;
  }

  void disconnect() {
    stopBackgroundSync();
    _device?.disconnect();
    _device = null;
    _writeCharacteristic = null;
    _readCharacteristic = null;
    _clearStoredDeviceId();
  }

  Future<void> _writeJson(Map<String, dynamic> json) async {
    if (_writeCharacteristic == null) return;
    final bytes = utf8.encode(jsonEncode(json));
    await _writeCharacteristic!.write(bytes);
  }

  Future<void> sendBookProgress(BookRecord book) async {
    await _writeJson({
      "t": "BOOK",
      "isbn": book.isbn,
      "title": book.title,
      "author": book.author,
      "genre": book.genre,
      "page": book.currentPage,
      "total": book.totalPages,
      "ts": DateTime.now().millisecondsSinceEpoch ~/ 1000
    });
  }

  /// Sends the current Android date/time to the device so its RTC is correct.
  Future<void> sendTimeSync() async {
    final now = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    await _writeJson({"t": "TIME", "val": now});
  }

  /// Asks the device to refresh the read characteristic with the current
  /// ebookmarks database. Call `readDeviceProgress` afterwards to fetch it.
  Future<void> requestDeviceBookmarks() async {
    await _writeJson({"t": "GET_BOOKMARKS"});
  }

  Future<String?> readDeviceProgress() async {
    if (_readCharacteristic == null) return null;
    final value = await _readCharacteristic!.read();
    if (value.isEmpty) return null;
    return utf8.decode(value);
  }

  /// Uploads a file to the device over BLE in small chunks.
  /// The firmware reassembles chunks and writes them to `/books/&lt;fileName&gt;`.
  Future<void> uploadFile(File file, String fileName) async {
    if (_writeCharacteristic == null) throw Exception('Not connected');

    final bytes = await file.readAsBytes();
    final totalChunks = (bytes.length / _mtuPayload).ceil();

    // Send header so the firmware knows a transfer is starting.
    await _writeJson({
      "t": "FILE",
      "name": fileName,
      "size": bytes.length,
      "total": totalChunks,
      "chunk": -1,
      "data": "",
    });

    for (int i = 0; i < totalChunks; i++) {
      final start = i * _mtuPayload;
      final end = (start + _mtuPayload < bytes.length) ? start + _mtuPayload : bytes.length;
      final chunk = bytes.sublist(start, end);

      await _writeJson({
        "t": "FILE",
        "name": fileName,
        "chunk": i,
        "total": totalChunks,
        "data": base64Encode(chunk),
      });

      // Small delay to avoid flooding the BLE stack.
      await Future.delayed(const Duration(milliseconds: 30));
    }
  }

  Timer? _syncTimer;

  /// Starts a background timer that periodically reads the device's bookmark
  /// database and calls [onBookmarksChanged] when a newer state is detected.
  void startBackgroundSync({required void Function(List<BookRecord> books) onBookmarksChanged,
                            Duration interval = const Duration(seconds: 5)}) {
    stopBackgroundSync();
    _syncTimer = Timer.periodic(interval, (_) async {
      if (!isConnected) return;
      try {
        final jsonStr = await readDeviceProgress();
        if (jsonStr == null || jsonStr.isEmpty) return;
        final List<dynamic> jsonList = jsonDecode(jsonStr);
        final books = jsonList.map((json) => BookRecord.fromJson(json)).toList();
        onBookmarksChanged(books);
      } catch (e) {
        // Ignore transient read errors.
      }
    });
  }

  void stopBackgroundSync() {
    _syncTimer?.cancel();
    _syncTimer = null;
  }

  /// Sends a single book's progress to the device if currently connected.
  Future<void> syncBookIfConnected(BookRecord book) async {
    if (!isConnected) return;
    await sendBookProgress(book);
  }
}
