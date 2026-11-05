# NER Companion (android_app)

Flutter companion app for the EPD-Reader (NER). It talks to the reader over
Bluetooth LE only — the reader itself never goes online. The app:

- tracks books / bookmarks and uploads EPUBs to the reader,
- sets the reader's clock on every connect,
- syncs **todos** (NootTask + Google Tasks) and **Google Calendar** events to the
  reader on every connect, and pushes note edits made on the reader back to
  NootTask.

## Todo & calendar sync

Open **Device → Todo & calendar sync → Settings** (or the tune icon on the
**Todos** tab).

- **NootTask**: server URL (default `https://apps.padanalytics.co.uk`), log in
  with your NootTask username/password. Only the access/refresh tokens are
  stored, never the password; if the refresh token expires (30 days unused)
  you are asked to log in again. Use the category switches to stop categories
  being synced ("Don't sync").
- **Google**: *Connect Google account* uses the account(s) on the phone and
  asks for read-only access to Tasks and Calendar. Tick the task lists and the
  calendars you own that should go to the reader (new task lists are ticked
  automatically; by default only your primary calendar is ticked).
- **Include completed items** applies to both NootTask and Google Tasks.

A sync runs automatically every time the reader connects (after the clock
is set), or with **Sync now**. The last fetched data is cached on the phone, so
the reader still gets the most recent copy when the phone is offline. The
**Todos** tab shows the same items (pull down to refresh from the network).

Calendar sync covers 7 days back to 90 days ahead, at most 1500 events.

## Google Cloud setup (required once for Google Tasks / Calendar)

1. In the [Google Cloud console](https://console.cloud.google.com/) create (or
   pick) a project.
2. **APIs & Services → Library**: enable **Google Tasks API** and
   **Google Calendar API**.
3. **APIs & Services → OAuth consent screen**: configure it (External is fine),
   add the scopes `https://www.googleapis.com/auth/tasks.readonly` and
   `https://www.googleapis.com/auth/calendar.readonly`, and while the app is in
   *Testing* add your Google account as a **test user**.
4. **APIs & Services → Credentials → Create credentials → OAuth client ID →
   Android**:
   - Package name: `com.example.android_app`
   - SHA-1 of the signing certificate. For debug builds (release builds are
     currently signed with the debug key too):

     ```bash
     keytool -list -v -keystore ~/.android/debug.keystore -alias androiddebugkey -storepass android
     ```

     If you later sign releases with your own key, add another Android client
     with that key's SHA-1.
5. Optional: create an OAuth client of type **Web application** and pass its
   client ID at build time. The read-only Tasks/Calendar authorization used by
   this app does not need it, but Google Sign-In requires it if you ever see
   *"serverClientId must be provided on Android"*:

   ```bash
   flutter build apk --debug --dart-define=GOOGLE_SERVER_CLIENT_ID=1234-abc.apps.googleusercontent.com
   ```

No `google-services.json` / Firebase is needed. Configuration mistakes
(wrong SHA-1 or package name, API not enabled, account not a test user)
usually show up as "Google sign-in cancelled" or "Google sign-in failed".

## Building

```bash
flutter pub get
flutter test
flutter build apk --debug           # build/app/outputs/flutter-apk/app-debug.apk
```

or from the repository root `./build_android.sh` (set
`GOOGLE_SERVER_CLIENT_ID=...` in the environment to pass the Web client ID).
