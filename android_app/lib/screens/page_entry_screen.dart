import 'package:flutter/material.dart';
import '../models/book_record.dart';
import '../services/book_storage_service.dart';
import '../services/ble_service.dart';

class PageEntryScreen extends StatefulWidget {
  const PageEntryScreen({super.key});

  @override
  State<PageEntryScreen> createState() => _PageEntryScreenState();
}

class _PageEntryScreenState extends State<PageEntryScreen> {
  final _pageController = TextEditingController();
  final _totalController = TextEditingController();
  final _customGenreController = TextEditingController();
  BookRecord? _book;

  final List<String> _presetGenres = [
    'Unknown',
    'Fiction',
    'Non-Fiction',
    'Mystery',
    'Thriller',
    'Science Fiction',
    'Fantasy',
    'Biography',
    'History',
    'Self-Help',
    'Poetry',
  ];

  String _selectedDropdownValue = 'Unknown';
  bool _showCustomField = false;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    if (_book == null) {
      final args = ModalRoute.of(context)?.settings.arguments as BookRecord?;
      if (args != null) {
        _book = args;
        _pageController.text = args.currentPage.toString();
        _totalController.text = args.totalPages.toString();

        // Normalize genre and check if it fits in presets
        final initialGenre = args.genre.isEmpty ? 'Unknown' : args.genre;
        if (_presetGenres.contains(initialGenre)) {
          _selectedDropdownValue = initialGenre;
          _showCustomField = false;
        } else {
          _selectedDropdownValue = 'Custom...';
          _customGenreController.text = initialGenre;
          _showCustomField = true;
        }
      }
    }
  }

  void _updateBookFields() {
    if (_book != null) {
      _book!.currentPage = int.tryParse(_pageController.text) ?? 0;
      _book!.totalPages = int.tryParse(_totalController.text) ?? 0;
      
      String finalGenre = _selectedDropdownValue;
      if (_selectedDropdownValue == 'Custom...') {
        finalGenre = _customGenreController.text.trim();
        if (finalGenre.isEmpty) {
          finalGenre = 'Unknown';
        }
      }
      _book!.genre = finalGenre;
      _book!.lastUpdated = DateTime.now();
    }
  }

  void _saveLocal() async {
    if (_book != null) {
      _updateBookFields();
      await BookStorageService().addOrUpdateBook(_book!);
      await BleService().syncBookIfConnected(_book!);
      if (!mounted) return;
      Navigator.pop(context, _book);
    }
  }

  void _saveAndSync() async {
    if (_book != null) {
      _updateBookFields();
      await BookStorageService().addOrUpdateBook(_book!);

      final ble = BleService();
      if (ble.isConnected) {
        try {
          await ble.sendBookProgress(_book!);
          if (!mounted) return;
          ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Synced to device')));
          Navigator.pop(context, _book);
        } catch (e) {
          if (!mounted) return;
          ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Sync failed: $e')));
        }
      } else {
        if (!mounted) return;
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Not connected to BLE. Please connect first.')),
        );
        Navigator.pop(context, _book);
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    if (_book == null) return const Scaffold();

    return Scaffold(
      appBar: AppBar(title: const Text('Edit Book')),
      body: SingleChildScrollView(
        child: Padding(
          padding: const EdgeInsets.all(16.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(_book!.title, style: const TextStyle(fontSize: 24, fontWeight: FontWeight.bold)),
              const SizedBox(height: 8),
              Text(_book!.author, style: const TextStyle(fontSize: 16, color: Colors.grey)),
              const SizedBox(height: 32),
              TextField(
                controller: _pageController,
                decoration: const InputDecoration(labelText: 'Current Page', border: OutlineInputBorder()),
                keyboardType: TextInputType.number,
              ),
              const SizedBox(height: 16),
              TextField(
                controller: _totalController,
                decoration: const InputDecoration(labelText: 'Total Pages', border: OutlineInputBorder()),
                keyboardType: TextInputType.number,
              ),
              const SizedBox(height: 16),
              DropdownButtonFormField<String>(
                initialValue: _selectedDropdownValue,
                decoration: const InputDecoration(
                  labelText: 'Genre',
                  border: OutlineInputBorder(),
                ),
                items: [
                  ..._presetGenres.map((g) => DropdownMenuItem(value: g, child: Text(g))),
                  const DropdownMenuItem(value: 'Custom...', child: Text('Custom...')),
                ],
                onChanged: (val) {
                  setState(() {
                    if (val != null) {
                      _selectedDropdownValue = val;
                      _showCustomField = (val == 'Custom...');
                    }
                  });
                },
              ),
              if (_showCustomField) ...[
                const SizedBox(height: 16),
                TextField(
                  controller: _customGenreController,
                  decoration: const InputDecoration(
                    labelText: 'Enter Custom Genre',
                    border: OutlineInputBorder(),
                  ),
                ),
              ],
              const SizedBox(height: 32),
              ElevatedButton(
                onPressed: _saveLocal,
                style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF1B263B)),
                child: const Text('Save', style: TextStyle(color: Colors.white)),
              ),
              const SizedBox(height: 16),
              ElevatedButton(
                onPressed: _saveAndSync,
                style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF00B4D8)),
                child: const Text('Save & Sync to Device', style: TextStyle(color: Colors.white)),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
