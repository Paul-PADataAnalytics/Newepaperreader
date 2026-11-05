import 'package:android_app/models/sync_models.dart';
import 'package:android_app/services/noot_task_service.dart';
import 'package:flutter_test/flutter_test.dart';

Map<String, dynamic> act(String type, Map<String, dynamic> props) =>
    {'activityType': type, 'properties': props, 'version': '1.0.0'};

Map<String, dynamic> noot(String id, List<Map<String, dynamic>> activities, {bool deleted = false}) =>
    {'id': id, 'activities': activities, 'deleted': deleted, 'version': 1};

void main() {
  final noots = [
    noot('parent-1', [act('parent-category', {'name': 'Pets', 'colour': '0xFF4CAF50'})]),
    noot('cat-fish', [act('category-definition', {'name': 'Fish', 'colour': '0xFF9C27B0', 'parent': 'parent-1'})]),
    noot('cat-work', [act('category-definition', {'name': 'Work', 'colour': '0xFF000000'})]),
    // Title + completion in the todo activity itself.
    noot('t1', [
      act('todo', {'title': 'Angelfish', 'priority': 'High', 'completed': false}),
      act('category-assignment', {'categoryNootId': 'cat-fish'}),
      act('note', {'content': 'Feed\ntwice', 'format': 'markdown'}),
      act('schedule', {'startTime': '2026-11-05T12:00:00Z'}),
    ]),
    // Separate title / completion activities win.
    noot('t2', [
      act('todo', {'title': 'old title', 'completed': false}),
      act('title', {'title': 'Report'}),
      act('completion', {'completed': true}),
      act('category-assignment', {'categoryNootId': 'cat-work'}),
    ]),
    // Encrypted note and unknown category.
    noot('t3', [
      act('todo', {'title': 'Secret', 'priority': 'urgent'}),
      act('title', {'title': 'xyz', 'encrypted': true}),
      act('note', {'content': 'ciphertext', 'encrypted': true}),
      act('category-assignment', {'categoryNootId': 'missing'}),
    ]),
    noot('t4', [act('todo', {'title': 'Deleted'})], deleted: true),
    noot('j1', [act('journal', {'title': 'Not a todo'})]),
  ];

  test('parses todos with both title/completion shapes and categories', () {
    final data = parseNootTaskData(noots);
    final byId = {for (final t in data.todos) t.id: t};
    expect(byId.keys.toSet(), {'t1', 't2', 't3'});

    final t1 = byId['t1']!;
    expect(t1.src, kSourceNootTask);
    expect(t1.title, 'Angelfish');
    expect(t1.done, isFalse);
    expect(t1.pri, 'high');
    expect(t1.list, 'Pets / Fish');
    expect(t1.listId, 'cat-fish');
    expect(t1.notes, 'Feed\ntwice');
    expect(t1.notesEditable, isTrue);
    final local = DateTime.utc(2026, 11, 5, 12).toLocal();
    expect(t1.due,
        '${local.year}-${local.month.toString().padLeft(2, '0')}-${local.day.toString().padLeft(2, '0')}');

    final t2 = byId['t2']!;
    expect(t2.title, 'Report');
    expect(t2.done, isTrue);
    expect(t2.list, 'Work');
    expect(t2.pri, isNull);
    expect(t2.notes, isNull);

    final t3 = byId['t3']!;
    expect(t3.title, 'Secret');
    expect(t3.notes, isNull);
    expect(t3.notesEditable, isFalse);
    expect(t3.list, kUncategorisedName);
    expect(t3.listId, kUncategorisedId);
    expect(t3.pri, isNull);
  });

  test('lists categories with display names and Uncategorised last', () {
    final cats = parseNootTaskData(noots).categories;
    expect(cats.map((c) => c.name).toList(), ['Pets / Fish', 'Work', 'Uncategorised']);
    expect(cats.map((c) => c.id).toList(), ['cat-fish', 'cat-work', kUncategorisedId]);
  });

  test('activitiesWithNotes replaces or appends the note activity', () {
    final withNote = [
      act('todo', {'title': 'A'}),
      act('note', {'content': 'old', 'format': 'markdown'}),
    ];
    final replaced = activitiesWithNotes(withNote, 'new');
    expect(replaced.length, 2);
    expect(replaced[0], withNote[0]);
    expect(replaced[1]['properties'], {'content': 'new', 'format': 'markdown'});
    expect(withNote[1]['properties']['content'], 'old');

    final appended = activitiesWithNotes([act('todo', {'title': 'A'})], 'hello');
    expect(appended.length, 2);
    expect(appended[1]['activityType'], 'note');
    expect(appended[1]['properties'], {'content': 'hello', 'format': 'markdown'});

    expect(() => activitiesWithNotes([act('note', {'content': 'x', 'encrypted': true})], 'y'),
        throwsA(isA<NootTaskEncryptedException>()));
  });

  test('activitiesWithNotes with empty text removes a plain note and adds none', () {
    final todo = act('todo', {'title': 'A'});
    expect(activitiesWithNotes([todo, act('note', {'content': 'old', 'format': 'markdown'})], ''), [todo]);
    expect(activitiesWithNotes([todo], ''), [todo]);
    final rich = act('note', {'content': 'old', 'pinned': true});
    expect(activitiesWithNotes([todo, rich], '')[1]['properties'], {'content': '', 'pinned': true, 'format': 'markdown'});
  });
}
