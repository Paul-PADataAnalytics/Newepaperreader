import 'package:flutter/material.dart';

import '../models/sync_models.dart';
import '../services/ble_service.dart';
import '../services/google_service.dart';
import '../services/noot_task_service.dart';
import '../services/sync_service.dart';
import '../services/sync_settings.dart';

/// Configures what the todo / calendar sync sends to the EPD-Reader.
class SyncSettingsScreen extends StatefulWidget {
  const SyncSettingsScreen({super.key});

  @override
  State<SyncSettingsScreen> createState() => _SyncSettingsScreenState();
}

class _SyncSettingsScreenState extends State<SyncSettingsScreen> {
  final SyncSettings _settings = SyncSettings();
  final NootTaskService _nootTask = NootTaskService();
  final GoogleService _google = GoogleService();
  final SyncService _sync = SyncService();

  final _urlCtrl = TextEditingController();
  final _userCtrl = TextEditingController();
  final _passCtrl = TextEditingController();

  bool _includeCompleted = false;

  bool _ntLoggedIn = false;
  bool _ntBusy = false;
  String? _ntError;
  List<NootCategory>? _categories;
  Set<String> _excluded = {};

  bool _gConnected = false;
  String? _gAccount;
  bool _gBusy = false;
  String? _gError;
  List<GoogleCollection>? _taskLists;
  Set<String> _selectedLists = {};
  List<GoogleCollection>? _calendars;
  Set<String> _selectedCalendars = {};

  @override
  void initState() {
    super.initState();
    _init();
  }

  @override
  void dispose() {
    _urlCtrl.dispose();
    _userCtrl.dispose();
    _passCtrl.dispose();
    super.dispose();
  }

  void _showSnack(String message) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
  }

  Future<void> _init() async {
    _urlCtrl.text = await _settings.getNootTaskUrl();
    _userCtrl.text = await _nootTask.storedUsername() ?? '';
    _includeCompleted = await _settings.getIncludeCompleted();
    _excluded = await _settings.getExcludedCategoryIds();
    _ntLoggedIn = await _nootTask.isLoggedIn();
    _gConnected = await _google.isConnected();
    _gAccount = await _settings.getGoogleAccount();
    if (mounted) setState(() {});
    if (_ntLoggedIn) _loadCategories();
    if (_gConnected) _loadGoogle();
  }

  // NootTask -------------------------------------------------------------

  Future<void> _saveUrl() async {
    await _settings.setNootTaskUrl(_urlCtrl.text);
    _urlCtrl.text = await _settings.getNootTaskUrl();
  }

  Future<void> _login() async {
    setState(() {
      _ntBusy = true;
      _ntError = null;
    });
    try {
      await _saveUrl();
      await _nootTask.login(_userCtrl.text.trim(), _passCtrl.text);
      _passCtrl.clear();
      _ntLoggedIn = true;
      _loadCategories();
    } catch (e) {
      _ntError = '$e';
    } finally {
      if (mounted) setState(() => _ntBusy = false);
    }
  }

  Future<void> _logout() async {
    await _nootTask.logout();
    await _sync.clearCache(kSourceNootTask);
    setState(() {
      _ntLoggedIn = false;
      _categories = null;
    });
  }

  Future<void> _loadCategories() async {
    setState(() {
      _ntBusy = true;
      _ntError = null;
    });
    try {
      _categories = await _nootTask.fetchCategories().timeout(const Duration(seconds: 60));
    } on NootTaskAuthException catch (e) {
      _ntLoggedIn = false;
      _ntError = e.message;
    } catch (e) {
      _ntError = 'Could not load categories: $e';
    } finally {
      if (mounted) setState(() => _ntBusy = false);
    }
  }

  Future<void> _toggleCategory(String id, bool sync) async {
    await _settings.setCategoryExcluded(id, !sync);
    _excluded = await _settings.getExcludedCategoryIds();
    if (mounted) setState(() {});
  }

  // Google ---------------------------------------------------------------

  Future<void> _connectGoogle() async {
    setState(() {
      _gBusy = true;
      _gError = null;
    });
    try {
      _gAccount = await _google.connect();
      _gConnected = true;
      await _loadGoogle();
    } catch (e) {
      _gError = '$e';
    } finally {
      if (mounted) setState(() => _gBusy = false);
    }
  }

  Future<void> _disconnectGoogle() async {
    await _google.disconnect();
    await _sync.clearCache(kSourceGoogleTasks);
    await _sync.clearCache('cal');
    setState(() {
      _gConnected = false;
      _gAccount = null;
      _taskLists = null;
      _calendars = null;
    });
  }

  Future<void> _loadGoogle() async {
    setState(() {
      _gBusy = true;
      _gError = null;
    });
    try {
      final lists = await _google.listTaskLists().timeout(const Duration(seconds: 30));
      _selectedLists = await _settings.resolveSelectedTaskLists(lists.map((l) => l.id));
      _taskLists = lists;
      final cals = await _google.listOwnedCalendars().timeout(const Duration(seconds: 30));
      _selectedCalendars =
          await _settings.getSelectedCalendarIds() ?? cals.where((c) => c.primary).map((c) => c.id).toSet();
      _calendars = cals;
    } catch (e) {
      _gError = '$e';
    } finally {
      if (mounted) setState(() => _gBusy = false);
    }
  }

  Future<void> _toggleList(String id, bool selected) async {
    await _settings.setTaskListSelected(id, selected);
    setState(() => selected ? _selectedLists.add(id) : _selectedLists.remove(id));
  }

  Future<void> _toggleCalendar(String id, bool selected) async {
    final set = {..._selectedCalendars};
    selected ? set.add(id) : set.remove(id);
    await _settings.setSelectedCalendarIds(set);
    setState(() => _selectedCalendars = set);
  }

  // UI -------------------------------------------------------------------

  Widget _header(String text) => Padding(
        padding: const EdgeInsets.fromLTRB(4, 20, 4, 8),
        child: Text(text,
            style: const TextStyle(fontSize: 13, fontWeight: FontWeight.bold, color: Color(0xFF00B4D8), letterSpacing: 1)),
      );

  Widget _error(String? e) => e == null
      ? const SizedBox.shrink()
      : Padding(
          padding: const EdgeInsets.symmetric(vertical: 8),
          child: Text(e, style: const TextStyle(color: Colors.redAccent)),
        );

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Todo & calendar sync'),
        actions: [
          ValueListenableBuilder<bool>(
            valueListenable: _sync.running,
            builder: (context, running, _) => IconButton(
              tooltip: 'Sync with device now',
              icon: running
                  ? const SizedBox(width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2))
                  : const Icon(Icons.sync),
              onPressed: running
                  ? null
                  : () async {
                      if (!BleService().isConnected) {
                        _showSnack('Connect to the EPD-Reader first (Device tab).');
                        return;
                      }
                      await _sync.syncDevice();
                      _showSnack(_sync.status.value);
                    },
            ),
          ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.fromLTRB(16, 0, 16, 32),
        children: [
          _header('GENERAL'),
          Card(
            child: SwitchListTile(
              title: const Text('Include completed items'),
              subtitle: const Text('Applies to NootTask and Google Tasks'),
              value: _includeCompleted,
              onChanged: (v) async {
                await _settings.setIncludeCompleted(v);
                setState(() => _includeCompleted = v);
              },
            ),
          ),
          _header('NOOTTASK'),
          _nootTaskCard(),
          _header('GOOGLE'),
          _googleCard(),
          const SizedBox(height: 16),
          const Text(
            'Everything is synced to the EPD-Reader whenever it connects over Bluetooth. '
            'The reader itself never goes online. Note edits made on the reader are saved to NootTask; '
            'Google items are read-only.',
            style: TextStyle(color: Colors.white54, fontSize: 12),
          ),
        ],
      ),
    );
  }

  Widget _nootTaskCard() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            TextField(
              controller: _urlCtrl,
              enabled: !_ntLoggedIn && !_ntBusy,
              keyboardType: TextInputType.url,
              decoration: const InputDecoration(labelText: 'Server URL', hintText: SyncSettings.defaultNootTaskUrl),
              onSubmitted: (_) => _saveUrl(),
            ),
            const SizedBox(height: 12),
            if (_ntLoggedIn) ...[
              Row(
                children: [
                  const Icon(Icons.check_circle, color: Colors.greenAccent, size: 20),
                  const SizedBox(width: 8),
                  Expanded(child: Text('Logged in as ${_nootTask.loggedInUser.value ?? _userCtrl.text}')),
                  TextButton(onPressed: _ntBusy ? null : _logout, child: const Text('Log out')),
                ],
              ),
            ] else ...[
              TextField(
                controller: _userCtrl,
                enabled: !_ntBusy,
                autocorrect: false,
                decoration: const InputDecoration(labelText: 'Username'),
              ),
              TextField(
                controller: _passCtrl,
                enabled: !_ntBusy,
                obscureText: true,
                decoration: const InputDecoration(labelText: 'Password'),
                onSubmitted: (_) => _login(),
              ),
              const SizedBox(height: 12),
              FilledButton(onPressed: _ntBusy ? null : _login, child: const Text('Log in')),
            ],
            if (_ntBusy) const Padding(padding: EdgeInsets.only(top: 12), child: LinearProgressIndicator()),
            _error(_ntError),
            if (_ntLoggedIn) ...[
              const SizedBox(height: 8),
              Row(
                children: [
                  const Expanded(child: Text('Categories', style: TextStyle(fontWeight: FontWeight.bold))),
                  TextButton.icon(
                    onPressed: _ntBusy ? null : _loadCategories,
                    icon: const Icon(Icons.refresh, size: 18),
                    label: const Text('Refresh categories'),
                  ),
                ],
              ),
              for (final c in _categories ?? const <NootCategory>[])
                SwitchListTile(
                  dense: true,
                  contentPadding: EdgeInsets.zero,
                  title: Text(c.name),
                  subtitle: Text(_excluded.contains(c.id) ? "Don't sync" : 'Sync'),
                  value: !_excluded.contains(c.id),
                  onChanged: (v) => _toggleCategory(c.id, v),
                ),
            ],
          ],
        ),
      ),
    );
  }

  Widget _googleCard() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            if (_gConnected)
              Row(
                children: [
                  const Icon(Icons.check_circle, color: Colors.greenAccent, size: 20),
                  const SizedBox(width: 8),
                  Expanded(child: Text('Connected${_gAccount != null ? ' as $_gAccount' : ''}')),
                  TextButton(onPressed: _gBusy ? null : _disconnectGoogle, child: const Text('Disconnect')),
                ],
              )
            else
              FilledButton.icon(
                onPressed: _gBusy ? null : _connectGoogle,
                icon: const Icon(Icons.account_circle),
                label: const Text('Connect Google account'),
              ),
            if (_gBusy) const Padding(padding: EdgeInsets.only(top: 12), child: LinearProgressIndicator()),
            _error(_gError),
            if (_gConnected) ...[
              const SizedBox(height: 8),
              Row(
                children: [
                  const Expanded(child: Text('Task lists', style: TextStyle(fontWeight: FontWeight.bold))),
                  IconButton(
                    tooltip: 'Reload lists and calendars',
                    onPressed: _gBusy ? null : _loadGoogle,
                    icon: const Icon(Icons.refresh, size: 20),
                  ),
                ],
              ),
              if (_taskLists != null && _taskLists!.isEmpty)
                const Text('No task lists found', style: TextStyle(color: Colors.white54)),
              for (final l in _taskLists ?? const <GoogleCollection>[])
                CheckboxListTile(
                  dense: true,
                  contentPadding: EdgeInsets.zero,
                  title: Text(l.title.isEmpty ? '(untitled list)' : l.title),
                  value: _selectedLists.contains(l.id),
                  onChanged: (v) => _toggleList(l.id, v ?? false),
                ),
              const SizedBox(height: 12),
              const Text('Calendars (owned by you)', style: TextStyle(fontWeight: FontWeight.bold)),
              if (_calendars != null && _calendars!.isEmpty)
                const Text('No calendars found', style: TextStyle(color: Colors.white54)),
              for (final c in _calendars ?? const <GoogleCollection>[])
                CheckboxListTile(
                  dense: true,
                  contentPadding: EdgeInsets.zero,
                  title: Text(c.title),
                  subtitle: c.primary ? const Text('Primary') : null,
                  value: _selectedCalendars.contains(c.id),
                  onChanged: (v) => _toggleCalendar(c.id, v ?? false),
                ),
            ],
          ],
        ),
      ),
    );
  }
}
