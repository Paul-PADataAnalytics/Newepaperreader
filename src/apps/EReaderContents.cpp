// E-reader table of contents: building it (EPUB TOC, or headings scanned
// from TXT/MD/RTF files), the Contents screen, and jumping to a chapter or
// to a percentage of a text book.

#include "EReaderApp.h"
#include "ui/UIFramework.h"
#include "ui/UIKit.h"
#include "reader/MarkdownParser.h"
#include "reader/RTFParser.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SD.h>
static uint32_t nowMs() { return millis(); }
#else
#include <chrono>
static uint32_t nowMs() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
#endif

extern uint8_t* framebuffer;

namespace {
using namespace UI;

constexpr size_t kMaxContentsEntries = 1500;
constexpr int GOTO_PANEL_H = 186;
const Rect kGotoSlider(MARGIN, HEADER_H + 54, W - 2 * MARGIN, 60);
const int kGotoSteps[] = {-10, -1, 1, 10};
const char* const kGotoLabels[] = {"-10", "-1", "+1", "+10"};

// 0..3 = the step buttons, 4 = Go.
Rect gotoButtonRect(int i) {
    const int gap = 10;
    const int w = (W - 2 * MARGIN - 4 * gap) / 5;
    return Rect(MARGIN + i * (w + gap), HEADER_H + 118, w, 58);
}

// Minimal read-only file access matching TextReader (SD on device, stdio in
// the simulator/web build), used to scan a text book without disturbing the
// reader's own open file.
class BookFile {
public:
    ~BookFile() { close(); }
    bool open(const std::string& path) {
#ifndef NATIVE_TESTING
        m_file = SD.open(path.c_str(), FILE_READ);
        return (bool)m_file;
#else
        m_file = fopen(path.c_str(), "rb");
        return m_file != nullptr;
#endif
    }
    size_t read(char* buffer, size_t size) {
#ifndef NATIVE_TESTING
        return m_file ? m_file.read((uint8_t*)buffer, size) : 0;
#else
        return m_file ? fread(buffer, 1, size, m_file) : 0;
#endif
    }
    void seek(size_t pos) {
#ifndef NATIVE_TESTING
        if (m_file) m_file.seek(pos);
#else
        if (m_file) fseek(m_file, (long)pos, SEEK_SET);
#endif
    }
    void close() {
#ifndef NATIVE_TESTING
        if (m_file) m_file.close();
#else
        if (m_file) fclose(m_file);
        m_file = nullptr;
#endif
    }

private:
#ifndef NATIVE_TESTING
    File m_file;
#else
    FILE* m_file = nullptr;
#endif
};

std::string trim(const std::string& s) {
    const size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, s.find_last_not_of(" \t\r\n") - start + 1);
}

std::string collapseSpaces(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (isspace((unsigned char)c)) {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string lowerCase(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)tolower((unsigned char)c);
    return out;
}

size_t findBytes(const char* hay, size_t len, const std::string& needle, size_t from = 0) {
    if (needle.empty() || len < needle.size()) return std::string::npos;
    for (size_t i = from; i + needle.size() <= len; ++i) {
        if (hay[i] == needle[0] && memcmp(hay + i, needle.data(), needle.size()) == 0) return i;
    }
    return std::string::npos;
}

// Start of the next <h1>..<h3> tag at or after `from`.
size_t findHeadingTag(const char* html, size_t len, size_t from) {
    for (size_t i = from; i + 3 < len; ++i) {
        if (html[i] == '<' && (html[i + 1] == 'h' || html[i + 1] == 'H') &&
            html[i + 2] >= '1' && html[i + 2] <= '3' &&
            (html[i + 3] == '>' || isspace((unsigned char)html[i + 3]))) {
            return i;
        }
    }
    return std::string::npos;
}

size_t findClosingHeading(const char* html, size_t len, size_t from) {
    for (size_t i = from; i + 2 < len; ++i) {
        if (html[i] == '<' && html[i + 1] == '/' && (html[i + 2] == 'h' || html[i + 2] == 'H')) return i;
    }
    return std::string::npos;
}

// ---- Heading detection for plain-text books --------------------------------

bool isNumberToken(const std::string& token) {
    std::string t = token;
    while (!t.empty() && strchr(".:)-,", t.back())) t.pop_back();
    if (t.empty()) return false;
    bool digits = true, roman = t.size() <= 8;
    for (char c : t) {
        if (!isdigit((unsigned char)c)) digits = false;
        if (!strchr("IVXLCDM", c)) roman = false;
    }
    if (digits || roman) return true;
    static const char* const words[] = {
        "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
        "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen",
        "eighteen", "nineteen", "twenty", "thirty", "forty", "fifty", "sixty", "seventy",
        "eighty", "ninety", "hundred", "first", "second", "third", "fourth", "fifth",
        "sixth", "seventh", "eighth", "ninth", "tenth", "eleventh", "twelfth", "last", "final"};
    std::string head = lowerCase(t.substr(0, t.find('-')));
    for (const char* w : words) {
        if (head == w) return true;
    }
    return false;
}

std::string firstWord(const std::string& s) {
    const size_t end = s.find_first_of(" \t");
    return end == std::string::npos ? s : s.substr(0, end);
}

enum class TextHeading { NONE, NUMBERED, BARE, NAMED };

TextHeading classifyTextLine(const std::string& line) {
    const std::string lower = lowerCase(line);
    static const char* const numbered[] = {"chapter", "part", "book", "volume", "section"};
    for (const char* kw : numbered) {
        const size_t n = strlen(kw);
        if (lower.compare(0, n, kw) != 0 || (lower.size() > n && isalpha((unsigned char)lower[n]))) continue;
        std::string rest = trim(line.substr(n));
        while (!rest.empty() && strchr(".:-", rest[0])) rest = trim(rest.substr(1));
        if (rest.empty()) return TextHeading::BARE;
        return isNumberToken(firstWord(rest)) ? TextHeading::NUMBERED : TextHeading::NONE;
    }
    static const char* const named[] = {"prologue", "epilogue", "interlude", "introduction",
                                        "preface", "foreword", "afterword"};
    for (const char* kw : named) {
        const size_t n = strlen(kw);
        if (lower.compare(0, n, kw) != 0) continue;
        const std::string rest = lower.substr(n);
        if (rest.empty() || rest == "." || rest == ":" || rest[0] == ':' ||
            rest.compare(0, 2, " -") == 0 || rest.compare(0, 4, " \xE2\x80\x94") == 0) {
            return TextHeading::NAMED;
        }
    }
    return TextHeading::NONE;
}

struct Heading {
    std::string title;
    size_t offset;
    int level;
};

// Fed the book one line at a time (with each line's byte offset); collects
// Markdown '#'/setext headings, or "Chapter X"-style lines in TXT/RTF.
class HeadingScanner {
public:
    enum Kind { TEXT, MARKDOWN, RTF };
    explicit HeadingScanner(Kind kind) : m_kind(kind) {}
    std::vector<Heading> headings;

    void line(size_t start, const std::string& raw, bool tooLong) {
        if (headings.size() >= kMaxContentsEntries) return;
        if (m_kind == MARKDOWN) markdownLine(start, raw, tooLong);
        else textLine(start, raw, tooLong);
    }

private:
    Kind m_kind;
    bool m_prevBlank = true;
    // Markdown
    bool m_inFence = false;
    bool m_prevIsPara = false;
    std::string m_prevText;
    size_t m_prevStart = 0;
    // TXT/RTF: a bare "Chapter" waiting for its number on a following line,
    // or a numbered heading that may take a subtitle from the next line.
    enum { NONE, NEED_NUMBER, SUBTITLE } m_pending = NONE;
    int m_pendingBlanks = 0;
    Heading m_pendingHeading;

    void add(const std::string& title, size_t offset, int level) {
        Heading h;
        h.title = collapseSpaces(title);
        h.offset = offset;
        h.level = level;
        if (!h.title.empty()) headings.push_back(h);
    }

    void markdownLine(size_t start, const std::string& raw, bool tooLong) {
        const std::string t = trim(raw);
        if (t.compare(0, 3, "```") == 0 || t.compare(0, 3, "~~~") == 0) {
            m_inFence = !m_inFence;
            m_prevIsPara = false;
            return;
        }
        if (m_inFence) return;

        size_t i = 0;
        while (i < raw.size() && i < 3 && raw[i] == ' ') ++i;
        size_t level = 0;
        while (i + level < raw.size() && raw[i + level] == '#') ++level;
        if (level >= 1 && level <= 6 && (i + level == raw.size() || raw[i + level] == ' ' || raw[i + level] == '\t')) {
            std::string title = trim(raw.substr(i + level));
            while (!title.empty() && title.back() == '#') title.pop_back();
            add(trim(MarkdownParser::stripMarkdown(trim(title))), start, (int)level);
            m_prevIsPara = false;
            return;
        }

        if (m_prevIsPara && t.size() >= 2 &&
            (t.find_first_not_of('=') == std::string::npos || t.find_first_not_of('-') == std::string::npos)) {
            add(trim(MarkdownParser::stripMarkdown(m_prevText)), m_prevStart, t[0] == '=' ? 1 : 2);
            m_prevIsPara = false;
            return;
        }

        m_prevIsPara = !t.empty() && !tooLong && t.size() <= 120 && !strchr("-*+>|", t[0]) &&
                       !(isdigit((unsigned char)t[0]) && t.find(". ") != std::string::npos && t.find(". ") < 4);
        m_prevText = t;
        m_prevStart = start;
    }

    void textLine(size_t start, const std::string& raw, bool tooLong) {
        std::string t = trim(m_kind == RTF ? RTFParser::stripRTF(raw) : raw);
        if (start == 0 && t.compare(0, 3, "\xEF\xBB\xBF") == 0) t = trim(t.substr(3));
        const bool blank = t.empty();
        // RTF paragraphs rarely have blank lines between them.
        const bool prevBlank = m_prevBlank || m_kind == RTF;
        m_prevBlank = blank;

        if (m_pending == NEED_NUMBER) {
            if (blank) {
                if (++m_pendingBlanks > 2) m_pending = NONE;
                return;
            }
            m_pending = NONE;
            if (t.size() <= 40 && isNumberToken(firstWord(t))) {
                add(m_pendingHeading.title + " " + t, m_pendingHeading.offset, 1);
                return;
            }
        } else if (m_pending == SUBTITLE) {
            m_pending = NONE;
            if (!blank && t.size() <= 50 && !strchr(".!?\"'", t.back()) &&
                classifyTextLine(t) == TextHeading::NONE) {
                headings.back().title += ": " + collapseSpaces(t);
                return;
            }
        }

        if (blank || !prevBlank || tooLong || t.size() > 60) return;
        switch (classifyTextLine(t)) {
            case TextHeading::NUMBERED:
            case TextHeading::NAMED: {
                const size_t before = headings.size();
                add(t, start, 1);
                if (headings.size() > before) m_pending = SUBTITLE;
                break;
            }
            case TextHeading::BARE:
                m_pendingHeading.title = t;
                m_pendingHeading.offset = start;
                m_pendingBlanks = 0;
                m_pending = NEED_NUMBER;
                break;
            default:
                break;
        }
    }
};

void drawToast(const std::string& message) {
    UIFramework::performFastPartialUpdate(framebuffer, 0, H / 2 - 40, W, 80, [message]() {
        fillRoundRect(MARGIN * 3, H / 2 - 36, W - MARGIN * 6, 72, 16, WHITE);
        strokeRoundRect(MARGIN * 3, H / 2 - 36, W - MARGIN * 6, 72, 16, 2, BLACK);
        textCentered(message, W / 2, H / 2 - 16, FONT_BODY, BLACK, true);
    });
}

} // namespace

// ---------------------------------------------------------------------------
// Building the table of contents
// ---------------------------------------------------------------------------

void EReaderApp::ensureContents() {
    if (m_contentsLoaded) return;
    m_contentsLoaded = true;
    m_contents.clear();
    m_contentsFromSpine = false;

    if (m_textReader.isOpen()) {
        buildTextContents();
        return;
    }
    if (m_chapters.empty()) return;

    std::vector<EpubParser::TocEntry> toc = m_epubParser.readToc();
    m_contents.reserve(toc.size());
    for (const auto& t : toc) {
        ContentsEntry e;
        e.title = t.title;
        e.fragment = t.fragment;
        e.chapter = t.spineIndex;
        e.offset = t.fragment.empty() ? 0 : -1;
        e.depth = t.depth;
        m_contents.push_back(e);
    }
    if (m_contents.empty()) {
        // No TOC: list the spine; titles come from each file's first heading
        // when its row is first shown (see contentsTitle()).
        m_contentsFromSpine = true;
        m_contents.resize(m_chapters.size());
        for (size_t i = 0; i < m_chapters.size(); ++i) m_contents[i].chapter = (int)i;
    }
}

void EReaderApp::buildTextContents() {
    std::string lowerPath = m_currentBookPath;
    for (char& c : lowerPath) c = (char)tolower((unsigned char)c);
    HeadingScanner::Kind kind = HeadingScanner::TEXT;
    if (lowerPath.size() >= 3 && lowerPath.compare(lowerPath.size() - 3, 3, ".md") == 0) kind = HeadingScanner::MARKDOWN;
    if (lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".rtf") == 0) kind = HeadingScanner::RTF;

    BookFile file;
    if (!file.open(m_currentBookPath)) return;
    const uint32_t started = nowMs();
    HeadingScanner scanner(kind);
    std::string line;
    bool tooLong = false;
    size_t lineStart = 0, fileOffset = 0;
    char buffer[2048];
    size_t n;
    while ((n = file.read(buffer, sizeof(buffer))) > 0 && scanner.headings.size() < kMaxContentsEntries) {
        for (size_t i = 0; i < n; ++i) {
            if (buffer[i] == '\n') {
                scanner.line(lineStart, line, tooLong);
                line.clear();
                tooLong = false;
                lineStart = fileOffset + i + 1;
            } else if (line.size() < 300) {
                line.push_back(buffer[i]);
            } else {
                tooLong = true;
            }
        }
        fileOffset += n;
    }
    if (!line.empty()) scanner.line(lineStart, line, tooLong);

    int minLevel = 6;
    for (const auto& h : scanner.headings) minLevel = std::min(minLevel, h.level);
    m_contents.reserve(scanner.headings.size());
    for (const auto& h : scanner.headings) {
        ContentsEntry e;
        e.title = h.title.size() > 160 ? h.title.substr(0, 160) : h.title;
        e.offset = (int)h.offset;
        e.depth = h.level - minLevel;
        m_contents.push_back(e);
    }
    printf("[EReader] %zu headings found in %ums\n", m_contents.size(), (unsigned)(nowMs() - started));
}

const std::string& EReaderApp::contentsTitle(int index) {
    ContentsEntry& e = m_contents[index];
    if (e.title.empty() && m_contentsFromSpine) {
        e.title = epubHeadingTitle(e.chapter);
        if (e.title.empty()) e.title = "Chapter " + std::to_string(e.chapter + 1);
    }
    return e.title;
}

std::string EReaderApp::epubHeadingTitle(int chapter) {
    if (chapter < 0 || chapter >= (int)m_chapters.size()) return "";
    size_t size = 0;
    char* html = m_epubParser.getFileContent(m_chapters[chapter], size);
    if (!html) return "";
    std::string title;
    size_t from = 0;
    for (int n = 0; n < 2; ++n) {
        const size_t open = findHeadingTag(html, size, from);
        // A split heading ("Chapter" / "One") is joined only if adjacent.
        if (open == std::string::npos || (n > 0 && open > from + 400)) break;
        const size_t body = findBytes(html, size, ">", open);
        const size_t close = body == std::string::npos ? body : findClosingHeading(html, size, body);
        if (close == std::string::npos) break;
        const std::string part = collapseSpaces(htmlToText(html + body + 1, close - body - 1));
        if (!part.empty()) title += (title.empty() ? "" : " ") + part;
        from = close;
        if (!title.empty() && title.find(' ') != std::string::npos) break;
    }
    free(html);
    return title.size() > 160 ? title.substr(0, 160) : title;
}

void EReaderApp::resolveContentsAnchors(int chapter, const char* html, size_t htmlLen,
                                        const char* text, size_t textLen) {
    if (!m_contentsLoaded) return;
    for (auto& e : m_contents) {
        if (e.chapter != chapter || e.offset >= 0) continue;
        e.offset = 0;
        size_t at = std::string::npos;
        const std::string patterns[] = {"id=\"" + e.fragment + "\"", "id='" + e.fragment + "'",
                                        "name=\"" + e.fragment + "\""};
        for (const auto& p : patterns) {
            at = findBytes(html, htmlLen, p);
            if (at != std::string::npos) break;
        }
        if (at == std::string::npos) continue;
        size_t tagStart = at;
        while (tagStart > 0 && html[tagStart] != '<') --tagStart;
        const std::string before = htmlToText(html, tagStart);
        // Anchors on the opening heading would otherwise skip the chapter
        // file's <title> text, making its first page look like it belongs to
        // the previous chapter - only honour anchors with real text before.
        size_t readable = 0;
        for (char c : before) readable += isspace((unsigned char)c) ? 0 : 1;
        if (readable < 100) continue;
        size_t off = before.size();
        while (off < textLen && isspace((unsigned char)text[off])) ++off;
        if (off < textLen) e.offset = (int)off;
    }
}

void EReaderApp::resolveCurrentChapterAnchors() {
    if (!m_contentsLoaded || m_textReader.isOpen() || !m_currentBookText ||
        m_currentChapterIndex < 0 || m_currentChapterIndex >= (int)m_chapters.size()) {
        return;
    }
    bool pending = false;
    for (const auto& e : m_contents) pending |= e.chapter == m_currentChapterIndex && e.offset < 0;
    if (!pending) return;
    size_t size = 0;
    char* html = m_epubParser.getFileContent(m_chapters[m_currentChapterIndex], size);
    if (html) {
        resolveContentsAnchors(m_currentChapterIndex, html, size, m_currentBookText, m_currentBookTextLen);
        free(html);
    } else {
        for (auto& e : m_contents) {
            if (e.chapter == m_currentChapterIndex && e.offset < 0) e.offset = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Where are we?
// ---------------------------------------------------------------------------

uint64_t EReaderApp::contentsKey(int index) const {
    const ContentsEntry& e = m_contents[index];
    return ((uint64_t)(uint32_t)e.chapter << 32) | (uint32_t)std::max(0, e.offset);
}

uint64_t EReaderApp::currentPositionKey() const {
    if (m_textReader.isOpen()) return (uint64_t)(m_textReader.getPosition() + (size_t)m_currentReadingOffset);
    return ((uint64_t)(uint32_t)std::max(0, m_currentChapterIndex) << 32) |
           (uint32_t)std::max(0, m_currentReadingOffset);
}

int EReaderApp::currentContentsEntry() {
    if (!m_contentsLoaded || m_contents.empty()) return -1;
    resolveCurrentChapterAnchors();
    const uint64_t here = currentPositionKey();
    int best = -1;
    uint64_t bestKey = 0;
    for (int i = 0; i < (int)m_contents.size(); ++i) {
        const uint64_t key = contentsKey(i);
        // Ties go to the later entry: unresolved anchors elsewhere in a chapter
        // all sort at its start, and the last of them is the furthest read.
        if (key <= here && (best < 0 || key >= bestKey)) {
            best = i;
            bestKey = key;
        }
    }
    return best;
}

int EReaderApp::adjacentContentsEntry(bool forward) {
    if (!m_contentsLoaded) return m_textReader.isOpen() ? -2 : -1;
    if (m_contents.empty()) return -1;
    const uint64_t here = currentPositionKey();
    int best = -1;
    uint64_t bestKey = 0;
    if (forward) {
        for (int i = 0; i < (int)m_contents.size(); ++i) {
            const uint64_t key = contentsKey(i);
            if (key > here && (best < 0 || key < bestKey)) {
                best = i;
                bestKey = key;
            }
        }
        return best;
    }
    // Like a music player: back to the start of this chapter first, then
    // to the one before it.
    const int current = currentContentsEntry();
    if (current >= 0 && contentsKey(current) < here) return current;
    const uint64_t limit = current >= 0 ? contentsKey(current) : here;
    for (int i = 0; i < (int)m_contents.size(); ++i) {
        const uint64_t key = contentsKey(i);
        if (key < limit && (best < 0 || key >= bestKey)) {
            best = i;
            bestKey = key;
        }
    }
    return best;
}

std::string EReaderApp::currentChapterLabel() {
    const int index = currentContentsEntry();
    return index >= 0 ? contentsTitle(index) : std::string();
}

// ---------------------------------------------------------------------------
// Jumping
// ---------------------------------------------------------------------------

void EReaderApp::loadTextChunk() {
    std::string text = m_textReader.getPageText();
    if (m_currentBookText) free(m_currentBookText);
    m_currentBookTextLen = text.length();
#ifndef NATIVE_TESTING
    m_currentBookText = (char*)ps_malloc(m_currentBookTextLen + 1);
    if (!m_currentBookText) m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#else
    m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#endif
    if (m_currentBookText) memcpy(m_currentBookText, text.c_str(), m_currentBookTextLen + 1);
    else m_currentBookTextLen = 0;
}

bool EReaderApp::jumpToTextPosition(size_t pos) {
    const size_t size = m_textReader.getFileSize();
    if (!m_textReader.isOpen() || size == 0) return false;
    if (pos >= size) pos = size - 1;
    m_textReader.setPosition(pos);
    loadTextChunk();
    m_currentReadingOffset = 0;
    m_pageHistory.clear();
    return true;
}

bool EReaderApp::jumpToContentsEntry(int index) {
    if (index < 0 || index >= (int)m_contents.size()) return false;
    if (m_textReader.isOpen()) {
        if (!jumpToTextPosition((size_t)m_contents[index].offset)) return false;
    } else {
        const int chapter = m_contents[index].chapter;
        if (chapter != m_currentChapterIndex || !m_currentBookText) {
            if (!loadChapter(chapter)) {
                m_chapterError = "This chapter could not be opened.";
                return false;
            }
        } else {
            resolveCurrentChapterAnchors();
        }
        int offset = std::max(0, m_contents[index].offset);
        if (offset >= (int)m_currentBookTextLen) offset = 0;
        m_currentReadingOffset = offset;
        m_pageHistory.clear();
    }
    finishJump();
    return true;
}

bool EReaderApp::jumpToPercent(int percent) {
    const size_t size = m_textReader.getFileSize();
    if (!m_textReader.isOpen() || size == 0) return false;
    size_t pos = percent <= 0 ? 0 : (size_t)((uint64_t)size * (uint64_t)percent / 100);
    if (pos >= size) pos = size - 1;
    if (pos > 0) {
        // Start at the beginning of the line (else word) containing pos.
        const size_t from = pos > 1024 ? pos - 1024 : 0;
        std::string window(pos - from, '\0');
        BookFile file;
        if (file.open(m_currentBookPath)) {
            file.seek(from);
            window.resize(file.read(&window[0], window.size()));
            size_t cut = window.find_last_of('\n');
            if (cut == std::string::npos) cut = window.find_last_of(' ');
            if (cut != std::string::npos) pos = from + cut + 1;
        }
    }
    if (!jumpToTextPosition(pos)) return false;
    finishJump();
    return true;
}

void EReaderApp::finishJump() {
    m_chapterPopup = false;
    m_menuOpen = false;
    m_chapterError.clear();
    m_turnsSinceFullRefresh = 0;
    saveCurrentPosition();
    rememberLastBook();
    drawReading();
}

bool EReaderApp::goToAdjacentChapter(bool forward) {
    const bool scanning = !m_contentsLoaded && m_textReader.isOpen();
    if (scanning) drawToast("Finding chapters...");
    ensureContents();
    const int target = adjacentContentsEntry(forward);
    if (target >= 0 && jumpToContentsEntry(target)) return true;
    // Nothing to go to: redraw so the menu shows the button disabled (and
    // the scan toast is gone).
    if (scanning || target >= 0) drawReading();
    return false;
}

// ---------------------------------------------------------------------------
// Contents screen
// ---------------------------------------------------------------------------

int EReaderApp::contentsListTop() const {
    return m_textReader.isOpen() ? HEADER_H + GOTO_PANEL_H : HEADER_H + 8;
}

int EReaderApp::contentsRowsPerPage() const {
    return std::max(1, (H - FOOTER_H - contentsListTop()) / ROW_H);
}

void EReaderApp::openContents() {
    if (!m_contentsLoaded && m_textReader.isOpen()) drawToast("Finding chapters...");
    ensureContents();
    m_menuOpen = false;
    m_chapterPopup = true;
    m_chapterError.clear();
    const int current = currentContentsEntry();
    m_chapterPage = current < 0 ? 0 : current / contentsRowsPerPage();
    m_gotoPercent = (int)(currentProgress() * 100.0f + 0.5f);
    drawContents();
}

void EReaderApp::drawGotoPanel() {
    const int top = HEADER_H;
    text("Go to position", MARGIN, top + 16, FONT_BODY, BLACK, true);
    textRight(std::to_string(m_gotoPercent) + "%", W - MARGIN, top + 16, FONT_BODY, BLACK, true);
    const int trackY = kGotoSlider.y + kGotoSlider.h / 2 - 4;
    progressBar(kGotoSlider.x, trackY, kGotoSlider.w, 8, m_gotoPercent / 100.0f);
    const int knobX = kGotoSlider.x + kGotoSlider.w * m_gotoPercent / 100;
    fillCircle(knobX, trackY + 4, 15, BLACK);
    fillCircle(knobX, trackY + 4, 7, WHITE);
    for (int i = 0; i < 4; ++i) {
        const int next = m_gotoPercent + kGotoSteps[i];
        button(gotoButtonRect(i), kGotoLabels[i], ButtonStyle::SECONDARY, FONT_SMALL + 4, next >= 0 && next <= 100);
    }
    button(gotoButtonRect(4), "Go", ButtonStyle::PRIMARY, FONT_SMALL + 4);
    fillRect(0, top + GOTO_PANEL_H - 2, W, 2, LIGHT);
}

void EReaderApp::drawContents() {
    const int rows = contentsRowsPerPage();
    const int count = (int)m_contents.size();
    const int pages = std::max(1, (count + rows - 1) / rows);
    if (m_chapterPage >= pages) m_chapterPage = pages - 1;
    if (m_chapterPage < 0) m_chapterPage = 0;
    const int current = currentContentsEntry();
    // Fallback titles may need to read their chapter file: do it up front.
    for (int i = m_chapterPage * rows; i < count && i < (m_chapterPage + 1) * rows; ++i) contentsTitle(i);

    UIFramework::performFullScreenDraw(framebuffer, [this, rows, count, pages, current]() {
        header("Contents", true);
        const bool textBook = m_textReader.isOpen();
        if (textBook) drawGotoPanel();
        const int listTop = contentsListTop();
        if (count == 0) {
            emptyState((listTop + H) / 2 - 20, textBook ? "No chapter headings found" : "No chapters found",
                       textBook ? "Use Go to position above to jump anywhere in the book." : "");
            return;
        }

        const size_t textSize = textBook ? m_textReader.getFileSize() : 0;
        const int start = m_chapterPage * rows;
        for (int row = 0; row < rows && start + row < count; ++row) {
            const int index = start + row;
            const ContentsEntry& e = m_contents[index];
            const int y = listTop + row * ROW_H;
            const bool selected = index == current;
            if (selected) {
                fillRoundRect(MARGIN, y + 6, W - 2 * MARGIN, ROW_H - 12, 12, PALE);
                fillRoundRect(MARGIN, y + 6, 8, ROW_H - 12, 4, BLACK);
            }
            int percent = 0;
            if (textBook && textSize) percent = (int)(((uint64_t)e.offset * 100 + textSize / 2) / textSize);
            else if (!m_chapters.empty()) {
                // Same estimate as the reading view; offsets are only known
                // within the loaded chapter.
                float within = 0.0f;
                if (e.chapter == m_currentChapterIndex && e.offset > 0 && m_currentBookTextLen) {
                    within = (float)e.offset / (float)m_currentBookTextLen;
                }
                percent = (int)((e.chapter + within) * 100.0f / m_chapters.size() + 0.5f);
            }
            const std::string trailing = std::to_string(percent) + "%";
            const int right = W - MARGIN - 16;
            const int x = MARGIN + 24 + std::min(e.depth, 2) * 32;
            const int maxW = right - textWidth(trailing, FONT_SMALL) - 16 - x;
            const bool bold = selected || e.depth == 0;
            const std::string title = ellipsize(e.title, maxW, FONT_BODY, bold);
            if (selected) {
                text(title, x, y + 14, FONT_BODY, BLACK, true);
                text("Reading now", x, y + 54, FONT_SMALL, DARK);
            } else {
                text(title, x, y + (ROW_H - lineHeight(FONT_BODY)) / 2, FONT_BODY, e.depth == 0 ? BLACK : INK, bold);
            }
            textRight(trailing, right, y + (ROW_H - lineHeight(FONT_SMALL)) / 2, FONT_SMALL, DARK);
            hRule(y + ROW_H - 1, LIGHT, MARGIN, W - MARGIN);
        }
        if (!m_chapterError.empty()) {
            textCentered(ellipsize(m_chapterError, W - 2 * MARGIN, FONT_SMALL + 4), W / 2,
                         listTop + rows * ROW_H + 10, FONT_SMALL + 4, BLACK, true);
        }
        pager(m_chapterPage, pages);
    });
}

void EReaderApp::handleContentsTouch(int x, int y) {
    if (hitBack(x, y)) {
        m_chapterPopup = false;
        m_chapterError.clear();
        drawReading();
        return;
    }
    if (y < HEADER_H) return;

    if (m_textReader.isOpen() && y < HEADER_H + GOTO_PANEL_H) {
        int next = m_gotoPercent;
        if (kGotoSlider.contains(x, y)) {
            next = (x - kGotoSlider.x) * 100 / kGotoSlider.w;
        } else if (gotoButtonRect(4).contains(x, y)) {
            jumpToPercent(m_gotoPercent);
            return;
        } else {
            for (int i = 0; i < 4; ++i) {
                if (gotoButtonRect(i).contains(x, y)) next += kGotoSteps[i];
            }
        }
        next = std::max(0, std::min(100, next));
        if (next != m_gotoPercent) {
            m_gotoPercent = next;
            UIFramework::performFastPartialUpdate(framebuffer, 0, HEADER_H, W, GOTO_PANEL_H,
                                                  [this]() { drawGotoPanel(); });
        }
        return;
    }

    const int rows = contentsRowsPerPage();
    const int pages = std::max(1, ((int)m_contents.size() + rows - 1) / rows);
    const int dir = hitPager(x, y, m_chapterPage, pages);
    if (dir != 0) {
        m_chapterPage += dir;
        drawContents();
        return;
    }
    const int listTop = contentsListTop();
    if (y >= listTop && y < listTop + rows * ROW_H) {
        const int index = m_chapterPage * rows + (y - listTop) / ROW_H;
        if (index < (int)m_contents.size() && !jumpToContentsEntry(index)) drawContents();
    }
}
