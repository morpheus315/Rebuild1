#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include "LanP2PNode.h"

/**
 * @brief 在线3D五子棋客户端类
 * 
 * 负责管理游戏客户端的所有功能：
 * - 匹配管理（发起、接受、拒绝匹配请求）
 * - 游戏状态管理（棋盘、回合、胜负）
 * - 网络通信（落子同步、棋盘同步）
 * - 多线程管理（请求超时处理、定时同步）
 */
class Client
{
	public:
		// ========== 构造与析构 ==========
		
		/**
		 * @brief 构造函数
		 * 
		 * 初始化客户端并注册网络回调
		 * 
		 * @param node 局域网P2P节点引用
		 */
		Client(lanp2p::LanP2PNode &node);
		
		/**
		 * @brief 析构函数
		 * 
		 * 清理资源、停止线程、断开匹配
		 */
		~Client();

		// 禁止拷贝构造和赋值操作
		Client(const Client &) = delete;
		Client &operator=(const Client &) = delete;

		// ========== 匹配管理 ==========
		
		/**
		 * @brief 获取当前可用的对等节点列表
		 * @return 可用节点的信息列表
		 */
		std::vector<lanp2p::PeerInfo> getAvailablePeers();
		
		/**
		 * @brief 向指定节点发送匹配请求
		 * @param peer 目标节点信息
		 * @return true 请求发送成功，false 已在匹配中或发送失败
		 */
		bool requestMatch(const lanp2p::PeerInfo &peer);

		/**
		 * @brief 检查是否正在匹配中
		 * @return true 正在匹配，false 未匹配
		 */
		bool isInMatch() const;
		
		/**
		 * @brief 主动结束当前匹配
		 * 
		 * 向对方发送中断消息，清理游戏状态
		 */
		void endMatch();

		/**
		 * @brief 获取当前匹配ID
		 * @return 匹配ID字符串
		 */
		std::string getMatchId() const;
		
		/**
		 * @brief 获取对手节点信息
		 * @return 对手的PeerInfo
		 */
		lanp2p::PeerInfo getMatchPeer() const;

		/**
		 * @brief 待处理的匹配请求信息
		 */
		struct PendingRequestInfo
		{
			std::string matchId;      // 匹配ID
			lanp2p::PeerInfo peer;    // 请求者信息
			std::string ip;           // 请求者IP
			uint16_t port{ 0 };       // 请求者端口
		};
		
		/**
		 * @brief 获取所有待处理请求的快照
		 * @return 待处理请求列表
		 */
		std::vector<PendingRequestInfo> getPendingRequestsSnapshot();
		
		/**
		 * @brief 响应待处理的匹配请求
		 * @param req 请求信息
		 * @param accept true接受，false拒绝
		 * @return true 响应成功，false 请求已过期
		 */
		bool respondToPendingRequest(const PendingRequestInfo &req, bool accept);

		// ========== 游戏状态查询 ==========
		
		/**
		 * @brief 检查是否轮到自己下棋
		 * @return true 轮到自己，false 轮到对手
		 */
		bool isMyTurn() const;
		
		/**
		 * @brief 获取自己的玩家标识
		 * @return '1' 或 '2'
		 */
		char getMyPlayer() const;
		
		/**
		 * @brief 检查游戏是否正在运行
		 * @return true 游戏进行中，false 游戏已结束
		 */
		bool isGameRunning() const;
		
		/**
		 * @brief 获取游戏结果
		 * @return 0未结束，1己方胜利，2对方胜利
		 */
		int getGameResult() const;

		// ========== 游戏操作 ==========
		
		/**
		 * @brief 尝试在指定位置放置己方棋子
		 * @param x X坐标
		 * @param y Y坐标
		 * @param z Z坐标
		 * @return true 落子成功，false 落子失败（不是自己回合/位置非法等）
		 */
		bool tryPlaceMyPiece(int x, int y, int z);
		
		/**
		 * @brief 尝试获取对手的落子
		 * @param outX 输出X坐标
		 * @param outY 输出Y坐标
		 * @param outZ 输出Z坐标
		 * @return true 获取到对手落子，false 对手未落子
		 */
		bool tryGetOpponentMove(int& outX, int& outY, int& outZ);
		
		/**
		 * @brief 初始化游戏状态
		 * 
		 * 创建新棋盘，确定先后手，启动同步线程
		 */
		void initGameState();
		
		/**
		 * @brief 获取距离上次同步的秒数
		 * @return 秒数
		 */
		int getTimeSinceLastSync() const;

	private:
		// ========== 网络相关 ==========
		
		lanp2p::LanP2PNode &_node;  // 局域网P2P通信节点

		/**
		 * @brief 匹配状态结构体
		 */
		struct MatchState
		{
			bool inMatch{ false };      // 是否在匹配中
			std::string matchId;        // 匹配ID
			lanp2p::PeerInfo peer;      // 对手信息
		};
		MatchState _match;              // 当前匹配状态
		mutable std::mutex _matchMutex; // 匹配状态互斥锁

		/**
		 * @brief 待处理的匹配请求结构体
		 */
		struct PendingRequest
		{
			bool has{ false };          // 是否有效
			std::string ip;             // 请求者IP
			uint16_t port{ 0 };         // 请求者端口
			std::string matchId;        // 匹配ID
			lanp2p::PeerInfo peer;      // 请求者信息
			std::chrono::steady_clock::time_point ts;  // 请求时间戳
		};

		std::mutex _pendingMutex;                      // 待处理请求队列互斥锁
		std::deque<PendingRequest> _pendingQueue;      // 待处理请求队列
		std::atomic<bool> _timeoutThreadRunning{ false }; // 超时线程运行标志
		std::thread _timeoutThread;                    // 超时处理线程
		const std::chrono::seconds _requestTimeout{ 30 }; // 请求超时时间（30秒）

		// ========== 游戏状态 ==========
		
		char *_chessBoard{ nullptr };      // 棋盘数组指针
		const int _boardSize{ 9 };         // 棋盘大小（9x9x9）
		bool _myTurn{ false };             // 是否轮到自己
		char _myPlayer{ '1' };             // 己方玩家标识
		std::atomic<bool> _gameRunning{ false };  // 游戏是否运行中
		bool _iAmMatchInitiator{ false };  // 是否是匹配发起者
		int _gameResult{ 0 };              // 游戏结果

		std::mutex _moveMutex;             // 落子互斥锁
		bool _opponentMoved{ false };      // 对手是否已落子
		int _opponentMove[3] { 0, 0, 0 };  // 对手落子坐标

		// ========== 同步相关 ==========
		
		std::atomic<bool> _isSyncing{ false };         // 是否正在同步
		std::chrono::steady_clock::time_point _lastSyncTime;  // 上次同步时间
		std::thread _syncThread;                       // 同步线程
		std::atomic<bool> _syncThreadRunning{ false }; // 同步线程运行标志

		// ========== 网络回调函数 ==========
		
		/**
		 * @brief 收到匹配请求的回调
		 */
		void onMatchRequest(const lanp2p::PeerInfo &p, const std::string &matchId);
		
		/**
		 * @brief 收到匹配响应的回调
		 */
		void onMatchResponse(const lanp2p::PeerInfo &p, bool accepted, const std::string &matchId);
		
		/**
		 * @brief 收到匹配中断的回调
		 */
		void onMatchInterrupted(const lanp2p::PeerInfo &p, const std::string &matchId);
		
		/**
		 * @brief 收到对手落子的回调
		 */
		void onGameMove(const lanp2p::PeerInfo &p, int x, int y, int z);
		
		/**
		 * @brief 收到棋盘同步数据的回调
		 */
		void onBoardSync(const lanp2p::PeerInfo &p, const std::string &boardState);

		// ========== 线程相关 ==========
		
		/**
		 * @brief 超时处理线程循环
		 * 
		 * 定期检查待处理请求队列，移除超时请求
		 */
		void timeoutThreadLoop();
		
		/**
		 * @brief 启动超时处理线程
		 */
		void startTimeoutThread();
		
		/**
		 * @brief 停止超时处理线程
		 */
		void stopTimeoutThread();
		
		/**
		 * @brief 清理游戏状态资源
		 * 
		 * 释放棋盘内存，停止同步线程
		 */
		void cleanupGameState();
		
		/**
		 * @brief 同步线程循环
		 * 
		 * 每5秒向对手发送一次完整棋盘状态
		 */
		void syncThreadLoop();
		
		/**
		 * @brief 启动同步线程
		 */
		void startSyncThread();
		
		/**
		 * @brief 停止同步线程
		 */
		void stopSyncThread();
		
		/**
		 * @brief 获取当前棋盘状态的序列化字符串
		 * @return 序列化后的棋盘状态
		 */
		std::string getBoardStateString() const;

	public:
		/**
		 * @brief 检查是否正在同步
		 * @return true 正在同步，false 未同步
		 */
		bool isSyncing() const { return _isSyncing.load(); }
};
