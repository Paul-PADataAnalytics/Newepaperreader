import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import 'package:path_provider/path_provider.dart';

import '../models/sync_models.dart';
import 'ble_service.dart';
import 'google_service.dart';
import 'noot_task_service.dart';
import 'sync_format.dart';
import 'sync_settings.dart';

/// Moves todos (NootTask + Google Tasks) and Google Calendar events to the
/// EPD-Reader, and note edits made on the device back to NootTask.
///
/// The phone keeps the last fetched data in its documents folder so the
/// Todos tab works offline and the device still gets data when the network
/// is down.
class SyncService {
  static final SyncService _instance = SyncService._internal();
  factory SyncService() => _instance;
  SyncService._internal();

  static const String editsPath = '/data/todo_edits.jsonl';
  static const Duration _sourceTimeout = Duration(seconds: 60);

  final BleService _ble = BleService();
  final NootTaskService _nootTask = NootTaskService();
  final GoogleService _google = GoogleService();
  final SyncSettings _settings = SyncSettings();

  /// Progress while syncing, otherwise the last result.
  final ValueNotifier<String> status = ValueNotifier('');
  final ValueNotifier<bool> running = ValueNotifier(false);
  final ValueNotifier<DateTime?> lastSyncTime = ValueNotifier(null);

  /// Bumped whenever the cached todos/events change.
  final ValueNotifier<int> cacheVersion = ValueNotifier(0);

  Future<void>? _deviceSync;
  Future<List<String>>? _todoRefresh;

  Future<void> loadLastResult() async {
    lastSyncTime.value = await _settings.getLastSyncTime();
    if (!running.value) status.value = await _settings.getLastSyncMessage() ?? '';
  }

  // ---------------------------------------------------------------------
  // Cache
  // ---------------------------------------------------------------------

  Future<File> _cacheFile(String name) async {
    final dir = Directory('${(await getApplicationDocumentsDirectory()).path}/sync_cache');
    if (!await dir.exists()) await dir.create(recursive: true);
    return File('${dir.path}/$name');
  }

  Future<Map<String, dynamic>> _readCache(String name) async {
    try {
      final f = await _cacheFile(name);
      if (!await f.exists()) return {};
      final v = jsonDecode(await f.readAsString());
      return v is Map<String, dynamic> ? v : {};
    } catch (_) {
      return {};
    }
  }

  Future<void> _writeCache(String name, Map<String, dynamic> data) async {
    final f = await _cacheFile(name);
    final tmp = File('${f.path}.tmp');
    await tmp.writeAsString(jsonEncode(data), flush: true);
    await tmp.rename(f.path);
    cacheVersion.value++;
  }

  List<TodoItem> _todosOf(Map<String, dynamic> cache, String src) =>
      ((cache[src] as Map<String, dynamic>?)?['items'] as List<dynamic>? ?? const [])
          .whereType<Map<String, dynamic>>()
          .map(TodoItem.fromJson)
          .toList();

  /// Forgets cached data of one source ([kSourceNootTask] / [kSourceGoogleTasks])
  /// or the calendar ('cal'), e.g. after logging out.
  Future<void> clearCache(String source) async {
    if (source == 'cal') {
      await _writeCache('events.json', {});
      return;
    }
    final cache = await _readCache('todos.json');
    cache.remove(source);
    await _writeCache('todos.json', cache);
  }

  /// Todos as last fetched, filtered by the sync settings (what the device gets).
  Future<List<TodoItem>> visibleTodos() async {
    final cache = await _readCache('todos.json');
    final includeCompleted = await _settings.getIncludeCompleted();
    final excluded = await _settings.getExcludedCategoryIds();
    final lists = await _settings.getSelectedTaskListIds();
    return sortTodos([
      ..._todosOf(cache, kSourceNootTask).where((t) => !excluded.contains(t.listId)),
      ..._todosOf(cache, kSourceGoogleTasks).where((t) => lists.contains(t.listId)),
    ].where((t) => includeCompleted || !t.done));
  }

  Future<DateTime?> todosFetchedAt() async {
    final cache = await _readCache('todos.json');
    int? latest;
    for (final src in [kSourceNootTask, kSourceGoogleTasks]) {
      final ms = (cache[src] as Map<String, dynamic>?)?['fetched'];
      if (ms is int && (latest == null || ms > latest)) latest = ms;
    }
    return latest == null ? null : DateTime.fromMillisecondsSinceEpoch(latest);
  }

  Future<void> _patchCachedNotes(String id, String notes) async {
    final cache = await _readCache('todos.json');
    final items = _todosOf(cache, kSourceNootTask);
    final i = items.indexWhere((t) => t.id == id);
    if (i < 0) return;
    final t = items[i];
    items[i] = TodoItem(
        src: t.src,
        id: t.id,
        list: t.list,
        listId: t.listId,
        title: t.title,
        done: t.done,
        due: t.due,
        pri: t.pri,
        notes: notes.isEmpty ? null : notes,
        notesEditable: t.notesEditable);
    (cache[kSourceNootTask] as Map<String, dynamic>)['items'] = items.map((t) => t.toJson()).toList();
    await _writeCache('todos.json', cache);
  }

  // ---------------------------------------------------------------------
  // Network fetches
  // ---------------------------------------------------------------------

  /// Fetches todos from NootTask and Google into the cache. Each source
  /// fails independently (keeping its cached copy). Returns problems.
  Future<List<String>> refreshTodos() => _todoRefresh ??= _refreshTodos().whenComplete(() => _todoRefresh = null);

  Future<List<String>> _refreshTodos() async {
    final problems = <String>[];
    final includeCompleted = await _settings.getIncludeCompleted();

    Future<List<TodoItem>?> nootTask() async {
      if (!await _nootTask.isLoggedIn()) return null;
      try {
        return await _nootTask.fetchTodos().timeout(_sourceTimeout);
      } catch (e) {
        problems.add('NootTask: ${_describe(e)} (using cached)');
        return null;
      }
    }

    Future<List<TodoItem>?> google() async {
      if (!await _google.isConnected()) return null;
      try {
        return await () async {
          final lists = await _google.listTaskLists();
          final selected = await _settings.resolveSelectedTaskLists(lists.map((l) => l.id));
          final items = <TodoItem>[];
          for (final l in lists.where((l) => selected.contains(l.id))) {
            items.addAll(await _google.fetchTasks(l.id, includeCompleted, listTitle: l.title));
          }
          return items;
        }()
            .timeout(_sourceTimeout);
      } catch (e) {
        problems.add('Google Tasks: ${_describe(e)} (using cached)');
        return null;
      }
    }

    final results = await Future.wait([nootTask(), google()]);
    final now = DateTime.now().millisecondsSinceEpoch;
    final cache = await _readCache('todos.json');
    var changed = false;
    for (final (src, items) in [(kSourceNootTask, results[0]), (kSourceGoogleTasks, results[1])]) {
      if (items == null) continue;
      cache[src] = {'fetched': now, 'items': items.map((t) => t.toJson()).toList()};
      changed = true;
    }
    if (changed) await _writeCache('todos.json', cache);
    return problems;
  }

  /// The calendar window: 7 days back to 90 days ahead (whole local days).
  (DateTime, DateTime) calendarWindow([DateTime? now]) {
    final n = now ?? DateTime.now();
    return (DateTime(n.year, n.month, n.day - 7), DateTime(n.year, n.month, n.day + 91));
  }

  /// Fetches the selected calendars' events; falls back to the cache.
  Future<(List<CalendarEvent>, String?)> _events(DateTime from, DateTime to) async {
    String? problem;
    if (await _google.isConnected()) {
      try {
        final events = await () async {
          final cals = await _google.listOwnedCalendars();
          final chosen = await _settings.getSelectedCalendarIds();
          final selected = cals.where((c) => chosen == null ? c.primary : chosen.contains(c.id)).toList();
          return _google.fetchEvents(selected, from, to);
        }()
            .timeout(_sourceTimeout);
        await _writeCache('events.json', {
          'fetched': DateTime.now().millisecondsSinceEpoch,
          'events': events.map((e) => e.toJson()).toList(),
        });
        return (events, null);
      } catch (e) {
        problem = 'Calendar: ${_describe(e)} (using cached)';
      }
    }
    final cache = await _readCache('events.json');
    final chosen = await _settings.getSelectedCalendarIds();
    final fromS = from.millisecondsSinceEpoch ~/ 1000;
    final toS = to.millisecondsSinceEpoch ~/ 1000;
    final events = (cache['events'] as List<dynamic>? ?? const [])
        .whereType<Map<String, dynamic>>()
        .map(CalendarEvent.fromJson)
        .where((e) => chosen == null || chosen.contains(e.calId))
        .where((e) => e.endKey > fromS && e.sortKey < toS)
        .toList();
    return (events, problem);
  }

  // ---------------------------------------------------------------------
  // Device sync
  // ---------------------------------------------------------------------

  /// Runs the full sync with the connected device. Only one runs at a time;
  /// a second call returns the running one.
  Future<void> syncDevice() => _deviceSync ??= _syncDevice().whenComplete(() => _deviceSync = null);

  Future<void> _syncDevice() async {
    if (!_ble.isConnected) return;
    running.value = true;
    final parts = <String>[];
    try {
      var info = _ble.deviceInfo.value;
      if (info == null) {
        try {
          info = DeviceInfo.fromJson(await _ble.request({'t': 'HELLO'}));
          _ble.deviceInfo.value = info;
        } catch (_) {}
      }
      final doTodos = info?.supports('todos') ?? false;
      final doCalendar = info?.supports('calendar') ?? false;
      if (!doTodos && !doCalendar) {
        parts.add('Clock set. This firmware has no todo/calendar support');
        return;
      }
      final ts = DateTime.now().millisecondsSinceEpoch ~/ 1000;

      if (doTodos) {
        status.value = 'Reading note edits from the device...';
        try {
          final m = await _pushDeviceEdits();
          if (m != null) parts.add(m);
        } catch (e) {
          parts.add('Note edits: ${_describe(e)}');
        }

        status.value = 'Fetching todos...';
        parts.addAll(await refreshTodos());
        final todos = await visibleTodos();
        status.value = 'Sending ${todos.length} todos...';
        try {
          await _upload('todos.jsonl', buildTodosJsonl(todos, ts));
          parts.add('${todos.length} todos');
        } catch (e) {
          parts.add('Todo upload failed: ${_describe(e)}');
        }
      }

      if (doCalendar) {
        status.value = 'Fetching calendar...';
        final (from, to) = calendarWindow();
        final (events, problem) = await _events(from, to);
        if (problem != null) parts.add(problem);
        status.value = 'Sending ${events.length} events...';
        try {
          await _upload('calendar.jsonl',
              buildCalendarJsonl(events, ts, from.millisecondsSinceEpoch ~/ 1000, to.millisecondsSinceEpoch ~/ 1000));
          parts.add('${events.length > kMaxCalendarEvents ? kMaxCalendarEvents : events.length} events');
        } catch (e) {
          parts.add('Calendar upload failed: ${_describe(e)}');
        }
      }
    } catch (e) {
      parts.add('Sync failed: ${_describe(e)}');
    } finally {
      final now = DateTime.now();
      final message = parts.join(' · ');
      try {
        await _settings.setLastSync(now, message);
      } catch (_) {}
      lastSyncTime.value = now;
      status.value = message;
      running.value = false;
    }
  }

  /// Pushes note edits made on the device to NootTask and acknowledges them.
  Future<String?> _pushDeviceEdits() async {
    final bytes = await _ble.readDeviceFile(editsPath);
    if (bytes.isEmpty) return null;
    final batch = parseTodoEdits(utf8.decode(bytes, allowMalformed: true));
    if (batch.isEmpty) return null;

    var loggedIn = await _nootTask.isLoggedIn();
    final failed = <String>{};
    var saved = 0;
    var dropped = 0;
    String? lastError;
    for (final edit in batch.latest.values) {
      if (edit.src != kSourceNootTask) {
        dropped++;
        continue;
      }
      if (!loggedIn) {
        failed.add(edit.id);
        lastError = 'NootTask login required';
        continue;
      }
      try {
        await _nootTask.updateNotes(edit.id, edit.notes).timeout(const Duration(seconds: 45));
        await _patchCachedNotes(edit.id, edit.notes);
        saved++;
      } on NootTaskNotFoundException {
        dropped++;
      } on NootTaskEncryptedException {
        dropped++;
      } on NootTaskAuthException catch (e) {
        failed.add(edit.id);
        lastError = e.message;
        loggedIn = false;
      } catch (e) {
        failed.add(edit.id);
        lastError = _describe(e);
      }
    }

    final upTo = batch.ackUpTo(failed);
    if (upTo != null) await _ble.todoEditsDone(upTo);

    final msg = <String>[];
    if (saved > 0) msg.add('$saved note edit${saved == 1 ? '' : 's'} saved');
    if (dropped > 0) msg.add('$dropped edit${dropped == 1 ? '' : 's'} discarded (todo deleted or notes encrypted)');
    if (failed.isNotEmpty) msg.add('${failed.length} note edit${failed.length == 1 ? '' : 's'} pending: $lastError');
    return msg.isEmpty ? null : msg.join(', ');
  }

  Future<void> _upload(String name, String content) async {
    final dir = await getTemporaryDirectory();
    final file = File('${dir.path}/$name');
    await file.writeAsString(content, flush: true);
    try {
      await _ble.uploadFile(file, name, folder: 'data');
    } finally {
      try {
        await file.delete();
      } catch (_) {}
    }
  }

  String _describe(Object e) {
    if (e is TimeoutException) return 'timed out';
    if (e is SocketException) return 'offline';
    if (e is HttpException || e is http.ClientException) return 'network error (offline?)';
    return '$e';
  }
}
