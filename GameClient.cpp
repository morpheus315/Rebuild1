#include "GameClient.h"
#include "chess-game.h"

// 构造函数：注册网络回调并启动待处理请求的超时线程
Client::Client(lanp2p::LanP2PNode &node)
	: _node(node)
{
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
	_node.setOnBoardSync([this](const lanp2p::PeerInfo &p, const std::string &bs)
	{
		this->onBoardSync(p, bs);
	});

	startTimeoutThread();
	_lastSyncTime = std::chrono::steady_clock::now();
}

// 析构函数
Client::~Client()
{
	{
		std::lock_guard<std::mutex> lk(_matchMutex);
		if (_match.inMatch)
		{
			_node.interruptMatch(_match.peer.ip, _match.peer.tcpPort, _match.matchId);
			_match = MatchState{};
		}
	}

	_gameRunning = false;

	_node.stop();

	_node.setOnMatchRequest(nullptr);
	_node.setOnMatchResponse(nullptr);
	_node.setOnMatchInterrupted(nullptr);
	_node.setOnGameMove(nullptr);
	_node.setOnBoardSync(nullptr);

	stopTimeoutThread();
	stopSyncThread();

	cleanupGameState();
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
			return false;
		}
	}

	// 生成随机匹配ID并发送请求
	std::string mid = lanp2p::LanP2PNode::generateMatchId();
	return _node.sendMatchRequest(peer.ip, peer.tcpPort, mid);
}

bool Client::isInMatch() const
{
	std::lock_guard<std::mutex> lk(_matchMutex);
	return _match.inMatch;
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
		_node.interruptMatch(peer.ip, peer.tcpPort, matchId);
		_gameRunning = false;
		stopSyncThread();
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
}

void Client::onMatchResponse(const lanp2p::PeerInfo &p, bool accepted, const std::string &matchId)
{
	if (accepted)
	{
		//我方作为请求发起者时，对方接受后建立本地对局状态，并标记"发起者"身份
		std::lock_guard<std::mutex> lk(_matchMutex);
		_match.inMatch = true;
		_match.peer = p;
		_match.matchId = matchId;
		_iAmMatchInitiator = true;
	}
}

void Client::onMatchInterrupted(const lanp2p::PeerInfo &p, const std::string &matchId)
{

	std::lock_guard<std::mutex> lk(_matchMutex);
	if (_match.inMatch && _match.matchId == matchId && _match.peer.id == p.id)
	{
		_match = MatchState{};
		_gameRunning = false; // 停止游戏循环
	}
}

void Client::onGameMove(const lanp2p::PeerInfo &p, int x, int y, int z)
{
	bool shouldProcess = false;
	{
		std::lock_guard<std::mutex> lk(_matchMutex);
		shouldProcess = (_match.inMatch && p.id == _match.peer.id);
	}

	if (shouldProcess)
	{
		if (x < 1 || x > _boardSize || y < 1 || y > _boardSize || z < 1 || z > _boardSize)
			return;

		std::lock_guard<std::mutex> lk(_moveMutex);
		_opponentMove[0] = x;
		_opponentMove[1] = y;
		_opponentMove[2] = z;
		_opponentMoved = true;
	}
}

void Client::onBoardSync(const lanp2p::PeerInfo &p, const std::string &boardState)
{
	std::lock_guard<std::mutex> lk(_matchMutex);
	if (!_match.inMatch || p.id != _match.peer.id)
		return;
	
	_isSyncing = true;
	
	if (_chessBoard)
	{
		char *tempBoard = (char*)calloc(_boardSize * _boardSize * _boardSize, sizeof(char));
		if (tempBoard)
		{
			DeserializeBoardState(_boardSize, tempBoard, boardState);
			
			for (int i = 0; i < _boardSize * _boardSize * _boardSize; ++i)
			{
				if (_chessBoard[i] == 0 && tempBoard[i] != 0)
				{
					_chessBoard[i] = tempBoard[i];
				}
			}
			
			free(tempBoard);
		}
	}
	
	_isSyncing = false;
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

void Client::syncThreadLoop()
{
	while (_syncThreadRunning.load())
	{
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - _lastSyncTime);
		
		if (elapsed.count() >= 5)
		{
			bool shouldSync = false;
			{
				std::lock_guard<std::mutex> lk(_matchMutex);
				shouldSync = _match.inMatch && _gameRunning.load();
			}
			
			if (shouldSync)
			{
				_isSyncing = true;
				
				std::string boardState = getBoardStateString();
				lanp2p::PeerInfo opponent;
				{
					std::lock_guard<std::mutex> lk(_matchMutex);
					opponent = _match.peer;
				}
				
				_node.sendBoardState(opponent.ip, opponent.tcpPort, boardState);
				
				_lastSyncTime = now;
				_isSyncing = false;
			}
		}
		
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
}

void Client::startSyncThread()
{
	_syncThreadRunning = true;
	_syncThread = std::thread(&Client::syncThreadLoop, this);
}

void Client::stopSyncThread()
{
	_syncThreadRunning = false;
	if (_syncThread.joinable())
	{
		_syncThread.join();
	}
}

std::string Client::getBoardStateString() const
{
	if (!_chessBoard)
		return "";
	return SerializeBoardState(_boardSize, _chessBoard);
}

void Client::initGameState()
{
	// 初始化棋盘（确保旧棋盘已释放）
	cleanupGameState();
	if (!OnlineInitChessBoard(&_chessBoard, _boardSize))
	{
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
	
	// 确保旧的同步线程已停止，再启动新的
	stopSyncThread();
	startSyncThread();
}

void Client::cleanupGameState()
{
	// 先停止同步线程，防止访问即将释放的棋盘内存
	stopSyncThread();
	
	if (_chessBoard)
	{
		free(_chessBoard);
		_chessBoard = nullptr;
	}
}

bool Client::tryPlaceMyPiece(int x, int y, int z)
{
	if (!_myTurn || !_gameRunning.load() || _isSyncing.load())
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

int Client::getTimeSinceLastSync() const
{
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - _lastSyncTime);
	return static_cast<int>(elapsed.count());
}

