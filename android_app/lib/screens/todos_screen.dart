import 'package:flutter/material.dart';

import '../models/sync_models.dart';
import '../services/ble_service.dart';
import '../services/sync_service.dart';
import 'sync_settings_screen.dart';

/// Read-only view of the todos synced to the EPD-Reader (NootTask + Google Tasks).
class TodosScreen extends StatefulWidget {
  const TodosScreen({super.key});

  @override
  State<TodosScreen> createState() => _TodosScreenState();
}

class _TodosScreenState extends State<TodosScreen> {
  final SyncService _sync = SyncService();
  final BleService _ble = BleService();
  List<TodoItem> _todos = [];
  DateTime? _fetchedAt;
  List<String> _problems = [];
  bool _loading = true;

  @override
  void initState() {
    super.initState();
    _sync.cacheVersion.addListener(_load);
    _sync.running.addListener(_rebuild);
    _ble.connected.addListener(_rebuild);
    _load();
  }

  @override
  void dispose() {
    _sync.cacheVersion.removeListener(_load);
    _sync.running.removeListener(_rebuild);
    _ble.connected.removeListener(_rebuild);
    super.dispose();
  }

  void _rebuild() {
    if (mounted) setState(() {});
  }

  Future<void> _load() async {
    final todos = await _sync.visibleTodos();
    final fetched = await _sync.todosFetchedAt();
    if (!mounted) return;
    setState(() {
      _todos = todos;
      _fetchedAt = fetched;
      _loading = false;
    });
  }

  Future<void> _refresh() async {
    final problems = await _sync.refreshTodos();
    await _load();
    if (mounted) setState(() => _problems = problems);
  }

  Future<void> _syncDevice() async {
    await _sync.syncDevice();
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(_sync.status.value)));
  }

  String _fmt(DateTime t) =>
      '${t.day}/${t.month} ${t.hour.toString().padLeft(2, '0')}:${t.minute.toString().padLeft(2, '0')}';

  @override
  Widget build(BuildContext context) {
    final groups = <String, List<TodoItem>>{};
    for (final t in _todos) {
      groups.putIfAbsent('${t.src}\u0000${t.list}', () => []).add(t);
    }
    final syncing = _sync.running.value;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Todos'),
        actions: [
          IconButton(
            tooltip: 'Sync settings',
            icon: const Icon(Icons.tune),
            onPressed: () async {
              await Navigator.of(context).push(MaterialPageRoute(builder: (_) => const SyncSettingsScreen()));
              _load();
            },
          ),
          IconButton(
            tooltip: _ble.isConnected ? 'Sync with device' : 'Device not connected',
            icon: syncing
                ? const SizedBox(width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2))
                : const Icon(Icons.sync),
            onPressed: (_ble.isConnected && !syncing) ? _syncDevice : null,
          ),
        ],
      ),
      body: RefreshIndicator(
        onRefresh: _refresh,
        child: _loading
            ? ListView(children: const [SizedBox(height: 200, child: Center(child: CircularProgressIndicator()))])
            : ListView(
                padding: const EdgeInsets.fromLTRB(12, 4, 12, 24),
                physics: const AlwaysScrollableScrollPhysics(),
                children: [
                  Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 8),
                    child: Text(
                      _fetchedAt == null
                          ? 'Nothing synced yet. Pull down to fetch, or set up sources in sync settings.'
                          : '${_todos.length} todos · fetched ${_fmt(_fetchedAt!)} · pull down to refresh',
                      style: const TextStyle(color: Colors.white54, fontSize: 12),
                    ),
                  ),
                  for (final p in _problems)
                    Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 2),
                      child: Text(p, style: const TextStyle(color: Colors.orangeAccent, fontSize: 12)),
                    ),
                  for (final entry in groups.entries) _group(entry.value),
                ],
              ),
      ),
    );
  }

  Widget _group(List<TodoItem> items) {
    final first = items.first;
    final isNt = first.src == kSourceNootTask;
    return Card(
      margin: const EdgeInsets.symmetric(vertical: 6),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 12, 12, 4),
            child: Row(
              children: [
                Expanded(
                  child: Text(first.list.isEmpty ? '(untitled list)' : first.list,
                      style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
                ),
                _badge(isNt ? 'NootTask' : 'Google', isNt ? const Color(0xFF4CAF50) : const Color(0xFF4285F4)),
              ],
            ),
          ),
          for (final t in items) _tile(t),
        ],
      ),
    );
  }

  Widget _badge(String text, Color color) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
        decoration: BoxDecoration(
          color: color.withValues(alpha: 0.2),
          border: Border.all(color: color),
          borderRadius: BorderRadius.circular(10),
        ),
        child: Text(text, style: TextStyle(color: color, fontSize: 11)),
      );

  Widget _tile(TodoItem t) {
    final sub = <String>[
      if (t.due != null) 'Due ${t.due}',
      if (t.pri != null) '${t.pri![0].toUpperCase()}${t.pri!.substring(1)} priority',
      if (!t.notesEditable) 'Notes encrypted',
    ].join(' · ');
    final title = Text(
      t.title,
      style: TextStyle(
        decoration: t.done ? TextDecoration.lineThrough : null,
        color: t.done ? Colors.white38 : null,
      ),
    );
    final leading = Icon(t.done ? Icons.check_box : Icons.check_box_outline_blank,
        color: t.done ? Colors.white38 : const Color(0xFF00B4D8));
    final subtitle = sub.isEmpty ? null : Text(sub, style: const TextStyle(fontSize: 12, color: Colors.white54));
    final notes = t.notes;
    if (notes == null || notes.isEmpty) {
      return ListTile(dense: true, leading: leading, title: title, subtitle: subtitle);
    }
    return ExpansionTile(
      dense: true,
      leading: leading,
      title: title,
      subtitle: subtitle,
      shape: const Border(),
      collapsedShape: const Border(),
      childrenPadding: const EdgeInsets.fromLTRB(72, 0, 16, 12),
      expandedCrossAxisAlignment: CrossAxisAlignment.start,
      children: [SelectableText(notes, style: const TextStyle(color: Colors.white70))],
    );
  }
}
