#include "GamesApp.h"
#include "Launcher.h"
#include "ui/UIFramework.h"
#include "ui/UIKit.h"
#include <ctime>

extern uint8_t* framebuffer;

namespace {
using namespace UI;

struct GameEntry {
    int app;
    const char* title;
    const char* blurb;
    UIIconId icon;
};

const GameEntry kGames[] = {
    {APP_TEARACE, "Tea Race 1866", "Race a clipper from China to London", ICON_SHIP},
    {APP_KEEPER, "The Keeper", "Keep a lighthouse through winter", ICON_LIGHTHOUSE},
    {APP_CODEBREAKER, "Codebreaker 1917", "Break the enemy cipher", ICON_KEY},
};
constexpr int kGameCount = sizeof(kGames) / sizeof(kGames[0]);
constexpr int kRowY = 112;
constexpr int kRowH = 92;
constexpr int kGridX = 74;
constexpr int kSudokuY = 214;
constexpr int kSudokuCell = 98;
constexpr int kLightsX = 78;
constexpr int kLightsY = 224;
constexpr int kLightsCell = 77;
constexpr int kSlidingX = 90;
constexpr int kSlidingY = 240;
constexpr int kSlidingCell = 120;
constexpr int kWordX = 50;
constexpr int kWordY = 190;
constexpr int kWordCell = 55;
constexpr int kWordSize = Games::WordSearch::side * kWordCell;

void drawPageFooter(int page, int pages) {
    fillRect(0, H - FOOTER_H, W, 2, BLACK);
    Rect previous(MARGIN, H - FOOTER_H + 12, 118, 64);
    Rect next(W - MARGIN - 118, H - FOOTER_H + 12, 118, 64);
    button(previous, "Previous", ButtonStyle::SECONDARY, FONT_BODY - 2, page > 0);
    button(next, "Next", ButtonStyle::SECONDARY, FONT_BODY - 2, page + 1 < pages);
    textCentered("Page " + std::to_string(page + 1) + " of " + std::to_string(pages),
                 W / 2, H - FOOTER_H + 27, FONT_BODY - 2, DARK);
}

void centeredCell(const std::string& value, const Rect& cell, float size, uint8_t color = BLACK) {
    textCentered(value, cell.x + cell.w / 2,
                 cell.y + (cell.h - lineHeight(size)) / 2, size, color, true);
}

void boardCell(const Rect& r, bool selected, bool marked = false, bool active = false) {
    uint8_t fill = active ? BLACK : (selected ? PALE : (marked ? PALE : WHITE));
    fillRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, fill);
    strokeRoundRect(r.x, r.y, r.w, r.h, 4, 2, BLACK);
}

void actionButton(const Rect& r, const std::string& label, bool primary = false) {
    button(r, label, primary ? ButtonStyle::PRIMARY : ButtonStyle::SECONDARY, FONT_BODY);
}
} // namespace

void GamesApp::onCreate() {
    m_screen = HUB;
    m_game = NONE;
    m_seed = static_cast<uint32_t>(std::time(nullptr)) | 1u;
}

void GamesApp::draw() {
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        if (m_screen == HUB) drawHub();
        else if (m_screen == PLAY) drawGame();
        else drawHelp();
    });
}

bool GamesApp::onBack() {
    if (m_screen == HELP) {
        m_screen = PLAY;
        draw();
        return true;
    }
    if (m_screen == PLAY) {
        m_screen = HUB;
        m_game = NONE;
        draw();
        return true;
    }
    return false;
}

void GamesApp::drawHub() {
    using namespace UI;
    header("Games", true);
    const char* names[] = {"Mini Sudoku", "Lights Out", "Sliding Puzzle", "Word Search"};
    const char* blurbs[] = {"Fill each row, column and box", "Turn every light off", "Slide tiles into order", "Find six words in the letter grid"};
    const UIIconId icons[] = {ICON_GAMES, ICON_GAMES, ICON_GAMES, ICON_BOOK};

    for (int i = 0; i < kGameCount; ++i) {
        int y = kRowY + i * kRowH;
        icon(kGames[i].icon, MARGIN, y + 18, 54);
        text(ellipsize(kGames[i].title, W - 146, FONT_BODY, true), MARGIN + 72, y + 10, FONT_BODY, BLACK, true);
        text(ellipsize(kGames[i].blurb, W - 146, FONT_BODY - 2), MARGIN + 72, y + 48, FONT_BODY - 2, DARK);
        chevronRight(W - MARGIN - 10, y + 44, 20, DARK);
        hRule(y + kRowH - 1);
    }

    for (int i = 0; i < 4; ++i) {
        int y = kRowY + (kGameCount + i) * kRowH;
        icon(icons[i], MARGIN, y + 18, 54);
        text(ellipsize(names[i], W - 146, FONT_BODY, true), MARGIN + 72, y + 10, FONT_BODY, BLACK, true);
        text(ellipsize(blurbs[i], W - 146, FONT_BODY - 2), MARGIN + 72, y + 48, FONT_BODY - 2, DARK);
        chevronRight(W - MARGIN - 10, y + 44, 20, DARK);
        hRule(y + kRowH - 1);
    }
    textCentered("Tap a game to begin. Rules are available on every puzzle screen.",
                 W / 2, 772, FONT_BODY - 2, DARK);
}

void GamesApp::startGame(Game game) {
    m_game = game;
    m_screen = PLAY;
    m_selected = -1;
    m_wordFirst = -1;
    m_feedback.clear();
    resetGame();
}

void GamesApp::resetGame() {
    m_seed += 0x9e3779b9u;
    if (m_game == SUDOKU) m_sudoku.reset(m_seed);
    else if (m_game == LIGHTS) m_lights.reset(m_seed);
    else if (m_game == SLIDING) m_sliding.reset(m_seed);
    else if (m_game == WORD_SEARCH) m_wordSearch.reset(m_seed);
    m_selected = -1;
    m_wordFirst = -1;
    m_feedback.clear();
    draw();
}

void GamesApp::finishTouchGame() {
    if ((m_game == SUDOKU && m_sudoku.won()) ||
        (m_game == LIGHTS && m_lights.won()) ||
        (m_game == SLIDING && m_sliding.won()) ||
        (m_game == WORD_SEARCH && m_wordSearch.won()))
        m_feedback = "Puzzle solved!";
}

void GamesApp::drawGame() {
    using namespace UI;
    const char* title = m_game == SUDOKU ? "Mini Sudoku" :
                        m_game == LIGHTS ? "Lights Out" :
                        m_game == SLIDING ? "Sliding Puzzle" : "Word Search";
    header(title, true, "Rules");
    textCentered(m_game == WORD_SEARCH ? "Tap the first and last letters of a word." :
                 m_game == SUDOKU ? "Select an empty square, then a number." :
                 m_game == LIGHTS ? "Tap a light to toggle it and its neighbors." :
                                    "Tap a tile beside the empty space.",
                 W / 2, 120, FONT_BODY - 2, DARK);

    if (m_game == SUDOKU) {
        for (int i = 0; i < 16; ++i) {
            int row = i / 4, col = i % 4;
            Rect r(kGridX + col * kSudokuCell, kSudokuY + row * kSudokuCell, kSudokuCell, kSudokuCell);
            boardCell(r, i == m_selected, false, m_sudoku.fixed[i]);
            if (m_sudoku.cells[i]) centeredCell(std::to_string(m_sudoku.cells[i]), r, FONT_TITLE,
                                                m_sudoku.fixed[i] ? WHITE : BLACK);
        }
        for (int i = 0; i <= 4; ++i) {
            int weight = (i == 0 || i == 4 || i == 2) ? 4 : 2;
            line(kGridX + i * kSudokuCell, kSudokuY, kGridX + i * kSudokuCell,
                 kSudokuY + 4 * kSudokuCell, weight, BLACK);
            line(kGridX, kSudokuY + i * kSudokuCell, kGridX + 4 * kSudokuCell,
                 kSudokuY + i * kSudokuCell, weight, BLACK);
        }
        int padY = 648, padW = 92, gap = 18;
        int padX = (W - 4 * padW - 3 * gap) / 2;
        for (int i = 0; i < 4; ++i) {
            Rect r(padX + i * (padW + gap), padY, padW, 68);
            actionButton(r, std::to_string(i + 1));
        }
        Rect clear(MARGIN, 738, W - 2 * MARGIN, 60);
        actionButton(clear, "Clear selected square");
    } else if (m_game == LIGHTS) {
        int left = 0;
        for (bool on : m_lights.cells) if (on) ++left;
        textCentered("Lights still on: " + std::to_string(left), W / 2, 166, FONT_BODY, BLACK, true);
        for (int i = 0; i < 25; ++i) {
            int row = i / 5, col = i % 5;
            Rect r(kLightsX + col * kLightsCell, kLightsY + row * kLightsCell, kLightsCell, kLightsCell);
            boardCell(r, false, false, m_lights.cells[i]);
            centeredCell(m_lights.cells[i] ? "ON" : "OFF", r, FONT_BODY - 2,
                         m_lights.cells[i] ? WHITE : DARK);
        }
        Rect reset(MARGIN, 700, W - 2 * MARGIN, 64);
        actionButton(reset, "New puzzle");
    } else if (m_game == SLIDING) {
        for (int i = 0; i < 9; ++i) {
            int row = i / 3, col = i % 3;
            Rect r(kSlidingX + col * kSlidingCell, kSlidingY + row * kSlidingCell,
                   kSlidingCell, kSlidingCell);
            boardCell(r, false, false, m_sliding.cells[i] == 0);
            if (m_sliding.cells[i]) centeredCell(std::to_string(m_sliding.cells[i]), r, FONT_TITLE);
        }
        Rect reset(MARGIN, 640, W - 2 * MARGIN, 64);
        actionButton(reset, "New puzzle");
    } else {
        for (int i = 0; i < 64; ++i) {
            int row = i / 8, col = i % 8;
            Rect r(kWordX + col * kWordCell, kWordY + row * kWordCell, kWordCell, kWordCell);
            boardCell(r, i == m_wordFirst, m_wordSearch.marked[i]);
            centeredCell(std::string(1, m_wordSearch.cells[i]), r, FONT_BODY - 2,
                         m_wordSearch.marked[i] ? BLACK : INK);
        }
        for (int i = 0; i < 6; ++i) {
            int col = i % 2, row = i / 2;
            int x = 70 + col * 220, y = 648 + row * 42;
            text(std::string(m_wordSearch.found[i] ? "✓ " : "• ") + Games::WordSearch::words[i],
                 x, y, FONT_BODY - 2, m_wordSearch.found[i] ? DARK : BLACK, m_wordSearch.found[i]);
        }
        Rect reset(MARGIN, 840, W - 2 * MARGIN, 60);
        actionButton(reset, "New puzzle");
    }
    if (!m_feedback.empty())
        textCentered(m_feedback, W / 2, m_game == WORD_SEARCH ? 778 : 806, FONT_BODY, BLACK, true);
}

void GamesApp::drawHelp() {
    using namespace UI;
    const char* name = m_game == SUDOKU ? "Mini Sudoku" :
                       m_game == LIGHTS ? "Lights Out" :
                       m_game == SLIDING ? "Sliding Puzzle" : "Word Search";
    header(std::string(name) + " rules", true);
    textCentered("Page " + std::to_string(m_helpPage + 1) + " of 2",
                 W / 2, HEADER_H + 24, FONT_BODY - 2, DARK);
    const char* first = "";
    const char* second = "";
    if (m_game == SUDOKU) {
        first = "Fill every empty square with 1, 2, 3 or 4. Each number appears once in every row and column.";
        second = "Each outlined 2 by 2 box also needs all four numbers. Select a blank square, then tap a number. Tap Clear to erase your entry.";
    } else if (m_game == LIGHTS) {
        first = "The goal is to turn every light off. Tap any square to change it.";
        second = "A tap flips that light plus its up, down, left and right neighbors. Lights toggle, so a second tap reverses the move.";
    } else if (m_game == SLIDING) {
        first = "Put the numbered tiles in order from 1 to 8, leaving the empty square at the lower right.";
        second = "Tap a tile next to the empty square to slide it into the gap. Only legal moves are accepted; every puzzle starts solvable.";
    } else {
        first = "Find PAPER, BOOK, INK, READ, PAGE and STORY in the letter grid. Words run horizontally, vertically or diagonally.";
        second = "Tap a word's first letter, then its last letter. Words may run forwards or backwards. Found words are marked in the grid and list.";
    }
    textWrapped(m_helpPage == 0 ? first : second, MARGIN + 18, HEADER_H + 108,
                W - 2 * MARGIN - 36, FONT_BODY, INK, 0, 14);
    drawPageFooter(m_helpPage, 2);
}

void GamesApp::handleTouch(int x, int y) {
    using namespace UI;
    if (m_screen == HUB) {
        if (y < kRowY) return;
        int row = (y - kRowY) / kRowH;
        if (row < 0 || row >= kGameCount + 4) return;
        if (row < kGameCount) {
            Launcher::getInstance().requestLaunch(kGames[row].app, APP_GAMES);
            return;
        }
        startGame(static_cast<Game>(SUDOKU + row - kGameCount));
        return;
    }

    if (m_screen == HELP) {
        if (x >= MARGIN && x < MARGIN + 118 && y >= H - FOOTER_H + 12 && y < H - 12 && m_helpPage > 0) {
            --m_helpPage;
            draw();
        } else if (x >= W - MARGIN - 118 && x < W - MARGIN && y >= H - FOOTER_H + 12 &&
                   y < H - 12 && m_helpPage < 1) {
            ++m_helpPage;
            draw();
        }
        return;
    }

    if (hitHeaderAction(x, y, "Rules")) {
        m_screen = HELP;
        m_helpPage = 0;
        draw();
        return;
    }

    if (m_game == SUDOKU) {
        if (x >= kGridX && x < kGridX + 4 * kSudokuCell && y >= kSudokuY &&
            y < kSudokuY + 4 * kSudokuCell) {
            int col = (x - kGridX) / kSudokuCell, row = (y - kSudokuY) / kSudokuCell;
            m_selected = row * 4 + col;
            draw();
            return;
        }
        if (m_selected >= 0 && y >= 648 && y < 716) {
            int width = 4 * 92 + 3 * 18;
            int start = (W - width) / 2;
            int col = (x - start) / 110;
            if (x >= start && col >= 0 && col < 4) {
                if (!m_sudoku.place(m_selected, col + 1)) m_feedback = "That number conflicts with a row, column or box.";
                else {
                    m_feedback.clear();
                    finishTouchGame();
                }
                draw();
                return;
            }
        }
        if (y >= 738 && y < 798 && x >= MARGIN && x < W - MARGIN && m_selected >= 0) {
            if (!m_sudoku.fixed[m_selected]) m_sudoku.place(m_selected, 0);
            m_feedback.clear();
            draw();
            return;
        }
    } else if (m_game == LIGHTS) {
        if (x >= kLightsX && x < kLightsX + 5 * kLightsCell &&
            y >= kLightsY && y < kLightsY + 5 * kLightsCell) {
            int col = (x - kLightsX) / kLightsCell, row = (y - kLightsY) / kLightsCell;
            m_lights.toggle(row * 5 + col);
            finishTouchGame();
            draw();
            return;
        }
        if (y >= 700 && y < 764) resetGame();
    } else if (m_game == SLIDING) {
        if (x >= kSlidingX && x < kSlidingX + 3 * kSlidingCell &&
            y >= kSlidingY && y < kSlidingY + 3 * kSlidingCell) {
            int col = (x - kSlidingX) / kSlidingCell, row = (y - kSlidingY) / kSlidingCell;
            if (!m_sliding.move(row * 3 + col)) m_feedback = "Choose a tile beside the empty square.";
            else {
                m_feedback.clear();
                finishTouchGame();
            }
            draw();
            return;
        }
        if (y >= 640 && y < 704) resetGame();
    } else if (m_game == WORD_SEARCH) {
        if (x >= kWordX && x < kWordX + kWordSize && y >= kWordY && y < kWordY + kWordSize) {
            int col = (x - kWordX) / kWordCell, row = (y - kWordY) / kWordCell;
            int cell = row * 8 + col;
            if (m_wordFirst < 0) {
                m_wordFirst = cell;
                m_feedback = "Now tap the last letter.";
            } else {
                int found = m_wordSearch.select(m_wordFirst, cell);
                m_wordFirst = -1;
                m_feedback = found >= 0 ? std::string("Found ") + Games::WordSearch::words[found] :
                                          "No matching word. Try another line.";
                finishTouchGame();
            }
            draw();
            return;
        }
        if (y >= 840 && y < 900) resetGame();
    }
}
