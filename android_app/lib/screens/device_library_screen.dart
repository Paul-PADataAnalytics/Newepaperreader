import 'package:flutter/material.dart';
import '../services/ble_service.dart';

/// Shows the set of ebook files known to be on the connected device's SD
/// card (as reported by the firmware's cached library index) and lets the
/// user delete them directly from the phone.
class DeviceLibraryScreen extends StatefulWidget {
  const DeviceLibraryScreen({super.key});

  @override
  State<DeviceLibraryScreen> createState() => _DeviceLibraryScreenState();
}

class _DeviceLibraryScreenState extends State<DeviceLibraryScreen> {
  List<DeviceLibraryEntry> _books = [];
  bool _isLoading = false;
  bool _stale = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    _refresh();
  }

  Future<void> _refresh() async {
    if (!BleService().isConnected) {
      setState(() {
        _books = [];
        _error = null;
        _isLoading = false;
      });
      return;
    }

    setState(() {
      _isLoading = true;
      _error = null;
    });

    try {
      final result = await BleService().getLibrary();
      if (!mounted) return;
      setState(() {
        _books = result.books;
        _stale = result.stale;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = 'Could not read device library: $e');
    } finally {
      if (mounted) setState(() => _isLoading = false);
    }
  }

  Future<void> _confirmDelete(DeviceLibraryEntry book) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: const Color(0xFF1B263B),
        title: const Text('Delete book?'),
        content: Text('Remove "${book.name}" from the device? This cannot be undone.'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(context, false), child: const Text('Cancel')),
          ElevatedButton(
            onPressed: () => Navigator.pop(context, true),
            style: ElevatedButton.styleFrom(backgroundColor: Colors.redAccent),
            child: const Text('Delete'),
          ),
        ],
      ),
    );

    if (confirmed != true || !mounted) return;

    setState(() => _books.remove(book));
    final ok = await BleService().deleteLibraryFile(book.name);
    if (!mounted) return;
    if (ok) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Deleted ${book.name}')));
    } else {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Failed to delete ${book.name}')));
      _refresh();
    }
  }

  String _formatSize(int bytes) {
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
    return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
  }

  @override
  Widget build(BuildContext context) {
    final connected = BleService().isConnected;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Device Library'),
        actions: [
          IconButton(
            icon: const Icon(Icons.refresh),
            onPressed: _isLoading ? null : _refresh,
          ),
        ],
      ),
      body: !connected
          ? const Center(
              child: Padding(
                padding: EdgeInsets.all(24.0),
                child: Text(
                  'Not connected to EPD-Reader. Connect in BLE Sync to view and manage the books on your device.',
                  textAlign: TextAlign.center,
                  style: TextStyle(color: Colors.grey),
                ),
              ),
            )
          : Column(
              children: [
                if (_isLoading) const LinearProgressIndicator(),
                if (_stale && !_isLoading)
                  const Padding(
                    padding: EdgeInsets.all(12.0),
                    child: Text(
                      'This list may be out of date. It will refresh automatically next time the E-Reader app is opened on the device.',
                      style: TextStyle(color: Colors.grey, fontSize: 12),
                      textAlign: TextAlign.center,
                    ),
                  ),
                if (_error != null)
                  Padding(
                    padding: const EdgeInsets.all(12.0),
                    child: Text(_error!, style: const TextStyle(color: Colors.redAccent)),
                  ),
                Expanded(
                  child: _books.isEmpty && !_isLoading
                      ? const Center(child: Text('No books found on the device.', style: TextStyle(color: Colors.grey)))
                      : RefreshIndicator(
                          onRefresh: _refresh,
                          child: ListView.builder(
                            itemCount: _books.length,
                            itemBuilder: (context, index) {
                              final book = _books[index];
                              return Card(
                                margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                                color: const Color(0xFF1B263B),
                                child: ListTile(
                                  leading: const Icon(Icons.menu_book, color: Color(0xFF00B4D8)),
                                  title: Text(book.name, style: const TextStyle(fontWeight: FontWeight.bold)),
                                  subtitle: Text(_formatSize(book.size)),
                                  trailing: IconButton(
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
            ),
    );
  }
}
