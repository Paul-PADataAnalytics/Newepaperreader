import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

import '../main.dart';
import '../models/book_record.dart';
import '../services/ble_service.dart';
import '../services/book_storage_service.dart';

/// Connection to the EPD-Reader: connect, device status and sync actions.
class BleSyncScreen extends StatefulWidget {
  const BleSyncScreen({super.key});

  @override
  State<BleSyncScreen> createState() => _BleSyncScreenState();
}

class _BleSyncScreenState extends State<BleSyncScreen> {
  final BleService _ble = BleService();
  final BookStorageService _storage = BookStorageService();
  List<BluetoothDevice> _devices = [];
  bool _busy = false;
  String _busyLabel = '';

  @override
  void initState() {
    super.initState();
    _ble.connected.addListener(_rebuild);
    _ble.deviceInfo.addListener(_rebuild);
    if (!_ble.isConnected) _autoConnect();
  }

  @override
  void dispose() {
    _ble.connected.removeListener(_rebuild);
    _ble.deviceInfo.removeListener(_rebuild);
    super.dispose();
  }

  void _rebuild() {
    if (mounted) setState(() {});
  }

  void _showSnack(String message) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
  }

  Future<bool> _ensureReady() async {
    final scan = await Permission.bluetoothScan.status;
    final connect = await Permission.bluetoothConnect.status;
    if (!scan.isGranted || !connect.isGranted) {
      _showSnack('Bluetooth permissions are required. Please grant them in app settings.');
      return false;
    }
    if (!await isBluetoothReady()) {
      _showSnack('Bluetooth is off. Please turn it on and try again.');
      return false;
    }
    return true;
  }

  Future<void> _run(String label, Future<void> Function() action) async {
    setState(() {
      _busy = true;
      _busyLabel = label;
    });
    try {
      await action();
    } catch (e) {
      _showSnack('$e');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _autoConnect() async {
    if (!await _ensureReady()) return;
    await _run('Looking for your EPD-Reader...', () async {
      if (await _ble.autoConnect()) {
        _startBackgroundSync();
      } else {
        final devices = await _ble.scan();
        if (mounted) setState(() => _devices = devices);
      }
    });
  }

  Future<void> _scan() async {
    if (!await _ensureReady()) return;
    await _run('Scanning...', () async {
      final devices = await _ble.scan();
      if (mounted) setState(() => _devices = devices);
      if (devices.isEmpty) _showSnack('No EPD-Reader found. Wake it up and make sure Bluetooth is on in its Settings.');
    });
  }

  Future<void> _connect(BluetoothDevice device) async {
    await _run('Connecting...', () async {
      await _ble.connect(device);
      _startBackgroundSync();
      _showSnack('Connected');
    });
  }

  void _startBackgroundSync() {
    _ble.startBackgroundSync(onBookmarksChanged: _mergeFromDevice);
  }

  Future<int> _mergeFromDevice(List<BookRecord> deviceBooks) async {
    final local = await _storage.loadBooks();
    int changed = 0;
    for (final d in deviceBooks) {
      final i = local.indexWhere((b) => b.isbn == d.isbn);
      if (i < 0 || d.lastUpdated.isAfter(local[i].lastUpdated)) {
        await _storage.addOrUpdateBook(d);
        changed++;
      }
    }
    return changed;
  }

  Future<void> _syncBookmarks() async {
    await _run('Syncing bookmarks...', () async {
      final pulled = await _mergeFromDevice(await _ble.fetchDeviceBookmarks());
      final books = await _storage.loadBooks();
      for (final b in books) {
        await _ble.sendBookProgress(b);
      }
      _showSnack('Bookmarks synced (${books.length} sent, $pulled updated from device)');
    });
  }

  @override
  Widget build(BuildContext context) {
    final connected = _ble.isConnected;
    final info = _ble.deviceInfo.value;
    return Scaffold(
      appBar: AppBar(title: const Text('Device')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Card(
            child: Padding(
              padding: const EdgeInsets.all(20),
              child: Row(
                children: [
                  Icon(connected ? Icons.bluetooth_connected : Icons.bluetooth_disabled,
                      size: 48, color: connected ? const Color(0xFF00B4D8) : Colors.white38),
                  const SizedBox(width: 16),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(connected ? 'EPD-Reader connected' : 'Not connected',
                            style: const TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
                        const SizedBox(height: 4),
                        Text(
                          connected
                              ? (info == null
                                  ? 'Reading device info...'
                                  : 'Firmware ${info.firmware}  ·  Battery ${info.battery}%${info.usb ? ' (USB)' : ''}')
                              : 'Wake the reader with its side button to connect.',
                          style: const TextStyle(color: Colors.white60),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
          ),
          if (_busy) ...[
            const SizedBox(height: 12),
            const LinearProgressIndicator(),
            const SizedBox(height: 8),
            Text(_busyLabel, textAlign: TextAlign.center, style: const TextStyle(color: Colors.white60)),
          ],
          const SizedBox(height: 16),
          if (connected) ...[
            FilledButton.icon(
              onPressed: _busy ? null : _syncBookmarks,
              icon: const Icon(Icons.sync),
              label: const Text('Sync bookmarks'),
              style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(52)),
            ),
            const SizedBox(height: 12),
            OutlinedButton.icon(
              onPressed: _busy ? null : () => _run('Setting the clock...', () async {
                await _ble.sendTimeSync();
                _showSnack('Device date and time updated');
              }),
              icon: const Icon(Icons.schedule),
              label: const Text('Set device clock'),
              style: OutlinedButton.styleFrom(minimumSize: const Size.fromHeight(52)),
            ),
            const SizedBox(height: 24),
            TextButton(
              onPressed: _busy ? null : () => _ble.disconnect(),
              child: const Text('Disconnect', style: TextStyle(color: Colors.redAccent)),
            ),
          ] else ...[
            FilledButton.icon(
              onPressed: _busy ? null : _autoConnect,
              icon: const Icon(Icons.bluetooth_searching),
              label: const Text('Connect'),
              style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(52)),
            ),
            const SizedBox(height: 12),
            OutlinedButton(
              onPressed: _busy ? null : _scan,
              style: OutlinedButton.styleFrom(minimumSize: const Size.fromHeight(52)),
              child: const Text('Scan for devices'),
            ),
            const SizedBox(height: 12),
            for (final d in _devices)
              Card(
                child: ListTile(
                  leading: const Icon(Icons.chrome_reader_mode),
                  title: Text(d.advName.isNotEmpty ? d.advName : 'EPD-Reader'),
                  subtitle: Text(d.remoteId.str),
                  trailing: FilledButton(onPressed: _busy ? null : () => _connect(d), child: const Text('Connect')),
                ),
              ),
          ],
        ],
      ),
    );
  }
}
