#pragma once

#include <string>
#include <raylib.h>

class Button
{
public:
    Button(Rectangle bounds,
           const std::string& text,
           Color normalColor = DARKGRAY,
           Color hoverColor = GRAY,
           Color pressedColor = LIGHTGRAY,
           Color textColor = RAYWHITE,
           int fontSize = 20);

    void SetText(const std::string& text);

    bool Draw();

private:
    Rectangle bounds_;
    std::string text_;
    Color normalColor_;
    Color hoverColor_;
    Color pressedColor_;
    Color textColor_;
    int fontSize_;
};
