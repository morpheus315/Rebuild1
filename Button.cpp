#include "Button.h"

#include <raylib.h>

Button::Button(Rectangle bounds,
               const std::string& text,
               Color normalColor,
               Color hoverColor,
               Color pressedColor,
               Color textColor,
               int fontSize)
    : bounds_(bounds),
      text_(text),
      normalColor_(normalColor),
      hoverColor_(hoverColor),
      pressedColor_(pressedColor),
      textColor_(textColor),
      fontSize_(fontSize)
{
}

void Button::SetText(const std::string& text)
{
    text_ = text;
}

void Button::SetBounds(const Rectangle& bounds)
{
    bounds_ = bounds;
}

void Button::SetFontSize(int fontSize)
{
    fontSize_ = fontSize;
}

bool Button::Draw()
{
    const Vector2 mouse = GetMousePosition();
    const bool hover = CheckCollisionPointRec(mouse, bounds_);
    const bool pressed = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const bool clicked = hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

    Color fill = normalColor_;
    if (pressed)
    {
        fill = pressedColor_;
    }
    else if (hover)
    {
        fill = hoverColor_;
    }

    DrawRectangleRec(bounds_, fill);
    DrawRectangleLinesEx(bounds_, 1.0f, Fade(BLACK, 0.5f));

    const int textWidth = MeasureText(text_.c_str(), fontSize_);
    const float textX = bounds_.x + (bounds_.width - static_cast<float>(textWidth)) * 0.5f;
    const float textY = bounds_.y + (bounds_.height - static_cast<float>(fontSize_)) * 0.5f;
    DrawText(text_.c_str(), static_cast<int>(textX), static_cast<int>(textY), fontSize_, textColor_);

    return clicked;
}
