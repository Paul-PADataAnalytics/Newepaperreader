import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../main.dart';
import '../models/book_record.dart';
import 'book_storage_service.dart';

/// A single ebook file on the device's SD card.
class DeviceLibraryEntry {
  final String name;
  final String path;
  final int size;

  DeviceLibraryEntry({required this.name, required this.path, required this.size});

  factory DeviceLibraryEntry.fromJson(Map<String, dynamic> json) => DeviceLibraryEntry(
        name: (json['n'] ?? json['name'] ?? '') as String,
        path: (json['p'] ?? json['path'] ?? '') as String,
        size: (json['s'] ?? json['size'] ?? 0) as int,
      );

  /// Sub-folder below /books, or '' for books at the top level.
  String get folder {
    const root = '/books/';
    if (!path.startsWith(root)) return '';
    final rest = path.substring(root.length);
    final slash = rest.lastIndexOf('/');
    return slash < 0 ? '' : rest.substring(0, slash);
  }
}

/// Information the device reports when we connect.
class DeviceInfo {
  final String firmware;
  final int protocol;
  final int battery;
  final bool usb;
  final int? freeBytes;

  DeviceInfo({required this.firmware, required this.protocol, required this.battery, required this.usb, this.freeBytes});

  factory DeviceInfo.fromJson(Map<String, dynamic> json) => DeviceInfo(
        firmware: (json['fw'] ?? '?') as String,
        protocol: (json['proto'] ?? 1) as int,
        battery: (json['batt'] ?? 0) as int,
        usb: json['usb'] == true,
        freeBytes: json['free'] is num ? (json['free'] as num).toInt() : null,
      );
}

class DeviceException implements Exception {
  final String message;
  DeviceException(this.message);
  @override
  String toString() => message;
}

/// Thrown by [BleService.uploadFile] when the caller cancels.
class UploadCancelled implements Exception {}

/// Lets the UI cancel a running upload.
class UploadController {
  bool _cancelled = false;
  void cancel() => _cancelled = true;
  bool get isCancelled => _cancelled;
}

/// BLE link to the EPD-Reader (companion protocol v2, see
/// manual/09_power_and_companion.md in the firmware repository).
///
/// Requests carry a request id (`rid`); the device answers on the response
/// characteristic (notification, with a read as fallback). Large answers
/// are paged. Files are streamed as binary frames using write-with-response,
/// so the device can apply back-pressure while it writes to its SD card.
class BleService {
  static final BleService _instance = BleService._internal();
  factory BleService() => _instance;
  BleService._internal();

  static const String serviceUuid = "12345678-1234-1234-1234-123456789abc";
  static const String writeUuid = "12345678-1234-1234-1234-123456789001";
  static const String notifyUuid = "12345678-1234-1234-1234-123456789002";
  static const String responseUuid = "12345678-1234-1234-1234-123456789003";
  static const String deviceName = "EPD-Reader";
  static const String _prefsDeviceIdKey = 'last_connected_device_id';
  static const int _fileDataFrame = 0x01;

  BluetoothDevice? _device;
  BluetoothCharacteristic? _write;
  BluetoothCharacteristic? _notify;
  BluetoothCharacteristic? _response;
  StreamSubscription? _notifySub;
  StreamSubscription? _responseSub;
  StreamSubscription? _stateSub;

  int _nextRid = 1;
  final Map<int, Completer<Map<String, dynamic>>> _pending = {};
  Future<void> _queue = Future.value();

  /// Connection state for widgets to listen to.
  final ValueNotifier<bool> connected = ValueNotifier(false);

  /// Latest HELLO answer from the device (null until connected).
  final ValueNotifier<DeviceInfo?> deviceInfo = ValueNotifier(null);

  /// Called whenever the device pushes a single book's progress.
  void Function(BookRecord book)? onSingleBookUpdated;

  bool get isConnected => _device?.isConnected == true && _write != null;

  // ---------------------------------------------------------------------
  // Discovery & connection
  // ---------------------------------------------------------------------

  Future<String?> _getStoredDeviceId() async =>
      (await SharedPreferences.getInstance()).getString(_prefsDeviceIdKey);

  Future<void> _storeDeviceId(String id) async =>
      (await SharedPreferences.getInstance()).setString(_prefsDeviceIdKey, id);

  Future<void> _clearStoredDeviceId() async =>
      (await SharedPreferences.getInstance()).remove(_prefsDeviceIdKey);

  Future<List<BluetoothDevice>> scan({Duration timeout = const Duration(seconds: 5)}) async {
    if (!await isBluetoothReady()) {
      throw DeviceException('Bluetooth is off. Please turn it on.');
    }
    final found = <BluetoothDevice>[];
    final sub = FlutterBluePlus.scanResults.listen((results) {
      for (final r in results) {
        final name = r.device.advName.isNotEmpty ? r.device.advName : r.advertisementData.advName;
        final hasService = r.advertisementData.serviceUuids.any((g) => g.str128 == serviceUuid);
        if ((name == deviceName || hasService) && !found.any((d) => d.remoteId == r.device.remoteId)) {
          found.add(r.device);
        }
      }
    });
    try {
      await FlutterBluePlus.startScan(timeout: timeout, androidScanMode: AndroidScanMode.lowLatency);
      await FlutterBluePlus.isScanning.where((s) => s == false).first;
    } finally {
      await sub.cancel();
    }
    return found;
  }

  Future<void> connect(BluetoothDevice device) async {
    await device.connect(timeout: const Duration(seconds: 15), mtu: 512);
    _device = device;

    await _discover(device);
    if (_write == null || _response == null) {
      // A phone that saw an older firmware may have cached its GATT table.
      try {
        await device.clearGattCache();
      } catch (_) {}
      await _discover(device);
    }
    if (_write == null || _response == null) {
      await disconnect(forget: false);
      throw DeviceException('This EPD-Reader firmware is too old for this app. Please update the device.');
    }

    _stateSub?.cancel();
    _stateSub = device.connectionState.listen((s) {
      if (s == BluetoothConnectionState.disconnected) _onDisconnected();
    });

    _responseSub?.cancel();
    await _response!.setNotifyValue(true);
    _responseSub = _response!.onValueReceived.listen(_handleResponseBytes);

    if (_notify != null) {
      _notifySub?.cancel();
      try {
        await _notify!.setNotifyValue(true);
        _notifySub = _notify!.onValueReceived.listen(_handleDeltaBytes);
      } catch (_) {}
    }

    await _storeDeviceId(device.remoteId.str);
    connected.value = true;

    try {
      deviceInfo.value = DeviceInfo.fromJson(await request({'t': 'HELLO'}));
    } catch (_) {}
    try {
      await sendTimeSync();
    } catch (_) {}
  }

  Future<void> _discover(BluetoothDevice device) async {
    _write = _notify = _response = null;
    final services = await device.discoverServices();
    for (final s in services) {
      if (s.uuid.str128 != serviceUuid) continue;
      for (final c in s.characteristics) {
        switch (c.uuid.str128) {
          case writeUuid:
            _write = c;
            break;
          case notifyUuid:
            _notify = c;
            break;
          case responseUuid:
            _response = c;
            break;
        }
      }
    }
  }

  void _onDisconnected() {
    _write = _notify = _response = null;
    _responseSub?.cancel();
    _notifySub?.cancel();
    _responseSub = _notifySub = null;
    for (final c in _pending.values) {
      if (!c.isCompleted) c.completeError(DeviceException('Device disconnected'));
    }
    _pending.clear();
    connected.value = false;
    deviceInfo.value = null;
  }

  Future<bool> reconnectToLastDevice() async {
    final lastId = await _getStoredDeviceId();
    if (lastId == null || lastId.isEmpty) return false;
    if (!await isBluetoothReady()) return false;
    try {
      await connect(BluetoothDevice.fromId(lastId));
      return true;
    } catch (_) {
      return false;
    }
  }

  /// Reconnects to the last device, or scans for one.
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
    } catch (_) {}
    return false;
  }

  Future<void> disconnect({bool forget = true}) async {
    stopBackgroundSync();
    final d = _device;
    _device = null;
    _onDisconnected();
    _stateSub?.cancel();
    _stateSub = null;
    try {
      await d?.disconnect();
    } catch (_) {}
    if (forget) await _clearStoredDeviceId();
  }

  // ---------------------------------------------------------------------
  // Request / response
  // ---------------------------------------------------------------------

  void _handleResponseBytes(List<int> bytes) {
    final msg = _decode(bytes);
    if (msg == null) return;
    final rid = msg['rid'];
    if (rid is int) {
      final c = _pending.remove(rid);
      if (c != null && !c.isCompleted) c.complete(msg);
    }
  }

  void _handleDeltaBytes(List<int> bytes) {
    final msg = _decode(bytes);
    if (msg == null || msg['t'] != 'BOOK') return;
    try {
      final book = BookRecord.fromJson(msg);
      BookStorageService().addOrUpdateBook(book);
      onSingleBookUpdated?.call(book);
    } catch (_) {}
  }

  Map<String, dynamic>? _decode(List<int> bytes) {
    if (bytes.isEmpty) return null;
    try {
      final v = jsonDecode(utf8.decode(bytes));
      return v is Map<String, dynamic> ? v : null;
    } catch (_) {
      return null; // e.g. a notification truncated by a small MTU
    }
  }

  /// Runs requests one at a time: the device has a single response slot.
  Future<T> _serialized<T>(Future<T> Function() fn) {
    final result = _queue.then((_) => fn());
    _queue = result.then((_) {}, onError: (_) {});
    return result;
  }

  Future<void> _writeJson(Map<String, dynamic> json) async {
    final w = _write;
    if (w == null) throw DeviceException('Not connected');
    await w.write(utf8.encode(jsonEncode(json)));
  }

  /// Sends a command and waits for the matching response.
  Future<Map<String, dynamic>> request(Map<String, dynamic> msg,
      {Duration timeout = const Duration(seconds: 15)}) {
    return _serialized(() => _requestNow(msg, timeout));
  }

  Future<Map<String, dynamic>> _requestNow(Map<String, dynamic> msg, Duration timeout) async {
    if (!isConnected) throw DeviceException('Not connected');
    final rid = _nextRid++;
    final completer = Completer<Map<String, dynamic>>();
    _pending[rid] = completer;
    await _writeJson({...msg, 'rid': rid});

    final deadline = DateTime.now().add(timeout);
    while (true) {
      try {
        return await completer.future.timeout(const Duration(milliseconds: 500));
      } on TimeoutException {
        // Notification may have been missed or truncated: read the slot.
        try {
          final r = _decode(await _response!.read());
          if (r != null && r['rid'] == rid) {
            _pending.remove(rid);
            return r;
          }
        } catch (_) {}
        if (DateTime.now().isAfter(deadline)) {
          _pending.remove(rid);
          throw DeviceException('The device did not answer (${msg['t']})');
        }
      }
    }
  }

  /// Fetches every page of a paged command and returns all items.
  Future<List<Map<String, dynamic>>> _paged(Map<String, dynamic> msg) async {
    final items = <Map<String, dynamic>>[];
    int off = 0;
    while (off >= 0) {
      final r = await request({...msg, 'off': off}, timeout: const Duration(seconds: 30));
      for (final i in (r['items'] as List<dynamic>? ?? [])) {
        items.add(i as Map<String, dynamic>);
      }
      off = (r['next'] ?? -1) as int;
      msg = {...msg}..remove('refresh');
    }
    return items;
  }

  // ---------------------------------------------------------------------
  // Commands
  // ---------------------------------------------------------------------

  Future<void> sendTimeSync() async {
    final now = DateTime.now();
    await request({
      't': 'TIME',
      'val': now.millisecondsSinceEpoch ~/ 1000,
      'tz': now.timeZoneOffset.inMinutes,
    });
  }

  Future<void> sendBookProgress(BookRecord book) async {
    await request({
      't': 'BOOK',
      'isbn': book.isbn,
      'title': book.title,
      'author': book.author,
      'genre': book.genre,
      'page': book.currentPage,
      'total': book.totalPages,
      'ts': book.lastUpdated.millisecondsSinceEpoch ~/ 1000,
    });
  }

  Future<void> syncBookIfConnected(BookRecord book) async {
    if (!isConnected) return;
    await sendBookProgress(book);
  }

  /// The device's bookmark database (books tracked in the eBookmark app).
  Future<List<BookRecord>> fetchDeviceBookmarks() async {
    final items = await _paged({'t': 'GET_BOOKMARKS'});
    return items.map(BookRecord.fromJson).toList();
  }

  /// Books in the device's /books folder (recursively).
  Future<List<DeviceLibraryEntry>> getLibrary({bool refresh = false}) async {
    final items = await _paged({'t': 'GET_LIBRARY', if (refresh) 'refresh': true});
    final books = items.map(DeviceLibraryEntry.fromJson).toList();
    books.sort((a, b) => a.path.toLowerCase().compareTo(b.path.toLowerCase()));
    return books;
  }

  Future<void> deleteLibraryFile(DeviceLibraryEntry book) async {
    final r = await request({'t': 'DELETE_FILE', 'path': book.path});
    if (r['ok'] != true) throw DeviceException((r['error'] ?? 'Delete failed') as String);
  }

  /// Streams [file] to the device's /books (or /images) folder.
  /// [onProgress] receives 0..1. Throws [UploadCancelled] if cancelled.
  Future<void> uploadFile(File file, String fileName,
      {String folder = 'books', void Function(double progress)? onProgress, UploadController? controller}) {
    return _serialized(() => _upload(file, fileName, folder, onProgress, controller));
  }

  Future<void> _upload(File file, String fileName, String folder, void Function(double)? onProgress,
      UploadController? controller) async {
    final w = _write;
    final d = _device;
    if (w == null || d == null) throw DeviceException('Not connected');
    final Uint8List data = await file.readAsBytes();

    final ready = await _requestNow({'t': 'FILE_BEGIN', 'name': fileName, 'size': data.length, 'dir': folder},
        const Duration(seconds: 15));
    if (ready['ok'] != true) throw DeviceException((ready['error'] ?? 'Device refused the file') as String);

    try {
      await d.requestConnectionPriority(connectionPriorityRequest: ConnectionPriority.high);
    } catch (_) {}

    // ATT payload is MTU - 3; 5 bytes of frame header.
    final chunk = ((d.mtuNow - 3 - 5).clamp(15, 495)).toInt();
    try {
      for (int off = 0; off < data.length; off += chunk) {
        if (controller?.isCancelled == true) {
          try {
            await _writeJson({'t': 'FILE_ABORT'});
          } catch (_) {}
          throw UploadCancelled();
        }
        final end = (off + chunk < data.length) ? off + chunk : data.length;
        final frame = Uint8List(5 + end - off);
        frame[0] = _fileDataFrame;
        frame.buffer.asByteData().setUint32(1, off, Endian.little);
        frame.setRange(5, frame.length, data, off);
        await w.write(frame);
        onProgress?.call(end / data.length);
      }
      if (data.isEmpty) onProgress?.call(1.0);
      final result = await _requestNow({'t': 'FILE_END', 'size': data.length}, const Duration(seconds: 30));
      if (result['ok'] != true) throw DeviceException((result['error'] ?? 'Upload failed') as String);
    } finally {
      try {
        await d.requestConnectionPriority(connectionPriorityRequest: ConnectionPriority.balanced);
      } catch (_) {}
    }
  }

  // ---------------------------------------------------------------------
  // Background bookmark sync
  // ---------------------------------------------------------------------

  Timer? _syncTimer;

  /// Periodically pulls the device's bookmarks. The device does not count
  /// these polls as user activity, so they never keep it awake.
  void startBackgroundSync(
      {required void Function(List<BookRecord> books) onBookmarksChanged,
      Duration interval = const Duration(seconds: 30)}) {
    stopBackgroundSync();
    Future<void> tick() async {
      if (!isConnected) return;
      try {
        onBookmarksChanged(await fetchDeviceBookmarks());
      } catch (_) {}
    }

    tick();
    _syncTimer = Timer.periodic(interval, (_) => tick());
  }

  void stopBackgroundSync() {
    _syncTimer?.cancel();
    _syncTimer = null;
  }
}
