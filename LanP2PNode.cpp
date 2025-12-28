#include "LanP2PNode.h"

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")
#include <chrono>
#include <random>
#include <sstream>
#include <cstdio>
#include <tuple>

namespace lanp2p
{

	// ========== 套接字工具函数 ==========
	
	/**
	 * @brief 关闭套接字（统一封装）
	 * 
	 * @param s 套接字句柄
	 */
	static void closesock(uintptr_t s)
	{
		closesocket(static_cast<SOCKET>(s));
	}

	/**
	 * @brief 设置套接字地址可重用
	 * 
	 * Windows下需要先关闭独占模式，再启用地址重用
	 * 
	 * @param s 套接字句柄
	 * @return true 设置成功，false 失败
	 */
	static bool setReuse(uintptr_t s)
	{
		int yes = 1;
		BOOL no = FALSE;
		// 关闭独占地址使用
		setsockopt(static_cast<SOCKET>(s), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&no, sizeof(no));
		// 启用地址重用
		return setsockopt(static_cast<SOCKET>(s), SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes)) == 0;
	}

	/**
	 * @brief 启用UDP广播功能
	 * 
	 * @param s 套接字句柄
	 * @return true 设置成功，false 失败
	 */
	static bool setBroadcast(uintptr_t s)
	{
		int yes = 1;
		return setsockopt(static_cast<SOCKET>(s), SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof(yes)) == 0;
	}

	// ========== 构造与析构 ==========

	/**
	 * @brief 构造函数
	 * 
	 * 初始化Winsock库并生成随机节点ID
	 */
	LanP2PNode::LanP2PNode(uint16_t discoveryPort, uint16_t tcpPort)
		: _discoveryPort(discoveryPort), _tcpPort(tcpPort), _nodeId(randomId())
	{
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
	}

	/**
	 * @brief 析构函数
	 * 
	 * 停止所有线程并清理Winsock
	 */
	LanP2PNode::~LanP2PNode()
	{
		stop();
		WSACleanup();
	}

	// ========== 生命周期管理 ==========

	/**
	 * @brief 启动完整功能
	 * 
	 * 启动：UDP广播、UDP监听、TCP监听、后台维护
	 */
	void LanP2PNode::start()
	{
		// 防止重复启动
		if (_running.exchange(true))
			return;
			
		// 设置所有模块为活跃状态
		_broadcastActive.store(true);
		_udpListenActive.store(true);
		_tcpActive.store(true);
		
		// 启动后台维护线程
		if (!_maintenanceActive.exchange(true)
		    && !_maintenanceThread.joinable())
		{
			_maintenanceThread = std::thread(&LanP2PNode::peersMaintenanceLoop, this);
		}
		
		// 启动三个主要线程
		_udpBroadcaster = std::thread(&LanP2PNode::udpBroadcastLoop, this);
		_udpListener = std::thread(&LanP2PNode::udpListenLoop, this);
		_tcpListener = std::thread(&LanP2PNode::tcpListenLoop, this);
	}

	/**
	 * @brief 仅启动广播与TCP监听
	 * 
	 * 适合只需要被动接受匹配的场景
	 */
	void LanP2PNode::startBroadcastOnly()
	{
		if (!_running.exchange(true))
		{
			// 首次进入运行状态
		}
		
		// 启动UDP广播线程
		if (!_broadcastActive.exchange(true)
		    && !_udpBroadcaster.joinable())
		{
			_udpBroadcaster = std::thread(&LanP2PNode::udpBroadcastLoop, this);
		}
		
		// 启动TCP监听线程
		if (!_tcpActive.exchange(true)
		    && !_tcpListener.joinable())
		{
			_tcpListener = std::thread(&LanP2PNode::tcpListenLoop, this);
		}
		
		// 启动后台维护线程
		if (!_maintenanceActive.exchange(true)
		    && !_maintenanceThread.joinable())
		{
			_maintenanceThread = std::thread(&LanP2PNode::peersMaintenanceLoop, this);
		}
	}

	/**
	 * @brief 开启UDP发现监听
	 * 
	 * 开始监听其他节点的广播
	 */
	void LanP2PNode::startUdpListen()
	{
		if (!_running)
			_running = true;
			
		if (!_udpListenActive.exchange(true)
		    && !_udpListener.joinable())
		{
			_udpListener = std::thread(&LanP2PNode::udpListenLoop, this);
		}
		
		if (!_maintenanceActive.exchange(true)
		    && !_maintenanceThread.joinable())
		{
			_maintenanceThread = std::thread(&LanP2PNode::peersMaintenanceLoop, this);
		}
	}

	/**
	 * @brief 停止UDP发现监听
	 * 
	 * 通过发送本地UDP包唤醒阻塞的recvfrom调用
	 */
	void LanP2PNode::stopUdpListen()
	{
		if (!_udpListenActive.exchange(false))
			return;
			
		// 发送本地UDP数据包唤醒阻塞的recvfrom
		uintptr_t ps = (uintptr_t)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if ((SOCKET)ps != INVALID_SOCKET)
		{
			sockaddr_in a{};
			a.sin_family = AF_INET;
			a.sin_port = htons(_discoveryPort);
			a.sin_addr.s_addr = inet_addr("127.0.0.1");
			sendto(static_cast<SOCKET>(ps), "", 0, 0, (sockaddr *)&a, sizeof(a));
			closesock(ps);
		}
		
		if (_udpListener.joinable())
			_udpListener.join();
	}

	/**
	 * @brief 停止全部功能
	 * 
	 * 停止所有线程并等待它们退出
	 */
	void LanP2PNode::stop()
	{
		if (!_running.exchange(false))
			return;
			
		// 设置所有标志为false
		_broadcastActive.store(false);
		_udpListenActive.store(false);
		_tcpActive.store(false);
		_maintenanceActive.store(false);
		
		// 发送本地UDP数据包以唤醒UDP监听线程
		uintptr_t ps = (uintptr_t)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if ((SOCKET)ps != INVALID_SOCKET)
		{
			sockaddr_in a{};
			a.sin_family = AF_INET;
			a.sin_port = htons(_discoveryPort);
			a.sin_addr.s_addr = inet_addr("127.0.0.1");
			sendto(static_cast<SOCKET>(ps), "", 0, 0, (sockaddr *)&a, sizeof(a));
			closesock(ps);
		}

		// 发送本地TCP连接以唤醒accept阻塞
		if (_tcpPort != 0)
		{
			uintptr_t ts = (uintptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if ((SOCKET)ts != INVALID_SOCKET)
			{
				sockaddr_in ta{};
				ta.sin_family = AF_INET;
				ta.sin_port = htons(_tcpPort);
				ta.sin_addr.s_addr = inet_addr("127.0.0.1");
				// 尝试连接（忽略失败）
				connect(static_cast<SOCKET>(ts), (sockaddr *)&ta, sizeof(ta));
				// 不需要发送数据，关闭即可让accept返回一个套接字
				closesock(ts);
			}
		}

		// 等待所有线程退出
		if (_udpBroadcaster.joinable())
			_udpBroadcaster.join();
		if (_udpListener.joinable())
			_udpListener.join();
		if (_tcpListener.joinable())
			_tcpListener.join();
		if (_maintenanceThread.joinable())
			_maintenanceThread.join();
	}

	// ========== 回调函数设置 ==========
	
	void LanP2PNode::setOnPeerDiscovered(const std::function<void(const PeerInfo &)> &cb)
	{
		_onPeerDiscovered = cb;
	}
	
	void LanP2PNode::setOnMatchRequest(const std::function<void(const PeerInfo &, const std::string &matchId)> &cb)
	{
		_onMatchRequest = cb;
	}
	
	void LanP2PNode::setOnMatchResponse(const
	                                    std::function<void(const PeerInfo &, bool accepted, const std::string &matchId)> &cb)
	{
		_onMatchResponse = cb;
	}
	
	void LanP2PNode::setOnMatchInterrupted(const std::function<void(const PeerInfo &, const std::string &matchId)> &cb)
	{
		_onMatchInterrupted = cb;
	}
	
	void LanP2PNode::setOnGameMove(const std::function<void(const PeerInfo &, int x, int y, int z)> &cb)
	{
		_onGameMove = cb;
	}
	
	void LanP2PNode::setOnBoardSync(const std::function<void(const PeerInfo &, const std::string &)> &cb)
	{
		_onBoardSync = cb;
	}

	// ========== 查询接口 ==========

	/**
	 * @brief 返回当前在线的对等节点快照
	 * 
	 * 自动移除超时的节点（但保留有活跃匹配的）
	 * 
	 * @return 节点信息列表
	 */
	std::vector<PeerInfo> LanP2PNode::getPeersSnapshot()
	{
		std::lock_guard<std::mutex> lk(_peersMutex);
		const uint64_t now = nowMs();
		
		// 移除超时节点（排除有活跃匹配的）
		for (auto it = _peersByKey.begin(); it != _peersByKey.end(); )
		{
			// 如果该节点有活跃匹配，跳过
			if (_matchesByKey.find(it->first) != _matchesByKey.end())
			{
				++it;
				continue;
			}
			
			// 检查是否超时
			if (_peerStaleMs > 0 && (now - it->second.lastSeenMs) > _peerStaleMs)
			{
				it = _peersByKey.erase(it);
			}
			else
			{
				++it;
			}
		}
		
		// 构造快照
		std::vector<PeerInfo> v;
		v.reserve(_peersByKey.size());
		for (auto& kv : _peersByKey)
			v.push_back(kv.second);
		return v;
	}

	// UDP广播循环（周期广播自身信息）
	// ========== UDP网络线程 ==========

	/**
	 * @brief UDP广播循环线程
	 * 
	 * 功能：
	 * 1. 等待TCP端口绑定完成（最多1秒）
	 * 2. 初始快速广播5次（每200ms一次）
	 * 3. 之后每5秒广播一次（同时发送到广播地址和本地回环）
	 * 
	 * 广播格式：
	 * - 无名称：DISC|nodeId|tcpPort|
	 * - 有名称：DISC|nodeId|tcpPort|nodeName|
	 */
	void LanP2PNode::udpBroadcastLoop()
	{
		// 创建UDP套接字
		uintptr_t s = (uintptr_t)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if ((SOCKET)s == INVALID_SOCKET)
			return;
		setReuse(s);
		setBroadcast(s);

		// 等待TCP端口绑定完成（最多等待1秒）
		for (int i = 0; i < 50 && _running && _broadcastActive && !_tcpBoundReady.load(); ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(20));

		// 配置广播地址（255.255.255.255）
		sockaddr_in addrBC{};
		addrBC.sin_family = AF_INET;
		addrBC.sin_port = htons(_discoveryPort);
		addrBC.sin_addr.s_addr = INADDR_BROADCAST;
		
		// 配置本地回环地址（127.0.0.1）
		sockaddr_in addrLoop{};
		addrLoop.sin_family = AF_INET;
		addrLoop.sin_port = htons(_discoveryPort);
		addrLoop.sin_addr.s_addr = inet_addr("127.0.0.1");

		// 初始快速广播5次（让其他节点快速发现）
		for (int b = 0; b < 5 && _running && _broadcastActive; ++b)
		{
			char buf[256];
			int len;
			
			// 构造广播消息
			if (_nodeName.empty())
				len = std::snprintf(buf, sizeof(buf), "DISC|%s|%u", _nodeId.c_str(), (unsigned)_tcpPort);
			else
				len = std::snprintf(buf, sizeof(buf), "DISC|%s|%u|%s", _nodeId.c_str(), (unsigned)_tcpPort, _nodeName.c_str());
			
			// 发送到广播地址和本地回环
			sendto(static_cast<SOCKET>(s), buf, len, 0, (sockaddr *)&addrBC, sizeof(addrBC));
			sendto(static_cast<SOCKET>(s), buf, len, 0, (sockaddr *)&addrLoop, sizeof(addrLoop));
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
		
		// 进入正常广播循环（每5秒一次）
		while (_running && _broadcastActive)
		{
			char buf[256];
			int len;
			
			// 构造广播消息
			if (_nodeName.empty())
				len = std::snprintf(buf, sizeof(buf), "DISC|%s|%u", _nodeId.c_str(), (unsigned)_tcpPort);
			else
				len = std::snprintf(buf, sizeof(buf), "DISC|%s|%u|%s", _nodeId.c_str(), (unsigned)_tcpPort, _nodeName.c_str());
			
			// 发送到广播地址和本地回环
			sendto(static_cast<SOCKET>(s), buf, len, 0, (sockaddr *)&addrBC, sizeof(addrBC));
			sendto(static_cast<SOCKET>(s), buf, len, 0, (sockaddr *)&addrLoop, sizeof(addrLoop));
			
			// 等待5秒（分10次，每次500ms，便于快速响应停止）
			for (int i = 0; i < 10 && _running && _broadcastActive; i++)
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
		
		closesock(s);
	}

	/**
	 * @brief UDP监听循环线程
	 * 
	 * 功能：
	 * 1. 绑定UDP端口并监听
	 * 2. 接收其他节点的DISC广播
	 * 3. 解析并更新对等节点表
	 * 4. 优先保留局域网IP，忽略回环地址（如果已有局域网记录）
	 * 
	 * 接收格式：DISC|nodeId|tcpPort|[nodeName]|
	 */
	void LanP2PNode::udpListenLoop()
	{
		// 创建UDP套接字
		uintptr_t s = (uintptr_t)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if ((SOCKET)s == INVALID_SOCKET)
			return;
		setReuse(s);
		
		// 绑定到发现端口
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(_discoveryPort);
		addr.sin_addr.s_addr = INADDR_ANY;  // 监听所有接口
		if (bind(static_cast<SOCKET>(s), (sockaddr *)&addr, sizeof(addr)) != 0)
		{
			closesock(s);
			return;
		}
		
		char buf[512];
		while (_running && _udpListenActive)
		{
			// 接收UDP数据包
			sockaddr_in from{};
			int fl = sizeof(from);
			int r = recvfrom(static_cast<SOCKET>(s), buf, sizeof(buf) - 1, 0, (sockaddr *)&from, &fl);
			
			// 检查是否应该停止
			if (!_udpListenActive)
				break;
			
			if (r <= 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}
			
			// 添加字符串终止符
			buf[r] = 0;
			std::string line(buf);
			
			// 解析DISC消息
			if (line.compare(0, 5, "DISC|") == 0)
			{
				// 格式：DISC|nodeId|tcpPort|[nodeName](
				size_t p1 = line.find('|', 5);                    // 第一个分隔符
				size_t p2 = line.find('|', p1 + 1);              // 第二个分隔符
				
				if (p1 != std::string::npos && p2 != std::string::npos)
				{
					// 提取字段
					std::string id = line.substr(5, p1 - 5);
					uint16_t tcpPort = (uint16_t)std::stoi(line.substr(p1 + 1));
					std::string ip = inet_ntoa(from.sin_addr);
					std::string name;
					
					// 提取可选的节点名称
					if (p2 != std::string::npos && p2 + 1 < line.size())
						name = line.substr(p2 + 1);
					
					// 忽略自己的广播
					if (id != _nodeId)
					{
						// 构造节点信息
						PeerInfo info;
						info.id = id;
						info.name = name;
						info.ip = ip;
						info.tcpPort = tcpPort;
						info.lastSeenMs = nowMs();
						std::string key = ip + ":" + std::to_string(tcpPort) + ":" + id;
						
						bool notify = false;
						{
							std::lock_guard<std::mutex> lk(_peersMutex);
							
							// 优先保留局域网IP记录，若存在则忽略相同ID的回环地址
							bool hasNonLoopForId = false;      // 是否已有非回环记录
							std::string loopKeyToErase;        // 要删除的回环记录key
							
							for (auto it = _peersByKey.begin(); it != _peersByKey.end(); ++it)
							{
								const PeerInfo &e = it->second;
								if (e.id == id)
								{
									if (e.ip != "127.0.0.1")
										hasNonLoopForId = true;  // 找到局域网记录
									else
										loopKeyToErase = it->first;  // 记录回环key
								}
							}
							
							// 如果当前是回环地址，但已有局域网记录，则忽略
							if (ip == "127.0.0.1" && hasNonLoopForId)
							{
								// 忽略回环地址
							}
							else
							{
								// 如果当前是局域网地址，删除旧的回环记录
								if (ip != "127.0.0.1" && !loopKeyToErase.empty())
									_peersByKey.erase(loopKeyToErase);
								
								// 更新或添加节点记录
								_peersByKey[key] = info;
								notify = true;
							}
						}
						
						// 触发发现回调
						if (notify && _onPeerDiscovered)
							_onPeerDiscovered(info);
					}
				}
			}
		}
		
		closesock(s);
	}

	// ========== 游戏数据发送函数 ==========

	/**
	 * @brief 发送游戏落子（带重试）
	 * 
	 * 创建短连接发送MOVE消息，最多重试3次
	 * 
	 * @param peerIp 对方IP
	 * @param peerTcpPort 对方TCP端口
	 * @param x X坐标
	 * @param y Y坐标
	 * @param z Z坐标
	 * @return true 发送成功，false 失败
	 */
	bool LanP2PNode::sendGameMove(const std::string &peerIp, uint16_t peerTcpPort, int x, int y, int z)
	{
		for (int attempt = 0; attempt < _maxSendRetries; ++attempt)
		{
			// 创建TCP套接字
			uintptr_t s = (uintptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if ((SOCKET)s == INVALID_SOCKET)
				return false;
			
			// 连接到对方
			sockaddr_in addr{};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(peerTcpPort);
			addr.sin_addr.s_addr = inet_addr(peerIp.c_str());
			if (connect(static_cast<SOCKET>(s), (sockaddr *)&addr, sizeof(addr)) != 0)
			{
				closesock(s);
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}
			
			// 构造MOVE消息：MOVE|x|y|z|
			std::ostringstream oss;
			oss << "MOVE|" << x << "|" << y << "|" << z << "|";
			
			// 发送消息
			bool ok = tcpSendFramed(s, oss.str());
			closesock(s);
			
			if (ok)
				return true;
			
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		return false;
	}

	/**
	 * @brief 发送棋盘状态同步（带重试）
	 * 
	 * 创建短连接发送SYNC消息，最多重试3次
	 * 
	 * @param peerIp 对方IP
	 * @param peerTcpPort 对方TCP端口
	 * @param boardState 序列化的棋盘状态
	 * @return true 发送成功，false 失败
	 */
	bool LanP2PNode::sendBoardState(const std::string &peerIp, uint16_t peerTcpPort, const std::string &boardState)
	{
		for (int attempt = 0; attempt < _maxSendRetries; ++attempt)
		{
			// 创建TCP套接字
			uintptr_t s = (uintptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if ((SOCKET)s == INVALID_SOCKET)
				return false;
			
			// 连接到对方
			sockaddr_in addr{};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(peerTcpPort);
			addr.sin_addr.s_addr = inet_addr(peerIp.c_str());
			if (connect(static_cast<SOCKET>(s), (sockaddr *)&addr, sizeof(addr)) != 0)
			{
				closesock(s);
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}
			
			// 构造SYNC消息：SYNC|nodeId|boardState|
			std::string payload = "SYNC|" + _nodeId + "|" + boardState + "|";
			
			// 发送消息
			bool ok = tcpSendFramed(s, payload);
			closesock(s);
			
			if (ok)
				return true;
			
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		return false;
	}

	// ========== TCP网络线程 ==========

	/**
	 * @brief TCP监听循环线程
	 * 
	 * 功能：
	 * 1. 绑定TCP端口（如果指定端口不可用，尝试后续端口，最多尝试128次）
	 * 2. 监听连接请求
	 * 3. 为每个新连接创建独立线程处理
	 */
	void LanP2PNode::tcpListenLoop()
	{
		// 创建TCP套接字
		uintptr_t s = (uintptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if ((SOCKET)s == INVALID_SOCKET)
			return;
		setReuse(s);

		uint16_t chosen = 0;
		
		// 如果未指定端口，使用系统自动分配
		if (_tcpPort == 0)
		{
			sockaddr_in addr{};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(0);  // 0表示自动分配
			addr.sin_addr.s_addr = INADDR_ANY;
			
			if (bind(static_cast<SOCKET>(s), (sockaddr *)&addr, sizeof(addr)) == 0)
			{
				// 获取实际分配的端口
				int len = sizeof(addr);
				if (getsockname(static_cast<SOCKET>(s), (sockaddr *)&addr, &len) == 0)
				{
					chosen = ntohs(addr.sin_port);
				}
			}
		}
		else
		{
			// 尝试绑定指定端口（如果失败，尝试后续端口）
			for (int attempt = 0; attempt < 128; ++attempt)
			{
				uint16_t portTry = (uint16_t)(_tcpPort + attempt);
				sockaddr_in addr{};
				addr.sin_family = AF_INET;
				addr.sin_port = htons(portTry);
				addr.sin_addr.s_addr = INADDR_ANY;
				
				if (bind(static_cast<SOCKET>(s), (sockaddr *)&addr, sizeof(addr)) == 0)
				{
					chosen = portTry;
					break;
				}
			}
		}
		
		// 绑定失败
		if (chosen == 0)
		{
			closesock(s);
			return;
		}
		
		// 更新TCP端口并设置绑定完成标志
		_tcpPort = chosen;
		_tcpBoundReady.store(true);

		// 开始监听（最多8个等待连接）
		if (listen(static_cast<SOCKET>(s), 8) != 0)
		{
			closesock(s);
			return;
		}
		
		// 主循环：接受连接
		while (_running && _tcpActive)
		{
			sockaddr_in cli{};
			int cl = sizeof(cli);
			uintptr_t c = (uintptr_t)accept(static_cast<SOCKET>(s), (sockaddr *)&cli, &cl);
			
			if ((SOCKET)c == INVALID_SOCKET)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}
			
			// 获取客户端IP并创建处理线程（分离线程，自动管理生命周期）
			std::string rip = inet_ntoa(cli.sin_addr);
			std::thread(&LanP2PNode::tcpConnectionHandler, this, c, rip).detach();
		}
		
		closesock(s);
	}

	/**
	 * @brief 根据IP和ID查找对等节点的TCP端口
	 * 
	 * @param ip IP地址
	 * @param id 节点ID
	 * @return TCP端口，找不到返回0
	 */
	uint16_t LanP2PNode::findPeerTcpPort(const std::string &ip, const std::string &id)
	{
		std::lock_guard<std::mutex> lk(_peersMutex);
		for (auto& kv : _peersByKey)
		{
			const PeerInfo &p = kv.second;
			if (p.ip == ip && p.id == id)
				return p.tcpPort;
		}
		return 0;
	}

	/**
	 * @brief TCP连接处理函数（独立线程）
	 * 
	 * 功能：
	 * 1. 设置接收超时（500ms）
	 * 2. 循环接收TCP帧
	 * 3. 解析协议并调用相应回调
	 * 
	 * 支持的协议：
	 * - REQ|fromId|fromPort|matchId|[toId]|[fromName]|  (匹配请求)
	 * - RESP|fromId|matchId|1/0|                         (匹配响应)
	 * - INT|fromId|matchId|                              (匹配中断)
	 * - HB|fromId|matchId|                               (心跳)
	 * - MOVE|x|y|z|                                      (游戏落子)
	 * - SYNC|fromId|boardState|                          (棋盘同步)
	 * 
	 * @param sock 套接字句柄
	 * @param remoteIp 远程IP地址
	 */
	void LanP2PNode::tcpConnectionHandler(uintptr_t sock, std::string remoteIp)
	{
		// 设置接收超时（500ms）
		{
			int timeoutMs = 500;
			setsockopt(static_cast<SOCKET>(sock), SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeoutMs, sizeof(timeoutMs));
		}
		
		std::string payload;
		
		// 主循环：接收并处理消息
		while (_running && tcpRecvFramed(sock, payload))
		{
			const uint64_t ts = nowMs();
			
			// ========== 处理匹配请求 ==========
			if (payload.compare(0, 4, "REQ|") == 0)
			{
				// 格式：REQ|fromId|fromPort|matchId|[toId]|[fromName]|
				size_t p1 = payload.find('|', 4);
				size_t p2 = (p1 != std::string::npos) ? payload.find('|', p1 + 1) : std::string::npos;
				size_t p3 = (p2 != std::string::npos) ? payload.find('|', p2 + 1) : std::string::npos;
				size_t p4 = (p3 != std::string::npos) ? payload.find('|', p3 + 1) : std::string::npos;
				size_t p5 = (p4 != std::string::npos) ? payload.find('|', p4 + 1) : std::string::npos;
				
				if (p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos)
				{
					// 解析字段
					std::string fromId = payload.substr(4, p1 - 4);
					uint16_t fromPort = 0;
					try
					{
						fromPort = (uint16_t)std::stoi(payload.substr(p1 + 1, p2 - (p1 + 1)));
					}
					catch (...) { fromPort = 0; }
					std::string matchId = payload.substr(p2 + 1, p3 - (p2 + 1));
					std::string toId;
					std::string fromName;
					if (p4 != std::string::npos)
					{
						toId = payload.substr(p3 + 1, p4 - (p3 + 1));
						if (p5 != std::string::npos)
							fromName = payload.substr(p4 + 1, p5 - (p4 + 1));
					}
					
					// 忽略自身请求
					if (fromId == _nodeId) continue;
					
					// 如果指定了目标ID但不是本节点，忽略
					if (!toId.empty() && toId != _nodeId) continue;
					
					// 用消息更新/插入对端表
					PeerInfo piMsg;
					piMsg.id = fromId;
					piMsg.name = fromName;
					piMsg.ip = remoteIp;
					piMsg.tcpPort = fromPort;
					piMsg.lastSeenMs = ts;
					{
						std::lock_guard<std::mutex> lk(_peersMutex);
						std::string key = piMsg.ip + ":" + std::to_string(piMsg.tcpPort) + ":" + piMsg.id;
						_peersByKey[key] = piMsg;
					}
					
					// 触发回调
					if (_onMatchRequest) 
						_onMatchRequest(piMsg, matchId);
					
					// 标记匹配为活跃
					markMatchActive(remoteIp, fromPort, fromId, matchId);
				}
			}
			// ========== 处理匹配响应 ==========
			else if (payload.compare(0, 5, "RESP|") == 0)
			{
				// 格式：RESP|fromId|matchId|1/0|
			 size_t p1 = payload.find('|', 5);
			 size_t p2 = payload.find('|', p1 + 1);
			 size_t p3 = payload.find('|', p2 + 1);
			 
			 if (p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos)
			 {
				 std::string fromId = payload.substr(5, p1 - 5);
				 std::string matchId = payload.substr(p1 + 1, p2 - (p1 + 1));
				 bool accepted = payload.substr(p2 + 1, p3 - (p2 + 1)) == "1";
			 
				 // 查找对方TCP端口
				 uint16_t ptcp = findPeerTcpPort(remoteIp, fromId);
			 
				 // 构造PeerInfo
				 PeerInfo pi;
				 pi.id = fromId;
				 pi.ip = remoteIp;
				 pi.tcpPort = ptcp;
				 pi.lastSeenMs = ts;
			 
				 // 从对等节点表中获取名称
				 {
					 std::lock_guard<std::mutex> lk(_peersMutex);
					 for (auto& kv : _peersByKey)
					 {
						 const PeerInfo &pr = kv.second;
						 if (pr.ip == remoteIp && pr.id == fromId)
						 {
							 pi.name = pr.name;
							 break;
						 }
					 }
				 }
			 
				 // 触发回调
				 if (_onMatchResponse)
					 _onMatchResponse(pi, accepted, matchId);
			 
				 // 如果拒绝，清理匹配状态
				 if (!accepted)
				 {
					 clearMatch(remoteIp, ptcp, fromId, matchId, false);
				 }
			 }
			}
			// ========== 处理匹配中断 ==========
			else if (payload.compare(0, 4, "INT|") == 0)
			{
				// 格式：INT|fromId|matchId|
				size_t p1 = payload.find('|', 4);
				size_t p2 = payload.find('|', p1 + 1);
				
				if (p1 != std::string::npos && p2 != std::string::npos)
				{
					std::string fromId = payload.substr(4, p1 - 4);
					if (fromId == _nodeId)
						continue;
					std::string matchId = payload.substr(p1 + 1, p2 - (p1 + 1));
					
					// 查找对方TCP端口
					uint16_t ptcp = findPeerTcpPort(remoteIp, fromId);
					
					// 构造PeerInfo
					PeerInfo pi;
					pi.id = fromId;
					pi.ip = remoteIp;
					pi.tcpPort = ptcp;
					pi.lastSeenMs = ts;
					
					// 从对等节点表中获取名称
					{
						std::lock_guard<std::mutex> lk(_peersMutex);
						for (auto& kv : _peersByKey)
						{
							const PeerInfo &pr = kv.second;
							if (pr.ip == remoteIp && pr.id == fromId)
							{
								pi.name = pr.name;
								break;
							}
						}
					}
					
					// 触发回调
					if (_onMatchInterrupted)
						_onMatchInterrupted(pi, matchId);
					
					// 清理匹配状态
					clearMatch(remoteIp, ptcp, fromId, matchId, false);
				}
			}
			// ========== 处理心跳 ==========
			else if (payload.compare(0, 3, "HB|") == 0)
			{
				// 格式：HB|fromId|matchId|
				size_t p1 = payload.find('|', 3);
				size_t p2 = (p1 != std::string::npos) ? payload.find('|', p1 + 1) : std::string::npos;
				
				if (p1 != std::string::npos && p2 != std::string::npos)
				{
					std::string fromId = payload.substr(3, p1 - 3);
					std::string matchId = payload.substr(p1 + 1, p2 - (p1 + 1));
					std::string key = remoteIp + ":" + std::to_string(findPeerTcpPort(remoteIp, fromId)) + ":" + fromId;
					
					// 更新心跳时间
					{
						std::lock_guard<std::mutex> lk(_peersMutex);
						auto it = _matchesByKey.find(key);
						if (it != _matchesByKey.end() && it->second.matchId == matchId)
							it->second.lastHbMs = nowMs();
					}
				}
			}
			// ========== 处理游戏落子 ==========
			else if (payload.compare(0, 5, "MOVE|") == 0)
			{
				// 格式：MOVE|x|y|z|
				size_t p1 = payload.find('|', 5);
				size_t p2 = (p1 != std::string::npos) ? payload.find('|', p1 + 1) : std::string::npos;
				size_t p3 = (p2 != std::string::npos) ? payload.find('|', p2 + 1) : std::string::npos;
				
				if (p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos)
				{
					try
					{
						// 解析坐标
						int x = std::stoi(payload.substr(5, p1 - 5));
						int y = std::stoi(payload.substr(p1 + 1, p2 - (p1 + 1)));
						int z = std::stoi(payload.substr(p2 + 1, p3 - (p2 + 1)));

						// 查找发送者信息
						PeerInfo pi;
						pi.ip = remoteIp;
						pi.lastSeenMs = ts;
						{
							std::lock_guard<std::mutex> lk(_peersMutex);
							for (auto& kv : _peersByKey)
							{
								const PeerInfo &p = kv.second;
								if (p.ip == remoteIp)
								{
									pi = p;
									break;
								}
							}
						}
						
						// 触发回调
						if (_onGameMove)
						{
							_onGameMove(pi, x, y, z);
						}
					}
					catch (...) { }
				}
			}
			// ========== 处理棋盘同步 ==========
			else if (payload.compare(0, 5, "SYNC|") == 0)
			{
				// 格式：SYNC|fromId|boardState|
				size_t p1 = payload.find('|', 5);
				size_t p2 = payload.find('|', p1 + 1);
				
				if (p1 != std::string::npos && p2 != std::string::npos)
				{
					std::string fromId = payload.substr(5, p1 - 5);
					std::string boardData = payload.substr(p1 + 1, p2 - (p1 + 1));
					
					// 查找对方TCP端口
					uint16_t ptcp = findPeerTcpPort(remoteIp, fromId);
					
					// 构造PeerInfo
					PeerInfo pi;
					pi.id = fromId;
					pi.ip = remoteIp;
					pi.tcpPort = ptcp;
					pi.lastSeenMs = ts;
					
					// 从对等节点表中获取名称
					{
						std::lock_guard<std::mutex> lk(_peersMutex);
						for (auto& kv : _peersByKey)
						{
							const PeerInfo &pr = kv.second;
							if (pr.ip == remoteIp && pr.id == fromId)
							{
								pi.name = pr.name;
								break;
							}
						}
					}
					
					// 触发回调
					if (_onBoardSync)
						_onBoardSync(pi, boardData);
				}
			}
		}
		
		closesock(sock);
	}

	// ========== TCP帧协议 ==========

	/**
	 * @brief 发送带长度前缀的TCP帧
	 * 
	 * 帧格式：[4字节长度(网络序)][payload数据]
	 * 
	 * @param sock 套接字
	 * @param payload 数据内容
	 * @return true 发送成功，false 失败
	 */
	bool LanP2PNode::tcpSendFramed(uintptr_t sock, const std::string &payload)
	{
		// 准备长度前缀（4字节网络序）
		uint32_t n = (uint32_t)payload.size();
		uint32_t be = htonl(n);
		int off = 0;
		int r = 0;
		
		// 发送长度前缀
		while (off < 4)
		{
			r = send(static_cast<SOCKET>(sock), ((const char *)&be) + off, 4 - off, 0);
			if (r <= 0)
				return false;
			off += r;
		}
		
		// 发送payload数据
		off = 0;
		while (off < (int)n)
		{
			r = send(static_cast<SOCKET>(sock), payload.data() + off, (int)n - off, 0);
			if (r <= 0)
				return false;
			off += r;
		}
		
		return true;
	}

	/**
	 * @brief 接收带长度前缀的TCP帧
	 * 
	 * @param sock 套接字
	 * @param outPayload 输出参数，接收到的数据
	 * @return true 接收成功，false 失败或连接断开
	 */
	bool LanP2PNode::tcpRecvFramed(uintptr_t sock, std::string& outPayload)
	{
		// 接收4字节长度前缀
		uint32_t be = 0; 
		int got = 0; 
		int r = 0;
		while (got < 4) 
		{ 
			r = recv(static_cast<SOCKET>(sock), ((char*)&be) + got, 4 - got, 0); 
			if (r <= 0) 
				return false; 
			got += r; 
		}
		
		// 转换为主机序
		uint32_t n = ntohl(be);
		
		// 检查长度是否合法（防止恶意数据）
		const uint32_t MAX_FRAME_SIZE = 8192;
		if (n > MAX_FRAME_SIZE) 
			return false;
		
		// 接收payload数据
		outPayload.resize(n);
		int off = 0;
		while (off < (int)n) 
		{ 
			r = recv(static_cast<SOCKET>(sock), &outPayload[off], (int)n - off, 0); 
			if (r <= 0) 
				return false; 
			off += r; 
		}
		
		return true;
	}

	// ========== 匹配相关发送函数 ==========

	/**
	 * @brief 发送匹配请求（带重试）
	 * 
	 * @param peerIp 对方IP
	 * @param peerTcpPort 对方TCP端口
	 * @param matchId 匹配ID
	 * @return true 发送成功，false 失败
	 */
	bool LanP2PNode::sendMatchRequest(const std::string &peerIp, uint16_t peerTcpPort, const std::string &matchId)
	{
		for (int attempt = 0; attempt < _maxSendRetries; ++attempt)
		{
			// 创建TCP套接字
			uintptr_t s = (uintptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if ((SOCKET)s == INVALID_SOCKET)
				return false;
			
			// 连接到对方
			sockaddr_in addr{};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(peerTcpPort);
			addr.sin_addr.s_addr = inet_addr(peerIp.c_str());
			if (connect(static_cast<SOCKET>(s), (sockaddr *)&addr, sizeof(addr)) != 0)
			{
				closesock(s);
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}
			
			// 查找对方ID
			std::string toId;
			{
				std::lock_guard<std::mutex> lk(_peersMutex);
				for (auto& kv : _peersByKey)
				{
					const PeerInfo &p = kv.second;
					if (p.ip == peerIp && p.tcpPort == peerTcpPort)
					{
						toId = p.id;
						break;
					}
				}
			}
			
			// 构造请求消息：REQ|fromId|fromPort|matchId|[toId]|[fromName]|
			std::ostringstream oss;
			if (toId.empty())
				oss << "REQ|" << _nodeId << "|" << _tcpPort << "|" << matchId << "||" << _nodeName << "|";
			else
				oss << "REQ|" << _nodeId << "|" << _tcpPort << "|" << matchId << "|" << toId << "|" << _nodeName << "|";
			
			// 发送消息
			bool ok = tcpSendFramed(s, oss.str());
			closesock(s);
			
			if (ok)
			{
				// 标记匹配为活跃
				if (!toId.empty())
					markMatchActive(peerIp, peerTcpPort, toId, matchId);
				return true;
			}
			
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		return false;
	}

	/**
	 * @brief 响应匹配请求（带重试）
	 * 
	 * @param peerIp 对方IP
	 * @param peerTcpPort 对方TCP端口
	 * @param matchId 匹配ID
	 * @param accept true接受，false拒绝
	 * @return true 发送成功，false 失败
	 */
	bool LanP2PNode::respondToMatch(const std::string &peerIp, uint16_t peerTcpPort, const std::string &matchId,
	                                bool accept)
	{
		// 查找对方ID
		std::string peerId;
		{
			std::lock_guard<std::mutex> lk(_peersMutex);
			for (auto& kv : _peersByKey)
			{
				const PeerInfo &p = kv.second;
				if (p.ip == peerIp && p.tcpPort == peerTcpPort)
				{
					peerId = p.id;
					break;
				}
			}
		}
		
		for (int attempt = 0; attempt < _maxSendRetries; ++attempt)
		{
			// 创建TCP套接字
			uintptr_t s = (uintptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if ((SOCKET)s == INVALID_SOCKET)
				return false;
			
			// 连接到对方
			sockaddr_in addr{};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(peerTcpPort);
			addr.sin_addr.s_addr = inet_addr(peerIp.c_str());
			if (connect(static_cast<SOCKET>(s), (sockaddr *)&addr, sizeof(addr)) != 0)
			{
				closesock(s);
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}
			
			// 构造响应消息：RESP|fromId|matchId|1/0|
			std::ostringstream oss;
			oss << "RESP|" << _nodeId << "|" << matchId << "|" << (accept ? "1" : "0") << "|";
			
			// 发送消息
			bool ok = tcpSendFramed(s, oss.str());
			closesock(s);
			
			if (ok)
			{
				// 如果接受，标记匹配为活跃
				if (accept && !peerId.empty())
					markMatchActive(peerIp, peerTcpPort, peerId, matchId);
				return true;
			}
			
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		return false;
	}

	/**
	 * @brief 发送匹配中断（带重试）
	 * 
	 * @param peerIp 对方IP
	 * @param peerTcpPort 对方TCP端口
	 * @param matchId 匹配ID
	 * @return true 发送成功，false 失败
	 */
	bool LanP2PNode::interruptMatch(const std::string &peerIp, uint16_t peerTcpPort, const std::string &matchId)
	{
		for (int attempt = 0; attempt < _maxSendRetries; ++attempt)
		{
			// 创建TCP套接字
			uintptr_t s = (uintptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if ((SOCKET)s == INVALID_SOCKET)
				return false;
			
			// 连接到对方
			sockaddr_in addr{};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(peerTcpPort);
			addr.sin_addr.s_addr = inet_addr(peerIp.c_str());
			if (connect(static_cast<SOCKET>(s), (sockaddr *)&addr, sizeof(addr)) != 0)
			{
				closesock(s);
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}
			
			// 构造中断消息：INT|fromId|matchId|
			std::ostringstream oss;
			oss << "INT|" << _nodeId << "|" << matchId << "|";
			
			// 发送消息
			bool ok = tcpSendFramed(s, oss.str());
			closesock(s);
			
			if (ok)
				return true;
			
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		return false;
	}

	/**
	 * @brief 发送TCP心跳（带重试）
	 * 
	 * @param ip 对方IP
	 * @param port 对方TCP端口
	 * @param matchId 匹配ID
	 * @return true 发送成功，false 失败
	 */
	bool LanP2PNode::sendTcpHeartbeat(const std::string &ip, uint16_t port, const std::string &matchId)
	{
		for (int attempt = 0; attempt < _maxSendRetries; ++attempt)
		{
			// 创建TCP套接字
			uintptr_t s = (uintptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if ((SOCKET)s == INVALID_SOCKET)
				return false;
			
			// 连接到对方
			sockaddr_in addr{};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(port);
			addr.sin_addr.s_addr = inet_addr(ip.c_str());
			if (connect(static_cast<SOCKET>(s), (sockaddr * )&addr, sizeof(addr)) != 0)
			{
				closesock(s);
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}
			// 构造心跳消息：HB|fromId|matchId|
			std::ostringstream oss;
			oss << "HB|" << _nodeId << "|" << matchId << "|";
			bool ok = tcpSendFramed(s, oss.str());
			closesock(s);
			if (ok)
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		return false;
	}

	// ========== 工具函数 ==========

	/**
	 * @brief 获取当前毫秒时间戳
	 * 
	 * @return 从epoch开始的毫秒数
	 */
	uint64_t LanP2PNode::nowMs()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	/**
	 * @brief 生成随机节点ID
	 * 
	 * @return 16位十六进制字符串
	 */
	std::string LanP2PNode::randomId()
	{
		std::random_device rd;
		std::mt19937_64 rng(rd());
		std::uniform_int_distribution<uint64_t> dist;
		uint64_t v = dist(rng);
		char buf[17];
		std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
		return std::string(buf);
	}

	/**
	 * @brief 生成随机匹配ID
	 * 
	 * @return 16位十六进制字符串
	 */
	std::string LanP2PNode::generateMatchId()
	{
		std::random_device rd;
		std::mt19937_64 rng(rd());
		std::uniform_int_distribution<uint64_t> dist;
		uint64_t v = dist(rng);
		char buf[17];
		std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
		return std::string(buf);
	}

	// ========== 后台维护线程 ==========

	/**
	 * @brief 后台维护循环线程
	 * 
	 * 定期执行以下任务（每5秒一次）：
	 * 1. 清理超时的对等节点（基于UDP广播超时）
	 * 2. 发送匹配心跳（每2秒一次）
	 * 3. 检测心跳超时并清理匹配（超过7秒）
	 */
	void LanP2PNode::peersMaintenanceLoop()
	{
		while (_running && _maintenanceActive)
		{
			const uint64_t now = nowMs();
			
			// ===== 1. 移除基于DISC的超时对端（保留有活跃匹配的节点）=====
			if (_peerStaleMs > 0)
			{
				std::lock_guard<std::mutex> lk(_peersMutex);
				for (auto it = _peersByKey.begin(); it != _peersByKey.end(); )
				{
					// 如果该节点有活跃匹配，跳过
					if (_matchesByKey.find(it->first) != _matchesByKey.end())
					{
						++it;
						continue;
					}
					
					// 检查是否超时
					if ((now - it->second.lastSeenMs) > _peerStaleMs)
					{
						it = _peersByKey.erase(it);
					}
					else 
						++it;
				}
			}
			
			// ===== 2. 构造匹配快照（避免持锁进行网络操作）=====
			std::vector<std::tuple<std::string, std::string, uint16_t, std::string, uint64_t>> matchSnapshot;
			{
				std::lock_guard<std::mutex> lk(_peersMutex);
				for (auto& kv : _matchesByKey)
				{
					const std::string &key = kv.first;
					
					// 解析key: "ip:port:id"
					size_t p1 = key.find(':');
					size_t p2 = (p1 == std::string::npos) ? std::string::npos : key.find(':', p1 + 1);
					std::string ip = (p1 == std::string::npos) ? std::string() : key.substr(0, p1);
					uint16_t port = 0;
					if (p1 != std::string::npos && p2 != std::string::npos)
					{
						try
						{
							port = (uint16_t)std::stoi(key.substr(p1 + 1, p2 - (p1 + 1)));
						}
						catch (...)
						{
							port = 0;
						}
					}
					
					matchSnapshot.emplace_back(key, ip, port, kv.second.matchId, kv.second.lastHbMs);
				}
			}
			
			// ===== 3. 根据快照发送心跳与处理超时 =====
			for (auto& t : matchSnapshot)
			{
				const std::string &key = std::get<0>(t);
				const std::string &ip = std::get<1>(t);
				uint16_t port = std::get<2>(t);
				const std::string &matchId = std::get<3>(t);
				uint64_t last = std::get<4>(t);
				const uint64_t now2 = nowMs();
				
				// 发送心跳（如果距离上次心跳超过间隔时间）
				if (_matchHeartbeatIntervalMs > 0 && (now2 - last) >= _matchHeartbeatIntervalMs)
				{
					sendTcpHeartbeat(ip, port, matchId);
					
					// 更新心跳时间
					std::lock_guard<std::mutex> lk(_peersMutex);
					auto it = _matchesByKey.find(key);
					if (it != _matchesByKey.end() && it->second.matchId == matchId)
						it->second.lastHbMs = now2;
				}
				
				// 检测心跳超时（超过阈值则清理匹配）
				if (_matchHeartbeatTimeoutMs > 0 && (now2 - last) > _matchHeartbeatTimeoutMs)
				{
					// 从key中提取peerId
					std::string peerId;
					size_t p1 = key.find(':');
					size_t p2 = (p1 == std::string::npos) ? std::string::npos : key.find(':', p1 + 1);
					if (p2 != std::string::npos)
						peerId = key.substr(p2 + 1);
					
					// 清理匹配（触发中断回调）
					clearMatch(ip, port, peerId, matchId, true);
				}
			}
			
			// ===== 4. 维持5秒节奏（细分为10次，每次500ms，便于快速响应停止）=====
			for (int i = 0; i < 10 && _running && _maintenanceActive; ++i)
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
	}

	/**
	 * @brief 在匹配表中标记对战为活跃状态
	 * 
	 * 用于心跳维护，防止匹配被超时清理
	 * 
	 * @param ip 对方IP
	 * @param tcpPort 对方TCP端口
	 * @param peerId 对方节点ID
	 * @param matchId 匹配ID
	 */
	void LanP2PNode::markMatchActive(const std::string &ip, uint16_t tcpPort, const std::string &peerId,
	                                 const std::string &matchId)
	{
		std::lock_guard<std::mutex> lk(_peersMutex);
		std::string key = ip + ":" + std::to_string(tcpPort) + ":" + peerId;
		auto &st = _matchesByKey[key];
		st.matchId = matchId;
		st.lastHbMs = nowMs();
	}

	/**
	 * @brief 清理匹配状态
	 * 
	 * 从匹配表和对等节点表中移除指定匹配
	 * 
	 * @param ip 对方IP
	 * @param tcpPort 对方TCP端口
	 * @param peerId 对方节点ID
	 * @param matchId 匹配ID
	 * @param notify 是否触发中断回调
	 */
	void LanP2PNode::clearMatch(const std::string &ip, uint16_t tcpPort, const std::string &peerId,
	                            const std::string &matchId, bool notify)
	{
		// 清理匹配表和对等节点表
		{
			std::lock_guard<std::mutex> lk(_peersMutex);
			
			// 从匹配表中移除
			_matchesByKey.erase(ip + ":" + std::to_string(tcpPort) + ":" + peerId);
			
			// 从对等节点表中移除该节点
			for (auto it = _peersByKey.begin(); it != _peersByKey.end(); )
			{
				const PeerInfo &p = it->second;
				if (p.ip == ip && p.tcpPort == tcpPort && p.id == peerId)
					it = _peersByKey.erase(it);
				else 
					++it;
			}
		}
		
		// 如果需要通知，触发中断回调
		if (notify && _onMatchInterrupted)
		{
			PeerInfo pi;
			pi.id = peerId;
			pi.ip = ip;
			pi.tcpPort = tcpPort;
			pi.lastSeenMs = nowMs();
			_onMatchInterrupted(pi, matchId);
		}
	}

} // namespace lanp2p
