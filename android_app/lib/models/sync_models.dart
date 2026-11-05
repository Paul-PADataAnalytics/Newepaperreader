/// Data synced from NootTask / Google to the phone and on to the EPD-Reader.
library;

/// Source tags used on the wire (see the todo/calendar sync protocol).
const String kSourceNootTask = 'nt';
const String kSourceGoogleTasks = 'gt';

/// Category id used for NootTask todos without a (resolvable) category.
const String kUncategorisedId = '_uncategorised';
const String kUncategorisedName = 'Uncategorised';

class TodoItem {
  /// [kSourceNootTask] or [kSourceGoogleTasks].
  final String src;
  final String id;

  /// Display group: NootTask category ("Parent / Category") or Google list title.
  final String list;

  /// NootTask category id (or [kUncategorisedId]) / Google task-list id.
  final String listId;
  final String title;
  final bool done;

  /// Local date YYYY-MM-DD.
  final String? due;

  /// low | medium | high (NootTask only).
  final String? pri;
  final String? notes;

  /// False when the notes are encrypted on the server and must not be edited.
  final bool notesEditable;

  const TodoItem({
    required this.src,
    required this.id,
    required this.list,
    required this.listId,
    required this.title,
    this.done = false,
    this.due,
    this.pri,
    this.notes,
    this.notesEditable = true,
  });

  factory TodoItem.fromJson(Map<String, dynamic> json) => TodoItem(
        src: (json['src'] ?? kSourceNootTask) as String,
        id: (json['id'] ?? '') as String,
        list: (json['list'] ?? '') as String,
        listId: (json['listId'] ?? '') as String,
        title: (json['title'] ?? '') as String,
        done: json['done'] == true,
        due: json['due'] as String?,
        pri: json['pri'] as String?,
        notes: json['notes'] as String?,
        notesEditable: json['notesEditable'] != false,
      );

  /// Cache representation (not the wire format; see [todoWireJson]).
  Map<String, dynamic> toJson() => {
        'src': src,
        'id': id,
        'list': list,
        'listId': listId,
        'title': title,
        'done': done,
        if (due != null) 'due': due,
        if (pri != null) 'pri': pri,
        if (notes != null) 'notes': notes,
        if (!notesEditable) 'notesEditable': false,
      };
}

class CalendarEvent {
  final String id;

  /// Google calendar id (used for selection, not sent to the device).
  final String calId;

  /// Calendar display name.
  final String cal;
  final String title;
  final bool allDay;

  /// Timed events: unix seconds (UTC).
  final int? start;
  final int? end;

  /// All-day events: first and last (inclusive) local date as yyyymmdd.
  final int? day;
  final int? endDay;
  final String? loc;
  final String? desc;

  const CalendarEvent({
    required this.id,
    required this.calId,
    required this.cal,
    required this.title,
    this.allDay = false,
    this.start,
    this.end,
    this.day,
    this.endDay,
    this.loc,
    this.desc,
  });

  factory CalendarEvent.fromJson(Map<String, dynamic> json) => CalendarEvent(
        id: (json['id'] ?? '') as String,
        calId: (json['calId'] ?? '') as String,
        cal: (json['cal'] ?? '') as String,
        title: (json['title'] ?? '') as String,
        allDay: json['allDay'] == true,
        start: (json['start'] as num?)?.toInt(),
        end: (json['end'] as num?)?.toInt(),
        day: (json['day'] as num?)?.toInt(),
        endDay: (json['endDay'] as num?)?.toInt(),
        loc: json['loc'] as String?,
        desc: json['desc'] as String?,
      );

  Map<String, dynamic> toJson() => {
        'id': id,
        'calId': calId,
        'cal': cal,
        'title': title,
        if (allDay) 'allDay': true,
        if (start != null) 'start': start,
        if (end != null) 'end': end,
        if (day != null) 'day': day,
        if (endDay != null) 'endDay': endDay,
        if (loc != null) 'loc': loc,
        if (desc != null) 'desc': desc,
      };

  /// Sort key: start for timed events, local midnight of [day] for all-day.
  int get sortKey {
    if (allDay && day != null) {
      final d = day!;
      return DateTime(d ~/ 10000, (d ~/ 100) % 100, d % 100).millisecondsSinceEpoch ~/ 1000;
    }
    return start ?? 0;
  }

  /// Last second (exclusive) the event covers, used for window filtering.
  int get endKey {
    if (allDay) {
      final d = endDay ?? day ?? 0;
      return DateTime(d ~/ 10000, (d ~/ 100) % 100, d % 100 + 1).millisecondsSinceEpoch ~/ 1000;
    }
    return end ?? start ?? 0;
  }
}

/// A NootTask category the user can choose to (not) sync.
class NootCategory {
  final String id;
  final String name;

  const NootCategory(this.id, this.name);
}

/// A Google task list or calendar.
class GoogleCollection {
  final String id;
  final String title;
  final bool primary;

  const GoogleCollection(this.id, this.title, {this.primary = false});
}

/// A note edit made on the device (latest line for that id).
class TodoEdit {
  final int seq;

  /// Lowest seq of all lines for this id (all superseded by [seq]).
  final int minSeq;
  final String src;
  final String id;
  final String notes;
  final int ts;

  const TodoEdit({
    required this.seq,
    required this.minSeq,
    required this.src,
    required this.id,
    required this.notes,
    required this.ts,
  });
}
