#pragma once
#include "Scene.h"
#include "MyInclude.h"
#include "Sprite.h"
#include <string>
#include <map>
#include <conio.h>

// 체스판 칸
const int ROW = 8;
const int COL = 8;

// 칸 크기
const int Width = 60;
const int Height = 60;

// 객체 구분을 위한 상수
const int PAWN = 0;
const int ROOK = 1;
const int KNIGHT = 2;
const int BISHOP = 3;
const int QUEEN = 4;
const int KING = 5;

// 패킷 타입
const int TYPE_ENTER = 0;
const int TYPE_INGAME = 1;
const int TYPE_EXIT = 2;

const int MAX_CLIENT = 10;

struct POSITION {
	POSITION() :x(0), y(0) {}
	POSITION(int x1, int y1) :x(x1), y(y1) {}
	int x;
	int y;
};

struct ClientInfo
{
	POSITION pos;
};

struct CS_SOCK {
	int key;
};

struct SC_SOCK {
	int type;
	int myId;
	ClientInfo clientInfo;
};

struct SC_INIT_SOCK {
	int myId;
	//std::map<int, POSITION> clientInfo;
	int clientId[MAX_CLIENT];
	POSITION clientInfo[MAX_CLIENT];
};

struct CONNECT_SOCK {
	bool isConnected;
};

struct CHESSBOARD {
	int index[COL][ROW] = { 0, };
	bool isFilled;
};

class Player {
private:
	POSITION pos;
	int pieceType;
	bool isDraw;
	Sprite* pSprite;
public:
	Player();
	Player(POSITION* p, int t, bool b);
	~Player();

	void MakeSprite();

	void getPos(POSITION* p) { *p = pos; }
	int getPieceType() { return pieceType; }
	bool getIsDraw() { return isDraw; }
	Sprite& getSprite() { return *pSprite; }

	void setPos(POSITION* p) { pos = *p; }
	void setPieceType(int t) { pieceType = t; }
	void setIsDraw(bool b) { isDraw = b; }
};

class Chess : public Scene
{
private:
	Sprite * pBGSprite;			// 사용할 이미지 또는 애니메이션
	CHESSBOARD* pChessBoard;
	//Player * pBlack;
	std::map<int, Player> pWhite;
	SOCKET sock;
	int myId;
	bool initComplete;
public:
	Chess();
	~Chess();
public:
	void Render(HDC* cDC);
	void MouseInput(int iMessage, int x, int y);
	void KeyboardInput(int iMessage, int wParam);
	void KeyboardCharInput(int wParam);
	void Enter();
	void Destroy();
	void Update();
};