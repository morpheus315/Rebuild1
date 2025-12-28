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

/**
 * @brief 匹配对手界面
 * 
 * 显示可用的对等节点列表和待处理的匹配请求
 * 提供以下功能：
 * - 发现局域网内的其他玩家（10秒）
 * - 向选中的玩家发送匹配请求
 * - 接受或拒绝收到的匹配请求
 * - 退出到主菜单
 * 
 * @param client 游戏客户端引用
 * @param node 局域网P2P节点引用
 * @return true 匹配成功，false 用户退出
 */
bool SeekPeer(Client &client, lanp2p::LanP2PNode &node)
{
    // ========== 创建UI按钮 ==========
    Button discoverBtn(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "Discover (10s)");
    Button requestBtn(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "Request Match");
    Button acceptBtn(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "Accept");
    Button rejectBtn(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "Reject");
    Button exitBtn(Rectangle{0.0f, 0.0f, 0.0f, 0.0f}, "Exit");

    // ========== 状态变量 ==========
    int selectedPeer = -1;       // 选中的对等节点索引（-1表示未选中）
    int selectedPending = -1;    // 选中的待处理请求索引
    std::string status = "Idle"; // 状态栏文本

    bool discoveryActive = false;  // 是否正在发现节点
    double discoveryEndTime = 0.0; // 发现结束时间

    // ========== 主循环 ==========
    while (!WindowShouldClose())
    {
        // 获取窗口尺寸
        const int screenWidth = GetScreenWidth();
        const int screenHeight = GetScreenHeight();
        
        // 计算UI布局参数（响应式设计）
        const float margin = std::max(16.0f, screenWidth * 0.02f);    // 边距
        const float spacing = std::max(8.0f, screenWidth * 0.01f);    // 间距
        float btnHeight = std::max(44.0f, screenHeight * 0.065f);     // 按钮高度
        float btnWidth = (screenWidth - margin * 2.0f - spacing * 4.0f) / 5.0f;  // 按钮宽度
        btnWidth = std::max(140.0f, btnWidth);
        const int btnFontSize = static_cast<int>(std::max(18.0f, btnHeight * 0.5f));
        const float topY = margin;

        // 设置按钮位置和大小
        discoverBtn.SetBounds(Rectangle{margin, topY, btnWidth, btnHeight});
        requestBtn.SetBounds(Rectangle{margin + (btnWidth + spacing) * 1.0f, topY, btnWidth, btnHeight});
        acceptBtn.SetBounds(Rectangle{margin + (btnWidth + spacing) * 2.0f, topY, btnWidth, btnHeight});
        rejectBtn.SetBounds(Rectangle{margin + (btnWidth + spacing) * 3.0f, topY, btnWidth, btnHeight});
        exitBtn.SetBounds(Rectangle{margin + (btnWidth + spacing) * 4.0f, topY, btnWidth, btnHeight});
        
        // 设置按钮字体大小
        discoverBtn.SetFontSize(btnFontSize);
        requestBtn.SetFontSize(btnFontSize);
        acceptBtn.SetFontSize(btnFontSize);
        rejectBtn.SetFontSize(btnFontSize);
        exitBtn.SetFontSize(btnFontSize);

        // 如果已经匹配成功，退出循环
        if (client.isInMatch())
            break;

        // 检查发现是否超时（10秒后自动停止）
        if (discoveryActive && GetTime() >= discoveryEndTime)
        {
            node.stopUdpListen();
            discoveryActive = false;
            status = "Discovery stopped";
        }

        // 获取当前数据
        const Vector2 mouse = GetMousePosition();
        auto peers = client.getAvailablePeers();          // 可用节点列表
        auto pending = client.getPendingRequestsSnapshot(); // 待处理请求列表
        
        // 验证选中索引的有效性
        if (selectedPeer >= static_cast<int>(peers.size()))
            selectedPeer = -1;
        if (selectedPending >= static_cast<int>(pending.size()))
            selectedPending = -1;

        // ========== 开始绘制 ==========
        BeginDrawing();
        ClearBackground(RAYWHITE);

        // 绘制标题和信息
        const float headerY = topY + btnHeight + margin;
        DrawText("Match Panel", static_cast<int>(margin), static_cast<int>(headerY), 28, DARKGRAY);
        DrawText(TextFormat("Local ID:%s  TCP:%d  Discovery:%d", 
                 node.getNodeId().c_str(), node.getTcpPort(), node.getDiscoveryPort()),
                 static_cast<int>(margin), screenHeight - 70, 22, DARKGRAY);
        DrawText(status.c_str(), static_cast<int>(margin), screenHeight - 40, 24, BLACK);

        // ========== 处理按钮点击 ==========
        
        // 发现按钮：开始10秒的节点发现
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

        // 请求匹配按钮：向选中的节点发送匹配请求
        if (requestBtn.Draw())
        {
            if (selectedPeer >= 0 && selectedPeer < static_cast<int>(peers.size()))
                status = client.requestMatch(peers[static_cast<size_t>(selectedPeer)]) ? 
                         "Match request sent" : "Match request failed";
            else
                status = "Select a peer";
        }

        // 接受按钮：接受选中的匹配请求
        if (acceptBtn.Draw())
        {
            if (selectedPending >= 0 && selectedPending < static_cast<int>(pending.size()))
                status = client.respondToPendingRequest(pending[static_cast<size_t>(selectedPending)], true) ? 
                         "Request accepted" : "Request expired";
            else
                status = "No request selected";
        }

        // 拒绝按钮：拒绝选中的匹配请求
        if (rejectBtn.Draw())
        {
            if (selectedPending >= 0 && selectedPending < static_cast<int>(pending.size()))
                status = client.respondToPendingRequest(pending[static_cast<size_t>(selectedPending)], false) ? 
                         "Request rejected" : "Request expired";
            else
                status = "No request selected";
        }

        // 退出按钮：返回主菜单
        if (exitBtn.Draw())
        {
            if (discoveryActive)
            {
                node.stopUdpListen();
                discoveryActive = false;
            }
            return false;
        }

        // ========== 绘制可用节点列表 ==========
        DrawText("Available peers", static_cast<int>(margin), 120, 24, BLACK);
        float peerY = 152.0f;
        
        for (size_t i = 0; i < peers.size(); ++i)
        {
            // 创建列表项矩形
            Rectangle item{margin, peerY, static_cast<float>(screenWidth - margin * 2.0f), 40.0f};
            bool hover = CheckCollisionPointRec(mouse, item);
            
            // 根据状态选择颜色（选中/悬停/正常）
            Color fill = (selectedPeer == static_cast<int>(i)) ? Fade(GREEN, 0.35f) : Fade(LIGHTGRAY, 0.35f);
            if (hover)
                fill = Fade(ORANGE, 0.35f);
            
            // 绘制列表项
            DrawRectangleRec(item, fill);
            DrawRectangleLinesEx(item, 1.0f, DARKGRAY);
            
            // 显示节点信息：序号 + 名称或ID + IP:端口
            std::string label = std::to_string(i + 1) + ". " + 
                                (peers[i].name.empty() ? peers[i].id : peers[i].name) +
                                " (" + peers[i].ip + ":" + std::to_string(peers[i].tcpPort) + ")";
            DrawText(label.c_str(), static_cast<int>(item.x) + 8, static_cast<int>(item.y) + 8, 22, BLACK);
            
            // 处理点击选中
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                selectedPeer = static_cast<int>(i);
            
            peerY += 44.0f;
        }

        // ========== 绘制待处理请求列表 ==========
        float pendingStart = peerY + 24.0f;
        if (pendingStart < 340.0f)
            pendingStart = 340.0f;
        DrawText("Pending requests", static_cast<int>(margin), static_cast<int>(pendingStart), 24, BLACK);
        float pendingY = pendingStart + 32.0f;
        
        for (size_t i = 0; i < pending.size(); ++i)
        {
            // 创建列表项矩形
            Rectangle item{margin, pendingY, static_cast<float>(screenWidth - margin * 2.0f), 40.0f};
            bool hover = CheckCollisionPointRec(mouse, item);
            
            // 根据状态选择颜色
            Color fill = (selectedPending == static_cast<int>(i)) ? Fade(SKYBLUE, 0.35f) : Fade(LIGHTGRAY, 0.35f);
            if (hover)
                fill = Fade(ORANGE, 0.35f);
            
            // 绘制列表项
            DrawRectangleRec(item, fill);
            DrawRectangleLinesEx(item, 1.0f, DARKGRAY);
            
            // 显示请求信息：序号 + 请求者名称/ID + IP:端口 + 匹配ID前6位
            std::string label = std::to_string(i + 1) + ". " + 
                                (pending[i].peer.name.empty() ? pending[i].peer.id : pending[i].peer.name) +
                                " (" + pending[i].ip + ":" + std::to_string(pending[i].port) + 
                                ") id=" + pending[i].matchId.substr(0, 6);
            DrawText(label.c_str(), static_cast<int>(item.x) + 8, static_cast<int>(item.y) + 8, 22, BLACK);
            
            // 处理点击选中
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                selectedPending = static_cast<int>(i);
            
            pendingY += 44.0f;
        }

        EndDrawing();
    }

    // 清理：如果发现仍在进行，停止它
    if (discoveryActive)
    {
        node.stopUdpListen();
        discoveryActive = false;
    }

    return client.isInMatch();
}

/**
 * @brief 匿名命名空间：游戏内部使用的常量和结构体
 */
namespace
{
constexpr int BoardSize = 9;  // 棋盘大小：9x9x9

/**
 * @brief 球体实例结构体
 * 
 * 用于表示棋盘上的一个格子位置（以球体显示）
 */
struct SphereInstance
{
    Vector3 position;         // 3D空间位置
    Color color;              // 颜色（包含透明度）
    float distanceToCamera;   // 到相机的距离（用于排序）
    int i, j, k;             // 棋盘逻辑坐标
};

/**
 * @brief 角度转弧度
 * @param degree 角度值
 * @return 弧度值
 */
float d2r(float degree)
{
    return degree * 3.1415926f / 180.0f;
}
} // namespace

/**
 * @brief 主游戏循环函数
 * 
 * 这是3D五子棋游戏的核心函数，提供完整的3D可视化和交互功能
 * 
 * 主要功能：
 * 1. 3D棋盘渲染（9x9x9网格，使用球体表示棋子）
 * 2. 多种视角切换（3D透视、X/Y/Z轴正交视图）
 * 3. 平面高亮与选择系统
 * 4. 鼠标交互（拖拽旋转、滚轮缩放、点击落子）
 * 5. 网络对战支持（通过Client对象）
 * 6. 游戏结果显示
 * 
 * 视角模式：
 * - vmode=0: 3D透视视图（可自由旋转）
 * - vmode=1: X轴正交视图（从X轴负方向看向原点）
 * - vmode=2: Y轴正交视图（从Y轴负方向看向原点）
 * - vmode=3: Z轴正交视图（从Z轴负方向看向原点）
 * 
 * 高亮模式（仅在3D视图下有效）：
 * - hlmode=0: 无高亮
 * - hlmode=1: 高亮Y-Z平面（固定X坐标）
 * - hlmode=2: 高亮X-Z平面（固定Y坐标）
 * - hlmode=3: 高亮X-Y平面（固定Z坐标）
 * 
 * @param client 游戏客户端指针（nullptr表示本地双人模式）
 * @return -1:用户关闭窗口，0:正常退出返回大厅
 */
int RunGame(Client* client)
{
    int screenWidth = 1980;
    int screenHeight = 1280;

    SetWindowSize(screenWidth, screenHeight);
    SetWindowTitle("3D Chess Online - Game");
    SetTargetFPS(30);

    // ========== 3D相机初始化 ==========
    Camera3D camera = { 0 };
    camera.position = Vector3{ 400.0f, 0.0f, 0.0f };  // 初始位置：X轴正方向400单位
    camera.target = Vector3{ 0.0f, 0.0f, 0.0f };       // 目标：原点
    camera.up = Vector3{ 0.0f, 1.0f, 0.0f };           // 上方向：Y轴正方向
    camera.fovy = 70.0f;                             // 视场角（度）
    camera.projection = CAMERA_PERSPECTIVE;          // 投影模式：透视

    SetTargetFPS(30);

    // ========== 相机控制参数 ==========
    float AngleTheta = 45.0f;          // 水平旋转角度（绕Y轴，0°为Z轴正方向）
    float AnglePhi = 45.0f;            // 垂直旋转角度（-89°到89°）
    float SphereDist = 10.0f;          // 棋子之间的距离
    float cameraDist = 400.0f;         // 相机到原点的距离
    float SphereRadius = 3.0f;         // 棋子球体半径
    float Axis_length = 200.0f;        // 坐标轴长度
    const float dragSensitivity = 0.3f; // 鼠标拖拽灵敏度

    // ========== UI按钮创建 ==========
    Button toggleButton(Rectangle{ 20.0f, 20.0f, 180.0f, 40.0f }, "Hide all spheres");
    Button xButton(Rectangle{ 20.0f, 70.0f, 180.0f, 40.0f }, "X axis");
    Button yButton(Rectangle{ 20.0f, 120.0f, 180.0f, 40.0f }, "Y axis");
    Button zButton(Rectangle{ 20.0f, 170.0f, 180.0f, 40.0f }, "Z axis");
    Button dButton(Rectangle{ 20.0f, 220.0f, 180.0f, 40.0f }, "3D");
    Button HighlightButtonx(Rectangle{ 20.0f, 270.0f, 180.0f, 40.0f }, "Y-Z Highlight");
    Button HighlightButtony(Rectangle{ 20.0f, 320.0f, 180.0f, 40.0f }, "X-Z Highlight");
    Button HighlightButtonz(Rectangle{ 20.0f, 370.0f, 180.0f, 40.0f }, "X-Y Highlight");

    // 创建数字按钮（0-9，用于选择平面）
    std::vector<Button> NumberButton;
    for (int i = 0; i <= 9; ++i)
    {
        NumberButton.push_back(Button(Rectangle{ 20.0f, (520.0f + i * 50.0f), 180.0f, 40.0f }, std::to_string(i)));
    }
    NumberButton[0].SetText("Reset");  // 0号按钮用于重置选择

    // ========== 游戏状态变量 ==========
    std::vector<SphereInstance> spheres;  // 球体实例列表（每帧重建）
    spheres.reserve(BoardSize * BoardSize * BoardSize);

    bool showSpheres = true;  // 是否显示球体
    int vmode = 0;            // 视角模式（0:3D, 1:X轴, 2:Y轴, 3:Z轴）
    int hlmode = 0;           // 高亮模式（0:无, 1:YZ平面, 2:XZ平面, 3:XY平面）
    int number = 0;           // 选中的平面编号（1-9，0表示未选中）
    int gameStep = 1;         // 当前回合数（从1开始）

    // ========== 颜色配置 ==========
    Color typeColor[4] = { GRAY, BLUE, RED, YELLOW };
    typeColor[0].a = 65;  // 空位（灰色，半透明）

    // ========== 初始相机位置计算 ==========
    camera.position.x = cameraDist * cosf(d2r(AnglePhi)) * sinf(d2r(AngleTheta));
    camera.position.z = cameraDist * cosf(d2r(AnglePhi)) * cosf(d2r(AngleTheta));
    camera.position.y = cameraDist * sinf(d2r(AnglePhi));

    // UI文本
    std::string BottomText = "3D View   ";
    std::string AddText;
    bool hasLastOpponentMove = false;
    int lastOpponentX = 0, lastOpponentY = 0, lastOpponentZ = 0;

    // ========== 棋盘状态数组 ==========
    // 0:空位, 1:蓝色棋子, 2:红色棋子
    // 使用[BoardSize+2]是为了边界检测方便
    short ColorBoard[BoardSize + 2][BoardSize + 2][BoardSize + 2];
    memset(ColorBoard, 0, sizeof(ColorBoard));

    // ========== 初始化网络对战 ==========
    if (client)
    {
        client->initGameState();
    }

    // ========== 游戏结束相关 ==========
    double gameOverTime = 0.0;  // 游戏结束的时间戳
    bool gameEnded = false;     // 游戏是否已结束

    // ========== 主游戏循环 ==========
    while (!WindowShouldClose())
    {
        screenWidth = GetScreenWidth();
        screenHeight = GetScreenHeight();

        if (client && !client->isGameRunning() && !gameEnded)
        {
            gameEnded = true;
            gameOverTime = GetTime();
        }

        // ===== 游戏结束画面（显示3秒后返回大厅） =====
        if (gameEnded)
        {
            if (GetTime() - gameOverTime > 3.0)
            {
                break;  // 退出游戏循环
            }

            BeginDrawing();
            ClearBackground(BLACK);

            // 根据游戏结果显示不同信息
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

        // ===== 接收对手的落子 =====
        if (client && !client->isMyTurn() && client->isGameRunning())
        {
            int x, y, z;
            if (client->tryGetOpponentMove(x, y, z))
            {
                // 根据本方玩家决定对手颜色
                int opponentColor = (client->getMyPlayer() == '1') ? 2 : 1;
                ColorBoard[x][y][z] = opponentColor;
                lastOpponentX = x;
                lastOpponentY = y;
                lastOpponentZ = z;
                hasLastOpponentMove = true;
                gameStep++;
            }
        }

        // ===== 每帧初始化 =====
        AddText.clear();
        spheres.clear();
        Axis_length = SphereDist * (BoardSize + 5.0f) / 2;

        // ===== 键盘控制：调整球体半径 =====
        if (IsKeyDown(KEY_O))  // O键：减小半径
        {
            if (SphereRadius > 1.0f)
            {
                SphereRadius -= 0.2f;
            }
        }
        if (IsKeyDown(KEY_P))  // P键：增大半径
        {
            if (SphereRadius < SphereDist / 2)
            {
                SphereRadius += 0.2f;
            }
        }

        // ===== 键盘和鼠标控制：3D视角旋转 =====
        if (vmode == 0)  // 仅在3D模式下允许旋转
        {
            // 方向键控制
            if (IsKeyDown(KEY_UP))
            {
                AnglePhi += 2.0f;
                if (AnglePhi > 89.0f)  // 限制垂直角度
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

            // 鼠标中键拖拽旋转
            if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
            {
                Vector2 md = GetMouseDelta();
                AngleTheta -= md.x * dragSensitivity;
                AnglePhi += md.y * dragSensitivity;

                // 限制角度范围
                if (AnglePhi > 89.0f) AnglePhi = 89.0f;
                if (AnglePhi < -89.0f) AnglePhi = -89.0f;
                if (AngleTheta < 0.0f) AngleTheta += 360.0f;
                if (AngleTheta >= 360.0f) AngleTheta -= 360.0f;
            }

            // 鼠标滚轮：调整球体间距
            float deltasize = GetMouseWheelMove();
            SphereDist += deltasize;
            if (SphereDist < 2 * SphereRadius)
                SphereDist = 2 * SphereRadius;  // 防止球体重叠
        }

        // ===== 更新相机位置（球坐标 -> 笛卡尔坐标） =====
        camera.position.x = cameraDist * cosf(d2r(AnglePhi)) * sinf(d2r(AngleTheta));
        camera.position.z = cameraDist * cosf(d2r(AnglePhi)) * cosf(d2r(AngleTheta));
        camera.position.y = cameraDist * sinf(d2r(AnglePhi));

        // ========== 开始绘制 ==========
        BeginDrawing();

        ClearBackground(BLACK);

        // ===== 3D场景绘制 =====
        BeginMode3D(camera);

        // 绘制坐标轴（Y轴:绿, X轴:红, Z轴:蓝）
        DrawLine3D(Vector3{ 0, Axis_length, 0 }, Vector3{ 0, 0, 0 }, GREEN);
        DrawLine3D(Vector3{ Axis_length, 0, 0 }, Vector3{ 0, 0, 0 }, RED);
        DrawLine3D(Vector3{ 0, 0, Axis_length }, Vector3{ 0, 0, 0 }, BLUE);

        if (showSpheres)
        {
            // ===== 准备球体渲染 =====
            const bool highlightPlaneActive = (vmode == 0 && hlmode != 0 && number != 0);
            const Vector2 mousePos = GetMousePosition();
            float closestGreyDistSq = std::numeric_limits<float>::max();
            std::size_t closestGreyIndex = std::numeric_limits<std::size_t>::max();
            const float posDelta = SphereDist * (BoardSize + 1.0f) / 2.0f;  // 偏移量（使棋盘居中）
            Vector3 lastOpponentWorldPos{};

            // ===== 遍历所有格子，创建球体实例 =====
            for (int i = 1; i <= BoardSize; ++i)
                for (int j = 1; j <= BoardSize; ++j)
                    for (int k = 1; k <= BoardSize; ++k)
                    {
                        // 获取基础颜色
                        Color color = typeColor[ColorBoard[i][j][k]];
                        color.a = 230;  // 默认不透明度

                        // 判断是否在高亮平面外
                        bool isunhighlighted = (hlmode == 1 && number != i) ||
                            (hlmode == 2 && number != j) ||
                            (hlmode == 3 && number != k);
                        bool ishighlighted = (hlmode == 1 && number == i) ||
                            (hlmode == 2 && number == j) ||
                            (hlmode == 3 && number == k);
                        // ===== 应用透明度规则 =====
                        if(ColorBoard[i][j][k] == 0&&!ishighlighted)color.a = 40;// 空位：非常透明
                        // 3D视图下的高亮模式：平面外的格子变暗
                        if (vmode == 0 && number != 0 && isunhighlighted && ColorBoard[i][j][k] != 0)   
                                color.a = 150;  // 已有棋子：半透明

                        // 正交视图下：只显示选中的平面
                        if (vmode != 0 && number != 0)
                        {
                            if (vmode == 1 && number != i)
                                color.a = 0;  // 完全隐藏
                            if (vmode == 2 && number != j)
                                color.a = 0;
                            if (vmode == 3 && number != k)
                                color.a = 0;
                        }

                        // ===== 计算3D世界坐标 =====
                        Vector3 worldPos = Vector3{
                            SphereDist * i - posDelta,
                            SphereDist * j - posDelta,
                            SphereDist * k - posDelta
                        };

                        if (hasLastOpponentMove && client && i == lastOpponentX && j == lastOpponentY && k == lastOpponentZ)
                            lastOpponentWorldPos = worldPos;

                        // 投影到屏幕坐标
                        Vector2 screenPos = GetWorldToScreen(worldPos, camera);

                        // ===== 正交视图下的鼠标悬停检测 =====
                        if (number != 0 && vmode != 0 && color.a > 60 &&
                            CheckCollisionPointCircle(mousePos, screenPos, 45.0f))
                        {
                            color.a = 100;  // 悬停时半透明
                            AddText = " --Mouse On (" + std::to_string(i) + "," +
                                std::to_string(j) + "," + std::to_string(k) + ")";

                            // 点击落子
                            if (ColorBoard[i][j][k] == 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                            {
                                if (client)
                                {
                                    // 网络对战模式：检查回合并发送
                                    if (client->isMyTurn() && client->tryPlaceMyPiece(i, j, k))
                                    {
                                        int myColor = (client->getMyPlayer() == '1') ? 1 : 2;
                                        ColorBoard[i][j][k] = myColor;
                                        gameStep++;
                                    }
                                }
                                else
                                {
                                    // 本地双人模式：轮流落子
                                    ColorBoard[i][j][k] = (gameStep % 2 + 1);
                                    ++gameStep;
                                }
                            }
                        }

                        // 完全透明的格子不添加到渲染列表
                        if (color.a == 0)
                            continue;

                        // 添加球体实例到列表
                        const std::size_t sphereIndex = spheres.size();
						// 以紫色显示对手的最近落子
                        if (i == lastOpponentX && j == lastOpponentY && k == lastOpponentZ)
                        {
                            int oc = (client->getMyPlayer() == '1') ? 2 : 1;
                            color.r = (oc == 1) ? 0 : 122;
                            color.g = 122;
                            color.b = (oc == 2) ? 0 : 122;
                        }
                        spheres.push_back({ worldPos, color, Vector3Distance(camera.position, worldPos), i, j, k });

                        // ===== 3D高亮模式：寻找最近的空位 =====
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

            // ===== 高亮距离鼠标最近的空位（3D高亮模式） =====
            if (highlightPlaneActive &&
                closestGreyIndex != std::numeric_limits<std::size_t>::max() &&
                closestGreyDistSq < 800)  // 距离阈值：约28像素
            {
                SphereInstance& highlighted = spheres[closestGreyIndex];
                Color highlightColor = YELLOW;
                highlightColor.a = highlighted.color.a;
                highlighted.color = highlightColor;

                AddText = " --Hover (" + std::to_string(highlighted.i) + "," +
                    std::to_string(highlighted.j) + "," + std::to_string(highlighted.k) + ")";

                // 点击落子
                if (ColorBoard[highlighted.i][highlighted.j][highlighted.k] == 0 &&
                    IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
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

            // ===== 渲染所有球体（按距离排序，实现正确的透明度混合） =====
            if (!spheres.empty())
            {
                // 从远到近排序（画家算法）
                std::sort(spheres.begin(), spheres.end(),
                    [](const SphereInstance& a, const SphereInstance& b)
                    { return a.distanceToCamera > b.distanceToCamera; });

                // 禁用深度写入（允许透明度混合）
                rlDisableDepthMask();
                for (const auto& sphere : spheres)
                    DrawSphere(sphere.position, SphereRadius, sphere.color);
                rlEnableDepthMask();
            }

            // 同方相邻棋子连线（仅3D视图，使用细圆柱，含斜向邻接，且仅当连续>=3）
            if (vmode == 0 && showSpheres)
            {
                const float lineRadius = SphereRadius * 0.12f;
                for (int i = 1; i <= BoardSize; ++i)
                    for (int j = 1; j <= BoardSize; ++j)
                        for (int k = 1; k <= BoardSize; ++k)
                        {
                            int colorId = ColorBoard[i][j][k];
                            if (colorId == 0) continue;

                            // 遍历唯一方向（正半空间），检查是否为该方向连线的起点
                            for (int dx = -1; dx <= 1; ++dx)
                                for (int dy = -1; dy <= 1; ++dy)
                                    for (int dz = -1; dz <= 1; ++dz)
                                    {
                                        if (dx == 0 && dy == 0 && dz == 0) continue;
                                        if (dx < 0) continue;
                                        if (dx == 0 && dy < 0) continue;
                                        if (dx == 0 && dy == 0 && dz < 0) continue;

                                        int prevI = i - dx, prevJ = j - dy, prevK = k - dz;
                                        if (prevI >= 1 && prevI <= BoardSize && prevJ >= 1 && prevJ <= BoardSize && prevK >= 1 && prevK <= BoardSize)
                                        {
                                            if (ColorBoard[prevI][prevJ][prevK] == colorId)
                                                continue; // 不是链条起点
                                        }

                                        // 前向统计长度
                                        int len = 1;
                                        int ni = i + dx, nj = j + dy, nk = k + dz;
                                        while (ni >= 1 && ni <= BoardSize && nj >= 1 && nj <= BoardSize && nk >= 1 && nk <= BoardSize && ColorBoard[ni][nj][nk] == colorId)
                                        {
                                            ++len;
                                            ni += dx; nj += dy; nk += dz;
                                        }

                                        if (len < 3) continue; // 少于3个不连线

                                        // 绘制链条圆柱
                                        Vector3 posA{ SphereDist * i - posDelta,
                                                      SphereDist * j - posDelta,
                                                      SphereDist * k - posDelta };
                                        Color lineColor = typeColor[colorId];
                                        lineColor.a = 200;
										double ThicknessFactor = (len==3)?0.15:0.3;
                                        ni = i + dx; nj = j + dy; nk = k + dz;
                                        for (int step = 1; step < len; ++step)
                                        {
                                            Vector3 posB{ SphereDist * ni - posDelta,
                                                          SphereDist * nj - posDelta,
                                                          SphereDist * nk - posDelta };
                                            DrawCylinderEx(posA, posB, ThicknessFactor* SphereRadius, ThicknessFactor * SphereRadius, 8, lineColor);
                                            posA = posB;
                                            ni += dx; nj += dy; nk += dz;
                                        }
                                    }
                        }
            }
        }
        EndMode3D();
        int currentPlayer = ((gameStep - 1) % 2) + 1;

        const float hudMargin = std::max(20.0f, screenWidth * 0.02f);
        const float turnRadius = std::clamp(screenHeight * 0.03f, 20.0f, 36.0f);
        const int turnFont = static_cast<int>(std::clamp(screenHeight * 0.025f, 16.0f, 28.0f));
        const int stepFont = static_cast<int>(std::clamp(screenHeight * 0.04f, 30.0f, 52.0f));
        const int bottomFont = static_cast<int>(std::clamp(screenHeight * 0.032f, 18.0f, 40.0f));
        const int axisFont = static_cast<int>(std::clamp(screenHeight * 0.02f, 14.0f, 22.0f));
        // ===== 当前玩家指示器（右上角圆圈） =====
        Vector2 turnCircleCenter{ screenWidth - hudMargin - turnRadius, hudMargin + turnRadius };
        DrawCircleV(turnCircleCenter, turnRadius, typeColor[currentPlayer]);

        float turnTextX = std::max(hudMargin, turnCircleCenter.x - std::max(220.0f, screenWidth * 0.18f));
        float turnTextY = hudMargin;
        if (client)
        {
            // 显示当前回合归属
            if (client->isMyTurn())
                DrawText("Your Turn", static_cast<int>(turnTextX), static_cast<int>(turnTextY), turnFont, GREEN);
            else
                DrawText("Opponent's Turn", static_cast<int>(turnTextX), static_cast<int>(turnTextY), turnFont, ORANGE);

            // 显示最近一次同步的时间和状态
            auto timeSinceSync = client->getTimeSinceLastSync();
            bool syncSuccess = true;
            std::string syncError;
            client->getLastSyncStatus(syncSuccess, syncError);
            
            // 构造同步状态文本
            std::string syncText = "Sync: ";
            if (client->isSyncing())
            {
                syncText += "Syncing...";
            }
            else if (syncSuccess)
            {
                syncText += "OK (" + std::to_string(timeSinceSync) + "s ago)";
            }
            else
            {
                syncText += "FAILED - " + syncError;
            }
            
            // 根据状态设置颜色
            Color syncColor;
            if (client->isSyncing())
            {
                syncColor = SKYBLUE;  // 正在同步：天蓝色
            }
            else if (!syncSuccess)
            {
                syncColor = RED;  // 同步失败：红色
            }
            else if (timeSinceSync < 6)
            {
                syncColor = GREEN;  // 最近同步成功：绿色
            }
            else if (timeSinceSync < 10)
            {
                syncColor = YELLOW;  // 稍久未同步：黄色
            }
            else
            {
                syncColor = ORANGE;  // 很久未同步：橙色
            }
            
            DrawText(syncText.c_str(), static_cast<int>(turnTextX), static_cast<int>(turnTextY) + 60, turnFont - 10, syncColor);
        }
        else
        {
            // 本地模式：显示当前玩家
            if (currentPlayer == 1)
                DrawText("BLUE's Turn", static_cast<int>(turnTextX), static_cast<int>(turnTextY), turnFont, WHITE);
            else
                DrawText("RED's Turn", static_cast<int>(turnTextX), static_cast<int>(turnTextY), turnFont, WHITE);
        }

        float stepX = screenWidth - hudMargin - stepFont * 1.5f;
        float stepY = screenHeight - hudMargin - stepFont * 1.5f;
        DrawText(std::to_string(gameStep).c_str(), static_cast<int>(stepX), static_cast<int>(stepY), stepFont, WHITE);


        // ===== 绘制坐标轴标签 =====
        Vector2 xPos = GetWorldToScreen(Vector3{ Axis_length, 0.0f, 0.0f }, camera);
        Vector2 yPos = GetWorldToScreen(Vector3{ 0.0f, Axis_length, 0.0f }, camera);
        Vector2 zPos = GetWorldToScreen(Vector3{ 0.0f, 0.0f, Axis_length }, camera);
        DrawText("x", static_cast<int>(xPos.x), static_cast<int>(xPos.y), axisFont, RED);
        DrawText("y", static_cast<int>(yPos.x), static_cast<int>(yPos.y), axisFont, GREEN);
        DrawText("z", static_cast<int>(zPos.x), static_cast<int>(zPos.y), axisFont, BLUE);
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

        // 绘制底部状态栏
        if (showSpheres)
            DrawText((BottomText + AddText).c_str(), static_cast<int>(hudMargin), static_cast<int>(bottomY), bottomFont, WHITE);
        else
            DrawText((BottomText + "(Hidden)").c_str(), static_cast<int>(hudMargin), static_cast<int>(bottomY), bottomFont, WHITE);
        // ========== 处理UI按钮点击 ==========

        // 切换显示/隐藏球体
        if (toggleButton.Draw())
        {
            showSpheres = !showSpheres;
        }

        // 切换到3D视图
        if (dButton.Draw())
        {
            hlmode = vmode;  // 保存之前的高亮模式
            vmode = 0;
            camera.projection = CAMERA_PERSPECTIVE;
            SphereRadius = 3.0f;
            SphereDist = 10.0f;
            AngleTheta = 45.0f;
            AnglePhi = 45.0f;
            cameraDist = 400.0f;
        }

        // 切换到X轴视图
        if (xButton.Draw())
        {
            if (hlmode != 1)
                number = 0;  // 切换轴时重置平面选择
            hlmode = 0;
            vmode = 1;
            camera.projection = CAMERA_ORTHOGRAPHIC;
            SphereRadius = 2.5f;
            SphereDist = 5.0f;
            AngleTheta = 90.0f;
            AnglePhi = 0.0f;
            cameraDist = 400.0f;
        }

        // 切换到Y轴视图
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

        // 切换到Z轴视图
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

        // ===== 高亮按钮（仅在3D视图下显示） =====
        if (vmode == 0)
        {
            if (HighlightButtonx.Draw())
            {
                number = 0;
                hlmode = 1;  // 高亮Y-Z平面
            }
            if (HighlightButtony.Draw())
            {
                number = 0;
                hlmode = 2;  // 高亮X-Z平面
            }
            if (HighlightButtonz.Draw())
            {
                number = 0;
                hlmode = 3;  // 高亮X-Y平面
            }
        }

        // ===== 数字按钮（选择平面编号） =====
        if (hlmode != 0 || vmode != 0)
        {
            // Reset按钮：清除平面选择
            if (NumberButton[0].Draw())
            {
                hlmode = 0;
                number = 0;
            }

            // 1-9号按钮：选择对应平面
            for (int i = 1; i <= BoardSize; ++i)
                if (NumberButton[i].Draw())
                    number = i;
        }

        EndDrawing();
    }

    // ========== 退出处理 ==========
    bool userClosedWindow = WindowShouldClose();

    return userClosedWindow ? -1 : 0;  // -1:关闭窗口, 0:正常退出
}

// 模式选择界面：在线 / 本地 / 退出
int SelectMode()
{
    SetWindowSize(900, 600);
    SetWindowTitle("3D Chess Online - Select Mode");

    Button onlineBtn(Rectangle{0,0,0,0}, "Online Match");
    Button localBtn(Rectangle{0,0,0,0}, "Local Game");
    Button exitBtn(Rectangle{0,0,0,0}, "Exit");

    while (!WindowShouldClose())
    {
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        float margin = std::max(20.0f, sw * 0.08f);
        float spacing = std::max(18.0f, sw * 0.04f);
        float btnWidth = std::max(200.0f, sw * 0.25f);
        float btnHeight = std::max(70.0f, sh * 0.12f);
        float centerX = sw * 0.5f;
        float topY = sh * 0.35f;
        int fontSize = static_cast<int>(std::max(26.0f, btnHeight * 0.4f));

        onlineBtn.SetBounds(Rectangle{ centerX - btnWidth - spacing * 0.5f, topY, btnWidth, btnHeight });
        localBtn.SetBounds(Rectangle{ centerX + spacing * 0.5f, topY, btnWidth, btnHeight });
        exitBtn.SetBounds(Rectangle{ centerX - btnWidth * 0.5f, topY + btnHeight + spacing, btnWidth, btnHeight * 0.8f });

        onlineBtn.SetFontSize(fontSize);
        localBtn.SetFontSize(fontSize);
        exitBtn.SetFontSize(static_cast<int>(fontSize * 0.8f));

        BeginDrawing();
        ClearBackground(RAYWHITE);

        const char* title = "Select Game Mode";
        int titleSize = static_cast<int>(std::max(32.0f, sh * 0.06f));
        DrawText(title, (sw - MeasureText(title, titleSize)) / 2, static_cast<int>(topY * 0.4f), titleSize, BLACK);

        if (onlineBtn.Draw()) return 1;
        if (localBtn.Draw()) return 2;
        if (exitBtn.Draw()) return 0;

        EndDrawing();
    }
    return 0;
}