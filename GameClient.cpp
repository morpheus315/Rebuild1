#include "GameClient.h"
#include "chess-game.h"

/**
 * @brief 客户端构造函数
 * 
 * 执行以下初始化操作：
 * 1. 注册所有网络事件的回调函数
 * 2. 启动请求超时处理线程
 * 3. 初始化同步时间戳
 * 
 * 注意：同步线程在 initGameState() 中启动，而不是在构造函数中
 */
Client::Client(lanp2p::LanP2PNode &node)
	: _node(node)
{
	// 注册匹配请求回调
	_node.setOnMatchRequest([this](const lanp2p::PeerInfo &p, const std::string &mid)
	{
		this->onMatchRequest(p, mid);
	});
	
	// 注册匹配响应回调
	_node.setOnMatchResponse([this](const lanp2p::PeerInfo &p, bool a, const std::string &mid)
	{
		this->onMatchResponse(p, a, mid);
	});
	
	// 注册匹配中断回调
	_node.setOnMatchInterrupted([this](const lanp2p::PeerInfo &p, const std::string &mid)
	{
		this->onMatchInterrupted(p, mid);
	});
	
	// 注册游戏落子回调
	_node.setOnGameMove([this](const lanp2p::PeerInfo &p, int x, int y, int z)
	{
		this->onGameMove(p, x, y, z);
	});
	
	// 注册棋盘同步回调
	_node.setOnBoardSync([this](const lanp2p::PeerInfo &p, const std::string &bs)
	{
		this->onBoardSync(p, bs);
	});

	// 启动请求超时处理线程
	startTimeoutThread();
	
	// 初始化同步时间戳为当前时间
	_lastSyncTime = std::chrono::steady_clock::now();
}

/**
 * @brief 客户端析构函数
 * 
 * 清理所有资源，执行顺序：
 * 1. 如果在匹配中，发送中断消息
 * 2. 停止游戏运行标志
 * 3. 停止网络节点
 * 4. 移除所有网络回调
 * 5. 停止所有线程
 * 6. 清理游戏状态（释放棋盘内存）
 */
Client::~Client()
{
	// 如果正在匹配中，发送中断消息
	{
		std::lock_guard<std::mutex> lk(_matchMutex);
		if (_match.inMatch)
		{
			_node.interruptMatch(_match.peer.ip, _match.peer.tcpPort, _match.matchId);
			_match = MatchState{};
		}
	}

	// 停止游戏运行
	_gameRunning = false;

	// 停止网络节点
	_node.stop();

	// 移除所有回调函数
	_node.setOnMatchRequest(nullptr);
	_node.setOnMatchResponse(nullptr);
	_node.setOnMatchInterrupted(nullptr);
	_node.setOnGameMove(nullptr);
	_node.setOnBoardSync(nullptr);

	// 停止线程
	stopTimeoutThread();
	stopSyncThread();

	// 清理游戏资源
	cleanupGameState();
}

/**
 * @brief 获取当前可用的对等节点列表
 * @return 节点信息列表
 */
std::vector<lanp2p::PeerInfo> Client::getAvailablePeers()
{
	return _node.getPeersSnapshot();
}

/**
 * @brief 请求与指定节点匹配
 * 
 * @param peer 目标节点信息
 * @return true 请求已发送，false 当前已在匹配中
 */
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

/**
 * @brief 检查是否正在匹配中
 */
bool Client::isInMatch() const
{
	std::lock_guard<std::mutex> lk(_matchMutex);
	return _match.inMatch;
}

/**
 * @brief 主动结束当前匹配
 * 
 * 执行步骤：
 * 1. 读取并清空匹配状态
 * 2. 向对手发送中断消息
 * 3. 停止游戏运行
 * 4. 停止同步线程
 */
void Client::endMatch()
{
	// 读取并清空匹配状态
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

	// 向对手发送中断消息并清理游戏状态
	if (wasInMatch)
	{
		_node.interruptMatch(peer.ip, peer.tcpPort, matchId);
		_gameRunning = false;
		stopSyncThread();
	}
}

/**
 * @brief 获取当前匹配ID
 */
std::string Client::getMatchId() const
{
	std::lock_guard<std::mutex> lk(_matchMutex);
	return _match.matchId;
}

/**
 * @brief 获取对手节点信息
 */
lanp2p::PeerInfo Client::getMatchPeer() const
{
	std::lock_guard<std::mutex> lk(_matchMutex);
	return _match.peer;
}

/**
 * @brief 获取待处理请求列表的快照
 * 
 * @return 待处理请求信息列表
 */
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

/**
 * @brief 响应待处理的匹配请求
 * 
 * @param req 请求信息
 * @param accept true接受，false拒绝
 * @return true 响应成功，false 请求不存在或已过期
 */
bool Client::respondToPendingRequest(const PendingRequestInfo &req, bool accept)
{
	PendingRequest target;
	bool found = false;
	
	// 从队列中查找并移除指定请求
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

	// 发送响应
	_node.respondToMatch(target.ip, target.port, target.matchId, accept);
	
	// 如果接受请求，建立匹配状态
	if (accept)
	{
		std::lock_guard<std::mutex> lk(_matchMutex);
		_match.inMatch = true;
		_match.peer = target.peer;
		_match.matchId = target.matchId;
		_iAmMatchInitiator = false;  // 响应者不是发起者
		_node.markMatchActive(target.ip, target.port, target.peer.id, target.matchId);
	}
	return true;
}

/**
 * @brief 检查是否轮到自己下棋
 */
bool Client::isMyTurn() const
{
	return _myTurn;
}

/**
 * @brief 获取自己的玩家标识
 */
char Client::getMyPlayer() const
{
	return _myPlayer;
}

/**
 * @brief 检查游戏是否正在运行
 */
bool Client::isGameRunning() const
{
	return _gameRunning.load();
}

/**
 * @brief 获取游戏结果
 */
int Client::getGameResult() const
{
	return _gameResult;
}

// ========== 网络回调函数 ==========

/**
 * @brief 收到匹配请求的回调
 * 
 * 将请求加入待处理队列，等待用户响应
 */
void Client::onMatchRequest(const lanp2p::PeerInfo &p, const std::string &matchId)
{
	PendingRequest pr;
	pr.has = true;
	pr.ip = p.ip;
	pr.port = p.tcpPort;
	pr.matchId = matchId;
	pr.peer = p;
	pr.ts = std::chrono::steady_clock::now();  // 记录接收时间
	
	{
		std::lock_guard<std::mutex> lk(_pendingMutex);
		_pendingQueue.push_back(std::move(pr));
	}
}

/**
 * @brief 收到匹配响应的回调
 * 
 * 如果对方接受，作为发起者建立匹配状态
 */
void Client::onMatchResponse(const lanp2p::PeerInfo &p, bool accepted, const std::string &matchId)
{
	if (accepted)
	{
		// 我方作为请求发起者时，对方接受后建立本地对局状态
		std::lock_guard<std::mutex> lk(_matchMutex);
		_match.inMatch = true;
		_match.peer = p;
		_match.matchId = matchId;
		_iAmMatchInitiator = true;  // 标记为发起者
	}
}

/**
 * @brief 收到匹配中断的回调
 * 
 * 清空匹配状态，停止游戏
 */
void Client::onMatchInterrupted(const lanp2p::PeerInfo &p, const std::string &matchId)
{
	std::lock_guard<std::mutex> lk(_matchMutex);
	if (_match.inMatch && _match.matchId == matchId && _match.peer.id == p.id)
	{
		_match = MatchState{};
		_gameRunning = false;
	}
}

/**
 * @brief 收到对手落子的回调
 * 
 * 验证并保存对手的落子信息
 */
void Client::onGameMove(const lanp2p::PeerInfo &p, int x, int y, int z)
{
	bool shouldProcess = false;
	
	// 验证是否来自当前对手
	{
		std::lock_guard<std::mutex> lk(_matchMutex);
		shouldProcess = (_match.inMatch && p.id == _match.peer.id);
	}

	if (shouldProcess)
	{
		// 验证坐标合法性
		if (x < 1 || x > _boardSize || y < 1 || y > _boardSize || z < 1 || z > _boardSize)
			return;

		// 保存对手落子信息
		std::lock_guard<std::mutex> lk(_moveMutex);
		_opponentMove[0] = x;
		_opponentMove[1] = y;
		_opponentMove[2] = z;
		_opponentMoved = true;
	}
}

/**
 * @brief 收到棋盘同步数据的回调
 * 
 * 用对方的棋盘状态填补己方棋盘的空位，防止网络丢包导致不同步
 */
void Client::onBoardSync(const lanp2p::PeerInfo &p, const std::string &boardState)
{
	std::lock_guard<std::mutex> lk(_matchMutex);
	
	// 验证来源
	if (!_match.inMatch || p.id != _match.peer.id)
		return;
	
	_isSyncing = true;
	
	if (_chessBoard)
	{
		// 创建临时棋盘用于解析
		char *tempBoard = (char*)calloc(_boardSize * _boardSize * _boardSize, sizeof(char));
		if (tempBoard)
		{
			// 解析对方的棋盘状态
			DeserializeBoardState(_boardSize, tempBoard, boardState);
			
			// 只同步空位：如果己方某位置为空但对方有棋子，则补上
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

// ========== 线程管理 ==========

/**
 * @brief 超时处理线程循环
 * 
 * 每200毫秒检查一次队列，移除超时的请求（30秒超时）
 */
void Client::timeoutThreadLoop()
{
	while (_timeoutThreadRunning.load())
	{
		PendingRequest pr;
		bool expired = false;
		
		// 检查队列首个请求是否超时
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
		
		// 如果请求超时，自动拒绝
		if (expired && pr.has)
		{
			_node.respondToMatch(pr.ip, pr.port, pr.matchId, false);
		}
		
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
}

/**
 * @brief 启动超时处理线程
 */
void Client::startTimeoutThread()
{
	_timeoutThreadRunning = true;
	_timeoutThread = std::thread(&Client::timeoutThreadLoop, this);
}

/**
 * @brief 停止超时处理线程
 */
void Client::stopTimeoutThread()
{
	_timeoutThreadRunning = false;
	if (_timeoutThread.joinable())
	{
		_timeoutThread.join();
	}
}

/**
 * @brief 同步线程循环
 * 
 * 每5秒向对方发送一次完整的棋盘状态，防止丢包或棋盘不同步
 */
void Client::syncThreadLoop()
{
	while (_syncThreadRunning.load())
	{
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - _lastSyncTime);
		
		// 检查是否到达同步时间（5秒）
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
				
				// 序列化当前棋盘状态
				std::string boardState = getBoardStateString();
				lanp2p::PeerInfo opponent;
				{
					std::lock_guard<std::mutex> lk(_matchMutex);
					opponent = _match.peer;
				}
				
				// 发送棋盘状态给对方
				bool sendSuccess = _node.sendBoardState(opponent.ip, opponent.tcpPort, boardState);
				
				// 更新同步状态
				{
					std::lock_guard<std::mutex> lk(_syncStatusMutex);
					_lastSyncSuccess = sendSuccess;
					if (!sendSuccess)
					{
						_lastSyncError = "Network send failed (connection timeout or refused)";
					}
					else
					{
						_lastSyncError.clear();
					}
				}
				
				_lastSyncTime = now;
				_isSyncing = false;
			}
		}
		
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
}

/**
 * @brief 启动同步线程
 */
void Client::startSyncThread()
{
	_syncThreadRunning = true;
	_syncThread = std::thread(&Client::syncThreadLoop, this);
}

/**
 * @brief 停止同步线程
 */
void Client::stopSyncThread()
{
	_syncThreadRunning = false;
	if (_syncThread.joinable())
	{
		_syncThread.join();
	}
}

/**
 * @brief 获取当前棋盘状态的序列化字符串
 */
std::string Client::getBoardStateString() const
{
	if (!_chessBoard)
		return "";
	return SerializeBoardState(_boardSize, _chessBoard);
}

/**
 * @brief 初始化游戏状态
 * 
 * 执行步骤：
 * 1. 清理旧的游戏状态
 * 2. 创建新的棋盘
 * 3. 根据匹配ID和角色确定先后手
 * 4. 初始化游戏变量
 * 5. 启动同步线程
 */
void Client::initGameState()
{
	// 清理旧状态（包括停止同步线程）
	cleanupGameState();
	
	// 初始化新棋盘
	if (!OnlineInitChessBoard(&_chessBoard, _boardSize))
	{
		endMatch();
		return;
	}

	// 获取匹配ID
	std::string matchId;
	{
		std::lock_guard<std::mutex> lk(_matchMutex);
		matchId = _match.matchId;
	}

	// 根据匹配ID的第一个字符判断奇偶性
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

	// 先后手规则：发起者+奇数ID=先手；响应者+偶数ID=先手
	bool iAmFirstPlayer = (_iAmMatchInitiator && matchIdIsOdd) || (!_iAmMatchInitiator && !matchIdIsOdd);

	// 初始化游戏变量
	_myPlayer = iAmFirstPlayer ? '1' : '2';
	_myTurn = (_myPlayer == '1');
	_gameRunning = true;
	_opponentMoved = false;
	_gameResult = 0;
	
	// 确保旧的同步线程已停止，再启动新的
	stopSyncThread();
	startSyncThread();
}

/**
 * @brief 清理游戏状态
 * 
 * 先停止同步线程（防止访问已释放的内存），再释放棋盘内存
 */
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

/**
 * @brief 尝试放置己方棋子
 * 
 * @return true 落子成功，false 落子失败
 */
bool Client::tryPlaceMyPiece(int x, int y, int z)
{
	// 检查前置条件：必须轮到自己、游戏运行中、未在同步
	if (!_myTurn || !_gameRunning.load() || _isSyncing.load())
		return false;
		
	int coords[3] = { x, y, z };
	
	// 更新棋盘状态
	if (UpdateBoardState(_boardSize, _chessBoard, coords, _myPlayer))
	{
		// 获取对手信息
		lanp2p::PeerInfo opponent;
		{
			std::lock_guard<std::mutex> lk(_matchMutex);
			opponent = _match.peer;
		}

		// 向对手发送落子信息
		_node.sendGameMove(opponent.ip, opponent.tcpPort, coords[0], coords[1], coords[2]);
		
		// 检查是否获胜
		if (CheckWin(_boardSize, _chessBoard, coords, _myPlayer))
		{
			_gameRunning = false;
			_gameResult = 1;  // 己方获胜
			return true;
		}
		
		// 交换回合
		_myTurn = false;
		return true;
	}
	
	return false;
}

/**
 * @brief 尝试获取对手的落子
 * 
 * @return true 获取成功，false 对手未落子
 */
bool Client::tryGetOpponentMove(int& outX, int& outY, int& outZ)
{
	std::lock_guard<std::mutex> lk(_moveMutex);
	
	if (_opponentMoved)
	{
		// 输出对手落子坐标
		outX = _opponentMove[0];
		outY = _opponentMove[1];
		outZ = _opponentMove[2];
		
		// 确定对手玩家标识
		char opponentPlayer = (_myPlayer == '1') ? '2' : '1';
		
		// 更新棋盘
		UpdateBoardState(_boardSize, _chessBoard, _opponentMove, opponentPlayer);
		
		// 检查对手是否获胜
		if (CheckWin(_boardSize, _chessBoard, _opponentMove, opponentPlayer))
		{
			_gameRunning = false;
			_gameResult = 2;  // 对方获胜
		}
		
		// 重置标志并交换回合
		_opponentMoved = false;
		_myTurn = true;
		return true;
	}
	
	return false;
}

/**
 * @brief 获取距离上次同步的秒数
 */
int Client::getTimeSinceLastSync() const
{
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - _lastSyncTime);
	return static_cast<int>(elapsed.count());
}

/**
 * @brief 获取最后一次同步的状态信息
 * 
 * @param outSuccess 输出参数：是否成功
 * @param outError 输出参数：错误信息（如果失败）
 */
void Client::getLastSyncStatus(bool& outSuccess, std::string& outError) const
{
	std::lock_guard<std::mutex> lk(_syncStatusMutex);
	outSuccess = _lastSyncSuccess;
	outError = _lastSyncError;
}

