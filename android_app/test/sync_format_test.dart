import 'dart:convert';

import 'package:android_app/models/sync_models.dart';
import 'package:android_app/services/google_service.dart';
import 'package:android_app/services/sync_format.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('todos.jsonl', () {
    test('writes meta + items sorted per spec with optional keys omitted', () {
      const items = [
        TodoItem(src: 'gt', id: 'g1', list: 'My Tasks', listId: 'L1', title: 'Buy milk', due: '2026-11-06', notes: 'semi'),
        TodoItem(src: 'nt', id: 'n3', list: 'Work', listId: 'c2', title: 'Done thing', done: true),
        TodoItem(src: 'nt', id: 'n2', list: 'Work', listId: 'c2', title: 'B no due'),
        TodoItem(src: 'nt', id: 'n1', list: 'Work', listId: 'c2', title: 'Z due', due: '2026-11-01', pri: 'low'),
        TodoItem(
            src: 'nt',
            id: 'n0',
            list: 'Pets / Fish',
            listId: 'c1',
            title: 'Angelfish',
            due: '2026-11-05',
            pri: 'high',
            notes: 'multi\nline'),
      ];
      expect(
        buildTodosJsonl(items, 1762345678),
        '{"k":"meta","v":1,"ts":1762345678}\n'
        '{"k":"item","src":"nt","id":"n0","list":"Pets / Fish","title":"Angelfish","done":false,"due":"2026-11-05","pri":"high","notes":"multi\\nline"}\n'
        '{"k":"item","src":"nt","id":"n1","list":"Work","title":"Z due","done":false,"due":"2026-11-01","pri":"low"}\n'
        '{"k":"item","src":"nt","id":"n2","list":"Work","title":"B no due","done":false}\n'
        '{"k":"item","src":"nt","id":"n3","list":"Work","title":"Done thing","done":true}\n'
        '{"k":"item","src":"gt","id":"g1","list":"My Tasks","title":"Buy milk","done":false,"due":"2026-11-06","notes":"semi"}\n',
      );
    });

    test('marks nt items with encrypted or trimmed notes read-only', () {
      const encrypted = TodoItem(src: 'nt', id: 'e', list: 'L', listId: 'l', title: 'Secret', notesEditable: false);
      const normal = TodoItem(src: 'nt', id: 'n', list: 'L', listId: 'l', title: 'Plain', notes: 'short');
      const google = TodoItem(src: 'gt', id: 'g', list: 'L', listId: 'l', title: 'G', notes: 'x', notesEditable: false);
      expect(jsonEncode(todoWireJson(encrypted)),
          '{"k":"item","src":"nt","id":"e","list":"L","title":"Secret","done":false,"ro":true}');
      expect(jsonEncode(todoWireJson(normal)),
          '{"k":"item","src":"nt","id":"n","list":"L","title":"Plain","done":false,"notes":"short"}');
      expect(todoWireJson(google).containsKey('ro'), isFalse);
      final big = TodoItem(src: 'nt', id: 'b', list: 'L', listId: 'l', title: 'T', notes: 'a' * 20000);
      final m = todoWireJson(big);
      expect(m['ro'], isTrue);
      expect((m['notes'] as String).length, lessThan(16000));
      expect(utf8.encode(jsonEncode(m)).length, lessThanOrEqualTo(kMaxLineBytes));
    });

    test('keeps every line under 16 KB', () {
      final big = TodoItem(src: 'nt', id: 'x', list: 'L', listId: 'l', title: 'T', notes: '"é\n' * 20000);
      final lines = buildTodosJsonl([big], 1).split('\n');
      expect(lines[1].length, greaterThan(1000));
      expect(lines[1], endsWith(',"ro":true}'));
      expect((jsonDecode(lines[1]) as Map)['ro'], isTrue);
      expect(lines.every((l) => utf8.encode(l).length <= 16384), isTrue);
      expect(utf8.encode(lines[1]).length, greaterThan(15000));
    });
  });

  group('calendar.jsonl', () {
    test('converts Google events and writes them sorted', () {
      final allDay = parseGoogleEvent({
        'id': 'e2',
        'summary': 'Holiday',
        'start': {'date': '2026-11-05'},
        'end': {'date': '2026-11-08'},
        'description': 'Trip',
      }, 'cal@x', 'Paul')!;
      expect(allDay.allDay, isTrue);
      expect(allDay.day, 20261105);
      expect(allDay.endDay, 20261107);

      final single = parseGoogleEvent({
        'id': 'e3',
        'summary': 'Birthday',
        'start': {'date': '2026-12-31'},
        'end': {'date': '2027-01-01'},
      }, 'cal@x', 'Paul')!;
      expect(single.day, 20261231);
      expect(single.endDay, 20261231);

      final timed = parseGoogleEvent({
        'id': 'e1',
        'summary': 'Dentist',
        'location': 'High St',
        'description': 'x' * 1500,
        'start': {'dateTime': '2026-11-01T14:00:00+00:00'},
        'end': {'dateTime': '2026-11-01T15:00:00Z'},
      }, 'cal@x', 'Paul')!;
      expect(timed.start, DateTime.utc(2026, 11, 1, 14).millisecondsSinceEpoch ~/ 1000);
      expect(timed.end, timed.start! + 3600);
      expect(timed.desc!.length, 1000);

      expect(parseGoogleEvent({'id': 'c', 'status': 'cancelled', 'start': {'date': '2026-11-05'}}, 'c', 'P'), isNull);

      final out = buildCalendarJsonl([single, allDay, timed], 100, 10, 20).split('\n');
      expect(out[0], '{"k":"meta","v":1,"ts":100,"from":10,"to":20}');
      expect(out[1],
          '{"k":"ev","id":"e1","cal":"Paul","title":"Dentist","start":${timed.start},"end":${timed.end},"loc":"High St","desc":"${'x' * 1000}"}');
      expect(out[2], '{"k":"ev","id":"e2","cal":"Paul","title":"Holiday","allDay":true,"day":20261105,"endDay":20261107,"desc":"Trip"}');
      expect(out[3], '{"k":"ev","id":"e3","cal":"Paul","title":"Birthday","allDay":true,"day":20261231,"endDay":20261231}');
      expect(out[4], '');
      expect(out.length, 5);
    });

    test('inclusiveEndDay handles month/year boundaries', () {
      expect(inclusiveEndDay(20260228, '2026-03-01'), 20260228);
      expect(inclusiveEndDay(20261230, '2027-01-02'), 20270101);
      expect(inclusiveEndDay(20261105, null), 20261105);
    });
  });

  test('parseGoogleTask takes the UTC date part and skips empty titles', () {
    final t = parseGoogleTask(
        {'id': 'a', 'title': 'Buy milk', 'status': 'completed', 'due': '2026-11-06T00:00:00.000Z', 'notes': 'n'},
        'L1', 'My Tasks')!;
    expect(t.src, kSourceGoogleTasks);
    expect(t.due, '2026-11-06');
    expect(t.done, isTrue);
    expect(t.list, 'My Tasks');
    expect(parseGoogleTask({'id': 'b', 'title': '  '}, 'L1', 'My Tasks'), isNull);
  });

  group('todo_edits.jsonl', () {
    const content = '{"seq":3,"src":"nt","id":"a","notes":"first","ts":1}\n'
        '{"seq":4,"src":"nt","id":"b","notes":"bee","ts":2}\n'
        'garbage line\n'
        '{"seq":7,"src":"nt","id":"a","notes":"second","ts":3}\n'
        '\n'
        '{"seq":9,"src":"nt","id":"c","notes":"","ts":4}\n';

    test('last seq per id wins and max seq is tracked', () {
      final batch = parseTodoEdits(content);
      expect(batch.latest.length, 3);
      expect(batch.latest['a']!.notes, 'second');
      expect(batch.latest['a']!.seq, 7);
      expect(batch.latest['a']!.minSeq, 3);
      expect(batch.latest['c']!.notes, '');
      expect(batch.maxSeq, 9);
      expect(batch.minSeq, 3);
    });

    test('out-of-order lines still keep the highest seq', () {
      final batch = parseTodoEdits('{"seq":5,"id":"a","notes":"new"}\n{"seq":2,"id":"a","notes":"old"}\n');
      expect(batch.latest['a']!.notes, 'new');
      expect(batch.latest['a']!.minSeq, 2);
      expect(batch.latest['a']!.src, 'nt');
    });

    test('ackUpTo only covers lines of pushed ids', () {
      final batch = parseTodoEdits(content);
      expect(batch.ackUpTo({}), 9);
      expect(batch.ackUpTo({'c'}), 8);
      expect(batch.ackUpTo({'b'}), 3);
      expect(batch.ackUpTo({'a'}), isNull);
      expect(parseTodoEdits('').ackUpTo({}), isNull);
    });
  });
}
