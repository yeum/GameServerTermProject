#include "Chess.h"
#include "FWMain.h"

const int BACKGROUND = 0;

#define SERVERIP "127.0.0.1"
#define SERVERPORT 9000
#define BUFSIZE    512

// ���� �Լ� ���� ��� �� ����
void err_quit(const char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	MessageBox(NULL, (LPCTSTR)lpMsgBuf, msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(1);
}

// ���� �Լ� ���� ���
void err_display(const char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[%s] %s", msg, (char *)lpMsgBuf);
	LocalFree(lpMsgBuf);
}

Player::Player() : pos(0, 0), pieceType(PAWN), isDraw(false) {}

Player::Player(POSITION* p, int t, bool b) : pSprite(NULL), pos(*p), pieceType(t), isDraw(b) {}

Player::~Player()
{
	if (pSprite)
		SAFE_DELETE(pSprite);
}

void Player::MakeSprite()
{
	pSprite = new Sprite;

	pSprite->Entry(PAWN, "image/w_pawn.bmp", pos.x*Width, pos.y*Height);
	pSprite->Entry(ROOK, "image/w_rook.bmp", pos.x*Width, pos.y*Height);
	pSprite->Entry(KNIGHT, "image/w_knight.bmp", pos.x*Width, pos.y*Height);
	pSprite->Entry(BISHOP, "image/w_bishop.bmp", pos.x*Width, pos.y*Height);
	pSprite->Entry(QUEEN, "image/w_queen.bmp", pos.x*Width, pos.y*Height);
	pSprite->Entry(KING, "image/w_king.bmp", pos.x*Width, pos.y*Height);
}

Chess::Chess()
{
	pBGSprite = NULL;
	pChessBoard = NULL;
	myId = 1;
	initComplete = false;
	//pBlack = NULL;
}
Chess::~Chess()
{
	if (pBGSprite)
		SAFE_DELETE(pBGSprite);
	if (pChessBoard)
		SAFE_DELETE(pChessBoard);
	//if(pBlack)
	//	SAFE_DELETE(pBlack);
	pWhite.clear();
}

void Chess::Enter()
{
	////////////////////// ���� ���� ////////////////////////
	int retval;

	// ���� IP�ּ� �Է¹ޱ�
	std::string s;
	printf("����IP�Է� : ");
	std::cin >> s;
	const char *serverIp = s.c_str();

	// ���� �ʱ�ȭ
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		PostQuitMessage(0);

	// socket()
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET) err_quit("socket()");


	// �ͺ��ŷ �������� ��ȯ
	u_long on = 1;
	retval = ioctlsocket(sock, FIONBIO, &on);
	if (retval == SOCKET_ERROR)
		err_quit("ioctlsocket()");
	
	// connect()
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(serverIp);
	serveraddr.sin_port = htons(SERVERPORT);
	retval = connect(sock, (SOCKADDR *)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR)
	{
		if(GetLastError() != WSAEWOULDBLOCK)
			err_quit("connect()");
	}

	retval = 0;
	while (retval == 0)
	{
		CONNECT_SOCK connectSock;
		retval = recv(sock, (char *)&connectSock, sizeof(CONNECT_SOCK), 0);
		if ( retval == SOCKET_ERROR)
		{
			if (GetLastError() != WSAEWOULDBLOCK)
			{
				err_display("recv()");
				return;
			}
			retval = 0;
		}
		else
		{
			if (!connectSock.isConnected)
			{
				printf("���� ������ Ŭ���̾�Ʈ ���� �ʰ��߽��ϴ�.\n");
				_getch();
				PostQuitMessage(0);
			}
		}
	}

	// �ڽ��� �����Ͽ� �̹� ���� ���̴� �������� ���� �޾ƿ���
	retval = 0;
	while (retval == 0)
	{
		SC_INIT_SOCK scInitSock;
		retval = recv(sock, (char *)&scInitSock, sizeof(SC_INIT_SOCK), 0);
		if (retval == SOCKET_ERROR)
		{
			if (GetLastError() != WSAEWOULDBLOCK)
			{
				err_display("recv()");
				return;
			}
			retval = 0;
		}
		else
		{
			myId = scInitSock.myId;
			// �迭���� ���ͷ� �ҰŸ� �����ο� �� �޾Ƽ� �� �ο��� ���� ������ ���� emplace_back
			for (int i = 0; i <= myId; ++i)
			{
				pWhite.emplace(scInitSock.clientId[i], Player(&scInitSock.clientInfo[i], PAWN, true));
				pWhite[scInitSock.clientId[i]].MakeSprite();
				printf("ClientID : %d, X : %d, Y : %d\n", scInitSock.clientId[i], scInitSock.clientInfo[i].x, scInitSock.clientInfo[i].y);
			}
			initComplete = true;
		}
	}
	////////////////////// ���� ���� ////////////////////////

	if (pBGSprite == NULL)
	{
		pBGSprite = new Sprite;
		pBGSprite->Entry(BACKGROUND, "image/chessboard.bmp", 0, 0);			// �ִϸ��̼ǿ� �ʿ��� �̹��� �ε�
	}

	if (pChessBoard == NULL)
		pChessBoard = new CHESSBOARD;
	
	/*if (pBlack == NULL)
	{
		POSITION tmp(4, 3);
		pBlack = new Player(&tmp,PAWN,false);
		pBlack->getSprite().Entry(PAWN, "image/b_pawn.bmp", tmp.x*Width, tmp.y*Height);
		pBlack->getSprite().Entry(ROOK, "image/bw_rook.bmp", tmp.x*Width, tmp.y*Height);
		pBlack->getSprite().Entry(KNIGHT, "image/b_knight.bmp", tmp.x*Width, tmp.y*Height);
		pBlack->getSprite().Entry(BISHOP, "image/b_bishop.bmp", tmp.x*Width, tmp.y*Height);
		pBlack->getSprite().Entry(QUEEN, "image/b_queen.bmp", tmp.x*Width, tmp.y*Height);
		pBlack->getSprite().Entry(KING, "image/b_king.bmp", tmp.x*Width, tmp.y*Height);
	}*/
}
void Chess::Destroy()
{
	if (pBGSprite)
		SAFE_DELETE(pBGSprite);
	if (pChessBoard)
		SAFE_DELETE(pChessBoard);
	//if (pBlack)
	//	SAFE_DELETE(pBlack);
	pWhite.clear();
}
void Chess::Render(HDC* cDC)
{
	pBGSprite->Render(cDC, BACKGROUND);

	for(auto& d : pWhite)
		d.second.getSprite().Render(cDC, d.second.getPieceType(), (UINT)RGB(255, 0, 255));
	//pBlack->getSprite().Render(cDC, pWhite->getPieceType(), (UINT)RGB(255, 0, 255));
	
}

Player tmpPlayer;

void Chess::Update()
{
	if (initComplete)
	{
		SC_SOCK scSock;
		if (recv(sock, (char *)&scSock, sizeof(SC_SOCK), 0) == SOCKET_ERROR)
		{
			if (GetLastError() != WSAEWOULDBLOCK)
			{
				err_display("recv()");
				return;
			}
		}
		else
		{// ��� Ŭ���̾�Ʈ���� ������ ��������?? �ʿ� ������ ���� ���͸� ������ �ϳ�?
			switch (scSock.type)
			{
			case TYPE_ENTER:
				tmpPlayer.setPos(&scSock.clientInfo.pos);
				tmpPlayer.setPieceType(PAWN);
				tmpPlayer.setIsDraw(true);
				pWhite.emplace(scSock.myId, tmpPlayer);
				pWhite[scSock.myId].MakeSprite();
				break;
			case TYPE_INGAME:
				pWhite[scSock.myId].setPos(&scSock.clientInfo.pos);
				pWhite[scSock.myId].getSprite().setLocation(pWhite[scSock.myId].getPieceType(), scSock.clientInfo.pos.x*Width, scSock.clientInfo.pos.y*Height);
				//printf("ClientID : %d, X : %d, Y : %d\n", scSock.myId, scSock.clientInfo.pos.x, scSock.clientInfo.pos.y);
				break;
			case TYPE_EXIT:
				printf("ClientID : %d ����\n", scSock.myId);
				pWhite.erase(scSock.myId);
				break;
			}
		}
	}
}
void Chess::MouseInput(int iMessage, int x, int y)
{
	switch (iMessage)
	{
	case WM_LBUTTONDOWN:
		//printf("%d %d\n", x, y);
		break;
	}
}
void Chess::KeyboardInput(int iMessage, int wParam)
{
	if (iMessage == WM_KEYDOWN)
	{
		int retval;
		switch (wParam)
		{
		case VK_UP:
		case VK_DOWN:
		case VK_RIGHT:
		case VK_LEFT:
			retval = send(sock, (char *)&wParam, sizeof(int), 0);
			if (retval == SOCKET_ERROR)
			{
				if (GetLastError() != WSAEWOULDBLOCK)
				{
					err_display("send()");
					break;
				}
			}
			break;
		}
	}
}
void Chess::KeyboardCharInput(int wParam)
{
	switch (wParam)
	{
	case VK_ESCAPE:
		PostQuitMessage(0);
		break;
	}
}
