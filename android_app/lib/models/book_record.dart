class HistoryEntry {
  final int timestamp; // Unix timestamp in seconds
  final int page;

  HistoryEntry({required this.timestamp, required this.page});

  Map<String, dynamic> toJson() => {'ts': timestamp, 'p': page};
  factory HistoryEntry.fromJson(Map<String, dynamic> json) => HistoryEntry(
        timestamp: json['ts'] ?? 0,
        page: json['p'] ?? 0,
      );
}

class BookRecord {
  final String isbn;
  final String title;
  final String author;
  String genre;
  int currentPage;
  int totalPages;
  DateTime lastUpdated;
  final List<HistoryEntry> history;

  BookRecord({
    required this.isbn,
    required this.title,
    required this.author,
    String genre = "Unknown",
    this.currentPage = 0,
    this.totalPages = 0,
    DateTime? lastUpdated,
    List<HistoryEntry>? history,
  })  : genre = (genre.isEmpty || genre.trim().toLowerCase() == "unknown genre") ? "Unknown" : genre,
        lastUpdated = lastUpdated ?? DateTime.now(),
        history = history ?? [];

  double get progress => totalPages > 0 ? currentPage / totalPages : 0.0;

  Map<String, dynamic> toJson() => {
        'isbn': isbn,
        'title': title,
        'author': author,
        'genre': genre,
        'currentPage': currentPage,
        'totalPages': totalPages,
        'lastUpdated': lastUpdated.toIso8601String(),
        'history': history.map((e) => e.toJson()).toList(),
      };

  factory BookRecord.fromJson(Map<String, dynamic> json) {
    // The firmware uses 'page' and 'total'; the app uses 'currentPage' and 'totalPages'.
    final currentPage = json['currentPage'] ?? json['page'] ?? 0;
    final totalPages = json['totalPages'] ?? json['total'] ?? 0;

    final history = json['history'] != null
        ? (json['history'] as List<dynamic>)
            .map((e) => HistoryEntry.fromJson(e as Map<String, dynamic>))
            .toList()
            .cast<HistoryEntry>()
        : <HistoryEntry>[];

    // Derive lastUpdated from the most recent history entry (firmware format)
    // or from the top-level ts field, falling back to the app's lastUpdated.
    DateTime lastUpdated;
    if (json['lastUpdated'] != null) {
      lastUpdated = DateTime.parse(json['lastUpdated']);
    } else if (history.isNotEmpty) {
      final latestTs = history.map((e) => e.timestamp).reduce((a, b) => a > b ? a : b);
      lastUpdated = DateTime.fromMillisecondsSinceEpoch(latestTs * 1000);
    } else if (json['ts'] != null) {
      lastUpdated = DateTime.fromMillisecondsSinceEpoch((json['ts'] as int) * 1000);
    } else {
      lastUpdated = DateTime.now();
    }

    return BookRecord(
      isbn: json['isbn'],
      title: json['title'],
      author: json['author'],
      genre: json['genre'] ?? "",
      currentPage: currentPage,
      totalPages: totalPages,
      lastUpdated: lastUpdated,
      history: history,
    );
  }
}
