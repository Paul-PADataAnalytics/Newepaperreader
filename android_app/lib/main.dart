import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:mobile_scanner/mobile_scanner.dart';
import 'package:http/http.dart' as http;

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});
  
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ISBN Scanner',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.blue),
        useMaterial3: true,
      ),
      home: const IsbnScannerPage(),
    );
  }
}

class IsbnScannerPage extends StatefulWidget {
  const IsbnScannerPage({super.key});

  @override
  State<IsbnScannerPage> createState() => _IsbnScannerPageState();
}

class _IsbnScannerPageState extends State<IsbnScannerPage> {
  final MobileScannerController controller = MobileScannerController(
    formats: const [BarcodeFormat.ean13], // ISBNs are typically EAN-13
  );

  bool isScanning = true;
  String scannedIsbn = '';
  String bookTitle = '';
  String bookAuthor = '';
  bool isLoading = false;
  String errorMessage = '';

  Future<void> fetchBookDetails(String isbn) async {
    setState(() {
      isLoading = true;
      errorMessage = '';
    });

    try {
      final response = await http.get(Uri.parse('https://www.googleapis.com/books/v1/volumes?q=isbn:$isbn'));
      
      if (response.statusCode == 200) {
        final data = json.decode(response.body);
        if (data['totalItems'] > 0) {
          final volumeInfo = data['items'][0]['volumeInfo'];
          setState(() {
            bookTitle = volumeInfo['title'] ?? 'Unknown Title';
            if (volumeInfo['authors'] != null && volumeInfo['authors'].isNotEmpty) {
              bookAuthor = volumeInfo['authors'].join(', ');
            } else {
              bookAuthor = 'Unknown Author';
            }
          });
        } else {
          setState(() {
            errorMessage = 'Book not found for ISBN $isbn';
          });
        }
      } else {
        setState(() {
          errorMessage = 'Failed to load book data.';
        });
      }
    } catch (e) {
      setState(() {
        errorMessage = 'Error: $e';
      });
    } finally {
      setState(() {
        isLoading = false;
      });
    }
  }

  void _onDetect(BarcodeCapture capture) {
    final List<Barcode> barcodes = capture.barcodes;
    for (final barcode in barcodes) {
      if (barcode.rawValue != null) {
        final String code = barcode.rawValue!;
        // Simple ISBN-13 validation: starts with 978 or 979
        if (code.startsWith('978') || code.startsWith('979')) {
          if (isScanning) {
            setState(() {
              isScanning = false;
              scannedIsbn = code;
              bookTitle = '';
              bookAuthor = '';
            });
            fetchBookDetails(code);
            // Stop scanning after first successful detect
            controller.stop();
          }
        }
      }
    }
  }

  void _resumeScanning() {
    setState(() {
      isScanning = true;
      scannedIsbn = '';
      bookTitle = '';
      bookAuthor = '';
      errorMessage = '';
    });
    controller.start();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('ISBN Scanner'),
        actions: [
          IconButton(
            color: Colors.white,
            icon: ValueListenableBuilder(
              valueListenable: controller,
              builder: (context, state, child) {
                switch (state.torchState) {
                  case TorchState.off:
                    return const Icon(Icons.flash_off, color: Colors.grey);
                  case TorchState.on:
                    return const Icon(Icons.flash_on, color: Colors.yellow);
                  default:
                    return const Icon(Icons.flash_auto, color: Colors.grey);
                }
              },
            ),
            iconSize: 32.0,
            onPressed: () => controller.toggleTorch(),
          ),
          IconButton(
            color: Colors.white,
            icon: ValueListenableBuilder(
              valueListenable: controller,
              builder: (context, state, child) {
                switch (state.cameraDirection) {
                  case CameraFacing.front:
                    return const Icon(Icons.camera_front);
                  case CameraFacing.back:
                    return const Icon(Icons.camera_rear);
                  default:
                    return const Icon(Icons.camera, color: Colors.grey);
                }
              },
            ),
            iconSize: 32.0,
            onPressed: () => controller.switchCamera(),
          ),
        ],
      ),
      body: Column(
        children: [
          Expanded(
            flex: 2,
            child: isScanning 
              ? MobileScanner(
                  controller: controller,
                  onDetect: _onDetect,
                )
              : Container(
                  color: Colors.black,
                  child: const Center(
                    child: Text(
                      'Scan Paused',
                      style: TextStyle(color: Colors.white, fontSize: 24),
                    ),
                  ),
                ),
          ),
          Expanded(
            flex: 1,
            child: Container(
              padding: const EdgeInsets.all(16.0),
              width: double.infinity,
              color: Colors.grey[200],
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                crossAxisAlignment: CrossAxisAlignment.center,
                children: [
                  if (scannedIsbn.isNotEmpty) ...[
                    Text('Scanned ISBN: $scannedIsbn', style: const TextStyle(fontWeight: FontWeight.bold)),
                    const SizedBox(height: 8),
                  ],
                  if (isLoading)
                    const CircularProgressIndicator()
                  else if (errorMessage.isNotEmpty)
                    Text(errorMessage, style: const TextStyle(color: Colors.red))
                  else if (bookTitle.isNotEmpty) ...[
                    Text('Title: $bookTitle', style: const TextStyle(fontSize: 18, fontWeight: FontWeight.bold), textAlign: TextAlign.center,),
                    const SizedBox(height: 4),
                    Text('Author: $bookAuthor', style: const TextStyle(fontSize: 16), textAlign: TextAlign.center,),
                  ] else ...[
                    const Text('Point camera at an ISBN barcode (EAN-13)'),
                  ],
                  const SizedBox(height: 16),
                  if (!isScanning)
                    ElevatedButton(
                      onPressed: _resumeScanning,
                      child: const Text('Scan Again'),
                    ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  @override
  void dispose() {
    controller.dispose();
    super.dispose();
  }
}
