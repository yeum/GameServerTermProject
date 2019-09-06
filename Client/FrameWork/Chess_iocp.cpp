#include "Chess_iocp.h"
#include "FWMain.h"
#include <winsock2.h>


Player::Player() : pos(0, 0), pieceType(PAWN), isDraw(false), hp(100), exp(0) {}

Player::Player(POSITION* p, int t, bool b) : pSprite(NULL), pos(*p), pieceType(t), isDraw(b), hp(100), exp(0) {}

Player::~Player()
{
	if (pSprite)
		SAFE_DELETE(pSprite);
}

void Player::MakePlayerSprite()
{
	pSprite = new Sprite;

	pSprite->Entry(PAWN, "image/character.bmp", pos.x*Width, pos.y*Height);
	/*pSprite->Entry(ROOK, "image/w_rook_s.bmp", pos.x*Width, pos.y*Height);
	pSprite->Entry(KNIGHT, "image/w_knight.bmp", pos.x*Width, pos.y*Height);
	pSprite->Entry(BISHOP, "image/w_bishop.bmp", pos.x*Width, pos.y*Height);
	pSprite->Entry(QUEEN, "image/w_queen.bmp", pos.x*Width, pos.y*Height);
	pSprite->Entry(KING, "image/w_king.bmp", pos.x*Width, pos.y*Height);*/
}

Chess_iocp::Chess_iocp()
{
	pSprites = new Sprite;
	pSprites->Entry(BACKGROUND, "image/caveMap.bmp", 0, 0);
	pSprites->Entry(BACKGROUND2, "image/caveMap2.bmp", 0, 0);
	pSprites->Entry(BACKGROUND3, "image/caveMap3.bmp", 0, 0);
	pSprites->Entry(NPC, "image/bat.bmp", 0, 0);
	pSprites->Entry(MONSTER_BIGWORM, "image/bigworm.bmp", 0, 0);
	pSprites->Entry(MONSTER_EYEBALL, "image/eyeball.bmp", 0, 0);
	pSprites->Entry(MONSTER_GHOST, "image/ghost.bmp", 0, 0);
	pSprites->Entry(MONSTER_SLIME, "image/slime.bmp", 0, 0);
	pSprites->Entry(PORTION, "image/portion.bmp", 0, 0);
	pSprites->Entry(COIN, "image/coin.bmp", 0, 0);
	pSprites->Entry(HIT, "image/hit.bmp", 0, 0);

	for (int i = 0; i < NUM_NPC; ++i)
	{
		npc[i].setPieceType(ROOK);
		//npc[i].MakeNPCSprite();
		npc[i].setIsDraw(false);
	}
	pChessBoard = NULL;
	myId = 1;
	initComplete = false;
	//pBlack = NULL;
}
Chess_iocp::~Chess_iocp()
{
	if (pChessBoard)
		SAFE_DELETE(pChessBoard);
	//if(pBlack)
	//	SAFE_DELETE(pBlack);
	pWhite.clear();
}

void Chess_iocp::Enter()
{
	if (pChessBoard == NULL)
		pChessBoard = new CHESSBOARD;
}
void Chess_iocp::Destroy()
{
	if (pChessBoard)
		SAFE_DELETE(pChessBoard);
	//if (pBlack)
	//	SAFE_DELETE(pBlack);
	pWhite.clear();
}
void Chess_iocp::Render(HDC* cDC)
{
	int count = 0;
	for (int y = 0; y < WORLD_HEIGHT / 30; ++y)
	{
		for (int x = 0; x < WORLD_WIDTH / 30; ++x)
		{
			// 64픽셀짜리 칸이 30개 있는 너비&높이
			int xPos = x * (30 * Width);
			int yPos = y * (30 * Height);
			if (count % 3 == 0)
				pSprites->Render(cDC, BACKGROUND, xPos + g_x * Width, yPos + g_y * Height);
			else if(count%3 == 1)
				pSprites->Render(cDC, BACKGROUND2, xPos + g_x * Width, yPos + g_y * Height);
			else if (count % 3 == 2)
				pSprites->Render(cDC, BACKGROUND3, xPos + g_x * Width, yPos + g_y * Height);
			++count;
		}
	}
	for (auto& d : pWhite)
	{
		if (d.first == myId)
		{
			// 캐릭터 render위치가 어색하지 않게 직접 재조정
			d.second.getSprite()->Render(cDC, d.second.getPieceType(), (UINT)RGB(255, 0, 255),
				d.second.getSprite()->getX(d.second.getPieceType())-Width/4, 
				d.second.getSprite()->getY(d.second.getPieceType()) - Height/2);
		}
		else
		{
			POSITION tmp;
			d.second.getPos(&tmp);

			d.second.getSprite()->Render(cDC, d.second.getPieceType(), (UINT)RGB(255, 0, 255),
				((tmp.x+g_x)*Width) - Width/4,
				((tmp.y+g_y)*Height) - Height / 2);
		}
	}
	for (int i = 0; i < NUM_NPC - MAX_USER; ++i)
	{
		if (true == npc[i].getIsDraw())
		{
			POSITION tmp;
			npc[i].getPos(&tmp);
			switch (npc[i].getObjClass())
			{
			case BAT:
				pSprites->Render(cDC, NPC, (UINT)RGB(255, 0, 255), (tmp.x + g_x)*Width, (tmp.y + g_y)*Height);
				break;
			case SLIME:
				pSprites->Render(cDC, MONSTER_SLIME, (UINT)RGB(255, 0, 255), (tmp.x + g_x)*Width, (tmp.y + g_y)*Height);
				break;
			case EYEBALL:
				pSprites->Render(cDC, MONSTER_EYEBALL, (UINT)RGB(255, 0, 255), (tmp.x + g_x)*Width, (tmp.y + g_y)*Height);
				break;
			case BIGWORM:
				pSprites->Render(cDC, MONSTER_BIGWORM, (UINT)RGB(255, 0, 255), (tmp.x + g_x)*Width, (tmp.y + g_y)*Height);
				break;
			case GHOST:
				pSprites->Render(cDC, MONSTER_GHOST, (UINT)RGB(255, 0, 255), (tmp.x + g_x)*Width, (tmp.y + g_y)*Height);
				break;
			}
		}
	}
	//pBlack->getSprite().Render(cDC, pWhite->getPieceType(), (UINT)RGB(255, 0, 255));

	// LEVEL 출력
	// HP 출력
	// EXP 출력
	// 아이템창 출력

	// 채팅창 출력
}

void Chess_iocp::Update()
{

}
void Chess_iocp::MouseInput(int iMessage, int x, int y)
{
	switch (iMessage)
	{
	case WM_LBUTTONDOWN:
		//printf("%d %d\n", x, y);
		break;
	}
}
void Chess_iocp::KeyboardInput(int iMessage, int wParam)
{
	if (iMessage == WM_KEYDOWN)
	{
		switch (wParam)
		{
		case VK_UP:
		case VK_DOWN:
		case VK_RIGHT:
		case VK_LEFT:
		{
			float now = clock() / CLOCKS_PER_SEC;
			if (now - pWhite[myId].getMoveTime() >= 1)
			{
				pWhite[myId].setMoveTime(now);
				net->SendMovePacket(wParam);
			}
			break;
		}
		case 'A':
		{
			float now = clock() / CLOCKS_PER_SEC;
			if (now - pWhite[myId].getAttackTime() >= 1)
			{
				pWhite[myId].setAttackTime(now);
				net->SendAttackPacket();
			}
			break;
		}
		}
	}
}
void Chess_iocp::KeyboardCharInput(int wParam)
{
	switch (wParam)
	{
	case VK_ESCAPE:
		net->SendLogoutPacket();
		PostQuitMessage(0);
		break;
	}
}

void Chess_iocp::ProcessPacket(char *ptr)
{
	Player tmpPlayer;
	POSITION myPos;
	POSITION tmpPos;
	sc_packet_login_ok *p = NULL;
	sc_packet_add_object *p1 = NULL;
	sc_packet_position *p2 = NULL;
	sc_packet_remove_object *p3 = NULL;
	sc_packet_stat_change *p4 = NULL;
	int moveX = 0;
	int moveY = 0;
	int myX = 0;
	int myY = 0;
	switch (ptr[1])
	{
	case SC_LOGIN_OK:
		p = reinterpret_cast<sc_packet_login_ok *>(ptr);
		myId = p->id;
		pWhite[myId].setPos(POSITION(p->x, p->y));
		pWhite[myId].setHp(p->HP);
		pWhite[myId].setExp(p->EXP);
		pWhite[myId].setLevel(p->LEVEL);
		pWhite[myId].setObjClass(PLAYER);
		initComplete = true;
		printf("Login OK! My ID : %d\n", p->id);
		break;
	case SC_LOGIN_FAIL:
		printf("Login Fail\n");
		break;
	case SC_ADD_OBJECT:
		p1 = reinterpret_cast<sc_packet_add_object *>(ptr);
		if (p1->id < MAX_USER)
		{
			tmpPlayer.setPos(POSITION((p1->x), (p1->y)));
			tmpPlayer.setPieceType(PAWN);
			tmpPlayer.setIsDraw(true);
			tmpPlayer.setObjClass(p1->obj_class);
			tmpPlayer.setMoveTime(clock() / CLOCKS_PER_SEC);
			tmpPlayer.setAttackTime(clock() / CLOCKS_PER_SEC);
			pWhite.emplace(p1->id, tmpPlayer);
			pWhite[p1->id].MakePlayerSprite();
			//pWhite[p1->id].getSprite()->setLocation(pWhite[p1->id].getPieceType(), pBGSprite[0]->getX(BACKGROUND) + p1->x*Width, pBGSprite[0]->getY(BACKGROUND) + p1->y*Height);
			//printf("Put Player ID : %d, X: %d, Y: %d\n", p1->id, p1->x, p1->y);
		}
		else
		{
			//if(NULL == npc[p1->id - MAX_USER].getSprite())
			//	npc[p1->id - MAX_USER].MakeNPCSprite();
			npc[p1->id - MAX_USER].setPos(p1->x, p1->y);
			//npc[p1->id - MAX_USER].getSprite()->setLocation(npc[p1->id - MAX_USER].getPieceType(), p1->x*Width, p1->y*Height);
			npc[p1->id - MAX_USER].setObjClass(p1->obj_class);
			npc[p1->id - MAX_USER].setIsDraw(true);
			//printf("Put NPC ID : %d, X: %d, Y: %d\n", p1->id, p1->x, p1->y);
		}
		break;
	case SC_POSITION:
		p2 = reinterpret_cast<sc_packet_position *>(ptr);

		pWhite[p2->id].getPos(&myPos);

		// 변화량
		moveX = p2->x - myPos.x;
		moveY = p2->y - myPos.y;

		if (myId == p2->id)
		{
			g_x -= moveX;
			g_y -= moveY;

			pWhite[myId].setPos(POSITION(p2->x, p2->y));
		}
		else if (p2->id < MAX_USER)
		{
			myX = pWhite[p2->id].getSprite()->getX(pWhite[p2->id].getPieceType());
			myY = pWhite[p2->id].getSprite()->getY(pWhite[p2->id].getPieceType());
			pWhite[p2->id].setPos(POSITION(p2->x, p2->y));
		}
		else
		{
			int npcX = npc[p2->id - MAX_USER].getSprite()->getX(npc[p2->id - MAX_USER].getPieceType());
			int npcY = npc[p2->id - MAX_USER].getSprite()->getY(npc[p2->id - MAX_USER].getPieceType());
			npc[p2->id - MAX_USER].setPos(p2->x, p2->y);
		}
		//pWhite[p2->id].getPos(&myPos);
		//std::cout << "Move Player ID : " << p2->id << ", X: " << p2->x << ", Y: " << p2->y << "\n";
		break;
	case SC_STAT_CHANGE:
		p4 = reinterpret_cast<sc_packet_stat_change *>(ptr);
		//std::cout << p4->id << " stat_change\n";
		if (p4->id < MAX_USER)
		{
			pWhite[p4->id].setLevel(p4->LEVEL);
			pWhite[p4->id].setHp(p4->HP);
			pWhite[p4->id].setExp(p4->EXP);
			std::cout << p4->id << " Player LEVEL : " << p4->LEVEL << " HP : " << p4->HP << " / 100, EXP : " << p4->EXP << "\n";
		}
		else
		{
			npc[p4->id].setHp(p4->HP);
		}
		break;
	case SC_REMOVE_OBJECT:
		p3 = reinterpret_cast<sc_packet_remove_object *>(ptr);
		//printf("ClientID : %d 종료\n", p3->id);
		if (p3->id < MAX_USER)
			pWhite.erase(p3->id);
		else
		{
			npc[p3->id - MAX_USER].setIsDraw(false);
		}
		break;
	default:
		printf("Unknown PACKET type [%d]\n", ptr[1]);
	}
}

void Chess_iocp::setNetwork(void* n)
{
	net = reinterpret_cast<Network *>(n);
}
