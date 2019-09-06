/*
## ���� ���� : 1 v n - overlapped callback
1. socket()            : ���ϻ���
2. bind()            : ���ϼ���
3. listen()            : ���Ŵ�⿭����
4. accept()            : ������
5. read()&write()
WIN recv()&send    : ������ �а���
6. close()
WIN closesocket    : ��������
*/

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <stdio.h>  
#include <iostream>
#include <vector>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#define UNICODE  
#include <winsock2.h>
#include <Windows.h>
#include <sqlext.h> 

#include "protocol.h"
using namespace std;
using namespace chrono;
#pragma comment(lib, "Ws2_32.lib")

#define MAX_BUFFER		1024
#define VIEW_RADIUS		7	// �Ÿ� ���� 7�̻��̸� �Ⱥ��̵���

enum EVENT_TYPE { EV_MOVE, EV_HEAL, EV_RECV, EV_SEND };

// Overlapped����ü Ȯ��
struct OVER_EX {
	WSAOVERLAPPED	over;
	WSABUF			dataBuffer;
	char			messageBuffer[MAX_BUFFER];
	EVENT_TYPE		event_t;

};

class SOCKETINFO
{
public:
	// in_use�� x,y���� data race�߻�
	// in_use�� true�ΰ� Ȯ���ϰ� send�Ϸ��� �ϴµ� �� ������ ���� �����ؼ� false���ȴٸ�?
	// �׸��� send ���� �� �÷��̾ id�� �����Ѵٸ�? => ������ ���� send�� ��
	// mutex access_lock;
	bool in_use;
	OVER_EX over_ex;
	SOCKET socket;
	// �����Ұ��� �޸𸮸� �������� �����ϱ� ���� �ӽ������
	char packet_buffer[MAX_BUFFER];
	int prev_size;
	short x, y;
	int id;
	bool is_sleeping;
	// ID�� ����, ID�� ������� ���� ������ unordered_set, �� ������ ������ ������ ������ unordered��
	unordered_set<int> view_list;
public:
	SOCKETINFO() {
		in_use = false;
		over_ex.dataBuffer.len = MAX_BUFFER;
		over_ex.dataBuffer.buf = over_ex.messageBuffer;
		over_ex.event_t = EV_RECV;
	}
};


struct EVENT_ST
{
	// �� �̺�Ʈ�� � ��ü���� �߻��ؾ� �ϴ���
	int obj_id;
	// � �̺�Ʈ��
	EVENT_TYPE type;
	// ���� ����Ǿ�� �ϴ���
	high_resolution_clock::time_point start_time;

	// ����� �켱���� ť���� �ð������� �����ϴ� ���� �𸥴�. operator�� �������� ��.
	constexpr bool operator < (const EVENT_ST& _Left) const
	{
		return (start_time > _Left.start_time);
	}
};

mutex timer_l;
priority_queue <EVENT_ST> timer_queue;
SQLHENV henv;
SQLHDBC hdbc;
SQLHSTMT hstmt[MAX_USER];

// <id, client>
// worker thread���� data race�߻�. ��ȣ���� �ʿ�
//SOCKETINFO clients[MAX_USER];

//////////////////// NPC ���� ��� ù ��° ////////////////////
//class NPCINFO
//{
//public:
//	char x, y;
//	unordered_set<int> view_list;
//};
//SOCKETINFO clients[MAX_USER];
//NPCINFO npcs[NUM_NPC];
//// ����: �޸��� ���� ����. �������̴�.
//// ����: �Լ��� �ߺ� ������ �ʿ��ϴ�.
//// ex) Is_Near_Object(int a, int b) ��� �Ұ�.
////		��� Is_Near_Player_Player(int a, int b), Is_Near_Player_Npc(int a, int b), Is_Near_Npc_Npc(int a, int b)
////		�� ���� ������ �߰��� ��������� �Ѵ�.
////		���� ����� ����ؾ� ��.
//
////////////////////// NPC ���� ��� �� ��° ////////////////////
//// �� ���� ���
//class NPCINFO
//{
//public:
//	char x, y;
//	unordered_set<int> view_list;
//};
//
//class SOCKETINFO : public NPCINFO
//{
//public:
//	// in_use�� x,y���� data race�߻�
//	// in_use�� true�ΰ� Ȯ���ϰ� send�Ϸ��� �ϴµ� �� ������ ���� �����ؼ� false���ȴٸ�?
//	// �׸��� send ���� �� �÷��̾ id�� �����Ѵٸ�? => ������ ���� send�� ��
//	// mutex access_lock;
//	bool in_use;
//	OVER_EX over_ex;
//	SOCKET socket;
//	// �����Ұ��� �޸𸮸� �������� �����ϱ� ���� �ӽ������
//	char packet_buffer[MAX_BUFFER];
//	int prev_size;
//	// ID�� ����, ID�� ������� ���� ������ unordered_set, �� ������ ������ ������ ������ unordered��
//	unordered_set<int> view_list;
//public:
//	SOCKETINFO() {
//		in_use = false;
//		over_ex.dataBuffer.len = MAX_BUFFER;
//		over_ex.dataBuffer.buf = over_ex.messageBuffer;
//		over_ex.is_recv = true;
//	}
//};
//NPCINFO *objects[MAX_USER + NUM_NPC];

// ����: �Լ��� �ߺ� ������ �ʿ� ����.
// ����: �������� ���. -> new/delete���� ����. ��Ȱ��. -> ������ ������� �ʱ� ������ ��������
// Ư¡: ID ����� �ϰų� object_type ����� �ʿ��ϴ�. (�߰����� ���⵵)
// �÷��̾�� ��Ŷ������ �ϰ�, NPC�� ������ �ʾƾ� �Ѵ�.
// => �迭 ù 10���� �÷��̾��, Ȥ�� �� 10���� �÷��̾�� ������ ���ؾ� �Ѵ�.
//		�����Ӱ� ���� �� ��� �����ϱ� ���� object_type�� ����� �˻��ؾ� ��.

// NPC ���� �ǽ� �ܼ�����
// ID : 0 ~ 9 => �÷��̾�
// ID : 10 ~ 10 + NUM_NPC - 1 => NPC
// ���� �޸� ����. ������ �����͸� ������� �ʴ´ٴ� �� ��.
SOCKETINFO clients[MAX_USER + NUM_NPC];

HANDLE g_iocp;

void error_display(const char *msg, int err_no)
{
	WCHAR *lpMsgBuf;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, err_no, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
	cout << msg;
	wcout << L"���� [" << err_no << L"] " << lpMsgBuf << "\n";
	while (true);
	LocalFree(lpMsgBuf);
}

// ----------------------------------------------------------
void db_display_error(SQLHANDLE, SQLSMALLINT, RETCODE);
void db_display_error(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
{
	SQLSMALLINT iRec = 0;
	SQLINTEGER iError;
	WCHAR wszMessage[1000];
	WCHAR wszState[SQL_SQLSTATE_SIZE + 1];
	if (RetCode == SQL_INVALID_HANDLE) {
		fwprintf(stderr, L"Invalid handle!\n");
		return;
	}
	while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
		(SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT *)NULL) == SQL_SUCCESS) {
		if (wcsncmp(wszState, L"01004", 5)) {
			fwprintf(stderr, L"[%5.5s] %s (%d) \n", wszState, wszMessage, iError);
		}
	}
}

//-----------------------------------------------------------


void db_show_error() {
	printf("error\n");
}

bool compare_id(int client, int id)
{
	SQLRETURN retcode;
	SQLINTEGER result = -1, posx = -1, posy = -1, user_id = id;
	SQLLEN cb_result = 0, cb_posx = 0, cb_posy = 0;
	SQLLEN cb_uid, tmpIntType = SQL_INTEGER;
	bool isSuccess = false;

	setlocale(LC_ALL, "korean");

	// Allocate environment handle  
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

	// Set the ODBC version environment attribute  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		// Allocate connection handle  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

			// Set login timeout to 5 seconds  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

				// Connect to data source  
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2019_gs_sub7", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					cout << "Database Connect OK!\n";
					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt[client]);

					SQLBindParameter(hstmt[client], 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &user_id, sizeof(user_id), &tmpIntType);
					SQLBindParameter(hstmt[client], 2, SQL_PARAM_OUTPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &result, sizeof(result), &tmpIntType);
					//retcode = SQLExecDirect(hstmt, (SQLWCHAR *)L"SELECT user_id, user_name, user_level FROM user_table ORDER BY 2, 1, 3", SQL_NTS); // 2,1,3 ������ sort
					retcode = SQLExecDirect(hstmt[client], (SQLWCHAR *)L"Exec select_by_user_id ?, ?", SQL_NTS); // ������ 100�ʰ��ϴ� ������ ������ 
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

						// Bind columns 1, 2, and 3  
						//retcode = SQLBindCol(hstmt, 1, SQL_INTEGER, &uid, 10, &cb_uid/*callback. ������ ũ�� ����.*/);
						retcode = SQLBindCol(hstmt[client], 1, SQL_INTEGER, &result, 10, &cb_result);
						retcode = SQLBindCol(hstmt[client], 2, SQL_INTEGER, &posx, 10, &cb_posx);
						retcode = SQLBindCol(hstmt[client], 3, SQL_INTEGER, &posy, 10, &cb_posy);

						// Fetch and print each row of data. On an error, display a message and exit.  
						retcode = SQLFetch(hstmt[client]);
						if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
							db_show_error();
						if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
						{
							if (1 == result)
							{
								clients[client].id = id;
								clients[client].x = (int)posx;
								clients[client].y = (int)posy;
								isSuccess = true;
								cout << "position : " << clients[client].x << ", " << clients[client].y << "\n";
							}
						}
					}
					else {

						db_display_error(hstmt[client], SQL_HANDLE_STMT, retcode);
					}

					// Process data  
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						SQLCancel(hstmt[client]);
						SQLFreeHandle(SQL_HANDLE_STMT, hstmt[client]);
					}

					SQLDisconnect(hdbc);
				}
				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
			}
		}
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}
	return isSuccess;
}

void Initialize_PC()
{
	for (int i = 0; i < MAX_USER; ++i)
	{
		int npc_id = i + MAX_USER;
		clients[i].in_use = false;
	}
}

void add_timer(int obj_id, EVENT_TYPE et, high_resolution_clock::time_point start_time)
{
	timer_l.lock();
	timer_queue.emplace(EVENT_ST{ obj_id, et,  start_time });
	timer_l.unlock();
}

void Initialize_NPC()
{
	for (int i = 0; i < NUM_NPC; ++i)
	{
		int npc_id = i + MAX_USER;
		clients[npc_id].in_use = true;
		clients[npc_id].is_sleeping = true;
		// ������ġ
		clients[npc_id].x = rand() % WORLD_WIDTH;
		clients[npc_id].y = rand() % WORLD_HEIGHT;
		add_timer(npc_id, EV_MOVE, high_resolution_clock::now() + 1s);
	}
}

bool Is_Near_Object(int a, int b)
{
	if (VIEW_RADIUS < abs(clients[a].x - clients[b].x))
		return false;
	if (VIEW_RADIUS < abs(clients[a].y - clients[b].y))
		return false;
	return true;

}

bool is_NPC(int id)
{
	if ((id >= MAX_USER) && (id < MAX_USER + NUM_NPC))
		return true;
	else return false;
}

bool is_sleeping(int id)
{
	return clients[id].is_sleeping;
}

void wakeup_NPC(int id)
{
	// �׳� ������ ���� �÷��̾ ���� ��, �� ��ü�� ���� �ߺ� �����ؼ� �ӵ��� ���� NPC�� ���� ��.
	// ������ �ִ� �ֵ��� üũ�ؾ��Ѵ�.
	if (true == is_sleeping(id)) {
		clients[id].is_sleeping = false;
		EVENT_ST ev;
		ev.obj_id = id;
		ev.type = EV_MOVE;
		ev.start_time = high_resolution_clock::now() + 1s;
		// accept���� push�ϰ� timer���� pop�Ѵ�. data race�߻��Ͽ� ���α׷��� �״´�.
		// mutex�� ��ȣ�ؾ� �Ѵ�.
		timer_l.lock();
		timer_queue.push(ev);
		timer_l.unlock();
	}
}

void do_recv(char id)
{
	DWORD flags = 0;

	if (WSARecv(clients[id].socket, &clients[id].over_ex.dataBuffer, 1, NULL, &flags, &(clients[id].over_ex.over), 0))
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			cout << "Error - IO pending Failure\n";
			while (true);
		}
	}
	else {
		// �񵿱������ ���ư��� �ʰ� ��������� ���ư��ٴ� ����.
		//cout << "Non Overlapped Recv return.\n";
		//while (true);
	}
}

void send_packet(int client, void *packet)
{
	char *p = reinterpret_cast<char *>(packet);
	OVER_EX *ov = new OVER_EX;
	ov->dataBuffer.len = p[0];
	ov->dataBuffer.buf = ov->messageBuffer;
	ov->event_t = EV_SEND;
	memcpy(ov->messageBuffer, p, p[0]);
	ZeroMemory(&ov->over, sizeof(ov->over));
	int error = WSASend(clients[client].socket, &ov->dataBuffer, 1, 0, 0, &ov->over, NULL);
	if (0 != error)
	{
		int err_no = WSAGetLastError();
		if (err_no != WSA_IO_PENDING)
		{
			cout << "Error - IO pending Failure\n";
			error_display("WSASend in send_packet()	", err_no);
			while (true);
		}
	}
	else {
		// �񵿱������ ���ư��� �ʰ� ��������� ���ư��ٴ� ����.
		//cout << "Non Overlapped Send return.\n";
		//while (true);
	}
}

void send_pos_packet(int client, int player)
{
	sc_packet_move_player packet;
	packet.id = player;
	packet.size = sizeof(packet);
	packet.type = SC_MOVE_PLAYER;
	packet.x = clients[player].x;
	packet.y = clients[player].y;

	send_packet(client, &packet);
}

void send_login_ok_packet(int new_id)
{
	sc_packet_login_ok packet;
	packet.id = new_id;
	packet.size = sizeof(packet);
	packet.type = SC_LOGIN_OK;

	send_packet(new_id, &packet);
}

void send_id_ok_packet(int new_id)
{
	sc_packet_id_ok packet;
	packet.size = sizeof(packet);
	packet.type = SC_ID_OK;

	send_packet(new_id, &packet);
}

void send_put_player_packet(int client, int new_id)
{
	sc_packet_put_player packet;
	packet.id = new_id;
	packet.size = sizeof(packet);
	packet.type = SC_PUT_PLAYER;
	packet.x = clients[new_id].x;
	packet.y = clients[new_id].y;

	send_packet(client, &packet);
}
void send_remove_player_packet(int client, int id)
{
	sc_packet_remove_player packet;
	packet.id = id;
	packet.size = sizeof(packet);
	packet.type = SC_REMOVE_PLAYER;

	send_packet(client, &packet);
}

void disconnect_client(int id)
{
	for (int i = 0; i < MAX_USER; ++i)
	{
		if (false == clients[i].in_use)
			continue;
		if (i == id)
			continue;
		if (0 == clients[i].view_list.count(id))
			continue;

		// i�� ���� ���� �ִٸ� �������
		clients[i].view_list.erase(id);
		send_remove_player_packet(i, id);
	}
	closesocket(clients[id].socket);
	clients[id].in_use = false;
	clients[id].view_list.clear();
}

void process_packet(int client, char* packet)
{
	bool isMoving = false;
	cs_packet_my_id *p = reinterpret_cast<cs_packet_my_id *>(packet);
	int x = clients[client].x;
	int y = clients[client].y;

	auto old_vl = clients[client].view_list;
	// 0���� ������, 1���� ��ŶŸ��
	switch (p->type)
	{
	case CS_ID:
		if (false == compare_id(client, p->id))
		{
			disconnect_client(client);
			return;
		}

		clients[client].in_use = true;

		send_id_ok_packet(client);
		send_put_player_packet(client, client);
		// ���� �����鿡�� ���� ������ ������ ���
		for (int i = 0; i < MAX_USER; ++i)
		{
			if (false == clients[i].in_use)
				continue;
			if (false == Is_Near_Object(client, i))		// ��ó�� �ִ� ��쿡�� ������
				continue;
			if (i == client)
				continue;

			clients[i].view_list.insert(client);
			send_put_player_packet(i, client);
		}
		// ó�� ������ ������ ���� ������ ���
		// NPC�� ������ ��. ���������鿡�� NPC���� ���� �ʿ� X
		for (int i = 0; i < MAX_USER + NUM_NPC; ++i)
		{
			if (false == clients[i].in_use)
				continue;
			if (i == client)
				continue;
			if (false == Is_Near_Object(i, client))
				continue;
			// ó�� ���� �� NPC�̵��� ��Ű�� ����
			if (true == is_NPC(i)) wakeup_NPC(i);
			clients[client].view_list.insert(i);
			send_put_player_packet(client, i);
		}
		break;
	case CS_UP:
		if (y > 0)
		{
			y--;
			isMoving = true;
		}
		break;
	case CS_DOWN:
		if (y < (WORLD_HEIGHT - 1))
		{
			y++;
			isMoving = true;
		}
		break;
	case CS_LEFT:
		if (x > 0)
		{
			x--;
			isMoving = true;
		}
		break;
	case CS_RIGHT:
		if (x < (WORLD_WIDTH - 1))
		{
			x++;
			isMoving = true;
		}
		break;
	default:
		wcout << L"���ǵ��� ���� ��Ŷ ���� ����!!\n";
		while (true);
	}

	if (false == isMoving)
		return;
	
	clients[client].x = x;
	clients[client].y = y;

	unordered_set<int> new_vl;
	for (int i = 0; i < MAX_USER; ++i)
	{
		if (i == client) continue;
		if (false == clients[i].in_use) continue;
		if (false == Is_Near_Object(i, client)) continue;
		new_vl.insert(i);
	}

	// NPC������ viewList�� �ֱ�
	for (int i = MAX_USER; i < MAX_USER + NUM_NPC; ++i)
	{
		if (false == Is_Near_Object(i, client)) continue;
		new_vl.insert(i);
	}

	// �ϴ� ������ ������
	send_pos_packet(client, client);

	// 1. old_vl���� �ְ� new_vl���� �ִ� ��ü
	for (auto pl : old_vl)
	{
		// ���� ���
		if (0 == new_vl.count(pl)) continue;
		// NPC��� �ƹ��͵� �� �ʿ� ����
		if (true == is_NPC(pl)) continue;
		// ��� viewlist�� ���� ���� ��
		if (0 < clients[pl].view_list.count(client))
			send_pos_packet(pl, client);
		else {	// ��� viewlist�� ���� ���� ��
			clients[pl].view_list.insert(client);
			send_put_player_packet(pl, client);
		}
	}

	// 2. old_vl�� ���� new_vl���� �ִ� �÷��̾�(���θ����ֵ�)
	for (auto pl : new_vl)
	{
		if (0 < old_vl.count(pl)) continue;
		// �� viewlist�� ��� �߰� �� put player
		clients[client].view_list.insert(pl);
		send_put_player_packet(client, pl);
		if (true == is_NPC(pl)) {
			wakeup_NPC(pl);
			continue;
		}
		// ���� ��� viewlist�� ���ٸ�
		if (0 == clients[pl].view_list.count(client))
		{
			clients[pl].view_list.insert(client);
			send_put_player_packet(pl, client);
		}
		else // �ִٸ�
			send_pos_packet(pl, client);
	}

	// 3. old_vl�� �ְ� new_vl���� ���� �÷��̾�
	for (auto pl : old_vl)
	{
		if (0 < new_vl.count(pl)) continue;
		clients[client].view_list.erase(pl);
		send_remove_player_packet(client, pl);
		if (true == is_NPC(pl)) continue;
		if (0 == clients[pl].view_list.count(pl))
		{
			clients[pl].view_list.erase(client);
			send_remove_player_packet(pl, client);
		}
	}
}

void random_move_NPC(int id)
{
	int x = clients[id].x;
	int y = clients[id].y;
	// �̵� �� �˻�
	unordered_set<int> old_vl;
	for (int i = 0; i < MAX_USER; ++i)
	{
		if (false == clients[i].in_use) continue;
		if (false == Is_Near_Object(id, i)) continue;
		old_vl.insert(i);
	}
	switch (rand() % 4)
	{
	case 0: if (x > 0) x--; break;
	case 1: if (x < (WORLD_WIDTH - 1)) x++; break;
	case 2: if (y > 0) y--; break;
	case 3: if (y < (WORLD_HEIGHT - 1)) y++; break;
	}
	clients[id].x = x;
	clients[id].y = y;
	// �̵� �� �˻�
	unordered_set<int> new_vl;
	for (int i = 0; i < MAX_USER; ++i)
	{
		if (false == clients[i].in_use) continue;
		if (false == Is_Near_Object(id, i)) continue;
		new_vl.insert(i);
	}
	// ���� ���� �÷��̾�, ��� ���� �÷��̾� ó��
	for (auto pl : new_vl)
	{
		if (0 == clients[pl].view_list.count(pl))
		{
			// ���θ������� �÷��̾�Ե� �˸�
			clients[pl].view_list.insert(id);
			send_put_player_packet(pl, id);
		}
		else send_put_player_packet(pl, id);
	}
	// ����� �÷��̾� ó��
	for (auto pl : old_vl)
	{
		if (0 == new_vl.count(pl))
		{
			// ��� �÷��̾� �丮��Ʈ�� ���� ���� �������� �ʾҴٸ�
			if (0 < clients[pl].view_list.count(id))
				clients[pl].view_list.erase(id);
			send_remove_player_packet(pl, id);
		}
	}
}

void process_event(EVENT_ST &ev)
{
	switch (ev.type)
	{
	case EV_MOVE:
	{

		bool player_is_near = false;
		for (int i = 0; i < MAX_USER; ++i)
		{
			// timer �����峪 �̺�Ʈ ť�� ������� ������ �����ϰ� ���� �� ������
			// 1�ʿ� �� ���� �ƴ϶� �� �ʿ� �� �� ������.
			// => �� �ʿ��� �ֵ鸸 �����̰� �ؾ�. => �÷��̾� ������ �ִ� NPC��
			if (false == clients[i].in_use) continue;
			if (false == Is_Near_Object(i, ev.obj_id)) continue;
			player_is_near = true;
			break;
		}

		// �׷��� �� �� ���� NPC���� �ٽ� ����� �ʴ´�.
		// �÷��̾ ���� NPC���� ������� �Ѵ�.
		if (player_is_near) {
			random_move_NPC(ev.obj_id);
			add_timer(ev.obj_id, EV_MOVE, high_resolution_clock::now() + 1s);
		}
		else
		{
			clients[ev.obj_id].is_sleeping = true;
		}
		break;
	}
	default:
		cout << "Unknown Event Error! \n";
		while (true);
	}
}

void worker_thread()
{
	while (true)
	{
		DWORD io_byte;
		ULONGLONG l_key;
		// �������� �����͸� �Ѱ����
		OVER_EX *over_ex;

		int is_error = GetQueuedCompletionStatus(g_iocp, &io_byte, &l_key, reinterpret_cast<LPWSAOVERLAPPED *>(&over_ex), INFINITE);

		if (0 == is_error)
		{
			int err_no = WSAGetLastError();
			if (64 == err_no)
			{
				disconnect_client(l_key);
				continue;
			}
			else
				error_display("GQCS : ", err_no);
		}

		if (0 == io_byte)
		{
			disconnect_client(l_key);
			continue;
		}


		int key = static_cast<int>(l_key);
		if (EV_RECV == over_ex->event_t)
		{
			// RECV ó��
			wcout << "Packet from Client: " << key << "\n";
			// ��Ŷ����
			// ���� ũ��
			int rest = io_byte;
			// ���� ����
			char *ptr = over_ex->messageBuffer;
			char packet_size = 0;

			// ��Ŷ ������ �˾Ƴ��� (�߹ݺ��� ����)
			if (0 < clients[key].prev_size)
				packet_size = clients[key].packet_buffer[0];

			while (0 < rest) {
				if (0 == packet_size) packet_size = ptr[0];	// ptr[0]�� ���ݺ��� ó���� ��Ŷ
				// ��Ŷ ó���Ϸ��� �󸶳� �� �޾ƾ� �ϴ°�?
				// ������ ���� �������� ���� ��Ŷ�� ���� �� ������ prev_size ���ֱ�
				int required = packet_size - clients[key].prev_size;
				if (required <= rest) {
					// ��Ŷ ���� �� �ִ� ���

					// �տ� ���ִ� �����Ͱ� ����Ǿ����� �� ������ �� �ڿ� ����ǵ��� prev_size�� �����ش�.
					memcpy(clients[key].packet_buffer + clients[key].prev_size, ptr, required);
					process_packet(key, clients[key].packet_buffer);
					rest -= required;
					ptr += required;
					// �̹� ���Ʊ� ������ ���� ��Ŷ�� ó���� �� �ֵ��� 0
					packet_size = 0;
					clients[key].prev_size = 0;
				}
				else {
					// ��Ŷ ���� �� ���� ���
					memcpy(clients[key].packet_buffer + clients[key].prev_size, ptr, rest);
					rest = 0;
					clients[key].prev_size += rest;
				}
			}

			do_recv(key);
		}
		else if (EV_SEND == over_ex->event_t)
		{
			// SEND ó��
			delete over_ex;
		}
		else if (EV_MOVE == over_ex->event_t)
		{
			EVENT_ST ev;
			ev.obj_id = key;
			ev.start_time = high_resolution_clock::now() + 1s;
			ev.type = EV_MOVE;
			process_event(ev);
			delete over_ex;
		}// �̺�Ʈ ���� ��� �߰�
		else
		{
			cout << "Unknown Event\n";
			while (true);
		}
	}
}



int do_accept()
{
	// Winsock Start - windock.dll �ε�
	WSADATA WSAData;
	if (WSAStartup(MAKEWORD(2, 2), &WSAData) != 0)
	{
		cout << "Error - Can not load 'winsock.dll' file\n";
		return 1;
	}

	// 1. ���ϻ���											Overlapped�ҰŸ� ������ WSA_FLAG_OVERLAPPED
	SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listenSocket == INVALID_SOCKET)
	{
		cout << "Error - Invalid socket\n";
		return 1;
	}

	// �������� ��ü����
	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = PF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	// 2. ���ϼ���
	// std�� bind�� ȣ��ǹǷ� ������ bind�� �ҷ��ֱ� ���� �տ� ::����
	if (::bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
		cout << "Error - Fail bind\n";
		// 6. ��������
		closesocket(listenSocket);
		// Winsock End
		WSACleanup();
		return 1;
	}

	// 3. ���Ŵ�⿭����
	if (listen(listenSocket, 5) == SOCKET_ERROR)
	{
		cout << "Error - Fail listen\n";
		// 6. ��������
		closesocket(listenSocket);
		// Winsock End
		WSACleanup();
		return 1;
	}

	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(SOCKADDR_IN);
	memset(&clientAddr, 0, addrLen);
	SOCKET clientSocket;
	DWORD flags;

	// accept() ���ʹ� ���� ���� ��ƾ
	while (1)
	{
		// clientSocket�� �񵿱������ ����� ���ؼ��� listenSocket�� �񵿱���̾�� �Ѵ�.
		clientSocket = accept(listenSocket, (struct sockaddr *)&clientAddr, &addrLen);
		if (clientSocket == INVALID_SOCKET)
		{
			cout << "Error - Accept Failure\n";
			return 1;
		}

		// id�� 0~9���� ����� ����
		int new_id = -1;
		for (int i = 0; i < MAX_USER; ++i)
		{
			// i�� �ε����� �ϴ� ���� �� ���� �ִ°�?
			if (false == clients[i].in_use)
			{
				new_id = i;
				break;
			}
		}
		if (-1 == new_id)
		{
			cout << "MAX USER overflow\n";
			continue;
		}

		
		clients[new_id].socket = clientSocket;
		clients[new_id].prev_size = 0;
		//clients[new_id].x = clients[new_id].y = 10;
		clients[new_id].view_list.clear();
		ZeroMemory(&clients[new_id].over_ex.over, sizeof(clients[new_id].over_ex.over));

		flags = 0;

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), g_iocp, new_id, 0);
		send_login_ok_packet(new_id);

		
		do_recv(new_id);
	}

	// 6-2. ���� ��������
	closesocket(listenSocket);

	// Winsock End
	WSACleanup();

	return 0;
}


void do_AI()
{
	// �ӵ��� ������ ����
	// �纸�� for���� �� �� ���µ� 5�� �ɸ�
	// => �÷��̾� �þ߿� �ִ� �ֵ鸸 �̵��ϰ� ������
	while (true)
	{
		// �ʹ� ���� �����δ�. 1�ʿ� �� �� ����ǵ��� ����.
		// �׷��� �ذ�å�� �� �� ����. ���� �ӵ��� O(n^2) �����ϸ� ���� �� ����.
		// ���� timer�� �Ἥ �� �ʿ��� �ֵ鸸 �̵����Ѿ� �Ѵ�.

		this_thread::sleep_for(1s);
		auto ai_start_t = high_resolution_clock::now();
		for (int i = MAX_USER; i < MAX_USER + NUM_NPC; ++i)
		{
			bool need_move = false;
			for (int j = 0; j < MAX_USER; ++j)
			{
				if (false == clients[j].in_use) continue;
				if (false == Is_Near_Object(i, j)) continue;
				need_move = true;
			}
			if (need_move)
				random_move_NPC(i);
		}
		auto ai_end_t = high_resolution_clock::now();
		auto ai_time = ai_end_t - ai_start_t;
		//cout << "AI Process Time = " <<duration_cast<milliseconds>(ai_time).count() << "ms\n";
	}
}


void do_timer()
{
	while (true)
	{

		// ��� ����Ǹ� �������ϳ��� �ھ� �ϳ��� �� ��ƸԴ´�.
		// �׷��� sleep_for()�� ���ְԵǸ�, �̺�Ʈ�� ���� �� ���� ó��������ϴµ� ������ 1���̻� �ɸ��Եȴ�.
		// ���� ���߷��� �ʿ�
		this_thread::sleep_for(10ms);
		while (true)
		{
			// �� ������ �о�ͼ�
			// ����ִµ��� top�Ϸ��� �ؼ� ������ ����. ó�� �ʿ�.
			timer_l.lock();
			if (true == timer_queue.empty())
			{
				timer_l.unlock();
				break;
			}
			EVENT_ST ev = timer_queue.top();
			// �ð� �Ƴ� Ȯ��
			if (ev.start_time > high_resolution_clock::now())
			{
				timer_l.unlock();
				break;
			}
			timer_queue.pop();
			timer_l.unlock();
			// timer�����忡�� send ���� ������ ���� �����ϸ� x. worker�����忡�� �Ѱܾ� �Ѵ�.
			//process_event(ev);
			OVER_EX *over_ex = new OVER_EX;
			// NPC�� ���̵�� ���ϵ��� ���� ��ϵ� ���ߴµ� �־ �ǳ�? -> �ȴ�. �˻縦 ���� �ʱ� ������.
			// �׳� GetQueuedCompletionStatus�� ����� ���� ���̱� ������ ���� �������.
			// �׷��� overlapped�� �����Ѱ� ������ �ȵ�. send���� recv���� �Ǵ��� �ϱ� ���� ���ǹǷ�.
			// �츮�� �۾��� is_recv�� �Ǵܵ� �� ����. ���� �� ������ ���߼��� �����ϵ��� EVENT_TYPE���� ������ �ٲ� ����Ѵ�.
			over_ex->event_t = EV_MOVE;
			PostQueuedCompletionStatus(g_iocp, 1, ev.obj_id, &over_ex->over);
			// lock�� �ϰ� �ʿ䰡 �������� �ٷ� unlock����� ��. 
			// �߰��� break�� Ż���ع����� lock�� ����ä�� ������ �ȴ�. ��������
		}
	}
}

void do_db()
{
	SQLRETURN retcode;
	SQLINTEGER uid, posx, posy;
	SQLLEN cb_uid = 0, cb_posx = 0, cb_posy = 0, tmpIntType = SQL_INTEGER;
	float now = 0.f;

	setlocale(LC_ALL, "korean");

	while (true)
	{
		if ((float)clock() * 0.001f - now < 10)
			continue;
		now = (float)clock() * 0.001f;
		// Allocate environment handle  
		retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

		// Set the ODBC version environment attribute  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

			// Allocate connection handle  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

				// Set login timeout to 5 seconds  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
					for (int i = 0; i < MAX_USER; ++i)
					{
						// Connect to data source  
						retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2019_gs_sub7", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

						// Allocate statement handle  
						if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
							cout << "Database Connect OK!\n";

							if (false == clients[i].in_use)
								continue;
							retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt[i]);
							uid = clients[i].id;
							posx = clients[i].x;
							posy = clients[i].y;

							SQLBindParameter(hstmt[i], 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &uid, sizeof(uid), &tmpIntType);
							SQLBindParameter(hstmt[i], 2, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &posx, sizeof(posx), &tmpIntType);
							SQLBindParameter(hstmt[i], 3, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &posy, sizeof(posy), &tmpIntType);
							//retcode = SQLExecDirect(hstmt, (SQLWCHAR *)L"SELECT user_id, user_name, user_level FROM user_table ORDER BY 2, 1, 3", SQL_NTS); // 2,1,3 ������ sort
							retcode = SQLExecDirect(hstmt[i], (SQLWCHAR *)L"EXEC update_pos ?, ?, ?", SQL_NTS); // ������ 100�ʰ��ϴ� ������ ������ 
							if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

								cout << "client : " << i << "ID : " << clients[i].id << ", ��ġ DB����Ϸ�\n";

							}
							else
								db_display_error(hstmt[i], SQL_HANDLE_STMT, retcode);

						}
						else {

							db_display_error(hstmt[i], SQL_HANDLE_STMT, retcode);
						}

						// Process data  
						if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
							SQLCancel(hstmt[i]);
							SQLFreeHandle(SQL_HANDLE_STMT, hstmt[i]);
						}

						SQLDisconnect(hdbc);
					}
				}
				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
			}
		}
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}
}

int main()
{
	vector <thread> worker_threads;

	// error_display�� ����
	wcout.imbue(locale("korean"));

	// send_packet �ʿ��� ���� �̿��� ��ü�� �۾��� �õ��ߴٰ� ���� �߻�
	// �÷��̾ �ƴ� �ֵ鿡�� ������ ���� ��
	// �˰��� �÷��̾� in_use �ʱ�ȭ�� �������� �Ӹ� �ƴ϶�, Initialize_NPC���� in_use�� true�� �ٲ��ְ��־���
	Initialize_PC();
	Initialize_NPC();
	// iocp ����
	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	// worker_Thread����
	for (int i = 0; i < 4; ++i)
		worker_threads.emplace_back(thread{ worker_thread });
	thread accept_thread{ do_accept };
	thread timer_thread{ do_timer };
	thread db_thread{ do_db };
	db_thread.join();
	timer_thread.join();
	accept_thread.join();
	for (auto &th : worker_threads) th.join();
}