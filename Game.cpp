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
        const int screenWidth = GetScreenWidth();
        const int screenHeight = GetScreenHeight();
        const float margin = std::max(16.0f, screenWidth * 0.02f);
        const float spacing = std::max(8.0f, screenWidth * 0.01f);
        float btnHeight = std::max(44.0f, screenHeight * 0.065f);
        float btnWidth = (screenWidth - margin * 2.0f - spacing * 4.0f) / 5.0f;
        btnWidth = std::max(140.0f, btnWidth);
        const int btnFontSize = static_cast<int>(std::max(18.0f, btnHeight * 0.5f));
        const float topY = margin;

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

        BeginDrawing();
        ClearBackground(RAYWHITE);

        const float headerY = topY + btnHeight + margin;
        DrawText("Match Panel", static_cast<int>(margin), static_cast<int>(headerY), 28, DARKGRAY);
        DrawText(TextFormat("Local ID:%s  TCP:%d  Discovery:%d", node.getNodeId().c_str(), node.getTcpPort(), node.getDiscoveryPort()),
                 static_cast<int>(margin), screenHeight - 70, 22, DARKGRAY);
        DrawText(status.c_str(), static_cast<int>(margin), screenHeight - 40, 24, BLACK);

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

        DrawText("Available peers", static_cast<int>(margin), 120, 24, BLACK);
        float peerY = 152.0f;
        for (size_t i = 0; i < peers.size(); ++i)
        {
            Rectangle item{margin, peerY, static_cast<float>(screenWidth - margin * 2.0f), 40.0f};
            bool hover = CheckCollisionPointRec(mouse, item);
            Color fill = (selectedPeer == static_cast<int>(i)) ? Fade(GREEN, 0.35f) : Fade(LIGHTGRAY, 0.35f);
            if (hover)
                fill = Fade(ORANGE, 0.35f);
            DrawRectangleRec(item, fill);
            DrawRectangleLinesEx(item, 1.0f, DARKGRAY);
            std::string label = std::to_string(i + 1) + ". " + (peers[i].name.empty() ? peers[i].id : peers[i].name) +
                                 " (" + peers[i].ip + ":" + std::to_string(peers[i].tcpPort) + ")";
            DrawText(label.c_str(), static_cast<int>(item.x) + 8, static_cast<int>(item.y) + 8, 22, BLACK);
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                selectedPeer = static_cast<int>(i);
            peerY += 44.0f;
        }

        float pendingStart = peerY + 24.0f;
        if (pendingStart < 340.0f)
            pendingStart = 340.0f;
        DrawText("Pending requests", static_cast<int>(margin), static_cast<int>(pendingStart), 24, BLACK);
        float pendingY = pendingStart + 32.0f;
        for (size_t i = 0; i < pending.size(); ++i)
        {
            Rectangle item{margin, pendingY, static_cast<float>(screenWidth - margin * 2.0f), 40.0f};
            bool hover = CheckCollisionPointRec(mouse, item);
            Color fill = (selectedPending == static_cast<int>(i)) ? Fade(SKYBLUE, 0.35f) : Fade(LIGHTGRAY, 0.35f);
            if (hover)
                fill = Fade(ORANGE, 0.35f);
            DrawRectangleRec(item, fill);
            DrawRectangleLinesEx(item, 1.0f, DARKGRAY);
            std::string label = std::to_string(i + 1) + ". " + (pending[i].peer.name.empty() ? pending[i].peer.id : pending[i].peer.name) +
                                 " (" + pending[i].ip + ":" + std::to_string(pending[i].port) + ") id=" + pending[i].matchId.substr(0, 6);
            DrawText(label.c_str(), static_cast<int>(item.x) + 8, static_cast<int>(item.y) + 8, 22, BLACK);
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                selectedPending = static_cast<int>(i);
            pendingY += 44.0f;
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
    const int screenWidth = 1980;
    const int screenHeight = 1280;

    SetWindowSize(screenWidth, screenHeight);
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

    Button toggleButton(Rectangle{20.0f, 20.0f, 180.0f, 40.0f}, "Hide all spheres");
    Button xButton(Rectangle{20.0f, 70.0f, 180.0f, 40.0f}, "X axis");
    Button yButton(Rectangle{20.0f, 120.0f, 180.0f, 40.0f}, "Y axis");
    Button zButton(Rectangle{20.0f, 170.0f, 180.0f, 40.0f}, "Z axis");
    Button dButton(Rectangle{20.0f, 220.0f, 180.0f, 40.0f}, "3D");
    Button HighlightButtonx(Rectangle{20.0f, 270.0f, 180.0f, 40.0f}, "Y-Z Highlight");
    Button HighlightButtony(Rectangle{20.0f, 320.0f, 180.0f, 40.0f}, "X-Z Highlight");
    Button HighlightButtonz(Rectangle{20.0f, 370.0f, 180.0f, 40.0f}, "X-Y Highlight");

    std::vector<Button> NumberButton;
    for (int i = 0; i <= 9; ++i)
    {
        NumberButton.push_back(Button(Rectangle{20.0f, (520.0f + i * 50.0f), 180.0f, 40.0f}, std::to_string(i)));
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
            if (result == 1)
            {
                DrawText("YOU WIN!", screenWidth / 2 - 150, screenHeight / 2 - 50, 60, GREEN);
            }
            else if (result == 2)
            {
                DrawText("YOU LOSE!", screenWidth / 2 - 150, screenHeight / 2 - 50, 60, RED);
            }
            else
            {
                DrawText("GAME INTERRUPTED", screenWidth / 2 - 200, screenHeight / 2 - 50, 50, ORANGE);
            }
            
            DrawText("Returning to lobby in 3 seconds...", screenWidth / 2 - 250, screenWidth / 2 + 50, 24, WHITE);
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
                SphereInstance& highlighted = spheres[closestGreyIndex];
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
                std::sort(spheres.begin(), spheres.end(), [](const SphereInstance& a, const SphereInstance& b)
                          { return a.distanceToCamera > b.distanceToCamera; });

                rlDisableDepthMask();
                for (const auto& sphere : spheres)
                    DrawSphere(sphere.position, SphereRadius, sphere.color);
                rlEnableDepthMask();
            }
        }
        EndMode3D();
        int currentPlayer = ((gameStep - 1) % 2) + 1;
        DrawCircle(1900, 60, 30, typeColor[currentPlayer]);
        DrawText(std::to_string(gameStep).c_str(), 1850, 1230, 50, WHITE);
        
        if (client)
        {
            if (client->isMyTurn())
                DrawText("Your Turn", 1650, 60, 20, GREEN);
            else
                DrawText("Opponent's Turn", 1650, 60, 20, ORANGE);
            
            // 显示最近一次同步的时间（秒）
            auto timeSinceSync = client->getTimeSinceLastSync();
            std::string syncText = "Last Sync: " + std::to_string(timeSinceSync) + "s ago";
            Color syncColor = (timeSinceSync < 6) ? GREEN : (timeSinceSync < 10 ? YELLOW : RED);
            DrawText(syncText.c_str(), 1650, 90, 16, syncColor);
        }
        else
        {
            if (currentPlayer == 1)
                DrawText("BLUE's Turn", 1650, 60, 20, WHITE);
            else
                DrawText("RED's Turn", 1650, 60, 20, WHITE);
        }
        
        Vector2 xPos = GetWorldToScreen(Vector3{Axis_length, 0.0f, 0.0f}, camera);
        Vector2 yPos = GetWorldToScreen(Vector3{0.0f, Axis_length, 0.0f}, camera);
        Vector2 zPos = GetWorldToScreen(Vector3{0.0f, 0.0f, Axis_length}, camera);
        DrawText("x", (int)xPos.x, (int)xPos.y, 20, RED);
        DrawText("y", (int)yPos.x, (int)yPos.y, 20, GREEN);
        DrawText("z", (int)zPos.x, (int)zPos.y, 20, BLUE);
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
        if (showSpheres)
            DrawText((BottomText + AddText).c_str(), 40, 1230, 40, WHITE);
        else
            DrawText((BottomText + "(Hidden)").c_str(), 40, 1230, 40, WHITE);
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
        if (hlmode != 0 || vmode != 0)
        {
            if (NumberButton[0].Draw())
            {
                hlmode = 0;
                number = 0;
            }
            for (int i = 1; i <= BoardSize; ++i)
                if (NumberButton[i].Draw())
                    number = i;
        }

        EndDrawing();
    }

    bool userClosedWindow = WindowShouldClose();

    return userClosedWindow ? -1 : 0;
}
