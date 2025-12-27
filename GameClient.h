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

class Client
{
	public:
		//构造&&析构
		Client(lanp2p::LanP2PNode &node);
		~Client();

		//防止异常复制
		Client(const Client &) = delete;
		Client &operator=(const Client &) = delete;

		std::vector<lanp2p::PeerInfo> getAvailablePeers();//获取当前活跃客户端列表
		bool requestMatch(const lanp2p::PeerInfo &peer);//请求匹配

		bool isInMatch() const;//是否匹配
		void endMatch();//断开匹配

		std::string getMatchId() const;//获取匹配ID
		lanp2p::PeerInfo getMatchPeer() const;//获取对端peerinfo

		struct PendingRequestInfo
		{
			std::string matchId;
			lanp2p::PeerInfo peer;
			std::string ip;
			uint16_t port{ 0 };
		};
		std::vector<PendingRequestInfo> getPendingRequestsSnapshot();
		bool respondToPendingRequest(const PendingRequestInfo &req, bool accept);

		bool isMyTurn() const;
		char getMyPlayer() const;
		bool isGameRunning() const;
		int getGameResult() const;

		bool tryPlaceMyPiece(int x, int y, int z);
		bool tryGetOpponentMove(int& outX, int& outY, int& outZ);
		void initGameState();

	private:
		lanp2p::LanP2PNode &_node;//局域网通信节点

		struct MatchState//match结构体
		{
			bool inMatch{ false };
			std::string matchId;
			lanp2p::PeerInfo peer;
		};
		MatchState _match;
		mutable std::mutex _matchMutex;

		struct PendingRequest//未处理的请求
		{
			bool has{ false };//有效值
			std::string ip;
			uint16_t port{ 0 };
			std::string matchId;
			lanp2p::PeerInfo peer;
			std::chrono::steady_clock::time_point ts;//持续时间
		};

		std::mutex _pendingMutex;
		std::deque<PendingRequest> _pendingQueue;
		std::atomic<bool> _timeoutThreadRunning{ false };
		std::thread _timeoutThread;
		const std::chrono::seconds _requestTimeout{ 30 };

		char *_chessBoard{ nullptr };
		const int _boardSize{ 9 };
		bool _myTurn{ false };
		char _myPlayer{ '1' };
		std::atomic<bool> _gameRunning{ false };
		bool _iAmMatchInitiator{ false };
		int _gameResult{ 0 };

		std::mutex _moveMutex;
		bool _opponentMoved{ false };//对端是否落子
		int _opponentMove[3] { 0, 0, 0 };//对端落子

		void onMatchRequest(const lanp2p::PeerInfo &p, const std::string &matchId);//回调：接到请求
		void onMatchResponse(const lanp2p::PeerInfo &p, bool accepted, const std::string &matchId);//回调：接到回答
		void onMatchInterrupted(const lanp2p::PeerInfo &p, const std::string &matchId);//回调：被打断
		void onGameMove(const lanp2p::PeerInfo &p, int x, int y, int z);//回调：对端下棋

		void timeoutThreadLoop();//清除pendingrequest中的过期信息
		void startTimeoutThread();//启动清理线程
		void stopTimeoutThread();//停止线程
		void cleanupGameState();//清理资源
};
