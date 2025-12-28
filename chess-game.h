#pragma once

#include <string>

/**
 * @brief 计算三维坐标在一维数组中的索引位置
 * 
 * 将三维棋盘坐标(x,y,z)转换为一维数组索引
 * 公式：index = (x-1) + (y-1)*BoardSize + (z-1)*BoardSize?
 * 
 * @param x X坐标（1到BoardSize）
 * @param y Y坐标（1到BoardSize）
 * @param z Z坐标（1到BoardSize）
 * @param BoardSize 棋盘大小（每个维度的格子数）
 * @return 一维数组索引
 */
int place(int x, int y, int z, int BoardSize);

/**
 * @brief 初始化在线对战的棋盘
 * 
 * 为棋盘分配内存并初始化为0（空棋盘）
 * 
 * @param pChessBoard 指向棋盘指针的指针（用于返回分配的内存地址）
 * @param BoardSize 棋盘大小
 * @return true 初始化成功，false 内存分配失败
 */
bool OnlineInitChessBoard(char **pChessBoard, int BoardSize);

/**
 * @brief 更新棋盘状态（放置棋子）
 * 
 * 检查落子位置是否合法，如果合法则放置棋子
 * 
 * @param BoardSize 棋盘大小
 * @param ChessBoard 棋盘数组
 * @param input 落子坐标数组 [x, y, z]
 * @param player 玩家标识（'1' 或 '2'）
 * @return true 落子成功，false 位置非法或已有棋子
 */
bool UpdateBoardState(int BoardSize, char *ChessBoard, int input[], char player);

/**
 * @brief 检查是否有玩家获胜
 * 
 * 检查从最后落子位置开始，是否在任意方向上形成五子连珠
 * 支持13个方向：3个轴向、6个平面对角线、4个空间对角线
 * 
 * @param BoardSize 棋盘大小
 * @param ChessBoard 棋盘数组
 * @param input 最后落子的坐标 [x, y, z]
 * @param player 玩家标识
 * @return 1 该玩家获胜，0 未获胜
 */
int CheckWin(int BoardSize, char *ChessBoard, int input[], char player);

/**
 * @brief 将棋盘状态序列化为字符串
 * 
 * 格式：x1,y1,z1,player1;x2,y2,z2,player2;...
 * 例如："1,2,3,1;4,5,6,2" 表示位置(1,2,3)有玩家1的棋子，位置(4,5,6)有玩家2的棋子
 * 只序列化非空位置
 * 
 * @param BoardSize 棋盘大小
 * @param ChessBoard 棋盘数组
 * @return 序列化后的字符串
 */
std::string SerializeBoardState(int BoardSize, char *ChessBoard);

/**
 * @brief 从字符串反序列化棋盘状态
 * 
 * 解析序列化字符串并恢复棋盘状态
 * 
 * @param BoardSize 棋盘大小
 * @param ChessBoard 目标棋盘数组（会被修改）
 * @param boardState 序列化的棋盘状态字符串
 */
void DeserializeBoardState(int BoardSize, char *ChessBoard, const std::string &boardState);
