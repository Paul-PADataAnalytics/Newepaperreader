import 'package:shared_preferences/shared_preferences.dart';

/// User choices for the todo / calendar sync, stored in SharedPreferences.
class SyncSettings {
  static final SyncSettings _instance = SyncSettings._internal();
  factory SyncSettings() => _instance;
  SyncSettings._internal();

  static const String defaultNootTaskUrl = 'https://apps.padanalytics.co.uk';

  static const String _kBaseUrl = 'sync_noottask_base_url';
  static const String _kExcludedCategories = 'sync_excluded_category_ids';
  static const String _kIncludeCompleted = 'sync_include_completed';
  static const String _kKnownTaskLists = 'sync_known_task_list_ids';
  static const String _kSelectedTaskLists = 'sync_selected_task_list_ids';
  static const String _kSelectedCalendars = 'sync_selected_calendar_ids';
  static const String _kLastSyncTime = 'sync_last_time';
  static const String _kLastSyncMessage = 'sync_last_message';
  static const String _kGoogleConnected = 'sync_google_connected';
  static const String _kGoogleAccount = 'sync_google_account';

  Future<SharedPreferences> get _prefs => SharedPreferences.getInstance();

  // NootTask ------------------------------------------------------------

  Future<String> getNootTaskUrl() async {
    final v = (await _prefs).getString(_kBaseUrl);
    return (v == null || v.trim().isEmpty) ? defaultNootTaskUrl : v;
  }

  Future<void> setNootTaskUrl(String url) async {
    var u = url.trim();
    while (u.endsWith('/')) {
      u = u.substring(0, u.length - 1);
    }
    await (await _prefs).setString(_kBaseUrl, u.isEmpty ? defaultNootTaskUrl : u);
  }

  /// Category ids that are NOT synced.
  Future<Set<String>> getExcludedCategoryIds() async =>
      ((await _prefs).getStringList(_kExcludedCategories) ?? const []).toSet();

  Future<void> setCategoryExcluded(String id, bool excluded) async {
    final set = await getExcludedCategoryIds();
    excluded ? set.add(id) : set.remove(id);
    await (await _prefs).setStringList(_kExcludedCategories, set.toList());
  }

  /// Applies to both NootTask and Google Tasks. Defaults to false.
  Future<bool> getIncludeCompleted() async => (await _prefs).getBool(_kIncludeCompleted) ?? false;

  Future<void> setIncludeCompleted(bool value) async => (await _prefs).setBool(_kIncludeCompleted, value);

  // Google --------------------------------------------------------------

  Future<bool> getGoogleConnected() async => (await _prefs).getBool(_kGoogleConnected) ?? false;

  Future<String?> getGoogleAccount() async => (await _prefs).getString(_kGoogleAccount);

  Future<void> setGoogleConnected(bool connected, {String? account}) async {
    final p = await _prefs;
    await p.setBool(_kGoogleConnected, connected);
    if (account != null) {
      await p.setString(_kGoogleAccount, account);
    } else if (!connected) {
      await p.remove(_kGoogleAccount);
    }
  }

  Future<Set<String>> getSelectedTaskListIds() async =>
      ((await _prefs).getStringList(_kSelectedTaskLists) ?? const []).toSet();

  /// Returns the selected ids among [allIds]; lists never seen before are
  /// selected by default.
  Future<Set<String>> resolveSelectedTaskLists(Iterable<String> allIds) async {
    final p = await _prefs;
    final known = (p.getStringList(_kKnownTaskLists) ?? const []).toSet();
    final selected = (p.getStringList(_kSelectedTaskLists) ?? const []).toSet();
    var changed = false;
    for (final id in allIds) {
      if (known.add(id)) {
        selected.add(id);
        changed = true;
      }
    }
    if (changed) {
      await p.setStringList(_kKnownTaskLists, known.toList());
      await p.setStringList(_kSelectedTaskLists, selected.toList());
    }
    return allIds.where(selected.contains).toSet();
  }

  Future<void> setTaskListSelected(String id, bool selected) async {
    final p = await _prefs;
    final set = (p.getStringList(_kSelectedTaskLists) ?? const []).toSet();
    selected ? set.add(id) : set.remove(id);
    final known = (p.getStringList(_kKnownTaskLists) ?? const []).toSet()..add(id);
    await p.setStringList(_kSelectedTaskLists, set.toList());
    await p.setStringList(_kKnownTaskLists, known.toList());
  }

  /// Null until the user changes the selection (then the primary calendar
  /// is used).
  Future<Set<String>?> getSelectedCalendarIds() async => (await _prefs).getStringList(_kSelectedCalendars)?.toSet();

  Future<void> setSelectedCalendarIds(Set<String> ids) async =>
      (await _prefs).setStringList(_kSelectedCalendars, ids.toList());

  // Status --------------------------------------------------------------

  Future<DateTime?> getLastSyncTime() async {
    final ms = (await _prefs).getInt(_kLastSyncTime);
    return ms == null ? null : DateTime.fromMillisecondsSinceEpoch(ms);
  }

  Future<String?> getLastSyncMessage() async => (await _prefs).getString(_kLastSyncMessage);

  Future<void> setLastSync(DateTime time, String message) async {
    final p = await _prefs;
    await p.setInt(_kLastSyncTime, time.millisecondsSinceEpoch);
    await p.setString(_kLastSyncMessage, message);
  }
}
