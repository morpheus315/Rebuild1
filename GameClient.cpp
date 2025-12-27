#include "GameClient.h"
#include "chess-game.h"
#include <iostream>

// 构造函数：注册网络回调并启动待处理请求的超时线程
Client::Client(lanp2p::LanP2PNode &node)
	: _node(node)
{
	// 向底层节点注册回调，绑定到本类的私有成员函数
	_node.setOnPeerDiscovered([this](const lanp2p::PeerInfo &p)
	{
		this->onPeerDiscovered(p);
	});
	_node.setOnMatchRequest([this](const lanp2p::PeerInfo &p, const std::string &mid)
	{
		this->onMatchRequest(p, mid);
	});
	_node.setOnMatchResponse([this](const lanp2p::PeerInfo &p, bool a, const std::string &mid)
	{
		this->onMatchResponse(p, a, mid);
	});
	_node.setOnMatchInterrupted([this](const lanp2p::PeerInfo &p, const std::string &mid)
	{
		this->onMatchInterrupted(p, mid);
	});
	_node.setOnGameMove([this](const lanp2p::PeerInfo &p, int x, int y, int z)
	{
		this->onGameMove(p, x, y, z);
	});

	// 启动后台线程以处理匹配请求的自动超时拒绝
	startTimeoutThread();
}

// 析构函数
Client::~Client()
{
	// 若在对局中，先发送中断消息通知对方
	{
		std::lock_guard<std::mutex> lk(_matchMutex);
		if (_match.inMatch)
		{
			_node.interruptMatch(_match.peer.ip, _match.peer.tcpPort, _match.matchId);
			_match = MatchState{};
		}
	}

	// 停止游戏循环
	_gameRunning = false;

	// 先停止节点及其所有线程，阻止后续回调触发
	_node.stop();

	// 清空回调（避免回调到已失效对象）
	_node.setOnPeerDiscovered(nullptr);
	_node.setOnMatchRequest(nullptr);
	_node.setOnMatchResponse(nullptr);
	_node.setOnMatchInterrupted(nullptr);
	_node.setOnGameMove(nullptr);

	// 清理超时线程
	stopTimeoutThread();

	// 释放棋盘内存
	cleanupGameState();
}


void Client::startDiscovery()
{
	std::cout << "[Client] Starting UDP discovery for 10 seconds..." << std::endl;
	_node.startUdpListen();
	std::this_thread::sleep_for(std::chrono::seconds(10));
	_node.stopUdpListen();
	std::cout << "[Client] UDP discovery stopped." << std::endl;
}

std::vector<lanp2p::PeerInfo> Client::getAvailablePeers()
{
	return _node.getPeersSnapshot();
}

bool Client::requestMatch(const lanp2p::PeerInfo &peer)
{
	// 防止在已处于对局时再次发起匹配
	{
		std::lock_guard<std::mutex> lk(_matchMutex);
		if (_match.inMatch)
		{
			std::cout << "Already in a match" << std::endl;
			return false;
		}
	}

	// 生成随机匹配ID并发送请求
	std::string mid = lanp2p::LanP2PNode::generateMatchId();
	std::cout << "[Client] Sending request to " << peer.ip << ":" << peer.tcpPort
	          << ", matchId=" << mid << ", fromId=" << _node.getNodeId() << std::endl;
	if (_node.sendMatchRequest(peer.ip, peer.tcpPort, mid))
	{
		std::cout << "Sent match request to "
		          << (peer.name.empty() ? peer.id : peer.name)
		          << ", matchId=" << mid
		          << std::endl;
		return true;
	}
	else
	{
		std::cout << "Failed to send" << std::endl;
		return false;
	}
}

void Client::handlePendingRequests()
{
	//依次处理所有排队等待的匹配请求
	while (true)
	{
		PendingRequest pr;
		bool hasOne = false;
		{
			std::lock_guard<std::mutex> lk(_pendingMutex);
			if (!_pendingQueue.empty())
			{
				pr = _pendingQueue.front();
				_pendingQueue.pop_front();
				hasOne = true;
			}
		}
		if (!hasOne)
		{
			std::cout << "No pending requests" << std::endl;
			break;
		}
		std::cout << "\n====== Match Request ======\n";
		std::cout << "from: "
		          << (pr.peer.name.empty() ? pr.peer.id : pr.peer.name)
		          << " (id=" << pr.peer.id << ") "
		          << pr.ip << ":" << pr.port
		          << " matchId=" << pr.matchId
		          << "\nAccept? (y/n): ";
		std::string resp;
		if (!std::getline(std::cin, resp))
			resp.clear();
		bool accept = (!resp.empty() && (resp[0] == 'y' || resp[0] == 'Y'));

		_node.respondToMatch(pr.ip, pr.port, pr.matchId, accept);
		if (accept)
		{
			//接受后进入对局并更新状态，我方角色为"应答者"。
			std::lock_guard<std::mutex> lk(_matchMutex);
			_match.inMatch = true;
			_match.peer = pr.peer;
			_match.matchId = pr.matchId;
			_iAmMatchInitiator = false;
			//注册心跳检测
			_node.markMatchActive(pr.ip, pr.port, pr.peer.id, pr.matchId);
			std::cout << "Accepted. Match starts." << std::endl;
		}
		else
		{
			std::cout << "Rejected" << std::endl;
		}
	}
}

bool Client::isInMatch() const
{
	std::lock_guard<std::mutex> lk(_matchMutex);
	return _match.inMatch;
}

void Client::startGame()
{
	{
		std::lock_guard<std::mutex> lk(_matchMutex);
		if (!_match.inMatch)
			return;
	}

	std::cout << "\n\n===== Game Start =====\n";

	lanp2p::PeerInfo opponent;
	{
		std::lock_guard<std::mutex> lk(_matchMutex);
		opponent = _match.peer;
	}
	std::cout << "Your opponent: " << (opponent.name.empty() ? opponent.id : opponent.name) << std::endl;

	initGameState();
	gameLoop();
	cleanupGameState();

	std::cout << "===== Game Over =====\n\n";

	std::lock_guard<std::mutex> lk(_matchMutex);
	_match = MatchState{}; // 重置对局状态
}

void Client::endMatch()
{
	// 主动结束比赛：读取并清空匹配状态，然后发送中断
	std::string matchId;
	lanp2p::PeerInfo peer;
	bool wasInMatch = false;

	{
		std::lock_guard<std::mutex> lk(_matchMutex);
		if (!_match.inMatch)
			return;

		matchId = _match.matchId;
		peer = _match.peer;
		wasInMatch = true;
		_match = MatchState{};
	}

	if (wasInMatch)
	{
		std::cout << "[Client] Sending interrupt signal to " << peer.ip << ":" << peer.tcpPort
		          << ", matchId=" << matchId << std::endl;
		_node.interruptMatch(peer.ip, peer.tcpPort, matchId);
		_gameRunning = false; // 确保游戏循环退出
		std::cout << "Game over (interrupted)" << std::endl;
	}
}

std::string Client::getMatchId() const
{
	std::lock_guard<std::mutex> lk(_matchMutex);
	return _match.matchId;
}

lanp2p::PeerInfo Client::getMatchPeer() const
{
	std::lock_guard<std::mutex> lk(_matchMutex);
	return _match.peer;
}

std::vector<Client::PendingRequestInfo> Client::getPendingRequestsSnapshot()
{
	std::vector<PendingRequestInfo> out;
	std::lock_guard<std::mutex> lk(_pendingMutex);
	for (const auto &pr : _pendingQueue)
	{
		PendingRequestInfo info;
		info.matchId = pr.matchId;
		info.peer = pr.peer;
		info.ip = pr.ip;
		info.port = pr.port;
		out.push_back(info);
	}
	return out;
}

bool Client::respondToPendingRequest(const PendingRequestInfo &req, bool accept)
{
	PendingRequest target;
	bool found = false;
	{
		std::lock_guard<std::mutex> lk(_pendingMutex);
		for (auto it = _pendingQueue.begin(); it != _pendingQueue.end(); ++it)
		{
			if (it->matchId == req.matchId && it->peer.id == req.peer.id)
			{
				target = *it;
				_pendingQueue.erase(it);
				found = true;
				break;
			}
		}
	}

	if (!found)
		return false;

	_node.respondToMatch(target.ip, target.port, target.matchId, accept);
	if (accept)
	{
		std::lock_guard<std::mutex> lk(_matchMutex);
		_match.inMatch = true;
		_match.peer = target.peer;
		_match.matchId = target.matchId;
		_iAmMatchInitiator = false;
		_node.markMatchActive(target.ip, target.port, target.peer.id, target.matchId);
	}
	return true;
}

bool Client::isMyTurn() const
{
	return _myTurn;
}

char Client::getMyPlayer() const
{
	return _myPlayer;
}

bool Client::isGameRunning() const
{
	return _gameRunning.load();
}

int Client::getGameResult() const
{
	return _gameResult;
}

// --- 私有方法（回调与线程） ---

void Client::onPeerDiscovered(const lanp2p::PeerInfo &p)
{
	std::cout << "[Client DBG] Peer discovered: name=" << (p.name.empty() ? "<noname>" : p.name)
	          << " id=" << p.id << " at " << p.ip << ":" << p.tcpPort << std::endl;
}

void Client::onMatchRequest(const lanp2p::PeerInfo &p, const std::string &matchId)
{
	// 收到对方发起的匹配请求，入队等待用户处理
	PendingRequest pr;
	pr.has = true;
	pr.ip = p.ip;
	pr.port = p.tcpPort;
	pr.matchId = matchId;
	pr.peer = p;
	pr.ts = std::chrono::steady_clock::now();
	{
		std::lock_guard<std::mutex> lk(_pendingMutex);
		_pendingQueue.push_back(std::move(pr));
	}
	std::cout << "\n[Request] From "
	          << (p.name.empty() ? p.id : p.name)
	          << ". Handle it in the main panel." << std::endl;
}

void Client::onMatchResponse(const lanp2p::PeerInfo &p, bool accepted, const std::string &matchId)
{
	std::cout << "\n";
	std::cout << "[Response] From "
	          << (p.name.empty() ? p.id : p.name)
	          << " match=" << matchId
	          << " accepted=" << (accepted ? "true" : "false") << std::endl;
	if (accepted)
	{
		//我方作为请求发起者时，对方接受后建立本地对局状态，并标记“发起者”身份
		std::lock_guard<std::mutex> lk(_matchMutex);
		_match.inMatch = true;
		_match.peer = p;
		_match.matchId = matchId;
		_iAmMatchInitiator = true;
	}
}

void Client::onMatchInterrupted(const lanp2p::PeerInfo &p, const std::string &matchId)
{
	std::cout << "[Interrupted] From "
	          << (p.name.empty() ? p.id : p.name)
	          << " match=" << matchId << std::endl;

	std::lock_guard<std::mutex> lk(_matchMutex);
	if (_match.inMatch && _match.matchId == matchId && _match.peer.id == p.id)
	{
		_match = MatchState{};
		_gameRunning = false; // 停止游戏循环
	}
}

void Client::onGameMove(const lanp2p::PeerInfo &p, int x, int y, int z)
{
	//仅在对局中且消息来自当前对手时才缓存其落子
	bool shouldProcess = false;
	{
		std::lock_guard<std::mutex> lk(_matchMutex);
		shouldProcess = (_match.inMatch && p.id == _match.peer.id);
	}

	if (shouldProcess)
	{
		// 前置校验坐标范围，忽略非法坐标
		if (x < 1 || x > _boardSize || y < 1 || y > _boardSize || z < 1 || z > _boardSize)
			return;

		std::lock_guard<std::mutex> lk(_moveMutex);
		_opponentMove[0] = x;
		_opponentMove[1] = y;
		_opponentMove[2] = z;
		_opponentMoved = true;
	}
}

void Client::timeoutThreadLoop()
{
	// 周期扫描未处理的匹配请求，超时自动拒绝
	while (_timeoutThreadRunning.load())
	{
		PendingRequest pr;
		bool expired = false;
		{
			std::lock_guard<std::mutex> lk(_pendingMutex);
			if (!_pendingQueue.empty())
			{
				auto &front = _pendingQueue.front();
				if (std::chrono::steady_clock::now() - front.ts > _requestTimeout)
				{
					pr = front;
					_pendingQueue.pop_front();
					expired = true;
				}
			}
		}
		if (expired && pr.has)
		{
			_node.respondToMatch(pr.ip, pr.port, pr.matchId, false);
			std::cout << "[Auto] Rejected (timeout) match " << pr.matchId << " for peer "
			          << (pr.peer.name.empty() ? pr.peer.id : pr.peer.name) << std::endl;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
}

void Client::startTimeoutThread()
{
	_timeoutThreadRunning = true;
	_timeoutThread = std::thread(&Client::timeoutThreadLoop, this);
}

void Client::stopTimeoutThread()
{
	_timeoutThreadRunning = false;
	if (_timeoutThread.joinable())
	{
		_timeoutThread.join();
	}
}

void Client::initGameState()
{
	// 初始化棋盘（确保旧棋盘已释放）
	cleanupGameState();
	if (!OnlineInitChessBoard(&_chessBoard, _boardSize))
	{
		std::cout << "FATAL: failed to init board" << std::endl;
		endMatch();
		return;
	}

	// 读取匹配ID并根据规则决定先后手
	std::string matchId;
	{
		std::lock_guard<std::mutex> lk(_matchMutex);
		matchId = _match.matchId;
	}

	bool matchIdIsOdd = false;
	if (!matchId.empty())
	{
		char firstChar = matchId[0];
		if (firstChar >= '0' && firstChar <= '9')
			matchIdIsOdd = ((firstChar - '0') % 2 == 1);
		else if (firstChar >= 'a' && firstChar <= 'f')
			matchIdIsOdd = ((firstChar - 'a') % 2 == 1);
		else if (firstChar >= 'A' && firstChar <= 'F')
			matchIdIsOdd = ((firstChar - 'A') % 2 == 1);
	}

	// 先后手规则：发起者+奇数先手；响应者+偶数先手
	bool iAmFirstPlayer = (_iAmMatchInitiator && matchIdIsOdd) || (!_iAmMatchInitiator && !matchIdIsOdd);

	_myPlayer = iAmFirstPlayer ? '1' : '2';
	_myTurn = (_myPlayer == '1');
	_gameRunning = true;
	_opponentMoved = false;
	_gameResult = 0;

	std::cout << "Board initialized, you are player " << _myPlayer
	          << " (" << (_iAmMatchInitiator ? "initiator" : "responder")
	          << ", matchId: " << matchId.substr(0, 4) << "...)." << std::endl;
	if (_myTurn)
	{
		std::cout << "Your turn" << std::endl;
	}
	else
	{
		std::cout << "Waiting for opponent move" << std::endl;
	}
}

void Client::cleanupGameState()
{
	if (_chessBoard)
	{
		free(_chessBoard);
		_chessBoard = nullptr;
	}
}

bool Client::tryPlaceMyPiece(int x, int y, int z)
{
	if (!_myTurn || !_gameRunning.load())
		return false;
	int coords[3] = { x,y,z };
	if (UpdateBoardState(_boardSize, _chessBoard, coords, _myPlayer))
	{
		// 读取对手信息，发送我方落子给对手
		lanp2p::PeerInfo opponent;
		{
			std::lock_guard<std::mutex> lk(_matchMutex);
			opponent = _match.peer;
		}

		_node.sendGameMove(opponent.ip, opponent.tcpPort, coords[0], coords[1], coords[2]);
		if (CheckWin(_boardSize, _chessBoard, coords, _myPlayer))
		{
			_gameRunning = false;
			_gameResult = 1;
			return true;
		}
		_myTurn = false;
		return true;
	}
	return false;
}

bool Client::tryGetOpponentMove(int& outX, int& outY, int& outZ)
{
	std::lock_guard<std::mutex> lk(_moveMutex);
	if (_opponentMoved)
	{
		outX = _opponentMove[0];
		outY = _opponentMove[1];
		outZ = _opponentMove[2];
		char opponentPlayer = (_myPlayer == '1') ? '2' : '1';
		UpdateBoardState(_boardSize, _chessBoard, _opponentMove, opponentPlayer);
		if (CheckWin(_boardSize, _chessBoard, _opponentMove, opponentPlayer))
		{
			_gameRunning = false;
			_gameResult = 2;
		}
		_opponentMoved = false;
		_myTurn = true;
		return true;
	}
	return false;
}
void Client::gameLoop()
{
	// 主循环：根据回合决定本地落子或处理对手落子
	while (_gameRunning.load())
	{
		if (_myTurn)
		{
			int coords[3];
			NativeGetChessPosition(coords);
			if (UpdateBoardState(_boardSize, _chessBoard, coords, _myPlayer))
			{
				// 读取对手信息，发送我方落子给对手
				lanp2p::PeerInfo opponent;
				{
					std::lock_guard<std::mutex> lk(_matchMutex);
					opponent = _match.peer;
				}

				_node.sendGameMove(opponent.ip, opponent.tcpPort, coords[0], coords[1], coords[2]);
				if (CheckWin(_boardSize, _chessBoard, coords, _myPlayer))
				{
					std::cout << "You win" << std::endl;
					_gameRunning = false;
					_gameResult = 1;
					break;
				}
				_myTurn = false;
				std::cout << "Waiting for opponent move" << std::endl;
			}
			else
			{
				std::cout << "Invalid input" << std::endl;
			}
		}
		else // 轮到对手
		{
			bool moved = false;
			{
				std::lock_guard<std::mutex> lk(_moveMutex);
				if (_opponentMoved)
				{
					char opponentPlayer = (_myPlayer == '1') ? '2' : '1';
					UpdateBoardState(_boardSize, _chessBoard, _opponentMove, opponentPlayer);
					if (CheckWin(_boardSize, _chessBoard, _opponentMove, opponentPlayer))
					{
						std::cout << "You lost" << std::endl;
						_gameRunning = false;
						_gameResult = 2;
					}
					_opponentMoved = false;
					moved = true;
				}
			}
			if (moved)
			{
				_myTurn = true;
				std::cout << "Your turn" << std::endl;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}
