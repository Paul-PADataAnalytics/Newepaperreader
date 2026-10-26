import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';

import '../services/ble_service.dart';
import '../widgets/upload_dialog.dart';

/// The books on the connected EPD-Reader's SD card: browse, send new books
/// from the phone and delete them.
class DeviceLibraryScreen extends StatefulWidget {
  const DeviceLibraryScreen({super.key});

  @override
  State<DeviceLibraryScreen> createState() => _DeviceLibraryScreenState();
}

class _DeviceLibraryScreenState extends State<DeviceLibraryScreen> {
  final BleService _ble = BleService();
  List<DeviceLibraryEntry> _books = [];
  bool _isLoading = false;
  bool _loadedOnce = false;
  String? _error;

  static const _bookExtensions = ['epub', 'txt', 'md', 'rtf'];

  @override
  void initState() {
    super.initState();
    _ble.connected.addListener(_onConnectionChanged);
    if (_ble.isConnected) _refresh();
  }

  @override
  void dispose() {
    _ble.connected.removeListener(_onConnectionChanged);
    super.dispose();
  }

  void _onConnectionChanged() {
    if (!mounted) return;
    setState(() {});
    if (_ble.isConnected && !_loadedOnce) _refresh();
  }

  Future<void> _refresh({bool rescan = false}) async {
    if (!_ble.isConnected) return;
    setState(() {
      _isLoading = true;
      _error = null;
    });
    try {
      final books = await _ble.getLibrary(refresh: rescan);
      if (!mounted) return;
      setState(() {
        _books = books;
        _loadedOnce = true;
      });
    } catch (e) {
      if (mounted) setState(() => _error = 'Could not read the device library: $e');
    } finally {
      if (mounted) setState(() => _isLoading = false);
    }
  }

  Future<void> _connect() async {
    setState(() => _isLoading = true);
    final ok = await _ble.autoConnect();
    if (!mounted) return;
    setState(() => _isLoading = false);
    if (!ok) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('EPD-Reader not found. Make sure it is awake and Bluetooth is on (Settings > Bluetooth).')),
      );
    }
  }

  Future<void> _pickAndUpload() async {
    final result = await FilePicker.platform.pickFiles(
      type: FileType.custom,
      allowedExtensions: _bookExtensions,
      allowMultiple: true,
    );
    if (result == null || !mounted) return;
    int sent = 0;
    for (final f in result.files) {
      if (f.path == null) continue;
      final ok = await uploadWithProgress(context, File(f.path!), f.name);
      if (!mounted) return;
      if (!ok) break;
      sent++;
    }
    if (sent > 0) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(sent == 1 ? 'Book sent to your EPD-Reader' : '$sent books sent to your EPD-Reader')),
      );
      _refresh();
    }
  }

  Future<void> _confirmDelete(DeviceLibraryEntry book) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Delete book?'),
        content: Text('Remove "${book.name}" from the device? This cannot be undone.'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(context, false), child: const Text('Cancel')),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            style: FilledButton.styleFrom(backgroundColor: Colors.redAccent),
            child: const Text('Delete'),
          ),
        ],
      ),
    );
    if (confirmed != true || !mounted) return;

    try {
      await _ble.deleteLibraryFile(book);
      if (!mounted) return;
      setState(() => _books.remove(book));
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Deleted ${book.name}')));
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Could not delete ${book.name}: $e')));
    }
  }

  String _formatSize(int bytes) {
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(0)} KB';
    return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
  }

  String _displayTitle(DeviceLibraryEntry b) {
    final dot = b.name.lastIndexOf('.');
    return (dot > 0 ? b.name.substring(0, dot) : b.name).replaceAll('_', ' ');
  }

  @override
  Widget build(BuildContext context) {
    final connected = _ble.isConnected;
    return Scaffold(
      appBar: AppBar(
        title: const Text('Device Library'),
        actions: [
          if (connected)
            IconButton(
              tooltip: 'Rescan SD card',
              icon: const Icon(Icons.refresh),
              onPressed: _isLoading ? null : () => _refresh(rescan: true),
            ),
        ],
      ),
      floatingActionButton: connected
          ? FloatingActionButton.extended(
              onPressed: _isLoading ? null : _pickAndUpload,
              icon: const Icon(Icons.upload_file),
              label: const Text('Send book'),
            )
          : null,
      body: !connected ? _buildDisconnected() : _buildList(),
    );
  }

  Widget _buildDisconnected() {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.bluetooth_disabled, size: 64, color: Colors.white38),
            const SizedBox(height: 16),
            const Text('Not connected', style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
            const SizedBox(height: 8),
            const Text(
              'Wake your EPD-Reader, then connect to browse its books and send new ones.',
              textAlign: TextAlign.center,
              style: TextStyle(color: Colors.white60),
            ),
            const SizedBox(height: 24),
            FilledButton.icon(
              onPressed: _isLoading ? null : _connect,
              icon: _isLoading
                  ? const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2))
                  : const Icon(Icons.bluetooth_searching),
              label: const Text('Connect'),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildList() {
    final total = _books.fold<int>(0, (s, b) => s + b.size);
    return Column(
      children: [
        if (_isLoading) const LinearProgressIndicator(),
        if (_error != null)
          Padding(
            padding: const EdgeInsets.all(12),
            child: Text(_error!, style: const TextStyle(color: Colors.redAccent)),
          ),
        if (_books.isNotEmpty)
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
            child: Row(
              children: [
                Text('${_books.length} books', style: const TextStyle(fontWeight: FontWeight.bold)),
                const Spacer(),
                Text(_formatSize(total), style: const TextStyle(color: Colors.white54)),
              ],
            ),
          ),
        Expanded(
          child: RefreshIndicator(
            onRefresh: () => _refresh(rescan: true),
            child: _books.isEmpty && !_isLoading
                ? ListView(
                    children: const [
                      SizedBox(height: 120),
                      Icon(Icons.menu_book, size: 56, color: Colors.white24),
                      SizedBox(height: 12),
                      Center(child: Text('No books on the device yet', style: TextStyle(color: Colors.white60))),
                      SizedBox(height: 4),
                      Center(child: Text('Tap "Send book" to add one.', style: TextStyle(color: Colors.white38))),
                    ],
                  )
                : ListView.builder(
                    padding: const EdgeInsets.only(bottom: 96),
                    itemCount: _books.length,
                    itemBuilder: (context, index) {
                      final book = _books[index];
                      final ext = book.name.contains('.') ? book.name.split('.').last.toUpperCase() : '';
                      final subtitle = [
                        if (book.folder.isNotEmpty) book.folder,
                        ext,
                        _formatSize(book.size),
                      ].where((s) => s.isNotEmpty).join('  ·  ');
                      return Card(
                        margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
                        child: ListTile(
                          leading: const Icon(Icons.menu_book, color: Color(0xFF00B4D8)),
                          title: Text(_displayTitle(book), maxLines: 2, overflow: TextOverflow.ellipsis),
                          subtitle: Text(subtitle),
                          trailing: IconButton(
                            tooltip: 'Delete from device',
                            icon: const Icon(Icons.delete_outline, color: Colors.redAccent),
                            onPressed: () => _confirmDelete(book),
                          ),
                        ),
                      );
                    },
                  ),
          ),
        ),
      ],
    );
  }
}
