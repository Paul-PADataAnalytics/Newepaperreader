import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:android_app/main.dart';

void main() {
  testWidgets('ISBN Scanner smoke test', (WidgetTester tester) async {
    // Build our app and trigger a frame.
    await tester.pumpWidget(const MyApp());

    // Verify that our title exists.
    expect(find.text('ISBN Scanner'), findsOneWidget);
    
    // Verify that the initial text is on the screen.
    expect(find.text('Point camera at an ISBN barcode (EAN-13)'), findsOneWidget);
  });
}
