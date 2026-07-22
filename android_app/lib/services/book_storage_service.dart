import 'dart:convert';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/book_record.dart';

class BookStorageService {
  static const String _key = 'tracked_books';

  Future<List<BookRecord>> loadBooks() async {
    final prefs = await SharedPreferences.getInstance();
    final String? jsonStr = prefs.getString(_key);
    if (jsonStr == null) return [];

    try {
      final List<dynamic> jsonList = jsonDecode(jsonStr);
      return jsonList.map((json) => BookRecord.fromJson(json)).toList();
    } catch (e) {
      return [];
    }
  }

  Future<void> saveBooks(List<BookRecord> books) async {
    final prefs = await SharedPreferences.getInstance();
    final String jsonStr = jsonEncode(books.map((b) => b.toJson()).toList());
    await prefs.setString(_key, jsonStr);
  }

  Future<void> addOrUpdateBook(BookRecord book) async {
    final books = await loadBooks();
    final index = books.indexWhere((b) => b.isbn == book.isbn);
    if (index >= 0) {
      books[index] = book;
    } else {
      books.add(book);
    }
    await saveBooks(books);
  }
}
