import 'dart:io';

void main() async {
  print('Connecting to mock server...');
  Socket? socket;
  try {
    socket = await Socket.connect('127.0.0.1', 9876, timeout: Duration(seconds: 5));
    print('Connected!');

    socket.listen((data) {
      final response = String.fromCharCodes(data);
      print('Received from server: $response');
      if (response == 'ACK: Hello from Dart') {
        print('Test passed: Received expected ACK.');
        socket?.destroy();
        exit(0);
      }
    }, onError: (e) {
      print('Error: $e');
      exit(1);
    });

    socket.write('Hello from Dart');
    print('Sent message to server.');

    // Wait for response
    await Future.delayed(Duration(seconds: 3));
    print('Test failed: Did not receive expected response in time.');
    socket.destroy();
    exit(1);
  } catch (e) {
    print('Connection failed: $e');
    exit(1);
  }
}
