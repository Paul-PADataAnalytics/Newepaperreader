import 'dart:convert';

import '../models/sync_models.dart';

/// Builders/parsers for the files exchanged with the EPD-Reader
/// (todos.jsonl, calendar.jsonl, todo_edits.jsonl). Pure functions so they
/// can be unit tested.

/// The device rejects lines longer than 16 KB; stay a little below.
const int kMaxLineBytes = 16000;
const int kMaxEventDescChars = 1000;
const int kMaxCalendarEvents = 1500;

int _cmpText(String a, String b) {
  final c = a.toLowerCase().compareTo(b.toLowerCase());
  return c != 0 ? c : a.compareTo(b);
}

/// Sorted by src (nt first), list name, not-done before done, due date
/// (undated last), then title.
List<TodoItem> sortTodos(Iterable<TodoItem> items) {
  int srcRank(String s) => s == kSourceNootTask ? 0 : (s == kSourceGoogleTasks ? 1 : 2);
  final list = items.toList();
  list.sort((a, b) {
    var c = srcRank(a.src).compareTo(srcRank(b.src));
    if (c != 0) return c;
    c = _cmpText(a.list, b.list);
    if (c != 0) return c;
    c = (a.done ? 1 : 0).compareTo(b.done ? 1 : 0);
    if (c != 0) return c;
    if (a.due != b.due) {
      if (a.due == null) return 1;
      if (b.due == null) return -1;
      c = a.due!.compareTo(b.due!);
      if (c != 0) return c;
    }
    c = _cmpText(a.title, b.title);
    if (c != 0) return c;
    return a.id.compareTo(b.id);
  });
  return list;
}

/// Truncates [s] to at most [maxBytes] UTF-8 bytes on a code point boundary.
String truncateUtf8(String s, int maxBytes) {
  if (maxBytes <= 0) return '';
  if (utf8.encode(s).length <= maxBytes) return s;
  final out = StringBuffer();
  var used = 0;
  for (final r in s.runes) {
    final ch = String.fromCharCode(r);
    final n = utf8.encode(ch).length;
    if (used + n > maxBytes) break;
    out.write(ch);
    used += n;
  }
  return out.toString();
}

String truncateChars(String s, int maxChars) {
  if (s.runes.length <= maxChars) return s;
  return String.fromCharCodes(s.runes.take(maxChars));
}

int _lineBytes(Map<String, dynamic> m) => utf8.encode(jsonEncode(m)).length;

/// Shrinks the given string fields (in order) until the encoded line fits.
Map<String, dynamic> _fitLine(Map<String, dynamic> m, List<String> shrinkable) {
  for (final key in shrinkable) {
    var guard = 0;
    while (_lineBytes(m) > kMaxLineBytes && m[key] is String && guard++ < 8) {
      final value = m[key] as String;
      final without = _lineBytes({...m, key: ''});
      final room = kMaxLineBytes - without;
      final encodedValue = _lineBytes(m) - without;
      if (room <= 0 || encodedValue <= 0) {
        m[key] = '';
        break;
      }
      // Escaping makes the encoded value larger than the raw one: scale down.
      final target = (utf8.encode(value).length * room / encodedValue).floor() - 8;
      m[key] = truncateUtf8(value, target);
    }
  }
  return m;
}

/// The wire object for one todo item (key order as in the spec).
Map<String, dynamic> todoWireJson(TodoItem t) {
  final m = <String, dynamic>{
    'k': 'item',
    'src': t.src,
    'id': t.id,
    'list': t.list,
    'title': t.title,
    'done': t.done,
    if (t.due != null && t.due!.isNotEmpty) 'due': t.due,
    if (t.pri != null && t.pri!.isNotEmpty) 'pri': t.pri,
    if (t.notes != null && t.notes!.isNotEmpty) 'notes': t.notes,
    if (t.src == kSourceNootTask && !t.notesEditable) 'ro': true,
  };
  _fitLine(m, const ['notes', 'title', 'list']);
  // Editing trimmed notes on the device would overwrite the full text.
  if (t.src == kSourceNootTask && m['ro'] != true && m['notes'] != t.notes && t.notes != null) {
    m['ro'] = true;
    _fitLine(m, const ['notes', 'title', 'list']);
  }
  return m;
}

/// Contents of /data/todos.jsonl. [ts] is unix seconds of the sync.
String buildTodosJsonl(Iterable<TodoItem> items, int ts) {
  final b = StringBuffer()
    ..write(jsonEncode({'k': 'meta', 'v': 1, 'ts': ts}))
    ..write('\n');
  for (final t in sortTodos(items)) {
    b
      ..write(jsonEncode(todoWireJson(t)))
      ..write('\n');
  }
  return b.toString();
}

/// Events sorted by start (all-day at local midnight of `day`).
List<CalendarEvent> sortEvents(Iterable<CalendarEvent> events) {
  final list = events.toList();
  list.sort((a, b) {
    var c = a.sortKey.compareTo(b.sortKey);
    if (c != 0) return c;
    if (a.allDay != b.allDay) return a.allDay ? -1 : 1;
    c = _cmpText(a.title, b.title);
    if (c != 0) return c;
    return a.id.compareTo(b.id);
  });
  return list;
}

Map<String, dynamic> eventWireJson(CalendarEvent e) {
  final desc = e.desc == null ? null : truncateChars(e.desc!, kMaxEventDescChars);
  final m = <String, dynamic>{
    'k': 'ev',
    'id': e.id,
    'cal': e.cal,
    'title': e.title,
    if (e.allDay) ...{
      'allDay': true,
      'day': e.day,
      'endDay': e.endDay ?? e.day,
    } else ...{
      'start': e.start,
      'end': e.end ?? e.start,
    },
    if (e.loc != null && e.loc!.isNotEmpty) 'loc': e.loc,
    if (desc != null && desc.isNotEmpty) 'desc': desc,
  };
  return _fitLine(m, const ['desc', 'loc', 'title']);
}

/// Contents of /data/calendar.jsonl for the window [from]..[to] (unix s).
String buildCalendarJsonl(Iterable<CalendarEvent> events, int ts, int from, int to) {
  final b = StringBuffer()
    ..write(jsonEncode({'k': 'meta', 'v': 1, 'ts': ts, 'from': from, 'to': to}))
    ..write('\n');
  for (final e in sortEvents(events).take(kMaxCalendarEvents)) {
    b
      ..write(jsonEncode(eventWireJson(e)))
      ..write('\n');
  }
  return b.toString();
}

/// yyyymmdd int for a date.
int dayInt(DateTime d) => d.year * 10000 + d.month * 100 + d.day;

/// Parses a Google all-day date ("YYYY-MM-DD") to yyyymmdd.
int? parseDateToDayInt(String? s) {
  if (s == null || s.length < 10) return null;
  final y = int.tryParse(s.substring(0, 4));
  final m = int.tryParse(s.substring(5, 7));
  final d = int.tryParse(s.substring(8, 10));
  if (y == null || m == null || d == null) return null;
  return y * 10000 + m * 100 + d;
}

/// Converts a Google exclusive all-day end date to the inclusive last day.
int? inclusiveEndDay(int startDay, String? exclusiveEnd) {
  final end = parseDateToDayInt(exclusiveEnd);
  if (end == null || end <= startDay) return startDay;
  final d = DateTime.utc(end ~/ 10000, (end ~/ 100) % 100, end % 100).subtract(const Duration(days: 1));
  final last = dayInt(d);
  return last < startDay ? startDay : last;
}

/// Local date YYYY-MM-DD.
String formatLocalDate(DateTime d) {
  final l = d.toLocal();
  return '${l.year.toString().padLeft(4, '0')}-${l.month.toString().padLeft(2, '0')}-${l.day.toString().padLeft(2, '0')}';
}

/// Parsed /data/todo_edits.jsonl.
class TodoEditBatch {
  /// Latest edit for each id.
  final Map<String, TodoEdit> latest;
  final int maxSeq;
  final int minSeq;

  const TodoEditBatch(this.latest, this.maxSeq, this.minSeq);

  bool get isEmpty => latest.isEmpty;

  /// Highest seq that may be acknowledged with TODO_EDITS_DONE given the ids
  /// whose push failed: every line with seq <= result belongs to a pushed id.
  /// Null when nothing can be acknowledged.
  int? ackUpTo(Set<String> failedIds) {
    if (latest.isEmpty) return null;
    var upTo = maxSeq;
    for (final id in failedIds) {
      final e = latest[id];
      if (e != null && e.minSeq - 1 < upTo) upTo = e.minSeq - 1;
    }
    return upTo < minSeq ? null : upTo;
  }
}

/// Parses the device's edit log. Later lines (higher seq) for the same id win.
TodoEditBatch parseTodoEdits(String content) {
  final latest = <String, TodoEdit>{};
  var maxSeq = -1;
  var minSeq = -1;
  for (final raw in const LineSplitter().convert(content)) {
    final line = raw.trim();
    if (line.isEmpty) continue;
    Map<String, dynamic> m;
    try {
      final v = jsonDecode(line);
      if (v is! Map<String, dynamic>) continue;
      m = v;
    } catch (_) {
      continue;
    }
    final seq = m['seq'];
    final id = m['id'];
    if (seq is! num || id is! String || id.isEmpty) continue;
    final s = seq.toInt();
    if (s > maxSeq) maxSeq = s;
    if (minSeq < 0 || s < minSeq) minSeq = s;
    final prev = latest[id];
    final edit = TodoEdit(
      seq: s,
      minSeq: prev == null ? s : (prev.minSeq < s ? prev.minSeq : s),
      src: m['src'] is String ? m['src'] as String : kSourceNootTask,
      id: id,
      notes: m['notes'] is String ? m['notes'] as String : '',
      ts: (m['ts'] as num?)?.toInt() ?? 0,
    );
    if (prev == null || s >= prev.seq) {
      latest[id] = edit;
    } else {
      latest[id] = TodoEdit(
          seq: prev.seq, minSeq: edit.minSeq, src: prev.src, id: id, notes: prev.notes, ts: prev.ts);
    }
  }
  return TodoEditBatch(latest, maxSeq, minSeq);
}
