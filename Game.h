#pragma once

// 前向声明，避免头文件循环依赖
class Client;
namespace lanp2p
{
	class LanP2PNode;
}

/**
 * @brief 匹配对手界面
 * 
 * 显示匹配大厅，允许玩家：
 * - 发现局域网内的其他玩家
 * - 向其他玩家发送匹配请求
 * - 接受或拒绝其他玩家的匹配请求
 * 
 * @param client 客户端对象引用
 * @param node 局域网P2P节点引用
 * @return true 匹配成功，false 用户退出
 */
bool SeekPeer(Client &client, lanp2p::LanP2PNode &node);

/**
 * @brief 运行游戏主循环
 * 
 * 显示3D棋盘并处理游戏逻辑：
 * - 渲染3D棋盘和棋子
 * - 处理玩家输入（落子、视角控制等）
 * - 处理在线对战的网络同步
 * - 显示游戏状态（回合、获胜等）
 * 
 * @param client 客户端对象指针，nullptr表示离线模式
 * @return -1 用户关闭窗口，0 游戏正常结束
 */
int RunGame(Client* client=nullptr);
