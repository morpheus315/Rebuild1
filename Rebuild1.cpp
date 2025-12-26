#include "Game.h"
#include "LanP2PNode.h"
#include "GameClient.h"
#include <iostream>
#include <string>

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#ifdef Rectangle
#undef Rectangle
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

	// 创建节点（UDP发现端口37000，TCP随机端口），启动广播和TCP监听
	LanP2PNode node(37000, 0);
	node.setPeerStaleMs(15000);
	node.startBroadcastOnly();

	// 创建客户端，负责回调接入与游戏流程
	Client client(node);

	// 设置全局指针并注册控制台关闭事件处理
	g_node = &node;
	g_client = &client;
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

	// 设置显示名
	std::cout << "Enter display name (optional): ";
	std::string name;
	std::getline(std::cin, name);
	if (!name.empty())
		node.setNodeName(name);

	std::cout << "Initialization done. Your ID: " << node.getNodeId()
		<< ", TCP port: " << node.getTcpPort()
		<< ", discovery port: " << node.getDiscoveryPort() << std::endl;

	while (true)
	{
		bool matched = SeekPeer(client, node);
		if (!matched)
			break;

		if (client.isInMatch())
		{
			client.startGame();
		}
	}

	return 0;
}




