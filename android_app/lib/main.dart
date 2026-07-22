import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import 'screens/main_screen.dart';
import 'screens/page_entry_screen.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await _requestPermissions();
  runApp(const BookSyncReaderApp());
}

Future<void> _requestPermissions() async {
  // Request runtime permissions for BLE, location, and storage.
  final statuses = await [
    Permission.bluetoothScan,
    Permission.bluetoothConnect,
    Permission.location,
    Permission.storage,
    Permission.manageExternalStorage,
  ].request();

  // Log any permanently denied permissions for debugging.
  statuses.forEach((permission, status) {
    if (status.isPermanentlyDenied) {
      debugPrint('Permission $permission permanently denied. Open app settings.');
    }
  });
}

/// Returns true if the Bluetooth adapter is on and usable.
Future<bool> isBluetoothReady() async {
  if (await FlutterBluePlus.isSupported == false) {
    return false;
  }
  // On Android, the adapterState stream reports the current radio state.
  final state = await FlutterBluePlus.adapterState.first;
  return state == BluetoothAdapterState.on;
}

class BookSyncReaderApp extends StatelessWidget {
  const BookSyncReaderApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'NER Companion',
      theme: ThemeData(
        brightness: Brightness.dark,
        scaffoldBackgroundColor: const Color(0xFF0D1B2A),
        appBarTheme: const AppBarTheme(
          backgroundColor: Color(0xFF0D1B2A),
          elevation: 0,
        ),
        colorScheme: const ColorScheme.dark(
          primary: Color(0xFF00B4D8),
          secondary: Color(0xFF00B4D8),
          surface: Color(0xFF1B263B),
        ),
        useMaterial3: true,
      ),
      initialRoute: '/',
      routes: {
        '/': (context) => const MainScreen(),
        '/page-entry': (context) => const PageEntryScreen(),
      },
    );
  }
}
