#include "CalculatorApp.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "ui/UIFramework.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cctype>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#else
#include <stdio.h>
#endif

extern uint8_t *framebuffer;
extern TypographyEngine typography;

struct CalcButton {
    int x, y, w, h;
    const char* label;
};

static const CalcButton buttons[] = {
    // Row 0 (y=200, rowH=150)
    {0, 200, 135, 150, "C"},
    {135, 200, 135, 150, "Exit"},
    {270, 200, 135, 150, "/"},
    {405, 200, 135, 150, "*"},
    
    // Row 1 (y=350, rowH=150)
    {0, 350, 135, 150, "7"},
    {135, 350, 135, 150, "8"},
    {270, 350, 135, 150, "9"},
    {405, 350, 135, 150, "-"},
    
    // Row 2 (y=500, rowH=150)
    {0, 500, 135, 150, "4"},
    {135, 500, 135, 150, "5"},
    {270, 500, 135, 150, "6"},
    {405, 500, 135, 150, "+"},
    
    // Row 3 (y=650, rowH=150)
    {0, 650, 135, 150, "1"},
    {135, 650, 135, 150, "2"},
    {270, 650, 135, 150, "3"},
    {405, 650, 135, 300, "="}, // Spans row 3 and row 4 (height 300 to bottom)
    
    // Row 4 (y=800, rowH=150)
    {0, 800, 270, 150, "0"}, // Double width
    {270, 800, 135, 150, "."}
};

void CalculatorApp::onCreate() {
    currentValue = "0";
    expression = "";
    afterEquals = false;

    // Calculator is designed for portrait orientation: left side of the device
    // becomes the top of the screen.
    DisplayHAL::setPortrait(true);
}

void CalculatorApp::onDestroy() {
    // Restore landscape orientation when leaving the calculator.
    DisplayHAL::setPortrait(false);
}

void CalculatorApp::draw() {
    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();

    // 1. Clear whole screen
    UIFramework::clearArea(framebuffer, 0, 0, w, h);

    // 2. Draw display value and expression
    drawDisplay();

    // 3. Draw buttons
    for (const auto& btn : buttons) {
        UIFramework::drawButton(framebuffer, btn.x, btn.y, btn.w, btn.h, "");
        
        // Render text label centered
        typography.setFontSize(36.0f);
        int labelW = typography.measureText(btn.label);
        int textX = btn.x + (btn.w - labelW) / 2;
        int textY = btn.y + (btn.h - 36) / 2;
        typography.renderText(btn.label, textX, textY, framebuffer, 0x00);
    }

    // 4. Update the screen
    DisplayHAL::display(framebuffer);
}

void CalculatorApp::drawDisplay() {
    int w = DisplayHAL::getWidth();

    // Step 1: Localized clear pass - wipe display area in memory & flush to E-Ink panel to reset microspheres
    UIFramework::clearArea(framebuffer, 0, 0, w, 199);
    DisplayHAL::display(framebuffer);

    // Step 2: Draw horizontal separator and render updated values
    DisplayHAL::drawHLine(0, 199, w, 0x00, framebuffer);

    // Render expression (smaller font size, top-right aligned)
    if (!expression.empty()) {
        typography.setFontSize(24.0f);
        int exprW = typography.measureText(expression);
        int exprX = w - 20 - exprW;
        if (exprX < 20) exprX = 20; // Bound to left margin if too long
        typography.renderText(expression, exprX, 40, framebuffer, 0x04); // Dark grey text
    }

    // Render current value (larger font size, bottom-right aligned)
    typography.setFontSize(54.0f);
    std::string valToDraw = currentValue.empty() ? "0" : currentValue;
    int valW = typography.measureText(valToDraw);
    int valX = w - 20 - valW;
    if (valX < 20) valX = 20; // Bound to left margin if too long
    typography.renderText(valToDraw, valX, 115, framebuffer, 0x00); // Solid black text
}

void CalculatorApp::handleTouch(int x, int y) {
    for (const auto& btn : buttons) {
        if (x >= btn.x && x <= btn.x + btn.w && y >= btn.y && y <= btn.y + btn.h) {
            handleButtonPress(btn.label);
            break;
        }
    }
}

void CalculatorApp::handleButtonPress(const std::string& label) {
    if (label == "Exit") {
        extern void exitToSystemLauncher();
        exitToSystemLauncher();
        return;
    }
    
    if (label == "C") {
        currentValue = "0";
        expression = "";
        afterEquals = false;
    } else if (label == "+" || label == "-" || label == "*" || label == "/") {
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
            } else {
                currentValue += label;
            }
        }
    }
    
    draw();
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
