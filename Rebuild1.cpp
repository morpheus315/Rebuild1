#include "Game.h"
#include "LanP2PNode.h"
#include "GameClient.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <atomic>

#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#undef NOGDI
#undef NOUSER

#include <raylib.h>

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

// 全局指针，用于控制台关闭事件处理
static Client* g_client = nullptr;
static lanp2p::LanP2PNode* g_node = nullptr;

// 控制台关闭事件处理
static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
	if (ctrlType == CTRL_CLOSE_EVENT || ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT)
	{
		// 在进程终止前发送中断消息
		if (g_client && g_client->isInMatch())
		{
			g_client->endMatch();
		}
		if (g_node)
		{
			g_node->stop();
		}
		return TRUE;
	}
	return FALSE;
}

int main()
{
    using namespace lanp2p;

#if 0
    // Debug: if you need a console while using Windows subsystem, enable this block.
    // AllocConsole();
    // FILE* fpout = nullptr;
    // FILE* fperr = nullptr;
    // freopen_s(&fpout, "CONOUT$", "w", stdout);
    // freopen_s(&fperr, "CONOUT$", "w", stderr);
    // std::ios::sync_with_stdio();
#endif

    LanP2PNode node(37000, 0);
    node.setPeerStaleMs(15000);
    node.startBroadcastOnly();

    Client client(node);

    g_node = &node;
    g_client = &client;
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // ========== 创建唯一的窗口 ==========
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(920, 720, "3D Chess Online");
    SetWindowMinSize(640, 480);
    SetTargetFPS(60);

    // ========== 第一步：输入名字 ==========
    SetWindowSize(400, 200);
    SetWindowTitle("Enter Your Name");

    std::string name;
    bool nameEntered = false;

    while (!WindowShouldClose() && !nameEntered)
    {
        int key = GetCharPressed();
        while (key > 0)
        {
            if ((key >= 32) && (key <= 125) && (name.length() < 20))
            {
                name += (char)key;
            }
            key = GetCharPressed();
        }

        if (IsKeyPressed(KEY_BACKSPACE) && name.length() > 0)
        {
            name.pop_back();
        }

        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
        {
            if (name.empty())
            {
                name = "Guest";
            }
            nameEntered = true;
        }

        int screenWidth = GetScreenWidth();
        int screenHeight = GetScreenHeight();

        int textWidth = MeasureText("Enter Your Name:", 24);
        int inputBoxWidth = 300;
        int inputBoxHeight = 50;
        int inputBoxX = (screenWidth - inputBoxWidth) / 2;
        int inputBoxY = (screenHeight - inputBoxHeight) / 2;

        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("Enter Your Name:", (screenWidth - textWidth) / 2, inputBoxY - 60, 24, DARKGRAY);
        DrawRectangleLines(inputBoxX, inputBoxY, inputBoxWidth, inputBoxHeight, BLACK);
        DrawText(name.c_str(), inputBoxX + 10, inputBoxY + 15, 24, BLACK);
        DrawText("Press ENTER to continue", (screenWidth - MeasureText("Press ENTER to continue", 20)) / 2, inputBoxY + 70, 20, GRAY);
        DrawText("(ESC or close window to exit)", (screenWidth - MeasureText("(ESC or close window to exit)", 18)) / 2, inputBoxY + 100, 18, LIGHTGRAY);
        DrawText(TextFormat("Chars: %d", (int)name.length()), inputBoxX + inputBoxWidth - 60, inputBoxY - 25, 16, RED);
        EndDrawing();
    }

    if (WindowShouldClose())
    {
        // user requested close from GUI, clean up and exit
        client.endMatch();
        node.stop();
        CloseWindow();
        // give threads a moment to stop
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return 0;
    }

    if (!name.empty())
        node.setNodeName(name);

    // ========== 第二步：主循环（匹配 -> 游戏） ==========
    while (true)
    {
        SetWindowSize(920, 720);
        SetWindowTitle("3D Chess Online - Match Lobby");

        bool matched = SeekPeer(client, node);

        if (!matched || WindowShouldClose())
        {
            break;
        }

        if (client.isInMatch())
        {
            int gameResult = RunGame(&client);
            client.endMatch();

            if (gameResult == -1 || WindowShouldClose())
            {
                break;
            }

            // ensure window still exists and restore lobby size/title
            SetWindowSize(920, 720);
            SetWindowTitle("3D Chess Online - Match Lobby");
        }
    }

    // final cleanup
    if (client.isInMatch()) client.endMatch();
    node.stop();
    // small sleep to let background threads exit cleanly
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    if (!WindowShouldClose())
        CloseWindow();

    return 0;
}

// Add WinMain wrapper so linker can find entry point when building with /SUBSYSTEM:WINDOWS
#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    // Call regular main
    return main();
}
#endif