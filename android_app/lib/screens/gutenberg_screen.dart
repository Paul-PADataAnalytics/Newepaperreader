import 'package:flutter/material.dart';
import 'dart:async';
import 'dart:convert';
import 'package:http/http.dart' as http;
import 'package:path_provider/path_provider.dart';
import 'dart:io';
import '../services/ble_service.dart';
import '../widgets/upload_dialog.dart';

class GutenbergScreen extends StatefulWidget {
  const GutenbergScreen({super.key});

  @override
  State<GutenbergScreen> createState() => _GutenbergScreenState();
}

class _GutenbergScreenState extends State<GutenbergScreen> {
  final _searchController = TextEditingController();
  List<dynamic> _results = [];
  bool _isLoading = false;
  bool _hasSearched = false;
  int _searchGeneration = 0;

  void _search() async {
    final query = _searchController.text.trim();
    if (query.isEmpty || _isLoading) return;

    final int myGeneration = ++_searchGeneration;
    setState(() {
      _isLoading = true;
      _hasSearched = true;
    });

    try {
      // gutendex.com's search endpoint is frequently slow (tens of seconds),
      // so give it real room but still fail with a clear message instead of
      // spinning forever and looking hung.
      final response = await http
          .get(Uri.parse('https://gutendex.com/books/?search=${Uri.encodeQueryComponent(query)}'))
          .timeout(const Duration(seconds: 45));
      if (!mounted || myGeneration != _searchGeneration) return;
      if (response.statusCode == 200) {
        final data = jsonDecode(response.body);
        setState(() => _results = data['results'] as List<dynamic>);
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Search failed: HTTP ${response.statusCode}')),
        );
      }
    } on TimeoutException {
      if (!mounted || myGeneration != _searchGeneration) return;
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
        content: Text('Project Gutenberg is taking too long to respond. Please try again.'),
      ));
    } catch (e) {
      if (!mounted || myGeneration != _searchGeneration) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Search error: $e')));
    } finally {
      if (mounted && myGeneration == _searchGeneration) setState(() => _isLoading = false);
    }
  }

  void _showDownloadOptions(dynamic book) {
    showModalBottomSheet(
      context: context,
      backgroundColor: const Color(0xFF1B263B),
      isScrollControlled: true,
      builder: (context) {
        final title = book['title'];
        final authors = (book['authors'] as List).map((a) => a['name']).join(', ');
        final subjects = (book['subjects'] as List).join(', ');
        final formats = book['formats'] as Map<String, dynamic>;

        String? epubUrl = formats['application/epub+zip'];
        String? txtUrl = formats['text/plain; charset=us-ascii'] ?? formats['text/plain'];

        return SafeArea(
          child: Padding(
            padding: EdgeInsets.only(
              left: 16.0,
              right: 16.0,
              top: 16.0,
              bottom: MediaQuery.of(context).viewInsets.bottom + 32.0,
            ),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Text(title, style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                const SizedBox(height: 8),
                Text('By $authors', style: const TextStyle(fontSize: 16, color: Colors.grey)),
                const SizedBox(height: 8),
                Text('Subjects: $subjects', style: const TextStyle(fontSize: 12, color: Colors.grey)),
                const SizedBox(height: 24),
                if (epubUrl != null)
                  ElevatedButton.icon(
                    onPressed: () {
                      Navigator.pop(context);
                      _downloadFile(epubUrl, '$title.epub', canUpload: true);
                    },
                    icon: const Icon(Icons.download),
                    label: const Text('Download EPUB'),
                    style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF00B4D8), foregroundColor: Colors.white),
                  ),
                const SizedBox(height: 8),
                if (txtUrl != null)
                  ElevatedButton.icon(
                    onPressed: () {
                      Navigator.pop(context);
                      _downloadFile(txtUrl, '$title.txt', canUpload: true);
                    },
                    icon: const Icon(Icons.download),
                    label: const Text('Download TXT'),
                  ),
                if (epubUrl == null && txtUrl == null)
                  const Text('No suitable formats available'),
                const SizedBox(height: 16),
              ],
            ),
          ),
        );
      },
    );
  }

  Future<void> _downloadFile(String url, String fileName, {bool canUpload = false}) async {
    // Sanitize filename
    fileName = fileName.replaceAll(RegExp(r'[<>:"/\\|?*]'), '_');

    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Downloading $fileName...')));

    try {
      final response = await http.get(Uri.parse(url)).timeout(const Duration(seconds: 30));
      if (response.statusCode == 200 || response.statusCode == 302) {
        Directory? dir = await getDownloadsDirectory();
        dir ??= await getApplicationDocumentsDirectory();

        final file = File('${dir.path}/$fileName');
        await file.writeAsBytes(response.bodyBytes);

        if (!mounted) return;

        // A plain SnackBarAction is easy to miss and disappears in a few
        // seconds - ask explicitly (and persistently) whether to upload now
        // when we're connected, instead of relying on the user catching it.
        if (canUpload && BleService().isConnected) {
          final shouldUpload = await showDialog<bool>(
            context: context,
            builder: (context) => AlertDialog(
              backgroundColor: const Color(0xFF1B263B),
              title: const Text('Download complete'),
              content: Text('$fileName was saved. Upload it to your EPD-Reader now?'),
              actions: [
                TextButton(onPressed: () => Navigator.pop(context, false), child: const Text('Not now')),
                ElevatedButton(
                  onPressed: () => Navigator.pop(context, true),
                  style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF00B4D8)),
                  child: const Text('Upload to Device'),
                ),
              ],
            ),
          );
          if (shouldUpload == true) {
            await _uploadFile(file, fileName);
            return;
          }
        }

        if (!mounted) return;
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text(canUpload
              ? 'Saved to ${file.path}. Connect in the Device tab, then send it from the Library tab.'
              : 'Saved to ${file.path}'),
          action: canUpload && BleService().isConnected
              ? SnackBarAction(
                  label: 'Upload to Device',
                  onPressed: () => _uploadFile(file, fileName),
                )
              : null,
        ));
      } else {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Download failed: ${response.statusCode}')));
        }
      }
    } on TimeoutException {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Download timed out. Please try again.')));
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Error: $e')));
      }
    }
  }

  Future<void> _uploadFile(File file, String fileName) async {
    if (!BleService().isConnected) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Not connected to your EPD-Reader. Connect in the Device tab first.')),
      );
      return;
    }
    final ok = await uploadWithProgress(context, file, fileName);
    if (!mounted || !ok) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('$fileName is on your EPD-Reader')));
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Project Gutenberg')),
      body: Column(
        children: [
          Padding(
            padding: const EdgeInsets.all(16.0),
            child: Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _searchController,
                    decoration: const InputDecoration(
                      labelText: 'Search books',
                      border: OutlineInputBorder(),
                    ),
                    onSubmitted: (_) => _search(),
                  ),
                ),
                const SizedBox(width: 8),
                IconButton(
                  icon: const Icon(Icons.search, size: 32, color: Color(0xFF00B4D8)),
                  onPressed: _isLoading ? null : _search,
                ),
              ],
            ),
          ),
          if (_isLoading)
            const Padding(
              padding: EdgeInsets.only(bottom: 8.0),
              child: Column(
                children: [
                  LinearProgressIndicator(),
                  SizedBox(height: 8),
                  Text(
                    'Searching Project Gutenberg… this can take up to a minute.',
                    style: TextStyle(color: Colors.grey, fontSize: 12),
                    textAlign: TextAlign.center,
                  ),
                ],
              ),
            ),
          if (!_isLoading && _hasSearched && _results.isEmpty)
            const Expanded(
              child: Center(
                child: Text('No results found.', style: TextStyle(color: Colors.grey)),
              ),
            )
          else
            Expanded(
              child: ListView.builder(
                itemCount: _results.length,
                itemBuilder: (context, index) {
                  final book = _results[index];
                  final authors = (book['authors'] as List).map((a) => a['name']).join(', ');
                  return Card(
                    margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                    color: const Color(0xFF1B263B),
                    child: ListTile(
                      title: Text(book['title'], style: const TextStyle(fontWeight: FontWeight.bold)),
                      subtitle: Text('By $authors\nDownloads: ${book['download_count']}'),
                      trailing: const Icon(Icons.download),
                      onTap: () => _showDownloadOptions(book),
                    ),
                  );
                },
              ),
            ),
        ],
      ),
    );
  }
}
