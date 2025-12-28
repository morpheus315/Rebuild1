#include "Game.h"
#include "LanP2PNode.h"
#include "GameClient.h"
#include <string>
#include <thread>
#include <chrono>

// Windows API 头文件（减少包含内容以避免冲突）
#define WIN32_LEAN_AND_MEAN  // 排除不常用的Windows API
#define NOGDI                // 排除GDI相关定义
#define NOUSER               // 排除USER相关定义
#include <windows.h>
#undef NOGDI                 // 恢复GDI定义
#undef NOUSER                // 恢复USER定义

#include <raylib.h>

// 解决 Windows.h 和 raylib.h 之间的宏冲突
#ifdef Rectangle
#undef Rectangle
#endif
#ifdef DrawText
#undef DrawText
#endif
#ifdef CloseWindow
#undef CloseWindow
#endif
#ifdef ShowCursor
#undef ShowCursor
#endif

/**
 * @brief 程序主入口函数
 * 
 * 程序流程：
 * 1. 初始化局域网P2P节点
 * 2. 创建游戏客户端
 * 3. 显示名称输入窗口
 * 4. 进入主游戏循环（匹配大厅 -> 游戏 -> 循环）
 * 5. 清理资源并退出
 * 
 * @return 0 正常退出
 */
int main()
{
    using namespace lanp2p;

    // ========== 初始化网络节点 ==========
    
    // 创建局域网P2P节点（TCP端口37000，UDP自动分配）
    LanP2PNode node(37000, 0);
    
    // 设置节点过期时间（15秒无响应视为离线）
    node.setPeerStaleMs(15000);
    
    // 启动广播（只发送心跳，暂不监听）
    node.startBroadcastOnly();

    // 创建游戏客户端
    Client client(node);

    // ========== 初始化窗口 ==========
    
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);  // 允许窗口大小调整
    InitWindow(920, 720, "3D Chess Online");
    SetWindowMinSize(640, 480);              // 设置最小窗口尺寸
    SetTargetFPS(60);                        // 设置帧率

    // ========== 名称输入界面 ==========
    
    SetWindowSize(400, 200);
    SetWindowTitle("Enter Your Name");

    std::string name;          // 玩家名称
    bool nameEntered = false;  // 是否已输入名称

    // 名称输入循环
    while (!WindowShouldClose() && !nameEntered)
    {
        // 获取键盘输入的字符
        int key = GetCharPressed();
        while (key > 0)
        {
            // 接受可打印字符（ASCII 32-125），限制长度20
            if ((key >= 32) && (key <= 125) && (name.length() < 20))
            {
                name += (char)key;
            }
            key = GetCharPressed();
        }

        // 处理退格键
        if (IsKeyPressed(KEY_BACKSPACE) && name.length() > 0)
        {
            name.pop_back();
        }

        // 处理回车键（确认输入）
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
        {
            // 如果未输入名称，使用默认名称"Guest"
            if (name.empty())
            {
                name = "Guest";
            }
            nameEntered = true;
        }

        // ========== 绘制输入界面 ==========
        
        int screenWidth = GetScreenWidth();
        int screenHeight = GetScreenHeight();

        // 计算UI元素位置（居中对齐）
        int textWidth = MeasureText("Enter Your Name:", 24);
        int inputBoxWidth = 300;
        int inputBoxHeight = 50;
        int inputBoxX = (screenWidth - inputBoxWidth) / 2;
        int inputBoxY = (screenHeight - inputBoxHeight) / 2;

        BeginDrawing();
        ClearBackground(RAYWHITE);
        
        // 绘制标题
        DrawText("Enter Your Name:", (screenWidth - textWidth) / 2, inputBoxY - 60, 24, DARKGRAY);
        
        // 绘制输入框
        DrawRectangleLines(inputBoxX, inputBoxY, inputBoxWidth, inputBoxHeight, BLACK);
        
        // 绘制已输入的文本
        DrawText(name.c_str(), inputBoxX + 10, inputBoxY + 15, 24, BLACK);
        
        // 绘制提示文字
        DrawText("Press ENTER to continue", (screenWidth - MeasureText("Press ENTER to continue", 20)) / 2, inputBoxY + 70, 20, GRAY);
        DrawText("(ESC or close window to exit)", (screenWidth - MeasureText("(ESC or close window to exit)", 18)) / 2, inputBoxY + 100, 18, LIGHTGRAY);
        
        EndDrawing();
    }

    // 用户在输入名称前关闭窗口，直接退出
    if (WindowShouldClose())
    {
        client.endMatch();
        node.stop();
        CloseWindow();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return 0;
    }

    // 设置玩家名称到网络节点
    if (!name.empty())
        node.setNodeName(name);

    // ========== 主游戏循环 ==========
    
    while (true)
    {
        // 设置匹配大厅窗口
        SetWindowSize(1280, 720);
        SetWindowTitle("3D Chess Online - Match Lobby");

        // 显示匹配大厅，等待玩家匹配
        bool matched = SeekPeer(client, node);

        // 如果未匹配成功或用户退出，结束循环
        if (!matched || WindowShouldClose())
        {
            break;
        }

        // 如果匹配成功，开始游戏
        if (client.isInMatch())
        {
            // 运行游戏主循环
            int gameResult = RunGame(&client);
            
            // 游戏结束后，断开匹配
            client.endMatch();

            // 如果用户关闭窗口，退出程序
            if (gameResult == -1 || WindowShouldClose())
            {
                break;
            }

            // 游戏正常结束，返回匹配大厅继续下一轮
            SetWindowSize(1280, 720);
            SetWindowTitle("3D Chess Online - Match Lobby");
        }
    }

    // ========== 清理资源 ==========
    
    // 确保匹配已断开
    if (client.isInMatch()) 
        client.endMatch();
    
    // 停止网络节点
    node.stop();
    
    // 等待网络清理完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 如果窗口还未关闭，关闭它
    if (!WindowShouldClose())
        CloseWindow();

    return 0;
}

/**
 * @brief Windows程序入口点
 * 
 * 在Windows平台上，使用WinMain作为程序入口
 * 此函数简单地调用main函数
 */
#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    return main();
}
#endif