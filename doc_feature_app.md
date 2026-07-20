# Chunk E: Android App & TCP Mock Architecture

## Overview
This chunk implements the companion app and the communication layer necessary for the app to talk to the ESP32 E-Reader (or its native mock).

## Architecture & Classes

### ESP32 Firmware (`src/comm/`)
* **`AppComm`**
  * **Role**: Abstracts the communication layer (BLE for actual hardware, TCP for native mock testing).
  * **Implementation**:
    * **`init()`**: Initializes the communication medium. In `NATIVE_TESTING` mode, this spins up a non-blocking POSIX TCP socket listening on port `9876`. For real hardware, this would initialize the BLE GATT server (stubbed out for now).
    * **`poll()`**: Non-blocking poll for new data or client connections. In native mode, it uses `accept` and `read` to check for new TCP events.
    * **`sendData()`**: Transmits a message back to the connected client.
    * **`hasData()` & `getNextMessage()`**: Queue mechanism for processing incoming packets.

### Flutter Companion App (`android_app/`)
* **`MyApp` & `TCPTestPage` (`main.dart`)**:
  * The main entry point of the Flutter application. It runs a simple UI that allows the user to:
    1. Connect to the mock TCP server (`127.0.0.1:9876`).
    2. Monitor the connection status.
    3. Input arbitrary text and send it over the socket.
    4. View incoming messages (such as the `ACK` packets from the mock).
  * The Flutter app is initialized with Linux Desktop and Android compilation support.

### Testing (`android_app/test/tcp_mock_test.dart` & `run_integration_test.sh`)
* **`tcp_mock_test.dart`**: A Dart CLI script that acts as an automated test. It connects to the TCP socket, sends a `Hello from Dart` message, and expects an `ACK: Hello from Dart` reply.
* **`run_integration_test.sh`**: A shell script that automatically builds the native mock, runs it in the background, executes the Dart test suite, and tears down the mock server upon completion.
