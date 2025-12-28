#include "Button.h"
#include <raylib.h>

/**
 * @brief 按钮构造函数
 * 
 * 初始化按钮的所有属性，包括位置、文本、颜色等
 */
Button::Button(Rectangle bounds,
               const std::string& text,
               Color normalColor,
               Color hoverColor,
               Color pressedColor,
               Color textColor,
               int fontSize)
    : bounds_(bounds),           // 初始化按钮边界
      text_(text),               // 初始化按钮文本
      normalColor_(normalColor), // 初始化正常状态颜色
      hoverColor_(hoverColor),   // 初始化悬停状态颜色
      pressedColor_(pressedColor), // 初始化按下状态颜色
      textColor_(textColor),     // 初始化文本颜色
      fontSize_(fontSize)        // 初始化字体大小
{
}

/**
 * @brief 设置按钮显示的文本内容
 */
void Button::SetText(const std::string& text)
{
    text_ = text;
}

/**
 * @brief 设置按钮的位置和大小
 */
void Button::SetBounds(const Rectangle& bounds)
{
    bounds_ = bounds;
}

/**
 * @brief 设置按钮文本的字体大小
 */
void Button::SetFontSize(int fontSize)
{
    fontSize_ = fontSize;
}

/**
 * @brief 绘制按钮并处理交互
 * 
 * 工作流程：
 * 1. 获取鼠标位置
 * 2. 检测鼠标是否悬停在按钮上
 * 3. 检测按钮是否被按下（鼠标在按钮上且左键按下）
 * 4. 检测按钮是否被点击（鼠标在按钮上且左键释放）
 * 5. 根据状态选择合适的颜色
 * 6. 绘制按钮矩形和边框
 * 7. 计算文本居中位置并绘制
 * 
 * @return true 表示按钮在本帧被点击，false 表示未被点击
 */
bool Button::Draw()
{
    // 获取当前鼠标位置
    const Vector2 mouse = GetMousePosition();
    
    // 检测鼠标是否悬停在按钮矩形区域内
    const bool hover = CheckCollisionPointRec(mouse, bounds_);
    
    // 检测按钮是否被按下（鼠标悬停且左键按下）
    const bool pressed = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    
    // 检测按钮是否被点击（鼠标悬停且左键刚释放）
    const bool clicked = hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

    // 根据按钮状态选择填充颜色
    Color fill = normalColor_;  // 默认使用正常状态颜色
    if (pressed)
    {
        fill = pressedColor_;   // 按下时使用按下状态颜色
    }
    else if (hover)
    {
        fill = hoverColor_;     // 悬停时使用悬停状态颜色
    }

    // 绘制按钮矩形（填充色）
    DrawRectangleRec(bounds_, fill);
    
    // 绘制按钮边框（半透明黑色，线宽1.0）
    DrawRectangleLinesEx(bounds_, 1.0f, Fade(BLACK, 0.5f));

    // 计算文本宽度（用于居中对齐）
    const int textWidth = MeasureText(text_.c_str(), fontSize_);
    
    // 计算文本的X坐标（水平居中）
    const float textX = bounds_.x + (bounds_.width - static_cast<float>(textWidth)) * 0.5f;
    
    // 计算文本的Y坐标（垂直居中）
    const float textY = bounds_.y + (bounds_.height - static_cast<float>(fontSize_)) * 0.5f;
    
    // 绘制按钮文本
    DrawText(text_.c_str(), static_cast<int>(textX), static_cast<int>(textY), fontSize_, textColor_);

    // 返回按钮是否被点击
    return clicked;
}
