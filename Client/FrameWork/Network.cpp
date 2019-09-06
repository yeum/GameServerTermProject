#include "Network.h"
#pragma warning(disable : 4996)

Network::Network()
{
	sock = NULL;
	myId = -1;
	in_packet_size = 0;
	saved_packet_size = 0;
}

Network::~Network()
{

}

void Network::connectToServer(HWND hWnd)
{
	// 서버 IP주소 입력받기
	std::string s;
	printf("서버IP입력 : ");
	std::cin >> s;
	const char *serverIp = s.c_str();

	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		PostQuitMessage(0);

	// socket()
	sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
	if (sock == INVALID_SOCKET) err_quit("socket()");


	// 넌블로킹 소켓으로 전환
	/*u_long on = 1;
	int retval = ioctlsocket(sock, FIONBIO, &on);
	if (retval == SOCKET_ERROR)
		err_quit("ioctlsocket()");*/

	// connect()
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(serverIp);
	serveraddr.sin_port = htons(SERVER_PORT);
	int retval = WSAConnect(sock, (SOCKADDR *)&serveraddr, sizeof(serveraddr), NULL, NULL, NULL, NULL);
	if (retval == SOCKET_ERROR)
	{
		if (GetLastError() != WSAEWOULDBLOCK)
			err_quit("connect()");
	}

	WSAAsyncSelect(sock, hWnd, WM_SOCKET, FD_CLOSE | FD_READ);

	send_wsabuf.buf = send_buffer;
	send_wsabuf.len = BUF_SIZE;
	recv_wsabuf.buf = recv_buffer;
	recv_wsabuf.len = BUF_SIZE;


	std::wstring s2;
	printf("ID 입력 : ");
	std::wcin >> s2;

	SendLoginPacket(const_cast<wchar_t *>(s2.c_str()));

}

SOCKET Network::getSock()
{
	return sock;
}

void Network::ReadPacket(Scene* scene)
{
	DWORD iobyte, ioflag = 0;

	int retval = WSARecv(sock, &recv_wsabuf, 1, &iobyte, &ioflag, NULL, NULL);
	if (retval) {
		err_display("WSARecv()");
	}
	BYTE *ptr = reinterpret_cast<BYTE *>(recv_buffer);

	while (0 != iobyte)
	{
		if (0 == in_packet_size)
			in_packet_size = ptr[0];

		int required = in_packet_size - saved_packet_size;

		if (iobyte + saved_packet_size >= in_packet_size)
		{// 완성할 수 있을 때
			memcpy(packet_buffer + saved_packet_size, ptr, required);
			// 각 Scene의 ProcessPacket으로 처리를 넘김
			scene->ProcessPacket(packet_buffer);
			ptr += required;
			iobyte -= required;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else
		{// 완성 못 할 때
			memcpy(packet_buffer + saved_packet_size, ptr, iobyte);
			saved_packet_size += iobyte;
			iobyte = 0;
		}
	}
}

void Network::SendPacket()
{
	DWORD iobyte = 0;
	int retval = WSASend(sock, &send_wsabuf, 1, &iobyte, 0, NULL, NULL);
	if (retval)
	{
		if (GetLastError() != WSAEWOULDBLOCK)
		{
			err_display("WSASend()");
		}
	}
}

void Network::SendLoginPacket(wchar_t * c)
{
	cs_packet_login *packet = reinterpret_cast<cs_packet_login *>(send_buffer);
	memcpy(packet->player_id, c, sizeof(c));
	packet->size = sizeof(cs_packet_login);
	send_wsabuf.len = sizeof(cs_packet_login);
	packet->type = CS_LOGIN;

	SendPacket();
}

void Network::SendLogoutPacket()
{
	cs_packet_logout *packet = reinterpret_cast<cs_packet_logout *>(send_buffer);
	packet->size = sizeof(packet);
	send_wsabuf.len = sizeof(packet);
	packet->type = CS_LOGOUT;

	SendPacket();
}

void Network::SendMovePacket(int wParam)
{
	cs_packet_move *packet = NULL;
	switch (wParam)
	{
	case VK_UP:
		packet = reinterpret_cast<cs_packet_move *>(send_buffer);
		packet->size = sizeof(packet);
		send_wsabuf.len = sizeof(packet);
		packet->type = CS_MOVE;
		packet->direction = DIR_UP;
		break;
	case VK_DOWN:
		packet = reinterpret_cast<cs_packet_move *>(send_buffer);
		packet->size = sizeof(packet);
		send_wsabuf.len = sizeof(packet);
		packet->type = CS_MOVE;
		packet->direction = DIR_DOWN;
		break;
	case VK_RIGHT:
		packet = reinterpret_cast<cs_packet_move *>(send_buffer);
		packet->size = sizeof(packet);
		send_wsabuf.len = sizeof(packet);
		packet->type = CS_MOVE;
		packet->direction = DIR_RIGHT;
		break;
	case VK_LEFT:
		packet = reinterpret_cast<cs_packet_move *>(send_buffer);
		packet->size = sizeof(packet);
		send_wsabuf.len = sizeof(packet);
		packet->type = CS_MOVE;
		packet->direction = DIR_LEFT;
		break;
	}
	SendPacket();
}

void Network::SendAttackPacket()
{
	cs_packet_attack *packet = reinterpret_cast<cs_packet_attack *>(send_buffer);
	packet->size = sizeof(packet);
	send_wsabuf.len = sizeof(packet);
	packet->type = CS_ATTACK;

	SendPacket();
}

// 소켓 함수 오류 출력 후 종료
void Network::err_quit(const char *msg)
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

// 소켓 함수 오류 출력
void Network::err_display(const char *msg)
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