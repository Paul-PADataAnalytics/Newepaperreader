#pragma once
#include <string>

/**
 * MarkdownParser class
 * 
 * Provides basic stripping of Markdown formatting syntax.
 * Designed to clean up Markdown text before passing it to TypographyEngine.
 * 
 * Supported formatting stripped:
 * - Bold/Italic (*, **, _, __)
 * - Headings (#)
 * - Links ([text](url) becomes text)
 * - Images (![alt](url) becomes alt)
 * - Inline code blocks (`)
 * - Multi-line code blocks (```)
 * - Strikethrough (~~)
 */
class MarkdownParser {
public:
    static std::string stripMarkdown(const std::string& input) {
        std::string result;
        result.reserve(input.size());
        
        bool inCodeBlock = false;
        bool inInlineCode = false;
        
        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];
            
            // Code block ```
            if (c == '`' && i + 2 < input.size() && input[i+1] == '`' && input[i+2] == '`') {
                inCodeBlock = !inCodeBlock;
                i += 2;
                continue;
            }
            
            // Inline code `
            if (c == '`' && !inCodeBlock) {
                inInlineCode = !inInlineCode;
                continue;
            }
            
            // If in code, keep as is
            if (inCodeBlock || inInlineCode) {
                result += c;
                continue;
            }
            
            // Bold/Italic ** or * or __ or _
            if (c == '*' || c == '_') {
                if (i + 1 < input.size() && input[i+1] == c) {
                    i++; // skip second
                }
                continue;
            }
            
            // Headings #
            if (c == '#') {
                // Check if it's at start of line
                bool isStartOfLine = (i == 0 || input[i-1] == '\n');
                if (isStartOfLine) {
                    while (i < input.size() && input[i] == '#') {
                        i++;
                    }
                    if (i < input.size() && input[i] == ' ') {
                        // skip the space after heading
                    } else {
                        // backtrack if it was something else, though rare
                        i--;
                    }
                    continue;
                }
            }
            
            // Links [text](url) -> text
            if (c == '[') {
                size_t closeBracket = input.find(']', i);
                if (closeBracket != std::string::npos) {
                    size_t openParen = closeBracket + 1;
                    if (openParen < input.size() && input[openParen] == '(') {
                        size_t closeParen = input.find(')', openParen);
                        if (closeParen != std::string::npos) {
                            // Valid link, just append the text part
                            std::string linkText = input.substr(i + 1, closeBracket - i - 1);
                            result += linkText;
                            i = closeParen; // skip the whole [text](url)
                            continue;
                        }
                    }
                }
            }
            
            // Images ![alt](url) -> alt
            if (c == '!' && i + 1 < input.size() && input[i+1] == '[') {
                size_t closeBracket = input.find(']', i + 1);
                if (closeBracket != std::string::npos) {
                    size_t openParen = closeBracket + 1;
                    if (openParen < input.size() && input[openParen] == '(') {
                        size_t closeParen = input.find(')', openParen);
                        if (closeParen != std::string::npos) {
                            std::string altText = input.substr(i + 2, closeBracket - i - 2);
                            result += altText;
                            i = closeParen;
                            continue;
                        }
                    }
                }
            }
            
            // Strikethrough ~~
            if (c == '~' && i + 1 < input.size() && input[i+1] == '~') {
                i++;
                continue;
            }
            
            result += c;
        }
        
        return result;
    }
};
