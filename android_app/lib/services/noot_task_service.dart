import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';

import '../models/sync_models.dart';
import 'sync_format.dart';
import 'sync_settings.dart';

class NootTaskException implements Exception {
  final String message;
  NootTaskException(this.message);
  @override
  String toString() => message;
}

/// The stored login is missing or could not be refreshed.
class NootTaskAuthException extends NootTaskException {
  NootTaskAuthException([super.message = 'NootTask login required']);
}

class NootTaskNotFoundException extends NootTaskException {
  NootTaskNotFoundException(String id) : super('Noot $id not found');
}

/// The todo's notes are encrypted on the server and cannot be edited here.
class NootTaskEncryptedException extends NootTaskException {
  NootTaskEncryptedException() : super('Notes are encrypted');
}

/// Todos plus categories derived from one listing of the user's noots.
class NootTaskData {
  final List<TodoItem> todos;

  /// All categories (sorted), followed by "Uncategorised".
  final List<NootCategory> categories;

  const NootTaskData(this.todos, this.categories);
}

const _priorities = {'low', 'medium', 'high'};

List<Map<String, dynamic>> _activities(Map<String, dynamic> noot) =>
    (noot['activities'] as List<dynamic>? ?? const []).whereType<Map<String, dynamic>>().toList();

bool _isEncrypted(Map<String, dynamic> activity) {
  final p = activity['properties'];
  return p is Map && p['encrypted'] == true;
}

/// Properties of the first non-encrypted activity of [type], or null.
Map<String, dynamic>? _props(List<Map<String, dynamic>> acts, String type) {
  for (final a in acts) {
    if (a['activityType'] != type || _isEncrypted(a)) continue;
    final p = a['properties'];
    if (p is Map<String, dynamic>) return p;
  }
  return null;
}

String _str(dynamic v) => v is String ? v.trim() : '';

/// Turns the noots returned by `GET /api/noots` into todos and categories.
NootTaskData parseNootTaskData(List<Map<String, dynamic>> noots) {
  final live = noots.where((n) => n['deleted'] != true && n['id'] is String).toList();

  final parentNames = <String, String>{};
  for (final n in live) {
    final p = _props(_activities(n), 'parent-category');
    if (p != null) parentNames[n['id'] as String] = _str(p['name']);
  }

  final categoryNames = <String, String>{};
  for (final n in live) {
    final p = _props(_activities(n), 'category-definition');
    if (p == null) continue;
    var name = _str(p['name']);
    if (name.isEmpty) name = 'Unnamed category';
    final parent = parentNames[_str(p['parent'])];
    categoryNames[n['id'] as String] = (parent != null && parent.isNotEmpty) ? '$parent / $name' : name;
  }

  final todos = <TodoItem>[];
  for (final n in live) {
    final acts = _activities(n);
    final todo = _props(acts, 'todo');
    if (todo == null) continue;

    var title = _str(_props(acts, 'title')?['title']);
    if (title.isEmpty) title = _str(todo['title']);
    if (title.isEmpty) title = '(untitled)';

    final completion = _props(acts, 'completion');
    final done = (completion != null && completion['completed'] is bool)
        ? completion['completed'] as bool
        : todo['completed'] == true;

    final priRaw = _str(todo['priority']).toLowerCase();

    String? due;
    final start = _str(_props(acts, 'schedule')?['startTime']);
    if (start.isNotEmpty) {
      final dt = DateTime.tryParse(start);
      if (dt != null) due = formatLocalDate(dt);
    }

    final hasEncryptedNote = acts.any((a) => a['activityType'] == 'note' && _isEncrypted(a));
    final note = _props(acts, 'note');
    final content = note?['content'];
    final notes = hasEncryptedNote ? null : (content is String && content.isNotEmpty ? content : null);

    var categoryId = kUncategorisedId;
    var categoryName = kUncategorisedName;
    for (final a in acts) {
      if (a['activityType'] != 'category-assignment' || _isEncrypted(a)) continue;
      final p = a['properties'];
      final cid = p is Map ? _str(p['categoryNootId']) : '';
      final name = categoryNames[cid];
      if (name != null) {
        categoryId = cid;
        categoryName = name;
        break;
      }
    }

    todos.add(TodoItem(
      src: kSourceNootTask,
      id: n['id'] as String,
      list: categoryName,
      listId: categoryId,
      title: title,
      done: done,
      due: due,
      pri: _priorities.contains(priRaw) ? priRaw : null,
      notes: notes,
      notesEditable: !hasEncryptedNote,
    ));
  }

  final categories = categoryNames.entries.map((e) => NootCategory(e.key, e.value)).toList()
    ..sort((a, b) => a.name.toLowerCase().compareTo(b.name.toLowerCase()));
  categories.add(const NootCategory(kUncategorisedId, kUncategorisedName));
  return NootTaskData(todos, categories);
}

/// Returns a copy of [activities] with the note content set to [text]
/// (appending a markdown note activity if there is none).
List<Map<String, dynamic>> activitiesWithNotes(List<Map<String, dynamic>> activities, String text) {
  if (activities.any((a) => a['activityType'] == 'note' && _isEncrypted(a))) {
    throw NootTaskEncryptedException();
  }
  final out = <Map<String, dynamic>>[];
  var replaced = false;
  for (final a in activities) {
    if (!replaced && a['activityType'] == 'note') {
      final props = Map<String, dynamic>.from(a['properties'] as Map? ?? const {});
      // Clearing the notes removes a plain note activity entirely, so the
      // noot ends up as if it never had notes.
      if (text.isEmpty && props.keys.every((k) => k == 'content' || k == 'format')) {
        replaced = true;
        continue;
      }
      props['content'] = text;
      props.putIfAbsent('format', () => 'markdown');
      out.add({...a, 'properties': props});
      replaced = true;
    } else {
      out.add(a);
    }
  }
  if (!replaced && text.isNotEmpty) {
    out.add({
      'activityType': 'note',
      'properties': {'content': text, 'format': 'markdown'},
      'version': '1.0.0',
    });
  }
  return out;
}

/// Client for the NootTask REST API (login, todos, note updates).
class NootTaskService {
  static final NootTaskService _instance = NootTaskService._internal();
  factory NootTaskService() => _instance;
  NootTaskService._internal();

  static const String _kUser = 'noottask_username';
  static const String _kAccess = 'noottask_access_token';
  static const String _kRefresh = 'noottask_refresh_token';
  static const Duration timeout = Duration(seconds: 20);
  static const int _pageSize = 200;

  final http.Client _client = http.Client();
  Future<String>? _refreshing;

  /// Username of the stored login, or null when logged out.
  final ValueNotifier<String?> loggedInUser = ValueNotifier(null);

  Future<SharedPreferences> get _prefs => SharedPreferences.getInstance();

  /// Loads the stored login into [loggedInUser].
  Future<bool> isLoggedIn() async {
    final p = await _prefs;
    final ok = (p.getString(_kRefresh) ?? '').isNotEmpty;
    loggedInUser.value = ok ? p.getString(_kUser) : null;
    return ok;
  }

  Future<String?> storedUsername() async => (await _prefs).getString(_kUser);

  Future<Uri> _uri(String path, [Map<String, String>? query]) async {
    final base = await SyncSettings().getNootTaskUrl();
    return Uri.parse('$base$path').replace(queryParameters: query);
  }

  String _errorFrom(http.Response r, String fallback) {
    try {
      final j = jsonDecode(r.body);
      if (j is Map && j['error'] is String) return j['error'] as String;
    } catch (_) {}
    return '$fallback (HTTP ${r.statusCode})';
  }

  Future<void> login(String username, String password) async {
    final r = await _client
        .post(await _uri('/api/auth/login'),
            headers: {'Content-Type': 'application/json'},
            body: jsonEncode({'username': username, 'password': password}))
        .timeout(timeout);
    if (r.statusCode != 200) throw NootTaskException(_errorFrom(r, 'Login failed'));
    final j = jsonDecode(r.body) as Map<String, dynamic>;
    final access = _str(j['accessToken']);
    final refresh = _str(j['refreshToken']);
    if (access.isEmpty || refresh.isEmpty) throw NootTaskException('Login failed: no token returned');
    final p = await _prefs;
    await p.setString(_kUser, username);
    await p.setString(_kAccess, access);
    await p.setString(_kRefresh, refresh);
    loggedInUser.value = username;
  }

  Future<void> logout() => _dropTokens();

  Future<void> _dropTokens() async {
    final p = await _prefs;
    await p.remove(_kAccess);
    await p.remove(_kRefresh);
    loggedInUser.value = null;
  }

  Future<String> _refreshToken() => _refreshing ??= _doRefresh().whenComplete(() => _refreshing = null);

  Future<String> _doRefresh() async {
    final p = await _prefs;
    final refresh = p.getString(_kRefresh) ?? '';
    if (refresh.isEmpty) throw NootTaskAuthException();
    final r = await _client
        .post(await _uri('/api/auth/refresh'),
            headers: {'Content-Type': 'application/json'}, body: jsonEncode({'refreshToken': refresh}))
        .timeout(timeout);
    if (r.statusCode == 400 || r.statusCode == 401 || r.statusCode == 403) {
      await _dropTokens();
      throw NootTaskAuthException();
    }
    if (r.statusCode != 200) throw NootTaskException(_errorFrom(r, 'Token refresh failed'));
    final j = jsonDecode(r.body) as Map<String, dynamic>;
    final access = _str(j['accessToken']);
    if (access.isEmpty) throw NootTaskAuthException();
    await p.setString(_kAccess, access);
    final newRefresh = _str(j['refreshToken']);
    if (newRefresh.isNotEmpty) await p.setString(_kRefresh, newRefresh);
    return access;
  }

  Future<http.Response> _send(String method, Uri uri, {Object? body}) async {
    final p = await _prefs;
    var token = p.getString(_kAccess) ?? '';
    if (token.isEmpty) {
      if ((p.getString(_kRefresh) ?? '').isEmpty) throw NootTaskAuthException();
      token = await _refreshToken();
    }
    Future<http.Response> attempt(String t) {
      final req = http.Request(method, uri)..headers['Authorization'] = 'Bearer $t';
      if (body != null) {
        req.headers['Content-Type'] = 'application/json';
        req.body = jsonEncode(body);
      }
      return _client.send(req).then(http.Response.fromStream).timeout(timeout);
    }

    var r = await attempt(token);
    if (r.statusCode == 401) {
      r = await attempt(await _refreshToken());
      if (r.statusCode == 401) {
        await _dropTokens();
        throw NootTaskAuthException();
      }
    }
    return r;
  }

  /// Every (non-deleted) noot of the user.
  Future<List<Map<String, dynamic>>> fetchNoots() async {
    final all = <Map<String, dynamic>>[];
    for (var offset = 0;; offset += _pageSize) {
      final r = await _send('GET', await _uri('/api/noots', {'limit': '$_pageSize', 'offset': '$offset'}));
      if (r.statusCode != 200) throw NootTaskException(_errorFrom(r, 'Could not load todos'));
      final page = ((jsonDecode(r.body) as Map<String, dynamic>)['noots'] as List<dynamic>? ?? const [])
          .whereType<Map<String, dynamic>>()
          .toList();
      all.addAll(page);
      if (page.length < _pageSize) break;
    }
    return all;
  }

  Future<NootTaskData> fetchData() async => parseNootTaskData(await fetchNoots());

  Future<List<TodoItem>> fetchTodos() async => (await fetchData()).todos;

  Future<List<NootCategory>> fetchCategories() async => (await fetchData()).categories;

  /// Replaces the notes of todo [id] with [text].
  Future<void> updateNotes(String id, String text) async {
    final path = '/api/noots/${Uri.encodeComponent(id)}';
    final r = await _send('GET', await _uri(path));
    if (r.statusCode == 404) throw NootTaskNotFoundException(id);
    if (r.statusCode != 200) throw NootTaskException(_errorFrom(r, 'Could not load noot'));
    var j = jsonDecode(r.body) as Map<String, dynamic>;
    if (j['noot'] is Map<String, dynamic>) j = j['noot'] as Map<String, dynamic>;
    if (j['deleted'] == true) throw NootTaskNotFoundException(id);
    final activities = activitiesWithNotes(_activities(j), text);
    final put = await _send('PUT', await _uri(path), body: {'id': id, 'activities': activities});
    if (put.statusCode == 404) throw NootTaskNotFoundException(id);
    if (put.statusCode != 200) throw NootTaskException(_errorFrom(put, 'Could not save notes'));
  }
}
