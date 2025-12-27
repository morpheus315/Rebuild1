#include "Game.h"
#include "LanP2PNode.h"
#include "GameClient.h"
#include <string>
#include <thread>
#include <chrono>

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

int main()
{
    using namespace lanp2p;

    LanP2PNode node(37000, 0);
    node.setPeerStaleMs(15000);
    node.startBroadcastOnly();

    Client client(node);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(920, 720, "3D Chess Online");
    SetWindowMinSize(640, 480);
    SetTargetFPS(60);

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
        client.endMatch();
        node.stop();
        CloseWindow();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return 0;
    }

    if (!name.empty())
        node.setNodeName(name);

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

            SetWindowSize(920, 720);
            SetWindowTitle("3D Chess Online - Match Lobby");
        }
    }

    if (client.isInMatch()) client.endMatch();
    node.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    if (!WindowShouldClose())
        CloseWindow();

    return 0;
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    return main();
}
#endif