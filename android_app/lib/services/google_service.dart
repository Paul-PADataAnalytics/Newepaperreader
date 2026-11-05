import 'dart:async';
import 'dart:convert';

import 'package:google_sign_in/google_sign_in.dart';
import 'package:http/http.dart' as http;

import '../models/sync_models.dart';
import 'sync_format.dart';
import 'sync_settings.dart';

class GoogleException implements Exception {
  final String message;
  GoogleException(this.message);
  @override
  String toString() => message;
}

class GoogleAuthException extends GoogleException {
  GoogleAuthException([super.message = 'Google account not connected (connect it in Todo & calendar sync)']);
}

/// Converts a Google Tasks API task to a todo, or null if it has no title.
TodoItem? parseGoogleTask(Map<String, dynamic> json, String listId, String listTitle) {
  final title = (json['title'] is String ? json['title'] as String : '').trim();
  final id = json['id'];
  if (title.isEmpty || id is! String || json['deleted'] == true) return null;
  final due = json['due'];
  final notes = json['notes'];
  return TodoItem(
    src: kSourceGoogleTasks,
    id: id,
    list: listTitle,
    listId: listId,
    title: title,
    done: json['status'] == 'completed',
    // Google stores the due date as midnight UTC: the date part is the date.
    due: (due is String && due.length >= 10) ? due.substring(0, 10) : null,
    notes: (notes is String && notes.trim().isNotEmpty) ? notes : null,
    notesEditable: false,
  );
}

/// Turns simple HTML (as produced by the Calendar web UI) into plain text.
String htmlToPlainText(String s) {
  if (!s.contains('<') && !s.contains('&')) return s;
  var t = s
      .replaceAll(RegExp(r'<br\s*/?>', caseSensitive: false), '\n')
      .replaceAll(RegExp(r'</(p|div|li)>', caseSensitive: false), '\n')
      .replaceAll(RegExp(r'<li[^>]*>', caseSensitive: false), '- ')
      .replaceAll(RegExp(r'<[^>]+>'), '');
  t = t
      .replaceAll('&nbsp;', ' ')
      .replaceAll('&lt;', '<')
      .replaceAll('&gt;', '>')
      .replaceAll('&quot;', '"')
      .replaceAll('&#39;', "'")
      .replaceAll('&amp;', '&');
  return t.replaceAll(RegExp(r'\n{3,}'), '\n\n').trim();
}

/// Converts a Calendar API event; null for cancelled / unusable events.
CalendarEvent? parseGoogleEvent(Map<String, dynamic> json, String calId, String calName) {
  if (json['status'] == 'cancelled') return null;
  final id = json['id'];
  if (id is! String) return null;
  final start = json['start'] as Map<String, dynamic>? ?? const {};
  final end = json['end'] as Map<String, dynamic>? ?? const {};
  final summary = (json['summary'] is String ? json['summary'] as String : '').trim();
  final loc = (json['location'] is String ? json['location'] as String : '').trim();
  final rawDesc = json['description'] is String ? htmlToPlainText(json['description'] as String) : '';
  final desc = truncateChars(rawDesc, kMaxEventDescChars);
  final title = summary.isEmpty ? '(No title)' : summary;

  if (start['date'] is String) {
    final day = parseDateToDayInt(start['date'] as String);
    if (day == null) return null;
    return CalendarEvent(
      id: id,
      calId: calId,
      cal: calName,
      title: title,
      allDay: true,
      day: day,
      endDay: inclusiveEndDay(day, end['date'] as String?),
      loc: loc.isEmpty ? null : loc,
      desc: desc.isEmpty ? null : desc,
    );
  }
  final s = DateTime.tryParse((start['dateTime'] ?? '') as String);
  if (s == null) return null;
  final e = DateTime.tryParse((end['dateTime'] ?? '') as String) ?? s;
  return CalendarEvent(
    id: id,
    calId: calId,
    cal: calName,
    title: title,
    start: s.millisecondsSinceEpoch ~/ 1000,
    end: e.millisecondsSinceEpoch ~/ 1000,
    loc: loc.isEmpty ? null : loc,
    desc: desc.isEmpty ? null : desc,
  );
}

/// Read-only access to the phone user's Google Tasks and Calendar.
class GoogleService {
  static final GoogleService _instance = GoogleService._internal();
  factory GoogleService() => _instance;
  GoogleService._internal();

  static const List<String> scopes = [
    'https://www.googleapis.com/auth/tasks.readonly',
    'https://www.googleapis.com/auth/calendar.readonly',
  ];
  static const String _serverClientId = String.fromEnvironment('GOOGLE_SERVER_CLIENT_ID');
  static const Duration timeout = Duration(seconds: 20);

  final http.Client _client = http.Client();
  Future<void>? _init;

  Future<void> _ensureInit() => _init ??= GoogleSignIn.instance
      .initialize(serverClientId: _serverClientId.isEmpty ? null : _serverClientId)
      .catchError((Object e) {
        _init = null;
        throw e;
      });

  Future<bool> isConnected() => SyncSettings().getGoogleConnected();

  /// Interactive: asks the user to pick an account and grant read access.
  /// Returns a label for the account (its primary calendar id, usually the
  /// e-mail address).
  Future<String?> connect() async {
    await _ensureInit();
    try {
      await GoogleSignIn.instance.authorizationClient.authorizeScopes(scopes);
    } on GoogleSignInException catch (e) {
      if (e.code == GoogleSignInExceptionCode.canceled) throw GoogleException('Google sign-in cancelled');
      final detail = e.description ?? e.code.name;
      if (detail.contains('UNREGISTERED_ON_API_CONSOLE') || detail.contains('DEVELOPER_ERROR')) {
        throw GoogleException('This app is not registered with Google yet. Create an Android OAuth client '
            'for com.example.android_app with this build\'s SHA-1 (see android_app/README.md).');
      }
      throw GoogleException('Google sign-in failed: $detail');
    }
    String? account;
    try {
      final primary = await _getJson(Uri.https('www.googleapis.com', '/calendar/v3/calendars/primary'));
      account = primary['id'] as String?;
    } catch (_) {}
    await SyncSettings().setGoogleConnected(true, account: account);
    return account;
  }

  /// Revokes this app's access so the next [connect] asks again (and can
  /// pick another account).
  Future<void> disconnect() async {
    try {
      await _ensureInit();
      final a = await GoogleSignIn.instance.authorizationClient.authorizationForScopes(scopes);
      if (a != null) {
        try {
          await _client.post(Uri.https('oauth2.googleapis.com', '/revoke', {'token': a.accessToken})).timeout(timeout);
        } catch (_) {}
        await GoogleSignIn.instance.authorizationClient.clearAuthorizationToken(accessToken: a.accessToken);
      }
      await GoogleSignIn.instance.disconnect();
    } catch (_) {}
    await SyncSettings().setGoogleConnected(false);
  }

  /// A fresh access token without user interaction.
  Future<String> _token() async {
    await _ensureInit();
    GoogleSignInClientAuthorization? a;
    try {
      a = await GoogleSignIn.instance.authorizationClient.authorizationForScopes(scopes);
    } on GoogleSignInException catch (e) {
      throw GoogleException('Google authorization failed: ${e.description ?? e.code.name}');
    }
    if (a == null) throw GoogleAuthException('Google access expired - reconnect in Todo & calendar sync');
    return a.accessToken;
  }

  Future<Map<String, dynamic>> _getJson(Uri uri) async {
    var token = await _token();
    Future<http.Response> get(String t) =>
        _client.get(uri, headers: {'Authorization': 'Bearer $t'}).timeout(timeout);
    var r = await get(token);
    if (r.statusCode == 401) {
      try {
        await GoogleSignIn.instance.authorizationClient.clearAuthorizationToken(accessToken: token);
      } catch (_) {}
      token = await _token();
      r = await get(token);
      if (r.statusCode == 401) throw GoogleAuthException();
    }
    if (r.statusCode != 200) {
      var msg = 'HTTP ${r.statusCode}';
      try {
        final e = (jsonDecode(r.body) as Map<String, dynamic>)['error'];
        if (e is Map && e['message'] is String) msg = e['message'] as String;
      } catch (_) {}
      throw GoogleException('Google: $msg');
    }
    return jsonDecode(r.body) as Map<String, dynamic>;
  }

  Future<List<Map<String, dynamic>>> _pagedItems(Uri Function(String? pageToken) build, {int max = 1 << 30}) async {
    final items = <Map<String, dynamic>>[];
    String? pageToken;
    do {
      final j = await _getJson(build(pageToken));
      items.addAll((j['items'] as List<dynamic>? ?? const []).whereType<Map<String, dynamic>>());
      pageToken = j['nextPageToken'] as String?;
    } while (pageToken != null && pageToken.isNotEmpty && items.length < max);
    return items;
  }

  Future<List<GoogleCollection>> listTaskLists() async {
    final items = await _pagedItems((pt) => Uri.https('tasks.googleapis.com', '/tasks/v1/users/@me/lists', {
          'maxResults': '100',
          'pageToken': ?pt,
        }));
    return [
      for (final i in items)
        if (i['id'] is String) GoogleCollection(i['id'] as String, (i['title'] ?? '') as String),
    ];
  }

  Future<List<TodoItem>> fetchTasks(String listId, bool includeCompleted, {String listTitle = ''}) async {
    final items = await _pagedItems((pt) => Uri(
          scheme: 'https',
          host: 'tasks.googleapis.com',
          pathSegments: ['tasks', 'v1', 'lists', listId, 'tasks'],
          queryParameters: {
            'showCompleted': '$includeCompleted',
            'showHidden': '$includeCompleted',
            'maxResults': '100',
            'pageToken': ?pt,
          },
        ));
    return [
      for (final i in items) ?parseGoogleTask(i, listId, listTitle),
    ];
  }

  /// Calendars the user owns (accessRole == owner).
  Future<List<GoogleCollection>> listOwnedCalendars() async {
    final items = await _pagedItems((pt) => Uri.https('www.googleapis.com', '/calendar/v3/users/me/calendarList', {
          'pageToken': ?pt,
        }));
    return [
      for (final c in items)
        if (c['accessRole'] == 'owner' && c['id'] is String)
          GoogleCollection(c['id'] as String, (c['summaryOverride'] ?? c['summary'] ?? c['id']) as String,
              primary: c['primary'] == true),
    ];
  }

  Future<List<CalendarEvent>> fetchEvents(List<GoogleCollection> calendars, DateTime from, DateTime to) async {
    final events = <CalendarEvent>[];
    for (final cal in calendars) {
      final items = await _pagedItems(
          (pt) => Uri(
                scheme: 'https',
                host: 'www.googleapis.com',
                pathSegments: ['calendar', 'v3', 'calendars', cal.id, 'events'],
                queryParameters: {
                  'singleEvents': 'true',
                  'orderBy': 'startTime',
                  'timeMin': from.toUtc().toIso8601String(),
                  'timeMax': to.toUtc().toIso8601String(),
                  'maxResults': '250',
                  'pageToken': ?pt,
                },
              ),
          max: kMaxCalendarEvents);
      for (final i in items) {
        final e = parseGoogleEvent(i, cal.id, cal.title);
        if (e != null) events.add(e);
      }
    }
    return sortEvents(events).take(kMaxCalendarEvents).toList();
  }
}
