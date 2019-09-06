#pragma once
#include "Scene.h"
#include "Sprite.h"
#include "Network.h"
#include <string>
#include <map>
#include <conio.h>
#include <time.h>

enum OBJ_CLASS { PLAYER, BAT, SLIME, EYEBALL, BIGWORM, GHOST };
enum DIR { UP = 0, DOWN, RIGHT, LEFT };
// Ä­ Å©±â
constexpr int Width = 40;
constexpr int Height = 40;

// ¸Ê º¹»ç ¼ö
constexpr int MAX_MAP_COPY = (WORLD_WIDTH * WORLD_HEIGHT) / 10000;

// °´Ã¼ ±¸ºÐÀ» À§ÇÑ »ó¼ö
constexpr int BACKGROUND = 0;
constexpr int BACKGROUND2 = 1;
constexpr int BACKGROUND3 = 2;
constexpr int NPC = 3;
constexpr int MONSTER_BIGWORM = 4;
constexpr int MONSTER_EYEBALL = 5;
constexpr int MONSTER_GHOST = 6;
constexpr int MONSTER_SLIME = 7;
constexpr int PORTION = 8;
constexpr int COIN = 9;
constexpr int HIT = 10;

constexpr int PAWN = 0;
constexpr int ROOK = 1;
constexpr int KNIGHT = 2;
constexpr int BISHOP = 3;
constexpr int QUEEN = 4;
constexpr int KING = 5;

struct POSITION {
	POSITION() :x(0), y(0) {}
	POSITION(int x1, int y1) :x(x1), y(y1) {}
	int x;
	int y;
};

struct CHESSBOARD {
	int index[WORLD_HEIGHT][WORLD_WIDTH] = { 0, };
	bool isFilled;
};

class Player {
private:
	POSITION pos;
	int pieceType;
	bool isDraw;
	Sprite* pSprite;
	int obj_class;
	int hp;
	int exp;
	int level;
	int dir;
	float moveTime, attackTime;
public:
	Player();
	Player(POSITION* p, int type, bool draw);
	~Player();

	void MakePlayerSprite();

	void getPos(POSITION* p) { *p = pos; }
	int getPieceType() { return pieceType; }
	bool getIsDraw() { return isDraw; }
	Sprite* getSprite() { return pSprite; }
	int getHp() { return hp; }
	int getExp() { return exp; }
	int getLevel() { return level; }
	int getMoveTime() { return moveTime; }
	int getAttackTime() { return attackTime; }
	int getObjClass() { return obj_class; }

	void setPos(POSITION p) { pos = p; }
	void setPos(int x, int y) { pos.x = x; pos.y = y; }
	void setPieceType(int t) { pieceType = t; }
	void setIsDraw(bool b) { isDraw = b; }
	void setHp(int h) { hp = h; }
	void setExp(int e) { exp = e; }
	void setLevel(int l) { level = l; }
	void setMoveTime(float mt) { moveTime = mt; }
	void setAttackTime(float at) { attackTime = at; }
	void setObjClass(int o) { obj_class = o; }
};

class Chess_iocp : public Scene
{
private:
	Network* net;
	Sprite * pSprites;
	CHESSBOARD* pChessBoard;
	//Player * pBlack;
	std::map<int, Player> pWhite;
	Player npc[NUM_NPC];
	//SOCKET sock;
	int myId;
	bool initComplete;
	int g_x;
	int g_y;
	/*WSABUF	send_wsabuf;
	char 	send_buffer[BUF_SIZE];
	WSABUF	recv_wsabuf;
	char	recv_buffer[BUF_SIZE];
	char	packet_buffer[BUF_SIZE];
	DWORD		in_packet_size = 0;
	int		saved_packet_size = 0;*/
public:
	Chess_iocp();
	~Chess_iocp();
public:
	void Render(HDC* cDC);
	void MouseInput(int iMessage, int x, int y);
	void KeyboardInput(int iMessage, int wParam);
	void KeyboardCharInput(int wParam);
	void Enter();
	void Destroy();
	void Update();
	void ProcessPacket(char *ptr);
	void setNetwork(void* n);
};