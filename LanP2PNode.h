#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdint>

/**
 * @brief 局域网点对点（LAN P2P）网络通信命名空间
 * 
 * 提供局域网内节点发现、匹配、游戏数据传输等功能
 */
namespace lanp2p
{
	/**
	 * @brief 对等节点信息结构体
	 * 
	 * 用于存储和传递局域网内其他节点的信息
	 */
	struct PeerInfo
	{
		std::string id;        // 节点唯一ID（16位十六进制字符串）
		std::string name;      // 节点显示名称（可为空）
		std::string ip;        // 节点IP地址
		uint16_t tcpPort{0};   // 节点TCP监听端口
		uint64_t lastSeenMs{0};// 最后一次被发现/收到消息的时间戳（毫秒）
	};

	/**
	 * @brief 局域网P2P节点类
	 * 
	 * 核心功能：
	 * - UDP广播：定期广播自身信息，让其他节点发现
	 * - UDP监听：接收其他节点的广播，更新对等节点列表
	 * - TCP监听：接受其他节点的连接，处理匹配、游戏消息
	 * - 后台维护：清理过期节点，维持匹配心跳
	 * 
	 * 协议格式：
	 * - UDP: "DISC|nodeId|tcpPort|[name]|"
	 * - TCP: 
	 *   - REQ|fromId|fromPort|matchId|[toId]|[fromName]|  (匹配请求)
	 *   - RESP|fromId|matchId|1/0|                         (匹配响应)
	 *   - INT|fromId|matchId|                              (匹配中断)
	 *   - HB|fromId|matchId|                               (心跳)
	 *   - MOVE|x|y|z|                                      (游戏落子)
	 *   - SYNC|fromId|boardState|                          (棋盘同步)
	 */
	class LanP2PNode
	{
		public:
			// ========== 构造与析构 ==========
			
			/**
			 * @brief 构造函数
			 * 
			 * @param discoveryPort UDP发现端口（用于广播和监听）
			 * @param tcpPort TCP监听端口（0表示自动分配）
			 */
			LanP2PNode(uint16_t discoveryPort, uint16_t tcpPort);
			
			/**
			 * @brief 析构函数
			 * 
			 * 停止所有线程并清理资源
			 */
			~LanP2PNode();

			// ========== 生命周期管理 ==========
			
			/**
			 * @brief 启动全部功能
			 * 
			 * 包括：UDP广播、UDP监听、TCP监听、后台维护
			 */
			void start();
			
			/**
			 * @brief 停止全部功能并等待线程退出
			 */
			void stop();

			/**
			 * @brief 仅启动广播和TCP监听
			 * 
			 * 不启动UDP监听，适合只需要被动接受匹配的场景
			 */
			void startBroadcastOnly();
			
			/**
			 * @brief 启动UDP发现监听
			 * 
			 * 开始监听其他节点的广播，更新对等节点列表
			 */
			void startUdpListen();
			
			/**
			 * @brief 停止UDP发现监听
			 */
			void stopUdpListen();

			// ========== 回调函数设置 ==========
			
			/**
			 * @brief 设置节点发现回调
			 * 
			 * 当发现新节点或节点信息更新时调用
			 * 
			 * @param cb 回调函数 void(const PeerInfo&)
			 */
			void setOnPeerDiscovered(const std::function<void(const PeerInfo &)> &cb);
			
			/**
			 * @brief 设置收到匹配请求的回调
			 * 
			 * @param cb 回调函数 void(const PeerInfo&, const std::string& matchId)
			 */
			void setOnMatchRequest(const std::function<void(const PeerInfo &, const std::string &matchId)> &cb);
			
			/**
			 * @brief 设置收到匹配响应的回调
			 * 
			 * @param cb 回调函数 void(const PeerInfo&, bool accepted, const std::string& matchId)
			 */
			void setOnMatchResponse(const std::function<void(const PeerInfo &, bool accepted, const std::string &matchId)> &cb);
			
			/**
			 * @brief 设置匹配中断的回调
			 * 
			 * @param cb 回调函数 void(const PeerInfo&, const std::string& matchId)
			 */
			void setOnMatchInterrupted(const std::function<void(const PeerInfo &, const std::string &matchId)> &cb);
			
			/**
			 * @brief 设置收到对手落子的回调
			 * 
			 * @param cb 回调函数 void(const PeerInfo&, int x, int y, int z)
			 */
			void setOnGameMove(const std::function<void(const PeerInfo &, int x, int y, int z)> &cb);
			
			/**
			 * @brief 设置收到棋盘同步的回调
			 * 
			 * @param cb 回调函数 void(const PeerInfo&, const std::string& boardState)
			 */
			void setOnBoardSync(const std::function<void(const PeerInfo &, const std::string &boardState)> &cb);

			// ========== 业务操作（网络发送） ==========
			
			/**
			 * @brief 发送匹配请求
			 * 
			 * @param peerIp 对方IP地址
			 * @param peerTcpPort 对方TCP端口
			 * @param matchId 匹配ID
			 * @return true 发送成功，false 失败
			 */
			bool sendMatchRequest(const std::string &peerIp, uint16_t peerTcpPort, const std::string &matchId);
			
			/**
			 * @brief 响应匹配请求
			 * 
			 * @param peerIp 对方IP地址
			 * @param peerTcpPort 对方TCP端口
			 * @param matchId 匹配ID
			 * @param accept true接受，false拒绝
			 * @return true 发送成功，false 失败
			 */
			bool respondToMatch(const std::string &peerIp, uint16_t peerTcpPort, const std::string &matchId, bool accept);
			
			/**
			 * @brief 发送匹配中断消息
			 * 
			 * @param peerIp 对方IP地址
			 * @param peerTcpPort 对方TCP端口
			 * @param matchId 匹配ID
			 * @return true 发送成功，false 失败
			 */
			bool interruptMatch(const std::string &peerIp, uint16_t peerTcpPort, const std::string &matchId);
			
			/**
			 * @brief 发送游戏落子
			 * 
			 * @param peerIp 对方IP地址
			 * @param peerTcpPort 对方TCP端口
			 * @param x X坐标
			 * @param y Y坐标
			 * @param z Z坐标
			 * @return true 发送成功，false 失败
			 */
			bool sendGameMove(const std::string &peerIp, uint16_t peerTcpPort, int x, int y, int z);
			
			/**
			 * @brief 发送棋盘状态同步
			 * 
			 * @param peerIp 对方IP地址
			 * @param peerTcpPort 对方TCP端口
			 * @param boardState 序列化的棋盘状态
			 * @return true 发送成功，false 失败
			 */
			bool sendBoardState(const std::string &peerIp, uint16_t peerTcpPort, const std::string &boardState);

			// ========== 查询接口 ==========
			
			/**
			 * @brief 获取当前活跃的对等节点列表快照
			 * 
			 * 自动移除超时的节点（基于 _peerStaleMs 设置）
			 * 
			 * @return 节点信息列表
			 */
			std::vector<PeerInfo> getPeersSnapshot();

			/**
			 * @brief 获取本节点ID
			 */
			std::string getNodeId() const
			{
				return _nodeId;
			}
			
			/**
			 * @brief 获取本节点TCP端口
			 */
			uint16_t getTcpPort() const
			{
				return _tcpPort;
			}
			
			/**
			 * @brief 获取UDP发现端口
			 */
			uint16_t getDiscoveryPort() const
			{
				return _discoveryPort;
			}

			// ========== 配置接口 ==========
			
			/**
			 * @brief 设置节点显示名称
			 * 
			 * @param name 显示名称（会在UDP广播中发送）
			 */
			void setNodeName(const std::string &name)
			{
				_nodeName = name;
			}
			
			/**
			 * @brief 获取节点显示名称
			 */
			std::string getNodeName() const
			{
				return _nodeName;
			}

			/**
			 * @brief 设置节点过期时间（毫秒）
			 * 
			 * 超过此时间未收到UDP广播的节点将被移除
			 * 
			 * @param ms 毫秒数（0表示不过期）
			 */
			void setPeerStaleMs(uint64_t ms)
			{
				_peerStaleMs = ms;
			}
			
			/**
			 * @brief 设置匹配心跳发送间隔（毫秒）
			 * 
			 * @param ms 毫秒数
			 */
			void setMatchHeartbeatIntervalMs(uint64_t ms)
			{
				_matchHeartbeatIntervalMs = ms;
			}
			
			/**
			 * @brief 设置匹配心跳超时阈值（毫秒）
			 * 
			 * 超过此时间未收到心跳，匹配将被清理
			 * 
			 * @param ms 毫秒数
			 */
			void setMatchHeartbeatTimeoutMs(uint64_t ms)
			{
				_matchHeartbeatTimeoutMs = ms;
			}

			/**
			 * @brief 设置网络发送失败时的重试次数
			 * 
			 * @param r 重试次数（必须>0）
			 */
			void setMaxSendRetries(int r)
			{
				if (r > 0)
					_maxSendRetries = r;
			}
			
			/**
			 * @brief 获取当前重试次数设置
			 */
			int getMaxSendRetries() const
			{
				return _maxSendRetries;
			}

			// ========== 内部辅助接口 ==========
			
			/**
			 * @brief 标记匹配为活跃状态
			 * 
			 * 将指定匹配加入活跃表，用于心跳维护
			 * 
			 * @param ip 对方IP
			 * @param tcpPort 对方TCP端口
			 * @param peerId 对方节点ID
			 * @param matchId 匹配ID
			 */
			void markMatchActive(const std::string &ip, uint16_t tcpPort, const std::string &peerId, const std::string &matchId);

			/**
			 * @brief 生成随机匹配ID
			 * 
			 * @return 16位十六进制字符串
			 */
			static std::string generateMatchId();

		private:
			// ========== 线程函数 ==========
			
			/**
			 * @brief UDP广播循环
			 * 
			 * 定期广播本节点信息（初始5次快速广播，之后每5秒一次）
			 */
			void udpBroadcastLoop();
			
			/**
			 * @brief UDP监听循环
			 * 
			 * 接收其他节点的DISC广播，更新对等节点表
			 */
			void udpListenLoop();
			
			/**
			 * @brief TCP监听循环
			 * 
			 * 监听TCP端口，接受连接并为每个连接创建处理线程
			 */
			void tcpListenLoop();
			
			/**
			 * @brief TCP连接处理函数
			 * 
			 * 在独立线程中运行，解析协议消息并调用相应回调
			 * 
			 * @param sock 套接字句柄
			 * @param remoteIp 远程IP地址
			 */
			void tcpConnectionHandler(uintptr_t sock, std::string remoteIp);
			
			/**
			 * @brief 后台维护循环
			 * 
			 * 定期执行：
			 * 1. 清理过期的对等节点
			 * 2. 发送匹配心跳
			 * 3. 检测心跳超时并清理匹配
			 */
			void peersMaintenanceLoop();

			// ========== TCP帧协议 ==========
			
			/**
			 * @brief 发送带长度前缀的TCP帧
			 * 
			 * 格式：[4字节网络序长度][payload数据]
			 * 
			 * @param sock 套接字
			 * @param payload 数据内容
			 * @return true 发送成功，false 失败
			 */
			bool tcpSendFramed(uintptr_t sock, const std::string &payload);
			
			/**
			 * @brief 接收带长度前缀的TCP帧
			 * 
			 * @param sock 套接字
			 * @param outPayload 输出参数，接收到的数据
			 * @return true 接收成功，false 失败或连接断开
			 */
			bool tcpRecvFramed(uintptr_t sock, std::string &outPayload);

			// ========== 工具函数 ==========
			
			/**
			 * @brief 获取当前时间戳（毫秒）
			 */
			static uint64_t nowMs();
			
			/**
			 * @brief 生成随机节点ID
			 * 
			 * @return 16位十六进制字符串
			 */
			static std::string randomId();

			/**
			 * @brief 根据 IP+ID 查找对等节点的TCP端口
			 * 
			 * @param ip IP地址
			 * @param id 节点ID
			 * @return TCP端口，找不到返回0
			 */
			uint16_t findPeerTcpPort(const std::string &ip, const std::string &id);

			/**
			 * @brief 清理匹配状态
			 * 
			 * 从匹配表和对等节点表中移除指定匹配
			 * 
			 * @param ip IP地址
			 * @param tcpPort TCP端口
			 * @param peerId 节点ID
			 * @param matchId 匹配ID
			 * @param notify 是否触发中断回调
			 */
			void clearMatch(const std::string &ip, uint16_t tcpPort, const std::string &peerId, const std::string &matchId,
			                bool notify);
			
			/**
			 * @brief 发送TCP心跳
			 * 
			 * @param ip IP地址
			 * @param port TCP端口
			 * @param matchId 匹配ID
			 * @return true 发送成功，false 失败
			 */
			bool sendTcpHeartbeat(const std::string &ip, uint16_t port, const std::string &matchId);

		private:
			// ========== 基础配置 ==========
			
			uint16_t _discoveryPort{0};  // UDP发现端口
			uint16_t _tcpPort{0};        // TCP监听端口
			std::string _nodeId;         // 本节点唯一ID
			std::string _nodeName;       // 本节点显示名称

			// ========== 模块运行状态与线程 ==========
			
			std::atomic<bool> _running{false};          // 总运行标志
			std::atomic<bool> _broadcastActive{false};  // UDP广播线程活跃标志
			std::atomic<bool> _udpListenActive{false};  // UDP监听线程活跃标志
			std::atomic<bool> _tcpActive{false};        // TCP监听线程活跃标志
			std::atomic<bool> _tcpBoundReady{false};    // TCP端口已绑定标志
			std::thread _udpBroadcaster;                // UDP广播线程
			std::thread _udpListener;                   // UDP监听线程
			std::thread _tcpListener;                   // TCP监听线程

			// 后台维护线程（对等节点过期清理、匹配心跳维持）
			std::atomic<bool> _maintenanceActive{false};
			std::thread _maintenanceThread;

			// ========== 事件回调 ==========
			
			std::function<void(const PeerInfo &)> _onPeerDiscovered;                       // 发现节点
			std::function<void(const PeerInfo &, const std::string &)> _onMatchRequest;    // 收到匹配请求
			std::function<void(const PeerInfo &, bool, const std::string &)> _onMatchResponse; // 收到匹配响应
			std::function<void(const PeerInfo &, const std::string &)> _onMatchInterrupted;    // 匹配中断
			std::function<void(const PeerInfo &, int x, int y, int z)> _onGameMove;        // 收到落子
			std::function<void(const PeerInfo &, const std::string &)> _onBoardSync;       // 收到棋盘同步

			// ========== 对等节点管理 ==========
			
			std::mutex _peersMutex;                                     // 保护 _peersByKey 和 _matchesByKey
			std::unordered_map<std::string, PeerInfo> _peersByKey;     // key格式: "ip:port:id"
			uint64_t _peerStaleMs{15000};                               // 节点过期阈值（毫秒，基于UDP发现）

			// ========== 匹配状态管理 ==========
			
			/**
			 * @brief 匹配状态结构体
			 */
			struct MatchState
			{
				std::string matchId;   // 匹配ID
				uint64_t lastHbMs{0};  // 最后心跳时间（毫秒）
			};
			std::unordered_map<std::string, MatchState> _matchesByKey;  // key格式: "ip:port:id"
			uint64_t _matchHeartbeatIntervalMs{2000};                   // 心跳发送间隔（毫秒）
			uint64_t _matchHeartbeatTimeoutMs{7000};                    // 心跳超时阈值（毫秒）

			// ========== 网络配置 ==========
			
			int _maxSendRetries{3};  // 网络发送失败时的最大重试次数
	};
}
