import 'package:flutter/material.dart';
import 'dart:convert';
import 'package:http/http.dart' as http;
import 'package:path_provider/path_provider.dart';
import 'dart:io';
import '../services/ble_service.dart';

class GutenbergScreen extends StatefulWidget {
  const GutenbergScreen({super.key});

  @override
  State<GutenbergScreen> createState() => _GutenbergScreenState();
}

class _GutenbergScreenState extends State<GutenbergScreen> {
  final _searchController = TextEditingController();
  List<dynamic> _results = [];
  bool _isLoading = false;

  void _search() async {
    final query = _searchController.text.trim();
    if (query.isEmpty) return;

    setState(() => _isLoading = true);

    try {
      final response = await http.get(Uri.parse('https://gutendex.com/books/?search=$query'));
      if (!mounted) return;
      if (response.statusCode == 200) {
        final data = jsonDecode(response.body);
        setState(() => _results = data['results'] as List<dynamic>);
      }
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Search error: $e')));
    } finally {
      if (mounted) setState(() => _isLoading = false);
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
      final response = await http.get(Uri.parse(url));
      if (response.statusCode == 200 || response.statusCode == 302) {
        Directory? dir = await getDownloadsDirectory();
        dir ??= await getApplicationDocumentsDirectory();

        final file = File('${dir.path}/$fileName');
        await file.writeAsBytes(response.bodyBytes);

        if (!mounted) return;
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text('Saved to ${file.path}'),
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
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Error: $e')));
      }
    }
  }

  Future<void> _uploadFile(File file, String fileName) async {
    if (!BleService().isConnected) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Not connected to EPD-Reader. Connect in BLE Sync first.')),
      );
      return;
    }

    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Uploading $fileName...')));
    try {
      await BleService().uploadFile(file, fileName);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Uploaded $fileName')));
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Upload failed: $e')));
    }
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
                  onPressed: _search,
                ),
              ],
            ),
          ),
          if (_isLoading)
            const LinearProgressIndicator(),
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
