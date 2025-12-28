#pragma once

#include <string>
#include <raylib.h>

/**
 * @brief 按钮类
 * 
 * 提供可交互的UI按钮功能，支持鼠标悬停、点击等状态
 * 使用 raylib 库进行图形渲染
 */
class Button
{
public:
    /**
     * @brief 构造函数
     * 
     * @param bounds 按钮的矩形边界（位置和大小）
     * @param text 按钮上显示的文本
     * @param normalColor 正常状态下的按钮颜色（默认：深灰色）
     * @param hoverColor 鼠标悬停时的按钮颜色（默认：灰色）
     * @param pressedColor 按钮被按下时的颜色（默认：浅灰色）
     * @param textColor 按钮文本颜色（默认：白色）
     * @param fontSize 文本字体大小（默认：20）
     */
    Button(Rectangle bounds,
           const std::string& text,
           Color normalColor = DARKGRAY,
           Color hoverColor = GRAY,
           Color pressedColor = LIGHTGRAY,
           Color textColor = RAYWHITE,
           int fontSize = 20);

    /**
     * @brief 设置按钮文本
     * @param text 新的按钮文本
     */
    void SetText(const std::string& text);

    /**
     * @brief 设置按钮边界（位置和大小）
     * @param bounds 新的矩形边界
     */
    void SetBounds(const Rectangle& bounds);

    /**
     * @brief 设置字体大小
     * @param fontSize 新的字体大小
     */
    void SetFontSize(int fontSize);

    /**
     * @brief 绘制按钮并检测点击
     * 
     * 此函数会：
     * 1. 根据鼠标状态（正常/悬停/按下）绘制按钮
     * 2. 绘制按钮文本（居中对齐）
     * 3. 检测是否被点击
     * 
     * @return true 如果按钮在本帧被点击（鼠标释放），false 否则
     */
    bool Draw();

private:
    Rectangle bounds_;       // 按钮的矩形边界
    std::string text_;       // 按钮文本
    Color normalColor_;      // 正常状态颜色
    Color hoverColor_;       // 悬停状态颜色
    Color pressedColor_;     // 按下状态颜色
    Color textColor_;        // 文本颜色
    int fontSize_;           // 字体大小
};
