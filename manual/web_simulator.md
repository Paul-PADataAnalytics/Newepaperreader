# Browser firmware simulator

The GitHub Pages simulator compiles and runs the repository's C++ firmware as
WebAssembly. It uses the firmware's native display and hardware mocks, with
the browser's pointer input forwarded to the firmware as touches.

## Using it

Click or tap the screen as you would touch the device. Below the screen:

- **Add books…** copies EPUB, TXT, MD or RTF files from your computer onto
  the simulated SD card's `/books` folder. Open the Library to see them. The
  files never leave your browser.
- **Reset simulator** erases added books, settings and reading positions and
  restarts with the demo content.

The simulated SD card is stored in the browser's IndexedDB, so books you add,
settings and reading positions survive reloads. On first start it is filled
with three public-domain books from Project Gutenberg (*Alice's Adventures in
Wonderland*, *The Time Machine* and *The Adventures of Sherlock Holmes*; see
`tools/web_demo/README.md`), the Markdown and RTF test documents, and a few
pictures. Deleted demo books are not re-added unless you reset.

## Build and test locally

Install Docker (or Emscripten) and PlatformIO, then install the native
environment's ArduinoJson dependency and build:

```sh
pio pkg install -e native
./tools/build_web.sh
```

Without a local `em++`, the script uses the `emscripten/emsdk:6.0.13` Docker
image (override with `EMSDK_IMAGE`). It writes `index.html`, `index.js`,
`index.wasm` and `index.data` to `docs/`. Serve the page over HTTP rather than
opening the HTML file directly:

```sh
python3 -m http.server 8000 --directory docs
```

Open <http://localhost:8000>. Browser developer tools show firmware log output.
Only the files listed in `WEB_ASSETS` in `tools/build_web.sh` are packaged;
personal books and ignored configuration files are never included.

## Publish with GitHub Pages

The workflow `.github/workflows/deploy-github-pages.yml` builds the simulator
from source and deploys it. One-time setup in the GitHub repository:

1. **Settings → Pages → Build and deployment → Source:** choose
   **GitHub Actions**.
2. **Settings → Environments → github-pages → Deployment branches and
   tags:** GitHub only allows the default branch (`main`) to deploy at first.
   To publish from `paul-padataanalytics-power-ui-revamp-companion-fixes`
   before it is merged, add that branch name as an allowed branch. Skip this
   step if you only publish from `main`.
3. Push the branch. The workflow runs on pushes to `main` or the revamp branch
   that touch firmware, web or workflow files. To run it by hand, open
   **Actions → Deploy web simulator to GitHub Pages → Run workflow**. The
   button appears once the workflow file exists on the default branch.
4. When the run finishes, open the site URL shown in the run summary or under
   **Settings → Pages**. It is normally
   `https://paul-padataanalytics.github.io/Newepaperreader/`.

The built files are also committed in `docs/`. If you prefer not to use
Actions, choose **Deploy from a branch** with the branch and `/docs` folder
instead, and rerun `./tools/build_web.sh` and commit `docs/` whenever the
firmware changes.

## Simulator limitations

This is the actual firmware UI and application code, but not an ESP32 or
e-paper hardware emulator. Display, touch, battery, clock, storage and other
device services use native-test mocks. Wi-Fi, BLE, the physical side button,
deep sleep and e-paper refresh behaviour (ghosting, flashing) are not
simulated. A browser with WebAssembly support is required.
