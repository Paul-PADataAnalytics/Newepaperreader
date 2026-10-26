import 'dart:io';

import 'package:flutter/material.dart';

import '../services/ble_service.dart';

/// Uploads [file] to the device while showing a modal progress dialog.
/// Returns true on success. Errors are shown to the user.
Future<bool> uploadWithProgress(BuildContext context, File file, String fileName, {String folder = 'books'}) async {
  final result = await showDialog<bool>(
    context: context,
    barrierDismissible: false,
    builder: (_) => _UploadDialog(file: file, fileName: fileName, folder: folder),
  );
  return result == true;
}

class _UploadDialog extends StatefulWidget {
  final File file;
  final String fileName;
  final String folder;

  const _UploadDialog({required this.file, required this.fileName, required this.folder});

  @override
  State<_UploadDialog> createState() => _UploadDialogState();
}

class _UploadDialogState extends State<_UploadDialog> {
  final _controller = UploadController();
  double _progress = 0;
  String? _error;
  bool _done = false;
  final _stopwatch = Stopwatch();

  @override
  void initState() {
    super.initState();
    _run();
  }

  Future<void> _run() async {
    _stopwatch.start();
    try {
      await BleService().uploadFile(widget.file, widget.fileName,
          folder: widget.folder,
          controller: _controller,
          onProgress: (p) {
            if (mounted) setState(() => _progress = p);
          });
      if (!mounted) return;
      setState(() => _done = true);
      await Future.delayed(const Duration(milliseconds: 700));
      if (mounted) Navigator.pop(context, true);
    } on UploadCancelled {
      if (mounted) Navigator.pop(context, false);
    } catch (e) {
      if (mounted) setState(() => _error = e.toString());
    }
  }

  String _rate() {
    final secs = _stopwatch.elapsedMilliseconds / 1000.0;
    if (secs < 0.5 || _progress <= 0) return '';
    final bytes = widget.file.lengthSync() * _progress;
    return '${(bytes / 1024 / secs).toStringAsFixed(1)} KB/s';
  }

  @override
  Widget build(BuildContext context) {
    final pct = (_progress * 100).round();
    return AlertDialog(
      title: Text(_error != null ? 'Upload failed' : (_done ? 'Sent to device' : 'Sending to device')),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(widget.fileName, maxLines: 2, overflow: TextOverflow.ellipsis),
          const SizedBox(height: 16),
          if (_error == null) ...[
            LinearProgressIndicator(value: _progress),
            const SizedBox(height: 8),
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [Text('$pct%'), Text(_rate(), style: const TextStyle(color: Colors.white54))],
            ),
          ] else
            Text(_error!, style: const TextStyle(color: Colors.redAccent)),
        ],
      ),
      actions: [
        if (_error != null)
          TextButton(onPressed: () => Navigator.pop(context, false), child: const Text('Close'))
        else if (!_done)
          TextButton(onPressed: _controller.cancel, child: const Text('Cancel')),
      ],
    );
  }
}
