import 'package:flutter/material.dart';
import '../models/book_record.dart';
import '../services/book_storage_service.dart';
import '../services/ble_service.dart';
import 'package:mobile_scanner/mobile_scanner.dart';
import 'dart:convert';
import 'package:http/http.dart' as http;

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  final BookStorageService _storageService = BookStorageService();
  List<BookRecord> _books = [];

  @override
  void initState() {
    super.initState();
    _loadBooks();
  }

  Future<void> _loadBooks() async {
    final books = await _storageService.loadBooks();
    setState(() {
      _books = books;
    });
  }

  void _scanBarcode() async {
    // Navigate to a simple scanner view
    final result = await Navigator.push(context, MaterialPageRoute(builder: (context) => const ScannerView()));
    if (result != null && result is String) {
      _lookupBook(result);
    }
  }

  Future<void> _lookupBook(String isbn) async {
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (context) => const Center(
        child: CircularProgressIndicator(),
      ),
    );

    BookRecord? foundBook;
    String? errorMessage;

    // 1. Try Google Books API (Primary)
    try {
      final googleUrl = Uri.parse('https://www.googleapis.com/books/v1/volumes?q=isbn:$isbn');
      final response = await http.get(googleUrl).timeout(const Duration(seconds: 8));
      
      if (response.statusCode == 200) {
        final data = jsonDecode(response.body);
        if (data['items'] != null && (data['items'] as List).isNotEmpty) {
          final info = data['items'][0]['volumeInfo'];
          foundBook = BookRecord(
            isbn: isbn,
            title: info['title'] as String? ?? 'Unknown Title',
            author: (info['authors'] as List?)?.join(', ') ?? 'Unknown Author',
            genre: (info['categories'] as List?)?.join(', ') ?? 'Unknown Genre',
            totalPages: info['pageCount'] as int? ?? 0,
          );
        } else {
          errorMessage = 'Not found on Google Books';
        }
      } else {
        errorMessage = 'Google Books HTTP ${response.statusCode}';
      }
    } catch (e) {
      errorMessage = 'Google Books error: $e';
    }

    // 2. Try Open Library API (Fallback)
    if (foundBook == null) {
      try {
        final openLibUrl = Uri.parse('https://openlibrary.org/api/books?bibkeys=ISBN:$isbn&jscmd=data&format=json');
        final response = await http.get(openLibUrl).timeout(const Duration(seconds: 8));

        if (response.statusCode == 200) {
          final Map<String, dynamic> data = jsonDecode(response.body);
          final String bookKey = data.keys.firstWhere(
            (k) => k.toUpperCase().startsWith('ISBN:'),
            orElse: () => '',
          );

          if (bookKey.isNotEmpty) {
            final info = data[bookKey];
            final title = info['title'] as String? ?? 'Unknown Title';
            
            final authorsList = info['authors'] as List?;
            final author = authorsList != null && authorsList.isNotEmpty
                ? authorsList.map((a) => a['name'] as String).join(', ')
                : 'Unknown Author';

            final subjectsList = info['subjects'] as List?;
            final genre = subjectsList != null && subjectsList.isNotEmpty
                ? subjectsList.map((s) => s['name'] as String).join(', ')
                : 'Unknown Genre';

            final pageCount = info['number_of_pages'] as int? ?? 0;

            foundBook = BookRecord(
              isbn: isbn,
              title: title,
              author: author,
              genre: genre,
              totalPages: pageCount,
            );
          }
        }
      } catch (e) {
        // Open Library lookup failed or timed out
      }
    }

    if (!mounted) return;
    Navigator.pop(context); // Pop progress dialog

    if (foundBook != null) {
      final updatedBook = await Navigator.pushNamed(
        context,
        '/page-entry',
        arguments: foundBook,
      );

      if (!mounted) return;
      if (updatedBook != null) {
        _loadBooks();
        if (updatedBook is BookRecord) {
          await BleService().syncBookIfConnected(updatedBook);
        }
      }
    } else {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(errorMessage != null 
              ? 'Lookup failed: $errorMessage (Open Library fallback also failed)'
              : 'Book with ISBN $isbn not found in databases.'),
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('My Books'),
      ),
      body: _books.isEmpty
          ? const Center(child: Text('No books added yet.'))
          : ListView.builder(
              itemCount: _books.length,
              itemBuilder: (context, index) {
                final book = _books[index];
                return Card(
                  margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                  color: const Color(0xFF1B263B),
                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                  child: ListTile(
                    contentPadding: const EdgeInsets.all(16),
                    title: Text(book.title, style: const TextStyle(fontWeight: FontWeight.bold)),
                    subtitle: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const SizedBox(height: 4),
                        Text(book.author),
                        const SizedBox(height: 12),
                        LinearProgressIndicator(
                          value: book.progress,
                          backgroundColor: Colors.white24,
                          valueColor: const AlwaysStoppedAnimation<Color>(Color(0xFF00B4D8)),
                        ),
                        const SizedBox(height: 8),
                        Text('${book.currentPage} / ${book.totalPages} pages', style: const TextStyle(fontSize: 12)),
                      ],
                    ),
                    onTap: () async {
                      final result = await Navigator.pushNamed(context, '/page-entry', arguments: book);
                      if (result != null) {
                        _loadBooks();
                      }
                    },
                  ),
                );
              },
            ),
      floatingActionButton: FloatingActionButton(
        onPressed: _scanBarcode,
        backgroundColor: const Color(0xFF00B4D8),
        child: const Icon(Icons.qr_code_scanner, color: Colors.white),
      ),
    );
  }
}

class ScannerView extends StatefulWidget {
  const ScannerView({super.key});

  @override
  State<ScannerView> createState() => _ScannerViewState();
}

class _ScannerViewState extends State<ScannerView> {
  bool _hasPopped = false;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Scan ISBN')),
      body: MobileScanner(
        onDetect: (capture) {
          if (_hasPopped) return;
          final List<Barcode> barcodes = capture.barcodes;
          for (final barcode in barcodes) {
            if (barcode.rawValue != null) {
              _hasPopped = true;
              Navigator.pop(context, barcode.rawValue);
              break;
            }
          }
        },
      ),
    );
  }
}
