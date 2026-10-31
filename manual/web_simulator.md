# Browser firmware simulator

The GitHub Pages simulator compiles and runs the repository's C++ firmware as
WebAssembly. It uses the firmware's native display and hardware mocks, with
SDL forwarding browser pointer input to the firmware.

## Build and test locally

Install Emscripten (or Docker) and PlatformIO, then install the native
environment's ArduinoJson dependency and build:

```sh
pio pkg install -e native
./tools/build_web.sh
```

The build writes `index.html`, `index.js`, `index.wasm`, and `index.data` to
`docs/`. To test the page locally, serve it over HTTP rather than opening the
HTML file directly:

```sh
python3 -m http.server 8000 --directory docs
```

Open <http://localhost:8000> in a modern browser. Click or tap the display to
interact. Browser developer tools show firmware log output and startup errors.
The build packages only the explicit demo fixtures listed in
`tools/build_web.sh`; personal books and ignored configuration files are not
included.

## Publish with GitHub Pages

1. In the repository, open **Settings → Pages** and set **Build and
   deployment → Source** to **GitHub Actions**.
2. Push to `paul-padataanalytics-power-ui-revamp-companion-fixes`. For the
   first publication, push the built `docs/` assets, choose **Deploy from a
   branch**, select that branch and `/docs`, and save. This works even when
   the repository's default branch remains `main`.
3. For automatic source builds, merge the workflow onto the default branch
   (GitHub needs it there to expose **Run workflow**), switch Pages Source to
   **GitHub Actions**, and select the UI revamp branch in the workflow
   dropdown before running it.
4. When the deployment completes, open the Pages URL shown in the workflow
   run or under **Settings → Pages**.

Once GitHub Actions is configured as the source, the workflow installs the
native dependency, builds the WebAssembly assets, and deploys `docs/`.
For branch-based publication instead, rebuild and commit the `docs/` assets
whenever firmware changes.

## Simulator limitations

This is the actual firmware UI and application code, but not an ESP32 or
e-paper hardware emulator. Display, touch, battery, clock, storage, and other
device services use native-test mocks; Wi-Fi, BLE, physical buttons, deep
sleep, and e-paper refresh behavior are unavailable. Filesystem changes are
in-memory and reset when the page is reloaded. A browser with WebAssembly and
WebGL support is required.
