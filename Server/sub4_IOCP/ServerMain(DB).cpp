/*
## 소켓 서버 : 1 v n - overlapped callback
1. socket()            : 소켓생성
2. bind()            : 소켓설정
3. listen()            : 수신대기열생성
4. accept()            : 연결대기
5. read()&write()
WIN recv()&send    : 데이터 읽고쓰기
6. close()
WIN closesocket    : 소켓종료
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
#define VIEW_RADIUS		7	// 거리 차가 7이상이면 안보이도록

enum EVENT_TYPE { EV_MOVE, EV_HEAL, EV_RECV, EV_SEND };

// Overlapped구조체 확장
struct OVER_EX {
	WSAOVERLAPPED	over;
	WSABUF			dataBuffer;
	char			messageBuffer[MAX_BUFFER];
	EVENT_TYPE		event_t;

};

class SOCKETINFO
{
public:
	// in_use와 x,y에서 data race발생
	// in_use가 true인걸 확인하고 send하려고 하는데 그 이전에 접속 종료해서 false가된다면?
	// 그리고 send 전에 새 플레이어가 id를 재사용한다면? => 엉뚱한 곳에 send가 됨
	// mutex access_lock;
	bool in_use;
	OVER_EX over_ex;
	SOCKET socket;
	// 조립불가한 메모리를 다음번에 조립하기 위한 임시저장소
	char packet_buffer[MAX_BUFFER];
	int prev_size;
	short x, y;
	int id;
	bool is_sleeping;
	// ID의 집합, ID를 순서대로 쓰지 않으니 unordered_set, 더 성능이 빠르기 때문에 무조건 unordered씀
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
	// 이 이벤트가 어떤 객체에서 발생해야 하느냐
	int obj_id;
	// 어떤 이벤트냐
	EVENT_TYPE type;
	// 언제 실행되어야 하느냐
	high_resolution_clock::time_point start_time;

	// 현재는 우선순위 큐에서 시간순서로 정렬하는 것을 모른다. operator를 만들어줘야 함.
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
// worker thread에서 data race발생. 상호배제 필요
//SOCKETINFO clients[MAX_USER];

//////////////////// NPC 구현 방법 첫 번째 ////////////////////
//class NPCINFO
//{
//public:
//	char x, y;
//	unordered_set<int> view_list;
//};
//SOCKETINFO clients[MAX_USER];
//NPCINFO npcs[NUM_NPC];
//// 장점: 메모리의 낭비가 없다. 직관적이다.
//// 단점: 함수의 중복 구현이 필요하다.
//// ex) Is_Near_Object(int a, int b) 사용 불가.
////		대신 Is_Near_Player_Player(int a, int b), Is_Near_Player_Npc(int a, int b), Is_Near_Npc_Npc(int a, int b)
////		세 가지 버전을 추가로 구현해줘야 한다.
////		따라서 상속을 사용해야 함.
//
////////////////////// NPC 구현 방법 두 번째 ////////////////////
//// 더 많이 사용
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
//	// in_use와 x,y에서 data race발생
//	// in_use가 true인걸 확인하고 send하려고 하는데 그 이전에 접속 종료해서 false가된다면?
//	// 그리고 send 전에 새 플레이어가 id를 재사용한다면? => 엉뚱한 곳에 send가 됨
//	// mutex access_lock;
//	bool in_use;
//	OVER_EX over_ex;
//	SOCKET socket;
//	// 조립불가한 메모리를 다음번에 조립하기 위한 임시저장소
//	char packet_buffer[MAX_BUFFER];
//	int prev_size;
//	// ID의 집합, ID를 순서대로 쓰지 않으니 unordered_set, 더 성능이 빠르기 때문에 무조건 unordered씀
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

// 장점: 함수의 중복 구현이 필요 없다.
// 단점: 포인터의 사용. -> new/delete하지 않음. 재활용. -> 생성자 사용하지 않기 때문에 비직관적
// 특징: ID 배분을 하거나 object_type 멤버가 필요하다. (추가적인 복잡도)
// 플레이어는 패킷보내야 하고, NPC는 보내지 않아야 한다.
// => 배열 첫 10개를 플레이어로, 혹은 뒤 10개를 플레이어로 영역을 정해야 한다.
//		자유롭게 섞어 쓸 경우 구분하기 위해 object_type을 만들어 검사해야 함.

// NPC 구현 실습 단순무식
// ID : 0 ~ 9 => 플레이어
// ID : 10 ~ 10 + NUM_NPC - 1 => NPC
// 어마어마한 메모리 낭비. 장점은 포인터를 사용하지 않는다는 것 뿐.
SOCKETINFO clients[MAX_USER + NUM_NPC];

HANDLE g_iocp;

void error_display(const char *msg, int err_no)
{
	WCHAR *lpMsgBuf;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, err_no, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
	cout << msg;
	wcout << L"에러 [" << err_no << L"] " << lpMsgBuf << "\n";
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
					//retcode = SQLExecDirect(hstmt, (SQLWCHAR *)L"SELECT user_id, user_name, user_level FROM user_table ORDER BY 2, 1, 3", SQL_NTS); // 2,1,3 순위로 sort
					retcode = SQLExecDirect(hstmt[client], (SQLWCHAR *)L"Exec select_by_user_id ?, ?", SQL_NTS); // 레벨이 100초과하는 유저를 가져옴 
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

						// Bind columns 1, 2, and 3  
						//retcode = SQLBindCol(hstmt, 1, SQL_INTEGER, &uid, 10, &cb_uid/*callback. 데이터 크기 리턴.*/);
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
		// 랜덤배치
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
	// 그냥 돌리면 여러 플레이어가 접속 시, 한 객체에 대해 중복 적용해서 속도가 빠른 NPC가 생길 것.
	// 돌리고 있는 애들을 체크해야한다.
	if (true == is_sleeping(id)) {
		clients[id].is_sleeping = false;
		EVENT_ST ev;
		ev.obj_id = id;
		ev.type = EV_MOVE;
		ev.start_time = high_resolution_clock::now() + 1s;
		// accept에서 push하고 timer에서 pop한다. data race발생하여 프로그램이 죽는다.
		// mutex로 보호해야 한다.
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
		// 비동기식으로 돌아가지 않고 동기식으로 돌아갔다는 오류.
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
		// 비동기식으로 돌아가지 않고 동기식으로 돌아갔다는 오류.
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

		// i가 나를 보고 있다면 지워줘야
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
	// 0번은 사이즈, 1번이 패킷타입
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
		// 기존 유저들에게 이후 접속한 유저들 출력
		for (int i = 0; i < MAX_USER; ++i)
		{
			if (false == clients[i].in_use)
				continue;
			if (false == Is_Near_Object(client, i))		// 근처에 있는 경우에만 보내기
				continue;
			if (i == client)
				continue;

			clients[i].view_list.insert(client);
			send_put_player_packet(i, client);
		}
		// 처음 접속한 나에게 기존 유저들 출력
		// NPC도 보내줄 것. 기존유저들에게 NPC정보 보낼 필요 X
		for (int i = 0; i < MAX_USER + NUM_NPC; ++i)
		{
			if (false == clients[i].in_use)
				continue;
			if (i == client)
				continue;
			if (false == Is_Near_Object(i, client))
				continue;
			// 처음 접속 시 NPC이동을 시키기 위해
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
		wcout << L"정의되지 않은 패킷 도착 오류!!\n";
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

	// NPC정보를 viewList에 넣기
	for (int i = MAX_USER; i < MAX_USER + NUM_NPC; ++i)
	{
		if (false == Is_Near_Object(i, client)) continue;
		new_vl.insert(i);
	}

	// 일단 나에게 보내야
	send_pos_packet(client, client);

	// 1. old_vl에도 있고 new_vl에도 있는 객체
	for (auto pl : old_vl)
	{
		// 없는 경우
		if (0 == new_vl.count(pl)) continue;
		// NPC라면 아무것도 할 필요 없음
		if (true == is_NPC(pl)) continue;
		// 상대 viewlist에 내가 있을 때
		if (0 < clients[pl].view_list.count(client))
			send_pos_packet(pl, client);
		else {	// 상대 viewlist에 내가 없을 때
			clients[pl].view_list.insert(client);
			send_put_player_packet(pl, client);
		}
	}

	// 2. old_vl에 없고 new_vl에만 있는 플레이어(새로만난애들)
	for (auto pl : new_vl)
	{
		if (0 < old_vl.count(pl)) continue;
		// 내 viewlist에 상대 추가 후 put player
		clients[client].view_list.insert(pl);
		send_put_player_packet(client, pl);
		if (true == is_NPC(pl)) {
			wakeup_NPC(pl);
			continue;
		}
		// 내가 상대 viewlist에 없다면
		if (0 == clients[pl].view_list.count(client))
		{
			clients[pl].view_list.insert(client);
			send_put_player_packet(pl, client);
		}
		else // 있다면
			send_pos_packet(pl, client);
	}

	// 3. old_vl에 있고 new_vl에는 없는 플레이어
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
	// 이동 전 검사
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
	// 이동 후 검사
	unordered_set<int> new_vl;
	for (int i = 0; i < MAX_USER; ++i)
	{
		if (false == clients[i].in_use) continue;
		if (false == Is_Near_Object(id, i)) continue;
		new_vl.insert(i);
	}
	// 새로 만난 플레이어, 계속 보는 플레이어 처리
	for (auto pl : new_vl)
	{
		if (0 == clients[pl].view_list.count(pl))
		{
			// 새로만났으니 플레이어에게도 알림
			clients[pl].view_list.insert(id);
			send_put_player_packet(pl, id);
		}
		else send_put_player_packet(pl, id);
	}
	// 헤어진 플레이어 처리
	for (auto pl : old_vl)
	{
		if (0 == new_vl.count(pl))
		{
			// 상대 플레이어 뷰리스트에 내가 아직 지워지지 않았다면
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
			// timer 스레드나 이벤트 큐의 오버헤드 때문에 과부하가 생겨 더 느려짐
			// 1초에 한 번이 아니라 수 초에 한 번 움직임.
			// => 꼭 필요한 애들만 움직이게 해야. => 플레이어 주위에 있는 NPC만
			if (false == clients[i].in_use) continue;
			if (false == Is_Near_Object(i, ev.obj_id)) continue;
			player_is_near = true;
			break;
		}

		// 그러나 한 번 멈춘 NPC들이 다시 깨어나지 않는다.
		// 플레이어가 주위 NPC들을 깨워줘야 한다.
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
		// 포인터의 포인터를 넘겨줘야
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
			// RECV 처리
			wcout << "Packet from Client: " << key << "\n";
			// 패킷조립
			// 남은 크기
			int rest = io_byte;
			// 실제 버퍼
			char *ptr = over_ex->messageBuffer;
			char packet_size = 0;

			// 패킷 사이즈 알아내기 (중반부터 가능)
			if (0 < clients[key].prev_size)
				packet_size = clients[key].packet_buffer[0];

			while (0 < rest) {
				if (0 == packet_size) packet_size = ptr[0];	// ptr[0]이 지금부터 처리할 패킷
				// 패킷 처리하려면 얼마나 더 받아야 하는가?
				// 이전에 받은 조립되지 않은 패킷이 있을 수 있으니 prev_size 빼주기
				int required = packet_size - clients[key].prev_size;
				if (required <= rest) {
					// 패킷 만들 수 있는 경우

					// 앞에 와있던 데이터가 저장되어있을 수 있으니 그 뒤에 저장되도록 prev_size를 더해준다.
					memcpy(clients[key].packet_buffer + clients[key].prev_size, ptr, required);
					process_packet(key, clients[key].packet_buffer);
					rest -= required;
					ptr += required;
					// 이미 계산됐기 때문에 다음 패킷을 처리할 수 있도록 0
					packet_size = 0;
					clients[key].prev_size = 0;
				}
				else {
					// 패킷 만들 수 없는 경우
					memcpy(clients[key].packet_buffer + clients[key].prev_size, ptr, rest);
					rest = 0;
					clients[key].prev_size += rest;
				}
			}

			do_recv(key);
		}
		else if (EV_SEND == over_ex->event_t)
		{
			// SEND 처리
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
		}// 이벤트 별로 계속 추가
		else
		{
			cout << "Unknown Event\n";
			while (true);
		}
	}
}



int do_accept()
{
	// Winsock Start - windock.dll 로드
	WSADATA WSAData;
	if (WSAStartup(MAKEWORD(2, 2), &WSAData) != 0)
	{
		cout << "Error - Can not load 'winsock.dll' file\n";
		return 1;
	}

	// 1. 소켓생성											Overlapped할거면 무조건 WSA_FLAG_OVERLAPPED
	SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listenSocket == INVALID_SOCKET)
	{
		cout << "Error - Invalid socket\n";
		return 1;
	}

	// 서버정보 객체설정
	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = PF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	// 2. 소켓설정
	// std의 bind가 호출되므로 소켓의 bind를 불러주기 위해 앞에 ::붙임
	if (::bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
		cout << "Error - Fail bind\n";
		// 6. 소켓종료
		closesocket(listenSocket);
		// Winsock End
		WSACleanup();
		return 1;
	}

	// 3. 수신대기열생성
	if (listen(listenSocket, 5) == SOCKET_ERROR)
	{
		cout << "Error - Fail listen\n";
		// 6. 소켓종료
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

	// accept() 부터는 서버 메인 루틴
	while (1)
	{
		// clientSocket을 비동기식으로 만들기 위해서는 listenSocket이 비동기식이어야 한다.
		clientSocket = accept(listenSocket, (struct sockaddr *)&clientAddr, &addrLen);
		if (clientSocket == INVALID_SOCKET)
		{
			cout << "Error - Accept Failure\n";
			return 1;
		}

		// id를 0~9까지 사용할 예정
		int new_id = -1;
		for (int i = 0; i < MAX_USER; ++i)
		{
			// i를 인덱스로 하는 값이 몇 개가 있는가?
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

	// 6-2. 리슨 소켓종료
	closesocket(listenSocket);

	// Winsock End
	WSACleanup();

	return 0;
}


void do_AI()
{
	// 속도가 느려서 기어간다
	// 재보니 for루프 한 번 도는데 5초 걸림
	// => 플레이어 시야에 있는 애들만 이동하게 만들자
	while (true)
	{
		// 너무 빨리 움직인다. 1초에 한 번 실행되도록 재우기.
		// 그러나 해결책이 될 순 없다. 실행 속도가 O(n^2) 과부하를 피할 수 없다.
		// 따라서 timer를 써서 꼭 필요한 애들만 이동시켜야 한다.

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

		// 계속 실행되면 스레드하나가 코어 하나를 다 잡아먹는다.
		// 그러나 sleep_for()를 해주게되면, 이벤트가 많을 때 빨리 처리해줘야하는데 무조건 1초이상 걸리게된다.
		// 따라서 이중루프 필요
		this_thread::sleep_for(10ms);
		while (true)
		{
			// 맨 위에꺼 읽어와서
			// 비어있는데도 top하려고 해서 문제가 생김. 처리 필요.
			timer_l.lock();
			if (true == timer_queue.empty())
			{
				timer_l.unlock();
				break;
			}
			EVENT_ST ev = timer_queue.top();
			// 시간 됐나 확인
			if (ev.start_time > high_resolution_clock::now())
			{
				timer_l.unlock();
				break;
			}
			timer_queue.pop();
			timer_l.unlock();
			// timer스레드에서 send 등의 복잡한 일을 실행하면 x. worker스레드에게 넘겨야 한다.
			//process_event(ev);
			OVER_EX *over_ex = new OVER_EX;
			// NPC의 아이디는 소켓따위 없고 등록도 안했는데 넣어도 되나? -> 된다. 검사를 하지 않기 때문에.
			// 그냥 GetQueuedCompletionStatus를 깨우기 위한 것이기 때문에 값은 상관없다.
			// 그러나 overlapped는 엉뚱한걸 넣으면 안됨. send인지 recv인지 판단을 하기 위해 사용되므로.
			// 우리의 작업은 is_recv로 판단될 수 없다. 따라서 이 변수를 다중선택 가능하도록 EVENT_TYPE형의 변수로 바꿔 사용한다.
			over_ex->event_t = EV_MOVE;
			PostQueuedCompletionStatus(g_iocp, 1, ev.obj_id, &over_ex->over);
			// lock을 하고 필요가 없어지면 바로 unlock해줘야 함. 
			// 중간에 break로 탈출해버리면 lock이 잡힌채로 끝나게 된다. 교착상태
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
							//retcode = SQLExecDirect(hstmt, (SQLWCHAR *)L"SELECT user_id, user_name, user_level FROM user_table ORDER BY 2, 1, 3", SQL_NTS); // 2,1,3 순위로 sort
							retcode = SQLExecDirect(hstmt[i], (SQLWCHAR *)L"EXEC update_pos ?, ?, ?", SQL_NTS); // 레벨이 100초과하는 유저를 가져옴 
							if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

								cout << "client : " << i << "ID : " << clients[i].id << ", 위치 DB저장완료\n";

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

	// error_display를 위함
	wcout.imbue(locale("korean"));

	// send_packet 쪽에서 소켓 이외의 개체에 작업을 시도했다고 에러 발생
	// 플레이어가 아닌 애들에게 소켓을 보낸 것
	// 알고보니 플레이어 in_use 초기화를 안해줬을 뿐만 아니라, Initialize_NPC에서 in_use를 true로 바꿔주고있었음
	Initialize_PC();
	Initialize_NPC();
	// iocp 생성
	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	// worker_Thread생성
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