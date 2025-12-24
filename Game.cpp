#include "Game.h"
#include "LanP2PNode.h"
#include "Button.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

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

int RunGame()
{
    const int screenWidth = 1980;
    const int screenHeight = 1280;

    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Sample");

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

    while (!WindowShouldClose())
    {
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
                                ColorBoard[i][j][k] = (gameStep % 2 + 1);
                                ++gameStep;
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
                    ColorBoard[highlighted.i][highlighted.j][highlighted.k] = (gameStep % 2 + 1);
                    ++gameStep;
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
        DrawCircle(1900, 60, 30, typeColor[(gameStep % 2 + 1)]);
        DrawText(std::to_string(gameStep).c_str(), 1850, 1230, 50, WHITE);
        if ((gameStep % 2 + 1) == 1)
            DrawText("BLUE's Turn", 1650, 60, 20, WHITE);
        if ((gameStep % 2 + 1) == 2)
            DrawText("RED's Turn", 1650, 60, 20, WHITE);
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

    CloseWindow();

    return 0;
}
