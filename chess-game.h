#pragma once

#include <string>

int place(int x, int y, int z, int BoardSize);
bool OnlineInitChessBoard(char **pChessBoard, int BoardSize);
bool UpdateBoardState(int BoardSize, char *ChessBoard, int input[], char player);
int CheckWin(int BoardSize, char *ChessBoard, int input[], char player);

std::string SerializeBoardState(int BoardSize, char *ChessBoard);
void DeserializeBoardState(int BoardSize, char *ChessBoard, const std::string &boardState);
