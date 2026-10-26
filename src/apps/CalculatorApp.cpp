#include "CalculatorApp.h"
#include "ui/UIFramework.h"
#include "ui/UIKit.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cctype>

extern uint8_t *framebuffer;

namespace {
using namespace UI;

const Rect kPanel(MARGIN, HEADER_H + 20, W - 2 * MARGIN, 200);
constexpr int KEY_GAP = 14;
constexpr int KEYS_TOP = HEADER_H + 20 + 200 + 24;
constexpr int KEYS_BOTTOM = H - 24;
constexpr int KEY_ROWS = 5;
constexpr int KEY_COLS = 4;

// Internal labels are what handleButtonPress() understands ("<" is
// backspace); the keypad shows typographic symbols instead.
struct CalcKey {
    const char* label;
    int col, row, colSpan, rowSpan;
    ButtonStyle style;
};

const CalcKey kKeys[] = {
    {"C", 0, 0, 1, 1, ButtonStyle::SUBTLE},
    {"<", 1, 0, 1, 1, ButtonStyle::SUBTLE},
    {"/", 2, 0, 1, 1, ButtonStyle::PRIMARY},
    {"*", 3, 0, 1, 1, ButtonStyle::PRIMARY},
    {"7", 0, 1, 1, 1, ButtonStyle::SECONDARY},
    {"8", 1, 1, 1, 1, ButtonStyle::SECONDARY},
    {"9", 2, 1, 1, 1, ButtonStyle::SECONDARY},
    {"-", 3, 1, 1, 1, ButtonStyle::PRIMARY},
    {"4", 0, 2, 1, 1, ButtonStyle::SECONDARY},
    {"5", 1, 2, 1, 1, ButtonStyle::SECONDARY},
    {"6", 2, 2, 1, 1, ButtonStyle::SECONDARY},
    {"+", 3, 2, 1, 1, ButtonStyle::PRIMARY},
    {"1", 0, 3, 1, 1, ButtonStyle::SECONDARY},
    {"2", 1, 3, 1, 1, ButtonStyle::SECONDARY},
    {"3", 2, 3, 1, 1, ButtonStyle::SECONDARY},
    {"=", 3, 3, 1, 2, ButtonStyle::PRIMARY},
    {"0", 0, 4, 2, 1, ButtonStyle::SECONDARY},
    {".", 2, 4, 1, 1, ButtonStyle::SECONDARY},
};

int colX(int c) { return MARGIN + c * (W - 2 * MARGIN + KEY_GAP) / KEY_COLS; }
int rowY(int r) { return KEYS_TOP + r * (KEYS_BOTTOM - KEYS_TOP + KEY_GAP) / KEY_ROWS; }

Rect keyRect(const CalcKey& k) {
    int x = colX(k.col), y = rowY(k.row);
    return Rect(x, y, colX(k.col + k.colSpan) - KEY_GAP - x, rowY(k.row + k.rowSpan) - KEY_GAP - y);
}

std::string symbolFor(char op) {
    switch (op) {
        case '*': return "\xC3\x97";      // multiplication sign
        case '/': return "\xC3\xB7";      // division sign
        case '-': return "\xE2\x88\x92";  // minus sign
        default: return std::string(1, op);
    }
}

std::string prettify(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '*' || c == '/' || c == '-') out += symbolFor(c);
        else out += c;
    }
    return out;
}

// Ellipsizes from the left so the most recent input stays visible.
std::string keepTail(const std::string& s, int maxW, float size, bool bold) {
    if (textWidth(s, size, bold) <= maxW) return s;
    std::string out = s;
    while (!out.empty() && textWidth("..." + out, size, bold) > maxW) {
        out.erase(0, 1);
        while (!out.empty() && ((unsigned char)out[0] & 0xC0) == 0x80) out.erase(0, 1);
    }
    return "..." + out;
}

void backspaceGlyph(const Rect& r) {
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    int l = cx - 26, ml = cx - 12, rt = cx + 24, hh = 16;
    line(l, cy, ml, cy - hh, 3, BLACK);
    line(ml, cy - hh, rt, cy - hh, 3, BLACK);
    line(rt, cy - hh, rt, cy + hh, 3, BLACK);
    line(rt, cy + hh, ml, cy + hh, 3, BLACK);
    line(ml, cy + hh, l, cy, 3, BLACK);
    int xc = cx + 5, d = 7;
    line(xc - d, cy - d, xc + d, cy + d, 3, BLACK);
    line(xc - d, cy + d, xc + d, cy - d, 3, BLACK);
}

constexpr size_t MAX_DIGITS = 15;
} // namespace

void CalculatorApp::onCreate() {
    currentValue = "0";
    expression = "";
    afterEquals = false;
}

void CalculatorApp::onDestroy() {
}

void CalculatorApp::draw() {
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        header("Calculator", true);
        drawDisplayContent();
        for (const CalcKey& k : kKeys) {
            Rect r = keyRect(k);
            std::string label = k.label;
            if (label == "<") label.clear();
            else if (label.size() == 1) label = symbolFor(label[0]);
            // Symbol glyphs are small at the digit size.
            bool symbol = !isdigit((unsigned char)k.label[0]) && k.label[0] != 'C';
            button(r, label, k.style, symbol ? FONT_TITLE + 20 : FONT_TITLE + 6);
            if (label.empty()) backspaceGlyph(r);
        }
    });
}

void CalculatorApp::drawDisplayContent() {
    const Rect& p = kPanel;
    strokeRoundRect(p.x, p.y, p.w, p.h, 24, 2, BLACK);
    int right = p.x + p.w - 28;
    int maxW = p.w - 56;

    if (!expression.empty()) {
        std::string expr = prettify(expression) + (afterEquals ? " =" : "");
        expr = keepTail(expr, maxW, FONT_BODY, false);
        textRight(expr, right, p.y + 24, FONT_BODY, DARK);
    }

    std::string value = prettify(currentValue.empty() ? "0" : currentValue);
    float size = 84.0f;
    while (size > 36.0f && textWidth(value, size, true) > maxW) size -= 4.0f;
    value = keepTail(value, maxW, size, true);
    int lh = lineHeight(size);
    textRight(value, right, p.y + p.h - 22 - lh, size, BLACK, true);
}

void CalculatorApp::drawDisplay() {
    const Rect& p = kPanel;
    UIFramework::performFastPartialUpdate(framebuffer, p.x, p.y, p.w, p.h, [this]() {
        drawDisplayContent();
    });
}

void CalculatorApp::handleTouch(int x, int y) {
    for (const CalcKey& k : kKeys) {
        if (keyRect(k).contains(x, y)) {
            handleButtonPress(k.label);
            return;
        }
    }
}

void CalculatorApp::backspace() {
    if (afterEquals || currentValue == "Error") {
        currentValue = "0";
        expression = "";
        afterEquals = false;
        return;
    }
    if (!currentValue.empty()) {
        currentValue.pop_back();
        if (currentValue.empty() || currentValue == "-") {
            currentValue = expression.empty() ? "0" : "";
        }
        return;
    }
    // Nothing typed since the last operator: remove the operator and bring
    // the previous operand back for editing.
    if (expression.size() >= 3) {
        expression.erase(expression.size() - 3);
        size_t sp = expression.find_last_of(' ');
        if (sp == std::string::npos) {
            currentValue = expression;
            expression.clear();
        } else {
            currentValue = expression.substr(sp + 1);
            expression.erase(sp + 1);
        }
        if (currentValue.empty()) currentValue = "0";
    }
}

void CalculatorApp::handleButtonPress(const std::string& label) {
    if (label == "C") {
        currentValue = "0";
        expression = "";
        afterEquals = false;
    } else if (label == "<") {
        backspace();
    } else if (label == "+" || label == "-" || label == "*" || label == "/") {
        if (currentValue == "Error") {
            currentValue = "0";
            expression = "";
        }
        if (afterEquals) {
            expression = currentValue + " " + label + " ";
            currentValue = "";
            afterEquals = false;
        } else {
            if (!currentValue.empty()) {
                expression += currentValue + " " + label + " ";
                currentValue = "";
            } else if (!expression.empty()) {
                // Replace last operator
                if (expression.length() >= 3) {
                    expression = expression.substr(0, expression.length() - 3) + " " + label + " ";
                }
            }
        }
    } else if (label == "=") {
        if (!afterEquals) {
            if (!currentValue.empty()) {
                expression += currentValue;
            }
            std::string cleaned = cleanExpression(expression);
            double result = 0.0;
            std::vector<Token> tokens = tokenize(cleaned);
            if (evaluateTokens(tokens, result)) {
                currentValue = formatResult(result);
            } else {
                currentValue = "Error";
            }
            afterEquals = true;
        }
    } else if (label == ".") {
        if (afterEquals) {
            currentValue = "0.";
            expression = "";
            afterEquals = false;
        } else {
            if (currentValue.empty()) {
                currentValue = "0.";
            } else if (currentValue.find('.') == std::string::npos) {
                currentValue += ".";
            }
        }
    } else {
        // Digits 0-9
        if (afterEquals) {
            currentValue = label;
            expression = "";
            afterEquals = false;
        } else {
            if (currentValue == "0") {
                currentValue = label;
            } else if (currentValue.size() < MAX_DIGITS) {
                currentValue += label;
            }
        }
    }

    drawDisplay();
}

std::vector<CalculatorApp::Token> CalculatorApp::tokenize(const std::string& exprStr) {
    std::vector<Token> tokens;
    std::string numStr = "";
    
    for (size_t i = 0; i < exprStr.length(); ++i) {
        char c = exprStr[i];
        if (isspace((unsigned char)c)) {
            continue;
        }
        if (isdigit((unsigned char)c) || c == '.') {
            numStr += c;
        } else if (c == '+' || c == '-' || c == '*' || c == '/') {
            if (!numStr.empty()) {
                double val = strtod(numStr.c_str(), nullptr);
                tokens.push_back({true, val, '\0'});
                numStr = "";
            }
            // Check for unary minus
            if (c == '-' && (tokens.empty() || (!tokens.back().isNumber))) {
                numStr = "-";
            } else {
                tokens.push_back({false, 0.0, c});
            }
        }
    }
    if (!numStr.empty()) {
        double val = strtod(numStr.c_str(), nullptr);
        tokens.push_back({true, val, '\0'});
    }
    return tokens;
}

bool CalculatorApp::evaluateTokens(const std::vector<Token>& tokens, double& result) {
    if (tokens.empty()) return false;
    
    // First pass: multiplication and division
    std::vector<Token> pass1;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (!tokens[i].isNumber && (tokens[i].op == '*' || tokens[i].op == '/')) {
            char op = tokens[i].op;
            if (pass1.empty() || i + 1 >= tokens.size() || !tokens[i+1].isNumber) {
                return false;
            }
            double left = pass1.back().value;
            double right = tokens[i+1].value;
            double val = 0.0;
            if (op == '*') {
                val = left * right;
            } else {
                if (right == 0.0) {
                    return false; // Division by zero
                }
                val = left / right;
            }
            pass1.back().value = val;
            i++;
        } else {
            pass1.push_back(tokens[i]);
        }
    }
    
    if (pass1.empty()) return false;
    
    // Second pass: addition and subtraction from left to right
    if (!pass1[0].isNumber) return false;
    double currentVal = pass1[0].value;
    
    for (size_t i = 1; i < pass1.size(); i += 2) {
        if (i + 1 >= pass1.size()) return false;
        Token opToken = pass1[i];
        Token numToken = pass1[i+1];
        if (opToken.isNumber || !numToken.isNumber) return false;
        
        if (opToken.op == '+') {
            currentVal += numToken.value;
        } else if (opToken.op == '-') {
            currentVal -= numToken.value;
        } else {
            return false;
        }
    }
    
    result = currentVal;
    return true;
}

std::string CalculatorApp::cleanExpression(const std::string& expr) {
    std::string cleaned = expr;
    while (!cleaned.empty() && isspace((unsigned char)cleaned.back())) {
        cleaned.pop_back();
    }
    if (!cleaned.empty()) {
        char last = cleaned.back();
        if (last == '+' || last == '-' || last == '*' || last == '/') {
            cleaned.pop_back();
        }
    }
    while (!cleaned.empty() && isspace((unsigned char)cleaned.back())) {
        cleaned.pop_back();
    }
    return cleaned;
}

std::string CalculatorApp::formatResult(double val) {
    if (std::isnan(val) || std::isinf(val)) return "Error";
    char buf[64];
    snprintf(buf, sizeof(buf), "%.10g", val);
    std::string s(buf);
    if (s == "-0") s = "0";
    return s;
}
