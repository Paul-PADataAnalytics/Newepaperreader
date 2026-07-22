#pragma once

#include <string>
#include <vector>
#include <stdint.h>

struct HistoryEntry {
    uint32_t timestamp; // Unix timestamp
    int page;
};

struct EBookmark {
    std::string isbn;
    std::string title;
    std::string author;
    std::string genre;
    int currentPage;
    int totalPages;
    std::vector<HistoryEntry> history;
};

class EBookmarkManager {
public:
    static EBookmarkManager& getInstance();

    bool initialize();
    bool load();
    bool save();

    const std::vector<EBookmark>& getBookmarks() const { return m_bookmarks; }
    
    // Add a new book or update metadata if it exists
    void addOrUpdateBook(const std::string& isbn, const std::string& title, 
                          const std::string& author, const std::string& genre, 
                          int currentPage, int totalPages);
                          
    // Update the reading page for a book, appending to history if it changed
    void updatePage(const std::string& isbn, int newPage, uint32_t timestamp = 0);
    
    // Update the reading page for a book matched by title (and optionally author).
    // Used when the e-reader advances pages and we want to push progress back to
    // the phone's bookmark database. Returns true if a matching book was updated.
    bool updatePageByTitle(const std::string& title, const std::string& author,
                           int newPage, uint32_t timestamp = 0);
    
    // Retrieve stats
    int getDaysReading(const EBookmark& book) const;
    float getAvgPagesPerSitting(const EBookmark& book) const;

private:
    EBookmarkManager() = default;
    ~EBookmarkManager() = default;
    EBookmarkManager(const EBookmarkManager&) = delete;
    EBookmarkManager& operator=(const EBookmarkManager&) = delete;

    std::vector<EBookmark> m_bookmarks;
    std::string getFilePath() const;
};
