#include "EBookmarkManager.h"
#include "AppStorage.h"
#include "AppComm.h"
#include <ArduinoJson.h>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SD.h>
#else
#include <cstdio>
#include <ctime>
#endif

namespace {
// Helper to read file to string
std::string readFileToString(const std::string& path) {
    std::string content;
#ifdef NATIVE_TESTING
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return "";
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        content += buf;
    }
    fclose(f);
#else
    if (!SD.exists(path.c_str())) return "";
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return "";
    while (f.available()) {
        content += (char)f.read();
    }
    f.close();
#endif
    return content;
}

// Helper to write string to file
bool writeStringToFile(const std::string& path, const std::string& content) {
#ifdef NATIVE_TESTING
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    fputs(content.c_str(), f);
    fclose(f);
    return true;
#else
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) return false;
    f.print(content.c_str());
    f.close();
    return true;
#endif
}

// Get current unix timestamp
uint32_t getCurrentTime() {
#ifdef NATIVE_TESTING
    return (uint32_t)time(nullptr);
#else
    time_t now;
    time(&now);
    return (uint32_t)now;
#endif
}
} // namespace

EBookmarkManager& EBookmarkManager::getInstance() {
    static EBookmarkManager instance;
    return instance;
}

std::string EBookmarkManager::getFilePath() const {
#ifdef NATIVE_TESTING
    return "books/ebookmarks.json";
#else
    return "/books/ebookmarks.json";
#endif
}

bool EBookmarkManager::initialize() {
    return load();
}

bool EBookmarkManager::load() {
    m_bookmarks.clear();
    std::string path = getFilePath();
    std::string jsonStr = readFileToString(path);
    if (jsonStr.empty()) return true; // File not found or empty is okay

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        EBookmark book;
        book.isbn = obj["isbn"] | "";
        book.title = obj["title"] | "";
        book.author = obj["author"] | "";
        book.genre = obj["genre"] | "";
        book.currentPage = obj["page"] | 0;
        book.totalPages = obj["total"] | 0;

        JsonArray historyArr = obj["history"];
        for (JsonObject hObj : historyArr) {
            HistoryEntry entry;
            entry.timestamp = hObj["ts"] | 0;
            entry.page = hObj["p"] | 0;
            book.history.push_back(entry);
        }
        m_bookmarks.push_back(book);
    }

    return true;
}

bool EBookmarkManager::save() {
    std::string path = getFilePath();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (const auto& book : m_bookmarks) {
        JsonObject obj = arr.add<JsonObject>();
        obj["isbn"] = book.isbn;
        obj["title"] = book.title;
        obj["author"] = book.author;
        obj["genre"] = book.genre;
        obj["page"] = book.currentPage;
        obj["total"] = book.totalPages;

        JsonArray historyArr = obj["history"].to<JsonArray>();
        for (const auto& h : book.history) {
            JsonObject hObj = historyArr.add<JsonObject>();
            hObj["ts"] = h.timestamp;
            hObj["p"] = h.page;
        }
    }

    std::string jsonStr;
    serializeJson(doc, jsonStr);
    AppComm::setReadBuffer(jsonStr);
    return writeStringToFile(path, jsonStr);
}

void EBookmarkManager::addOrUpdateBook(const std::string& isbn, const std::string& title, 
                                      const std::string& author, const std::string& genre, 
                                      int currentPage, int totalPages) {
    for (auto& book : m_bookmarks) {
        if (book.isbn == isbn) {
            book.title = title;
            book.author = author;
            book.genre = genre;
            book.totalPages = totalPages;
            if (book.currentPage != currentPage) {
                updatePage(isbn, currentPage);
            } else {
                save();
            }
            return;
        }
    }

    EBookmark newBook;
    newBook.isbn = isbn;
    newBook.title = title;
    newBook.author = author;
    newBook.genre = genre;
    newBook.currentPage = currentPage;
    newBook.totalPages = totalPages;
    
    // Add initial history entry if starting above 0
    if (currentPage > 0) {
        HistoryEntry entry;
        entry.timestamp = getCurrentTime();
        entry.page = currentPage;
        newBook.history.push_back(entry);
    }
    
    m_bookmarks.push_back(newBook);
    save();
}

void EBookmarkManager::updatePage(const std::string& isbn, int newPage, uint32_t timestamp) {
    for (auto& book : m_bookmarks) {
        if (book.isbn == isbn) {
            if (book.currentPage == newPage) return;
            
            book.currentPage = newPage;
            
            HistoryEntry entry;
            entry.timestamp = (timestamp == 0) ? getCurrentTime() : timestamp;
            entry.page = newPage;
            book.history.push_back(entry);
            
            save();
            if (timestamp == 0) {
                AppComm::sendBookDelta(book.isbn, book.title, book.author, book.currentPage, book.totalPages);
            }
            return;
        }
    }
}

bool EBookmarkManager::updatePageByTitle(const std::string& title, const std::string& author,
                                         int newPage, uint32_t timestamp) {
    for (auto& book : m_bookmarks) {
        if (book.title == title && (author.empty() || book.author == author)) {
            if (book.currentPage == newPage) return true;
            updatePage(book.isbn, newPage, timestamp);
            return true;
        }
    }
    return false;
}

int EBookmarkManager::getDaysReading(const EBookmark& book) const {
    if (book.history.empty()) return 0;
    
    std::vector<uint32_t> uniqueDays;
    for (const auto& entry : book.history) {
        time_t rawtime = entry.timestamp;
        struct tm* timeinfo = localtime(&rawtime);
        if (!timeinfo) continue;
        
        // Convert to a simple day representation: YYYYMMDD
        uint32_t dayVal = (timeinfo->tm_year + 1900) * 10000 + (timeinfo->tm_mon + 1) * 100 + timeinfo->tm_mday;
        
        bool found = false;
        for (uint32_t d : uniqueDays) {
            if (d == dayVal) {
                found = true;
                break;
            }
        }
        if (!found) {
            uniqueDays.push_back(dayVal);
        }
    }
    return uniqueDays.size();
}

float EBookmarkManager::getAvgPagesPerSitting(const EBookmark& book) const {
    if (book.history.size() <= 1) {
        // If there's 1 entry, and page is > 0, we can say that was one sitting.
        if (book.history.size() == 1) {
            return (float)book.history[0].page;
        }
        return 0.0f;
    }
    
    int totalPages = 0;
    int prevPage = 0; // Assuming they started at page 0
    int sittingsCount = 0;
    
    for (size_t i = 0; i < book.history.size(); i++) {
        int delta = book.history[i].page - prevPage;
        if (delta > 0) {
            totalPages += delta;
            sittingsCount++;
        }
        prevPage = book.history[i].page;
    }
    
    if (sittingsCount == 0) return 0.0f;
    return (float)totalPages / sittingsCount;
}
