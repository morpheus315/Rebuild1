#include<cstdlib>
#include "chess-game.h"
//棋盘位置逻辑：右手系xyz，先沿x再沿y再沿z，比如BoardSize=5，那*(ChessBoard+17)对应的棋子位置就是（2，4，1），计算公式：17=（2-1）+（4-1）* 5 +（1-1）* 25


//根据坐标输出棋子存储位置
int place(int x, int y, int z, int BoardSize)
{
	return (x - 1) + (y - 1) * BoardSize + (z - 1) * BoardSize * BoardSize;
}

bool OnlineInitChessBoard(char **pChessBoard, int BoardSize)
{
	size_t n = (size_t)BoardSize * (size_t)BoardSize * (size_t)BoardSize;
	*pChessBoard = (char *)calloc(n, sizeof(char));
	if (*pChessBoard == NULL)
	{
		return false;
	}
	return true;
}


//新函数：接受棋局描述，检查位置合法性，如果合法则更新棋局，否则输出非法标记
bool UpdateBoardState(int BoardSize, char *ChessBoard, int input[], char player)
{
	// 检查坐标是否在棋盘范围内
	if (input[0] < 1 || input[0] > BoardSize || input[1] < 1 || input[1] > BoardSize || input[2] < 1
	        || input[2] > BoardSize)
	{
		return false;
	}

	// 计算棋子在一维数组中的索引
	int newChessIndex = place(input[0], input[1], input[2], BoardSize);

	// 检查目标位置是否已有棋子
	if (ChessBoard[newChessIndex] != 0)
	{
		return false;
	}

	// 位置合法，更新棋盘状态
	ChessBoard[newChessIndex] = player;

	return true;
}

//以落子点为中心检测9*9*9的空间内是否有连着的5个棋子，有输出谁赢了以及怎么赢的并返回1，无返回0，各部分检测用花括号括起来以减少占用
int CheckWin(int BoardSize, char* ChessBoard, int input[], char player)
{
	int x = input[0], y = input[1], z = input[2];

	
	int dir[13][3] = {
		{1, 0, 0},
		{0, 1, 0},
		{0, 0, 1},
		{1, 1, 0},
		{1, -1, 0},
		{0, 1, 1},
		{0, 1, -1},
		{1, 0, 1},
		{1, 0, -1},
		{1, 1, 1},
		{1, 1, -1},
		{1, -1, 1},
		{1, -1, -1}
	};

	for (int d = 0; d < 13; d++)
	{
		int dx = dir[d][0];
		int dy = dir[d][1];
		int dz = dir[d][2];

		
		for (int st = -4; st <= 0; st++)
		{
			int count = 0;
			bool valid = true;

			for (int step = 0; step < 5; step++)
			{
				int nx = x + (st + step) * dx;
				int ny = y + (st + step) * dy;
				int nz = z + (st + step) * dz;

				if (nx < 1 || nx > BoardSize ||
					ny < 1 || ny > BoardSize ||
					nz < 1 || nz > BoardSize)
				{
					valid = false;
					break;
				}

				if (ChessBoard[place(nx, ny, nz, BoardSize)] == player)
				{
					count++;
				}
				else
				{
					break;
				}
			}

			if (valid && count == 5)
			{
				return 1;
			}
		}
	}

	return 0;
}