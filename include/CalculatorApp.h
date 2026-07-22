#pragma once

#include "Application.h"
#include <string>
#include <vector>

class CalculatorApp : public Application {
public:
    CalculatorApp() = default;
    virtual ~CalculatorApp() = default;

    virtual void onCreate() override;
    virtual void onDestroy() override;

    virtual void draw() override;
    virtual void handleTouch(int x, int y) override;

private:
    std::string currentValue = "0";
    std::string expression = "";
    bool afterEquals = false;

    // Helper structures for expression parsing
    struct Token {
        bool isNumber;
        double value;
        char op;
    };

    void drawDisplay();
    void handleButtonPress(const std::string& label);
    std::vector<Token> tokenize(const std::string& expr);
    bool evaluateTokens(const std::vector<Token>& tokens, double& result);
    std::string cleanExpression(const std::string& expr);
    std::string formatResult(double val);
};
