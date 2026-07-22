import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import '../main.dart';
import '../services/ble_service.dart';
import '../services/book_storage_service.dart';
import '../models/book_record.dart';
import 'dart:convert';

class BleSyncScreen extends StatefulWidget {
  const BleSyncScreen({super.key});

  @override
  State<BleSyncScreen> createState() => _BleSyncScreenState();
}

class _BleSyncScreenState extends State<BleSyncScreen> {
  final BleService _ble = BleService();
  final BookStorageService _storage = BookStorageService();
  List<BluetoothDevice> _devices = [];
  bool _isScanning = false;

  void _showSnack(String message) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
  }

  Future<bool> _ensureReady() async {
    // Verify runtime permissions first.
    final scanStatus = await Permission.bluetoothScan.status;
    final connectStatus = await Permission.bluetoothConnect.status;
    if (!scanStatus.isGranted || !connectStatus.isGranted) {
      _showSnack('Bluetooth permissions are required. Please grant them in app settings.');
      return false;
    }

    if (!await isBluetoothReady()) {
      _showSnack('Bluetooth is off. Please enable Bluetooth and try again.');
      return false;
    }
    return true;
  }

  void _startScan() async {
    if (!await _ensureReady()) return;

    setState(() {
      _isScanning = true;
      _devices = [];
    });
    try {
      final devices = await _ble.scan();
      if (!mounted) return;
      setState(() => _devices = devices);
    } catch (e) {
      _showSnack('Scan failed: $e');
    } finally {
      if (mounted) setState(() => _isScanning = false);
    }
  }

  void _connect(BluetoothDevice device) async {
    try {
      await _ble.connect(device);
      if (!mounted) return;
      _startBackgroundSync();
      setState(() {});
      _showSnack('Connected!');
    } catch (e) {
      _showSnack('Connect failed: $e');
    }
  }

  void _disconnect() {
    _ble.disconnect();
    setState(() {});
  }

  void _syncAll() async {
    final books = await _storage.loadBooks();
    for (var book in books) {
      await _ble.sendBookProgress(book);
      await Future.delayed(const Duration(milliseconds: 500));
    }
    if (!mounted) return;
    _showSnack('Synced all books');
  }

  void _readProgress() async {
    final jsonStr = await _ble.readDeviceProgress();
    if (!mounted) return;
    if (jsonStr != null && jsonStr.isNotEmpty) {
      try {
        final List<dynamic> jsonList = jsonDecode(jsonStr);
        for (var item in jsonList) {
          final deviceBook = BookRecord.fromJson(item);
          final localBooks = await _storage.loadBooks();
          final localIndex = localBooks.indexWhere((b) => b.isbn == deviceBook.isbn);
          if (localIndex < 0 || deviceBook.lastUpdated.isAfter(localBooks[localIndex].lastUpdated)) {
            await _storage.addOrUpdateBook(deviceBook);
          }
        }
        if (mounted) {
          _showSnack('Successfully synced from device');
        }
      } catch (e) {
        if (mounted) {
          _showSnack('Sync failed: $e');
        }
      }
    }
  }

  @override
  void initState() {
    super.initState();
    _autoConnect();
  }

  Future<void> _autoConnect() async {
    if (!await _ensureReady()) return;
    final ok = await _ble.autoConnect();
    if (!mounted) return;
    if (ok) {
      _startBackgroundSync();
      setState(() {});
      _showSnack('Reconnected to EPD-Reader');
    }
  }

  void _startBackgroundSync() {
    _ble.startBackgroundSync(onBookmarksChanged: (books) async {
      final localBooks = await _storage.loadBooks();
      for (var deviceBook in books) {
        final localIndex = localBooks.indexWhere((b) => b.isbn == deviceBook.isbn);
        if (localIndex < 0 || deviceBook.lastUpdated.isAfter(localBooks[localIndex].lastUpdated)) {
          await _storage.addOrUpdateBook(deviceBook);
        }
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('BLE Sync')),
      body: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          children: [
            if (_ble.isConnected) ...[
              const Icon(Icons.bluetooth_connected, size: 64, color: Color(0xFF00B4D8)),
              const SizedBox(height: 16),
              const Text('Connected to EPD-Reader', style: TextStyle(fontSize: 18)),
              const SizedBox(height: 32),
              ElevatedButton(
                onPressed: _syncAll,
                style: ElevatedButton.styleFrom(
                  minimumSize: const Size(double.infinity, 50),
                  backgroundColor: const Color(0xFF00B4D8),
                ),
                child: const Text('Sync All Books', style: TextStyle(color: Colors.white)),
              ),
              const SizedBox(height: 16),
              ElevatedButton(
                onPressed: _readProgress,
                style: ElevatedButton.styleFrom(
                  minimumSize: const Size(double.infinity, 50),
                  backgroundColor: const Color(0xFF1B263B),
                ),
                child: const Text('Read Device Progress', style: TextStyle(color: Colors.white)),
              ),
              const SizedBox(height: 16),
              ElevatedButton(
                onPressed: () async {
                  try {
                    await _ble.sendTimeSync();
                    _showSnack('Date & Time synced to device');
                  } catch (e) {
                    _showSnack('Time sync failed: $e');
                  }
                },
                style: ElevatedButton.styleFrom(
                  minimumSize: const Size(double.infinity, 50),
                  backgroundColor: const Color(0xFF2A9D8F),
                ),
                child: const Text('Sync Device Date & Time', style: TextStyle(color: Colors.white)),
              ),
              const Spacer(),
              TextButton(
                onPressed: _disconnect,
                child: const Text('Disconnect', style: TextStyle(color: Colors.red)),
              ),
            ] else ...[
              ElevatedButton(
                onPressed: _isScanning ? null : _startScan,
                child: Text(_isScanning ? 'Scanning...' : 'Scan for Devices'),
              ),
              const SizedBox(height: 8),
              ElevatedButton(
                onPressed: _isScanning ? null : _autoConnect,
                style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF1B263B)),
                child: const Text('Reconnect to Last Device', style: TextStyle(color: Colors.white)),
              ),
              const SizedBox(height: 16),
              Expanded(
                child: ListView.builder(
                  itemCount: _devices.length,
                  itemBuilder: (context, index) {
                    final d = _devices[index];
                    return ListTile(
                      title: Text(d.advName.isNotEmpty ? d.advName : 'Unknown Device'),
                      subtitle: Text(d.remoteId.toString()),
                      trailing: ElevatedButton(
                        onPressed: () => _connect(d),
                        child: const Text('Connect'),
                      ),
                    );
                  },
                ),
              ),
            ]
          ],
        ),
      ),
    );
  }
}
