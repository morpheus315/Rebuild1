#include "Game.h"
#include "LanP2PNode.h"
#include "Button.h"
#include "GameClient.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

bool SeekPeer(Client &client, lanp2p::LanP2PNode &node)
{
    Button discoverBtn(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "Discover (10s)");
    Button requestBtn(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "Request Match");
    Button acceptBtn(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "Accept");
    Button rejectBtn(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "Reject");
    Button exitBtn(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "Exit");

    int selectedPeer = -1;
    int selectedPending = -1;
    std::string status = "Idle";

    bool discoveryActive = false;
    double discoveryEndTime = 0.0;

    while (!WindowShouldClose())
    {
        const int w = GetScreenWidth();
        const int h = GetScreenHeight();
        const float margin = std::max(12.0f, w * 0.02f);
        const float spacing = std::max(6.0f, w * 0.01f);
        const float topY = margin;
        float btnHeight = std::max(32.0f, h * 0.055f);
        float btnWidth = (w - margin * 2.0f - spacing * 4.0f) / 5.0f;
        btnWidth = std::max(120.0f, btnWidth);
        const int btnFontSize = static_cast<int>(std::max(14.0f, btnHeight * 0.45f));

        discoverBtn.SetBounds(Rectangle{margin, topY, btnWidth, btnHeight});
        requestBtn.SetBounds(Rectangle{margin + (btnWidth + spacing) * 1.0f, topY, btnWidth, btnHeight});
        acceptBtn.SetBounds(Rectangle{margin + (btnWidth + spacing) * 2.0f, topY, btnWidth, btnHeight});
        rejectBtn.SetBounds(Rectangle{margin + (btnWidth + spacing) * 3.0f, topY, btnWidth, btnHeight});
        exitBtn.SetBounds(Rectangle{margin + (btnWidth + spacing) * 4.0f, topY, btnWidth, btnHeight});
        discoverBtn.SetFontSize(btnFontSize);
        requestBtn.SetFontSize(btnFontSize);
        acceptBtn.SetFontSize(btnFontSize);
        rejectBtn.SetFontSize(btnFontSize);
        exitBtn.SetFontSize(btnFontSize);

        if (client.isInMatch())
            break;

        if (discoveryActive && GetTime() >= discoveryEndTime)
        {
            node.stopUdpListen();
            discoveryActive = false;
            status = "Discovery stopped";
        }

        const Vector2 mouse = GetMousePosition();
        auto peers = client.getAvailablePeers();
        auto pending = client.getPendingRequestsSnapshot();
        if (selectedPeer >= static_cast<int>(peers.size()))
            selectedPeer = -1;
        if (selectedPending >= static_cast<int>(pending.size()))
            selectedPending = -1;

        const int headerFontSize = static_cast<int>(std::max(18.0f, h * 0.028f));
        const int bodyFontSize = static_cast<int>(std::max(16.0f, h * 0.024f));
        const float listWidth = w - margin * 2.0f;
        const float itemHeight = std::max(28.0f, h * 0.045f);
        const float itemSpacing = std::max(4.0f, h * 0.007f);

        BeginDrawing();
        ClearBackground(RAYWHITE);

        const float headerY = topY + btnHeight + margin;
        DrawText("Match Panel", static_cast<int>(margin), static_cast<int>(headerY), headerFontSize, DARKGRAY);
        DrawText(TextFormat("Local ID:%s  TCP:%d  Discovery:%d", node.getNodeId().c_str(), node.getTcpPort(), node.getDiscoveryPort()),
                 static_cast<int>(margin), h - static_cast<int>(margin * 1.8f), bodyFontSize, DARKGRAY);
        DrawText(status.c_str(), static_cast<int>(margin), h - static_cast<int>(margin * 0.9f), bodyFontSize, BLACK);

        if (discoverBtn.Draw())
        {
            if (!discoveryActive)
            {
                node.startUdpListen();
                discoveryActive = true;
                discoveryEndTime = GetTime() + 10.0;
                status = "Discovering (10s)";
            }
        }

        if (requestBtn.Draw())
        {
            if (selectedPeer >= 0 && selectedPeer < static_cast<int>(peers.size()))
                status = client.requestMatch(peers[static_cast<size_t>(selectedPeer)]) ? "Match request sent" : "Match request failed";
            else
                status = "Select a peer";
        }

        if (acceptBtn.Draw())
        {
            if (selectedPending >= 0 && selectedPending < static_cast<int>(pending.size()))
                status = client.respondToPendingRequest(pending[static_cast<size_t>(selectedPending)], true) ? "Request accepted" : "Request expired";
            else
                status = "No request selected";
        }

        if (rejectBtn.Draw())
        {
            if (selectedPending >= 0 && selectedPending < static_cast<int>(pending.size()))
                status = client.respondToPendingRequest(pending[static_cast<size_t>(selectedPending)], false) ? "Request rejected" : "Request expired";
            else
                status = "No request selected";
        }

        if (exitBtn.Draw())
        {
            if (discoveryActive)
            {
                node.stopUdpListen();
                discoveryActive = false;
            }
            return false;
        }

        float peerLabelY = headerY + headerFontSize + margin * 0.5f;
        DrawText("Available peers", static_cast<int>(margin), static_cast<int>(peerLabelY), bodyFontSize, BLACK);
        float peerY = peerLabelY + bodyFontSize + itemSpacing;
        for (size_t i = 0; i < peers.size(); ++i)
        {
            Rectangle item{margin, peerY, listWidth, itemHeight};
            bool hover = CheckCollisionPointRec(mouse, item);
            Color fill = (selectedPeer == static_cast<int>(i)) ? Fade(GREEN, 0.35f) : Fade(LIGHTGRAY, 0.35f);
            if (hover)
                fill = Fade(ORANGE, 0.35f);
            DrawRectangleRec(item, fill);
            DrawRectangleLinesEx(item, 1.0f, DARKGRAY);
            std::string label = std::to_string(i + 1) + ". " + (peers[i].name.empty() ? peers[i].id : peers[i].name) +
                                 " (" + peers[i].ip + ":" + std::to_string(peers[i].tcpPort) + ")";
            DrawText(label.c_str(), static_cast<int>(item.x + itemHeight * 0.25f), static_cast<int>(item.y + itemHeight * 0.2f), bodyFontSize, BLACK);
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                selectedPeer = static_cast<int>(i);
            peerY += itemHeight + itemSpacing;
        }

        float pendingStart = peerY + margin;
        if (pendingStart < peerLabelY + 180.0f * (h / 720.0f))
            pendingStart = peerLabelY + 180.0f * (h / 720.0f);
        DrawText("Pending requests", static_cast<int>(margin), static_cast<int>(pendingStart), bodyFontSize, BLACK);
        float pendingY = pendingStart + bodyFontSize + itemSpacing;
        for (size_t i = 0; i < pending.size(); ++i)
        {
            Rectangle item{margin, pendingY, listWidth, itemHeight};
            bool hover = CheckCollisionPointRec(mouse, item);
            Color fill = (selectedPending == static_cast<int>(i)) ? Fade(SKYBLUE, 0.35f) : Fade(LIGHTGRAY, 0.35f);
            if (hover)
                fill = Fade(ORANGE, 0.35f);
            DrawRectangleRec(item, fill);
            DrawRectangleLinesEx(item, 1.0f, DARKGRAY);
            std::string label = std::to_string(i + 1) + ". " + (pending[i].peer.name.empty() ? pending[i].peer.id : pending[i].peer.name) +
                                 " (" + pending[i].ip + ":" + std::to_string(pending[i].port) + ") id=" + pending[i].matchId.substr(0, 6);
            DrawText(label.c_str(), static_cast<int>(item.x + itemHeight * 0.25f), static_cast<int>(item.y + itemHeight * 0.2f), bodyFontSize, BLACK);
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                selectedPending = static_cast<int>(i);
            pendingY += itemHeight + itemSpacing;
        }

        EndDrawing();
    }

    if (discoveryActive)
    {
        node.stopUdpListen();
        discoveryActive = false;
    }

    return client.isInMatch();
}

namespace
{
constexpr int BoardSize = 9;

struct SphereInstance
{
    Vector3 position;
    Color color;
    float distanceToCamera;
    int i;
    int j;
    int k;
};

float d2r(float degree)
{
    return degree * 3.1415926f / 180.0f;
}
} // namespace

int RunGame(Client *client)
{
    constexpr int defaultScreenWidth = 1980;
    constexpr int defaultScreenHeight = 1280;

    SetWindowSize(defaultScreenWidth, defaultScreenHeight);
    SetWindowTitle("3D Chess Online - Game");
    SetTargetFPS(30);

    Camera3D camera = {0};
    camera.position = Vector3{400.0f, 0.0f, 0.0f};
    camera.target = Vector3{0.0f, 0.0f, 0.0f};
    camera.up = Vector3{0.0f, 1.0f, 0.0f};
    camera.fovy = 70.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(30);

    float AngleTheta = 45.0f;
    float AnglePhi = 45.0f;
    float SphereDist = 10.0f;
    float cameraDist = 400.0f;
    float SphereRadius = 3.0f;
    float Axis_length = 200.0f;
    const float dragSensitivity = 0.3f;

    Button toggleButton(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "Hide all spheres");
    Button xButton(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "X axis");
    Button yButton(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "Y axis");
    Button zButton(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "Z axis");
    Button dButton(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "3D");
    Button HighlightButtonx(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "Y-Z Highlight");
    Button HighlightButtony(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "X-Z Highlight");
    Button HighlightButtonz(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "X-Y Highlight");

    std::vector<Button> NumberButton;
    for (int i = 0; i <= 9; ++i)
    {
        NumberButton.push_back(Button(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, std::to_string(i)));
    }
    NumberButton[0].SetText("Reset");

    std::vector<SphereInstance> spheres;
    spheres.reserve(BoardSize * BoardSize * BoardSize);
    bool showSpheres = true;
    int vmode = 0;
    int hlmode = 0;
    int number = 0;
    int gameStep = 1;

    Color typeColor[4] = {GRAY, BLUE, RED, YELLOW};
    typeColor[0].a = 65;

    camera.position.x = cameraDist * cosf(d2r(AnglePhi)) * sinf(d2r(AngleTheta));
    camera.position.z = cameraDist * cosf(d2r(AnglePhi)) * cosf(d2r(AngleTheta));
    camera.position.y = cameraDist * sinf(d2r(AnglePhi));

    std::string BottomText = "3D View   ";
    std::string AddText;

    short ColorBoard[BoardSize + 2][BoardSize + 2][BoardSize + 2];
    memset(ColorBoard, 0, sizeof(ColorBoard));

    if (client)
    {
        client->initGameState();
    }

    double gameOverTime = 0.0;
    bool gameEnded = false;

    while (!WindowShouldClose())
    {
        const int screenWidth = GetScreenWidth();
        const int screenHeight = GetScreenHeight();

        const float margin = std::max(16.0f, screenWidth * 0.02f);
        const float baseBtnHeight = std::max(32.0f, screenHeight * 0.04f);
        const float baseSpacing = std::max(6.0f, screenHeight * 0.01f);
        const int mainBtnCount = 8;
        const int numberBtnCount = static_cast<int>(NumberButton.size());
        const float sectionGap = baseSpacing;
        float baseTotalHeight = mainBtnCount * baseBtnHeight + numberBtnCount * baseBtnHeight +
                                ((mainBtnCount - 1) + (numberBtnCount - 1)) * baseSpacing + sectionGap;
        float usableHeight = static_cast<float>(screenHeight) - 2.0f * margin;
        float scale = std::min(1.0f, usableHeight / baseTotalHeight);
        bool showNumberButtons = true;
        if (scale < 0.6f)
        {
            showNumberButtons = false;
            baseTotalHeight = mainBtnCount * baseBtnHeight + (mainBtnCount - 1) * baseSpacing;
            scale = std::max(usableHeight / baseTotalHeight, 0.6f);
        }
        const float btnHeight = baseBtnHeight * scale;
        const float btnSpacing = baseSpacing * scale;
        const float btnWidth = std::max(140.0f, screenWidth * 0.12f);
        const int btnFontSize = static_cast<int>(std::max(12.0f, btnHeight * 0.45f));
        float nextY = margin;

        auto setButton = [&](Button &btn)
        {
            btn.SetBounds(Rectangle{margin, nextY, btnWidth, btnHeight});
            btn.SetFontSize(btnFontSize);
            nextY += btnHeight + btnSpacing;
        };

        setButton(toggleButton);
        setButton(xButton);
        setButton(yButton);
        setButton(zButton);
        setButton(dButton);
        setButton(HighlightButtonx);
        setButton(HighlightButtony);
        setButton(HighlightButtonz);
        nextY += sectionGap;

        if (showNumberButtons)
        {
            for (int i = 0; i <= 9; ++i)
            {
                NumberButton[static_cast<size_t>(i)].SetBounds(Rectangle{margin, nextY, btnWidth, btnHeight});
                NumberButton[static_cast<size_t>(i)].SetFontSize(btnFontSize);
                nextY += btnHeight + btnSpacing;
            }
        }

        if (client && !client->isGameRunning() && !gameEnded)
        {
            gameEnded = true;
            gameOverTime = GetTime();
        }

        if (gameEnded)
        {
            if (GetTime() - gameOverTime > 3.0)
            {
                break;
            }

            BeginDrawing();
            ClearBackground(BLACK);

            int result = client->getGameResult();
            const int resultFont = static_cast<int>(std::clamp(screenHeight * 0.07f, 32.0f, 72.0f));
            const int infoFont = static_cast<int>(std::clamp(screenHeight * 0.03f, 18.0f, 32.0f));
            const char *resultText = "GAME INTERRUPTED";
            Color resultColor = ORANGE;
            if (result == 1)
            {
                resultText = "YOU WIN!";
                resultColor = GREEN;
            }
            else if (result == 2)
            {
                resultText = "YOU LOSE!";
                resultColor = RED;
            }
            int resultWidth = MeasureText(resultText, resultFont);
            int resultX = screenWidth / 2 - resultWidth / 2;
            int resultY = screenHeight / 2 - resultFont;
            DrawText(resultText, resultX, resultY, resultFont, resultColor);

            const char *infoText = "Returning to lobby in 3 seconds...";
            int infoWidth = MeasureText(infoText, infoFont);
            DrawText(infoText, screenWidth / 2 - infoWidth / 2, resultY + resultFont + infoFont, infoFont, WHITE);
            EndDrawing();
            continue;
        }

        if (client && !client->isMyTurn() && client->isGameRunning())
        {
            int x, y, z;
            if (client->tryGetOpponentMove(x, y, z))
            {
                int opponentColor = (client->getMyPlayer() == '1') ? 2 : 1;
                ColorBoard[x][y][z] = opponentColor;
                gameStep++;
            }
        }
        AddText.clear();
        spheres.clear();
        Axis_length = SphereDist * (BoardSize + 5.0f) / 2;

        if (IsKeyDown(KEY_O))
        {
            if (SphereRadius > 1.0f)
            {
                SphereRadius -= 0.2f;
            }
        }
        if (IsKeyDown(KEY_P))
        {
            if (SphereRadius < SphereDist / 2)
            {
                SphereRadius += 0.2f;
            }
        }
        if (vmode == 0)
        {
            if (IsKeyDown(KEY_UP))
            {
                AnglePhi += 2.0f;
                if (AnglePhi > 89.0f)
                {
                    AnglePhi = 89.0f;
                }
            }
            if (IsKeyDown(KEY_DOWN))
            {
                AnglePhi -= 2.0f;
                if (AnglePhi < -89.0f)
                {
                    AnglePhi = -89.0f;
                }
            }
            if (IsKeyDown(KEY_LEFT))
            {
                AngleTheta -= 3.0f;
                if (AngleTheta < 0.0f)
                {
                    AngleTheta += 360.0f;
                }
            }
            if (IsKeyDown(KEY_RIGHT))
            {
                AngleTheta += 3.0f;
                if (AngleTheta > 360.0f)
                {
                    AngleTheta -= 360.0f;
                }
            }

            if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
            {
                Vector2 md = GetMouseDelta();
                AngleTheta -= md.x * dragSensitivity;
                AnglePhi += md.y * dragSensitivity;
                if (AnglePhi > 89.0f) AnglePhi = 89.0f;
                if (AnglePhi < -89.0f) AnglePhi = -89.0f;
                if (AngleTheta < 0.0f) AngleTheta += 360.0f;
                if (AngleTheta >= 360.0f) AngleTheta -= 360.0f;
            }
            float deltasize = GetMouseWheelMove();
            SphereDist += deltasize;
            if (SphereDist < 2 * SphereRadius) SphereDist = 2 * SphereRadius;
        }

        camera.position.x = cameraDist * cosf(d2r(AnglePhi)) * sinf(d2r(AngleTheta));
        camera.position.z = cameraDist * cosf(d2r(AnglePhi)) * cosf(d2r(AngleTheta));
        camera.position.y = cameraDist * sinf(d2r(AnglePhi));

        BeginDrawing();

        ClearBackground(BLACK);
        BeginMode3D(camera);
        DrawLine3D(Vector3{0, Axis_length, 0}, Vector3{0, 0, 0}, GREEN);
        DrawLine3D(Vector3{Axis_length, 0, 0}, Vector3{0, 0, 0}, RED);
        DrawLine3D(Vector3{0, 0, Axis_length}, Vector3{0, 0, 0}, BLUE);

        if (showSpheres)
        {
            const bool highlightPlaneActive = (vmode == 0 && hlmode != 0 && number != 0);
            const Vector2 mousePos = GetMousePosition();
            float closestGreyDistSq = std::numeric_limits<float>::max();
            std::size_t closestGreyIndex = std::numeric_limits<std::size_t>::max();
            const float posDelta = SphereDist * (BoardSize + 1.0f) / 2.0f;
            for (int i = 1; i <= BoardSize; ++i)
                for (int j = 1; j <= BoardSize; ++j)
                    for (int k = 1; k <= BoardSize; ++k)
                    {
                        Color color = typeColor[ColorBoard[i][j][k]];
                        color.a = 230;
                        bool isunhighlighted = (hlmode == 1 && number != i) || (hlmode == 2 && number != j) || (hlmode == 3 && number != k);
                        if (vmode == 0 && number != 0 && isunhighlighted)
                        {
                            if (ColorBoard[i][j][k] == 0)
                                color.a = 40;
                            else
                                color.a = 150;
                        }
                        if (vmode != 0 && number != 0)
                        {
                            if (vmode == 1 && number != i)
                                color.a = 0;
                            if (vmode == 2 && number != j)
                                color.a = 0;
                            if (vmode == 3 && number != k)
                                color.a = 0;
                        }
                        Vector3 worldPos = Vector3{SphereDist * i - posDelta, SphereDist * j - posDelta, SphereDist * k - posDelta};
                        Vector2 screenPos = GetWorldToScreen(worldPos, camera);

                        if (number != 0 && vmode != 0 && color.a > 60 && CheckCollisionPointCircle(mousePos, screenPos, 45.0f))
                        {
                            color.a = 100;
                            AddText = " --Mouse On (" + std::to_string(i) + "," + std::to_string(j) + "," + std::to_string(k) + ")";
                            if (ColorBoard[i][j][k] == 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                            {
                                if (client)
                                {
                                    if (client->isMyTurn() && client->tryPlaceMyPiece(i, j, k))
                                    {
                                        int myColor = (client->getMyPlayer() == '1') ? 1 : 2;
                                        ColorBoard[i][j][k] = myColor;
                                        gameStep++;
                                    }
                                }
                                else
                                {
                                    ColorBoard[i][j][k] = (gameStep % 2 + 1);
                                    ++gameStep;
                                }
                            }
                        }

                        if (color.a == 0)
                            continue;

                        const std::size_t sphereIndex = spheres.size();
                        spheres.push_back({worldPos, color, Vector3Distance(camera.position, worldPos), i, j, k});

                        if (highlightPlaneActive && ColorBoard[i][j][k] == 0)
                        {
                            const bool isOnHighlightPlane =
                                (hlmode == 1 && number == i) ||
                                (hlmode == 2 && number == j) ||
                                (hlmode == 3 && number == k);
                            if (isOnHighlightPlane)
                            {
                                float distSq = Vector2DistanceSqr(mousePos, screenPos);
                                if (distSq < closestGreyDistSq)
                                {
                                    closestGreyDistSq = distSq;
                                    closestGreyIndex = sphereIndex;
                                }
                            }
                        }
                    }

            if (highlightPlaneActive && closestGreyIndex != std::numeric_limits<std::size_t>::max() && closestGreyDistSq < 800)
            {
                SphereInstance &highlighted = spheres[closestGreyIndex];
                Color highlightColor = YELLOW;
                highlightColor.a = highlighted.color.a;
                highlighted.color = highlightColor;

                AddText = " --Hover (" + std::to_string(highlighted.i) + "," + std::to_string(highlighted.j) + "," + std::to_string(highlighted.k) + ")";
                if (ColorBoard[highlighted.i][highlighted.j][highlighted.k] == 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                {
                    if (client)
                    {
                        if (client->isMyTurn() && client->tryPlaceMyPiece(highlighted.i, highlighted.j, highlighted.k))
                        {
                            int myColor = (client->getMyPlayer() == '1') ? 1 : 2;
                            ColorBoard[highlighted.i][highlighted.j][highlighted.k] = myColor;
                            gameStep++;
                        }
                    }
                    else
                    {
                        ColorBoard[highlighted.i][highlighted.j][highlighted.k] = (gameStep % 2 + 1);
                        ++gameStep;
                    }
                }
            }

            if (!spheres.empty())
            {
                std::sort(spheres.begin(), spheres.end(), [](const SphereInstance &a, const SphereInstance &b)
                          { return a.distanceToCamera > b.distanceToCamera; });

                rlDisableDepthMask();
                for (const auto &sphere : spheres)
                    DrawSphere(sphere.position, SphereRadius, sphere.color);
                rlEnableDepthMask();
            }
        }
        EndMode3D();
        int currentPlayer = ((gameStep - 1) % 2) + 1;

        Vector2 xPos = GetWorldToScreen(Vector3{Axis_length, 0.0f, 0.0f}, camera);
        Vector2 yPos = GetWorldToScreen(Vector3{0.0f, Axis_length, 0.0f}, camera);
        Vector2 zPos = GetWorldToScreen(Vector3{0.0f, 0.0f, Axis_length}, camera);
        const int axisFont = static_cast<int>(std::clamp(screenHeight * 0.02f, 14.0f, 22.0f));
        DrawText("x", static_cast<int>(xPos.x), static_cast<int>(xPos.y), axisFont, RED);
        DrawText("y", static_cast<int>(yPos.x), static_cast<int>(yPos.y), axisFont, GREEN);
        DrawText("z", static_cast<int>(zPos.x), static_cast<int>(zPos.y), axisFont, BLUE);

        const float hudMargin = std::max(20.0f, screenWidth * 0.02f);
        const float turnRadius = std::clamp(screenHeight * 0.03f, 20.0f, 36.0f);
        const int turnFont = static_cast<int>(std::clamp(screenHeight * 0.025f, 16.0f, 28.0f));
        const int stepFont = static_cast<int>(std::clamp(screenHeight * 0.04f, 30.0f, 52.0f));
        const int bottomFont = static_cast<int>(std::clamp(screenHeight * 0.032f, 18.0f, 40.0f));

        Vector2 turnCircleCenter{screenWidth - hudMargin - turnRadius, hudMargin + turnRadius};
        DrawCircleV(turnCircleCenter, turnRadius, typeColor[currentPlayer]);

        float turnTextX = std::max(hudMargin, turnCircleCenter.x - std::max(220.0f, screenWidth * 0.18f));
        float turnTextY = hudMargin;
        if (client)
        {
            if (client->isMyTurn())
                DrawText("Your Turn", static_cast<int>(turnTextX), static_cast<int>(turnTextY), turnFont, GREEN);
            else
                DrawText("Opponent's Turn", static_cast<int>(turnTextX), static_cast<int>(turnTextY), turnFont, ORANGE);
        }
        else
        {
            if (currentPlayer == 1)
                DrawText("BLUE's Turn", static_cast<int>(turnTextX), static_cast<int>(turnTextY), turnFont, WHITE);
            else
                DrawText("RED's Turn", static_cast<int>(turnTextX), static_cast<int>(turnTextY), turnFont, WHITE);
        }

        float stepX = screenWidth - hudMargin - stepFont * 1.5f;
        float stepY = screenHeight - hudMargin - stepFont * 1.5f;
        DrawText(std::to_string(gameStep).c_str(), static_cast<int>(stepX), static_cast<int>(stepY), stepFont, WHITE);

        if (vmode == 0)
        {
            BottomText = "3D View   ";
            if (hlmode != 0)
            {
                BottomText += " - Highlighting ";
                if (hlmode == 1)
                    BottomText += "X";
                if (hlmode == 2)
                    BottomText += "Y";
                if (hlmode == 3)
                    BottomText += "Z";
            }

            if (number != 0)
                BottomText += "=" + std::to_string(number) + " Plane";
        }
        else
        {
            if (vmode == 1)
                BottomText = "X Axis View   ";
            if (vmode == 2)
                BottomText = "Y Axis View   ";
            if (vmode == 3)
                BottomText = "Z Axis View   ";
            if (number != 0)
            {
                if (vmode == 1)
                    BottomText += " X=" + std::to_string(number) + " Plane";
                if (vmode == 2)
                    BottomText += " Y=" + std::to_string(number) + " Plane";
                if (vmode == 3)
                    BottomText += " Z=" + std::to_string(number) + " Plane";
            }
        }

        float bottomY = screenHeight - hudMargin - bottomFont - 4.0f;
        if (showSpheres)
            DrawText((BottomText + AddText).c_str(), static_cast<int>(hudMargin), static_cast<int>(bottomY), bottomFont, WHITE);
        else
            DrawText((BottomText + "(Hidden)").c_str(), static_cast<int>(hudMargin), static_cast<int>(bottomY), bottomFont, WHITE);

        if (toggleButton.Draw())
        {
            showSpheres = !showSpheres;
        }
        if (dButton.Draw())
        {
            hlmode = vmode;
            vmode = 0;
            camera.projection = CAMERA_PERSPECTIVE;
            SphereRadius = 3.0f;
            SphereDist = 10.0f;
            AngleTheta = 45.0f;
            AnglePhi = 45.0f;
            cameraDist = 400.0f;
        }
        if (xButton.Draw())
        {
            if (hlmode != 1)
                number = 0;
            hlmode = 0;
            vmode = 1;
            camera.projection = CAMERA_ORTHOGRAPHIC;
            SphereRadius = 2.5f;
            SphereDist = 5.0f;
            AngleTheta = 90.0f;
            AnglePhi = 0.0f;
            cameraDist = 400.0f;
        }
        if (yButton.Draw())
        {
            if (hlmode != 2)
                number = 0;
            hlmode = 0;
            vmode = 2;

            camera.projection = CAMERA_ORTHOGRAPHIC;
            SphereRadius = 2.5f;
            SphereDist = 5.0f;
            AngleTheta = 0.0f;
            AnglePhi = 90.0f;
            cameraDist = 400.0f;
        }
        if (zButton.Draw())
        {
            if (hlmode != 3)
                number = 0;
            hlmode = 0;
            vmode = 3;
            camera.projection = CAMERA_ORTHOGRAPHIC;
            SphereRadius = 2.5f;
            SphereDist = 5.0f;
            AngleTheta = 0.0f;
            AnglePhi = 0.0f;
            cameraDist = 400.0f;
        }
        if (vmode == 0)
        {

            if (HighlightButtonx.Draw())
            {
                number = 0;
                hlmode = 1;
            }
            if (HighlightButtony.Draw())
            {
                number = 0;
                hlmode = 2;
            }
            if (HighlightButtonz.Draw())
            {
                number = 0;
                hlmode = 3;
            }
        }
        if ((hlmode != 0 || vmode != 0) && showNumberButtons)
        {
            if (NumberButton[0].Draw())
            {
                hlmode = 0;
                number = 0;
            }
            for (int i = 1; i <= BoardSize; ++i)
                if (NumberButton[static_cast<size_t>(i)].Draw())
                    number = i;
        }

        EndDrawing();
    }

    bool userClosedWindow = WindowShouldClose();

    return userClosedWindow ? -1 : 0;
}
