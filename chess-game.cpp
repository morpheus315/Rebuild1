#include<cstdlib>
#include "chess-game.h"
#include <sstream>
#include <string>

/**
 * 三维棋盘逻辑坐标与存储坐标的转换关系
 * 
 * 例如：BoardSize=5时，坐标(2,4,1)对应的数组索引为：
 * index = (2-1) + (4-1)*5 + (1-1)*25 = 1 + 15 + 0 = 16
 * 即 *(ChessBoard+16) 对应的就是这个位置
 */

/**
 * @brief 将三维坐标转换为一维数组存储位置
 * 
 * 三维棋盘使用一维数组存储，此函数计算对应的索引
 * 
 * @param x X坐标（范围：1到BoardSize）
 * @param y Y坐标（范围：1到BoardSize）
 * @param z Z坐标（范围：1到BoardSize）
 * @param BoardSize 棋盘每个维度的大小
 * @return 一维数组中的索引位置
 */
int place(int x, int y, int z, int BoardSize)
{
	return (x - 1) + (y - 1) * BoardSize + (z - 1) * BoardSize * BoardSize;
}

/**
 * @brief 初始化在线对战棋盘
 * 
 * 为棋盘分配内存空间并清零
 * 
 * @param pChessBoard 指向棋盘指针的指针，用于返回分配的内存地址
 * @param BoardSize 棋盘大小
 * @return true 初始化成功，false 内存分配失败
 */
bool OnlineInitChessBoard(char **pChessBoard, int BoardSize)
{
	// 计算棋盘总格子数（三维空间）
	size_t n = (size_t)BoardSize * (size_t)BoardSize * (size_t)BoardSize;
	
	// 分配内存并初始化为0（calloc会自动清零）
	*pChessBoard = (char *)calloc(n, sizeof(char));
	
	// 检查内存分配是否成功
	if (*pChessBoard == NULL)
	{
		return false;
	}
	return true;
}


/**
 * @brief 更新棋盘状态（落子）
 * 
 * 此函数会检查落子位置的合法性，包括：
 * 1. 坐标是否在棋盘范围内
 * 2. 目标位置是否已有棋子
 * 如果检查通过，则在指定位置放置玩家的棋子
 * 
 * @param BoardSize 棋盘大小
 * @param ChessBoard 棋盘数组
 * @param input 落子坐标数组 [x, y, z]
 * @param player 玩家标识（'1' 表示玩家1，'2' 表示玩家2）
 * @return true 落子成功，false 落子失败（位置非法或已占用）
 */
bool UpdateBoardState(int BoardSize, char *ChessBoard, int input[], char player)
{
	// 检查坐标是否在合法范围内（1到BoardSize）
	if (input[0] < 1 || input[0] > BoardSize || 
	    input[1] < 1 || input[1] > BoardSize || 
	    input[2] < 1 || input[2] > BoardSize)
	{
		return false;  // 坐标超出棋盘范围
	}

	// 计算落子位置在一维数组中的索引
	int newChessIndex = place(input[0], input[1], input[2], BoardSize);

	// 检查目标位置是否已经有棋子（0表示空位）
	if (ChessBoard[newChessIndex] != 0)
	{
		return false;  // 该位置已被占用
	}

	// 位置合法且为空，放置棋子
	ChessBoard[newChessIndex] = player;

	return true;
}

/**
 * @brief 检查是否获胜
 * 
 * 以最后落子位置为中心，检查13个方向是否存在五子连珠：
 * - 3个轴向方向：x轴、y轴、z轴
 * - 6个平面对角线：xy平面2个、yz平面2个、xz平面2个
 * - 4个空间对角线
 * 
 * 对每个方向，会尝试5个不同的起始位置（包含当前落子位置的连续5子）
 * 
 * @param BoardSize 棋盘大小（9表示9x9x9的棋盘）
 * @param ChessBoard 棋盘数组
 * @param input 最后落子的坐标 [x, y, z]
 * @param player 玩家标识
 * @return 1 该玩家获胜，0 未获胜
 */
int CheckWin(int BoardSize, char* ChessBoard, int input[], char player)
{
	int x = input[0], y = input[1], z = input[2];

	// 定义13个方向的向量
	// 每个方向用 [dx, dy, dz] 表示
	int dir[13][3] = {
		{1, 0, 0},    // X轴正方向
		{0, 1, 0},    // Y轴正方向
		{0, 0, 1},    // Z轴正方向
		{1, 1, 0},    // XY平面对角线1
		{1, -1, 0},   // XY平面对角线2
		{0, 1, 1},    // YZ平面对角线1
		{0, 1, -1},   // YZ平面对角线2
		{1, 0, 1},    // XZ平面对角线1
		{1, 0, -1},   // XZ平面对角线2
		{1, 1, 1},    // 空间对角线1
		{1, 1, -1},   // 空间对角线2
		{1, -1, 1},   // 空间对角线3
		{1, -1, -1}   // 空间对角线4
	};

	// 遍历每个方向
	for (int d = 0; d < 13; d++)
	{
		int dx = dir[d][0];
		int dy = dir[d][1];
		int dz = dir[d][2];

		// 对于每个方向，尝试5个不同的起始位置
		// st从-4到0，表示当前落子位置在这5个连续位置中的相对位置
		for (int st = -4; st <= 0; st++)
		{
			int count = 0;      // 连续棋子计数
			bool valid = true;  // 是否所有位置都在棋盘内

			// 检查从起始位置开始的连续5个位置
			for (int step = 0; step < 5; step++)
			{
				// 计算当前检查位置的坐标
				int nx = x + (st + step) * dx;
				int ny = y + (st + step) * dy;
				int nz = z + (st + step) * dz;

				// 检查坐标是否在棋盘范围内
				if (nx < 1 || nx > BoardSize ||
					ny < 1 || ny > BoardSize ||
					nz < 1 || nz > BoardSize)
				{
					valid = false;  // 超出棋盘范围
					break;
				}

				// 检查该位置是否是当前玩家的棋子
				if (ChessBoard[place(nx, ny, nz, BoardSize)] == player)
				{
					count++;  // 找到一个己方棋子
				}
				else
				{
					break;  // 遇到空位或对方棋子，停止检查
				}
			}

			// 如果找到有效的五子连珠
			if (valid && count == 5)
			{
				return 1;  // 获胜
			}
		}
	}

	return 0;  // 未获胜
}

/**
 * @brief 将棋盘状态序列化为字符串
 * 
 * 遍历整个棋盘，将所有非空位置的信息编码为字符串
 * 格式：x,y,z,player;x,y,z,player;...
 * 
 * @param BoardSize 棋盘大小
 * @param ChessBoard 棋盘数组
 * @return 序列化后的字符串
 */
std::string SerializeBoardState(int BoardSize, char *ChessBoard)
{
	std::ostringstream oss;  // 字符串输出流
	bool first = true;       // 标记是否是第一个棋子（用于控制分号）
	
	// 遍历整个三维棋盘
	for (int i = 1; i <= BoardSize; ++i)
	{
		for (int j = 1; j <= BoardSize; ++j)
		{
			for (int k = 1; k <= BoardSize; ++k)
			{
				int idx = place(i, j, k, BoardSize);
				
				// 只序列化非空位置
				if (ChessBoard[idx] != 0)
				{
					// 如果不是第一个棋子，先添加分号分隔符
					if (!first) oss << ";";
					
					// 写入：x坐标,y坐标,z坐标,玩家标识
					oss << i << "," << j << "," << k << "," << (int)ChessBoard[idx];
					first = false;
				}
			}
		}
	}
	
	return oss.str();
}

/**
 * @brief 从字符串反序列化棋盘状态
 * 
 * 解析序列化字符串，恢复棋盘上的所有棋子
 * 
 * @param BoardSize 棋盘大小
 * @param ChessBoard 目标棋盘数组（会被修改）
 * @param boardState 序列化的棋盘状态字符串
 */
void DeserializeBoardState(int BoardSize, char *ChessBoard, const std::string &boardState)
{
	// 如果字符串为空，直接返回
	if (boardState.empty())
		return;
	
	std::istringstream iss(boardState);  // 字符串输入流
	std::string piece;                   // 单个棋子的信息
	
	// 以分号为分隔符读取每个棋子的信息
	while (std::getline(iss, piece, ';'))
	{
		if (piece.empty()) continue;  // 跳过空字符串
		
		int x, y, z, player;  // 棋子的坐标和玩家标识
		char comma;           // 逗号分隔符（读取后丢弃）
		std::istringstream pieceStream(piece);
		
		// 解析格式：x,y,z,player
		if (pieceStream >> x >> comma >> y >> comma >> z >> comma >> player)
		{
			// 验证坐标和玩家标识的合法性
			if (x >= 1 && x <= BoardSize && 
			    y >= 1 && y <= BoardSize && 
			    z >= 1 && z <= BoardSize && 
			    (player == 1 || player == 2))
			{
				// 在棋盘上放置棋子
				int idx = place(x, y, z, BoardSize);
				ChessBoard[idx] = (char)player;
			}
		}
	}
}