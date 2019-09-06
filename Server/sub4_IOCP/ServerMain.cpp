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
#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>

#define UNICODE  
#include <locale.h>
#include <winsock2.h>
#include "protocol.h"
#include <sqlext.h> 

extern "C" {
#include "include\lua.h"
#include "include\lauxlib.h"
#include "include\lualib.h"
}

using namespace std;
using namespace chrono;
#pragma comment(lib, "Ws2_32.lib")

#define MAX_BUFFER		1024

enum EVENT_TYPE { EV_REVIVE, EV_ATTACK, EV_DETECT_PLAYER_MOVE, EV_MOVE, EV_HEAL, EV_RECV, EV_SEND };
enum OBJ_CLASS {PLAYER, BAT, SLIME, EYEBALL, BIGWORM, GHOST};
enum MONSTER_TYPE {WAR, PEACE};
enum MOVE_TYPE {ROAMING, NONE};

// Overlapped구조체 확장
struct OVER_EX {
	WSAOVERLAPPED	over;
	WSABUF			dataBuffer;
	char			messageBuffer[MAX_BUFFER];
	EVENT_TYPE		event_t;
	int				target_client;
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
	short hp, exp, level, atk;
	bool is_sleeping;
	int obj_class;
	int type;
	int movetype;
	int move_minx, move_miny;
	int move_maxx, move_maxy;
	bool isAttack;
	int target_id;
	bool isHealing;
	mutex vl;
	// ID의 집합, ID를 순서대로 쓰지 않으니 unordered_set, 더 성능이 빠르기 때문에 무조건 unordered씀
	unordered_set<int> view_list;
	mutex lua;
	lua_State *L;
public:
	SOCKETINFO() {
		in_use = false;
		over_ex.dataBuffer.len = MAX_BUFFER;
		over_ex.dataBuffer.buf = over_ex.messageBuffer;
		over_ex.event_t = EV_RECV;

		L = luaL_newstate();
		luaL_openlibs(L);

		luaL_loadfile(L, "monster_ai.lua");
		lua_pcall(L, 0, 0, 0);
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
	int target_id;


	// 현재는 우선순위 큐에서 시간순서로 정렬하는 것을 모른다. operator를 만들어줘야 함.
	constexpr bool operator < (const EVENT_ST& _Left) const
	{
		return (start_time > _Left.start_time);
	}
};

mutex timer_l;
priority_queue <EVENT_ST> timer_queue;
SOCKETINFO clients[MAX_USER + NUM_NPC];
bool mapData[300][300];

HANDLE g_iocp;

void disconnect_client(int id);
void attack_player(int client, int obj_id, int atk);
bool is_NPC(int id);
void send_stat_change(int client, int obj_id);
void add_timer(int obj_id, EVENT_TYPE et, high_resolution_clock::time_point start_time);
bool Is_Near_Object(int a, int b);
void send_pos_packet(int client, int player);
void send_put_player_packet(int client, int new_id);
void wakeup_NPC(int id);
void send_remove_player_packet(int client, int id);

void error_display(const char *msg, int err_no)
{
	WCHAR *lpMsgBuf;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, err_no, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
	cout << msg;
	wcout << L"에러 [" << err_no << L"] " << lpMsgBuf << "\n";
	while (true);
	LocalFree(lpMsgBuf);
}

int API_get_x(lua_State *L)
{
	int obj_id = (int)lua_tonumber(L, -1);
	// 파라미터 1개와 자신을 호출한 함수도 pop하여 총 2개 pop
	lua_pop(L, 2);
	int x = clients[obj_id].x;
	lua_pushnumber(L, x);

	// 리턴파라미터 한 개 있다 알리기 위함
	return 1;
}

int API_get_y(lua_State *L)
{
	int obj_id = (int)lua_tonumber(L, -1);
	// 파라미터 1개와 자신을 호출한 함수도 pop하여 총 2개 pop
	lua_pop(L, 2);
	int y = clients[obj_id].y;
	lua_pushnumber(L, y);

	// 리턴파라미터 한 개 있다 알리기 위함
	return 1;
}

int API_SetIsAttack(lua_State *L)
{
	int client = (int)lua_tonumber(L, -2);
	int obj_id = (int)lua_tonumber(L, -1);
	lua_pop(L, 3);
	
	if (false == clients[obj_id].isAttack)
	{
		OVER_EX *ex_over = new OVER_EX;
		ex_over->event_t = EV_ATTACK;
		ex_over->target_client = client;
		PostQueuedCompletionStatus(g_iocp, 1, obj_id, &ex_over->over);
		// addtimer로 1초에 한 번 씩 공격하도록 구현
		//add_timer(obj_id, EV_ATTACK, high_resolution_clock::now() + 1s);
		clients[obj_id].isAttack = true;
	}
	return 0;
}

void Initialize_Map()
{
	ifstream fin;
	fin.open("MapData.txt", ios::binary);

	char ch;
	for (int i = 0; i < WORLD_HEIGHT; ++i)
	{
		for (int j = 0; j < WORLD_WIDTH; ++j)
		{
			fin.get(ch);
			if (ch == 'X')
			{
				mapData[j][i] = false;
			}
			else
			{
				mapData[j][i] = true;
			}
		}
	}

	fin.close();
}

void Initialize_PC()
{
	for (int i = 0; i < MAX_USER; ++i)
	{
		int npc_id = i + MAX_USER;
		clients[i].in_use = false;
		clients[i].isHealing = false;
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
	for (int i = NPC_ID_START; i < NUM_NPC; ++i)
	{
		int npc_id = i;
		
		// 랜덤배치
		while (true)
		{
			clients[npc_id].x = rand() % WORLD_WIDTH;
			clients[npc_id].y = rand() % WORLD_HEIGHT;
			if (true == mapData[clients[npc_id].y][clients[npc_id].x])
				break;
		}
		clients[npc_id].move_minx = clients[npc_id].x - 10;
		clients[npc_id].move_miny = clients[npc_id].y - 10;
		clients[npc_id].move_maxx = clients[npc_id].x + 10;
		clients[npc_id].move_maxy = clients[npc_id].y + 10;
		// 루아 변수 초기화
		lua_State *L = clients[npc_id].L;


		if (npc_id >= BAT_ID_START && npc_id < SLIME_ID_START)
		{
			clients[npc_id].obj_class = BAT;
		}
		else if (npc_id >= SLIME_ID_START && npc_id < EYEBALL_ID_START)
		{
			clients[npc_id].obj_class = SLIME;
			clients[npc_id].type = PEACE;
			clients[npc_id].movetype = ROAMING;
			clients[npc_id].level = 2;
			clients[npc_id].hp = 50;
			clients[npc_id].exp = clients[npc_id].level * 5;
			clients[npc_id].atk = 10;

		}
		else if (npc_id >= EYEBALL_ID_START && npc_id < BIGWORM_ID_START)
		{
			clients[npc_id].obj_class = EYEBALL;
			clients[npc_id].type = WAR;
			clients[npc_id].movetype = ROAMING;
			clients[npc_id].level = 5;
			clients[npc_id].hp = 50;
			clients[npc_id].exp = clients[npc_id].level * 5;
			clients[npc_id].atk = 15;
		}
		else if (npc_id >= BIGWORM_ID_START && npc_id < GHOST_ID_START)
		{
			clients[npc_id].obj_class = BIGWORM;
			clients[npc_id].type = WAR;
			clients[npc_id].movetype = NONE;
			clients[npc_id].level = 7;
			clients[npc_id].hp = 100;
			clients[npc_id].exp = clients[npc_id].level * 5;
			clients[npc_id].atk = 30;
		}
		else if (npc_id >= GHOST_ID_START)
		{
			clients[npc_id].obj_class = GHOST;
			clients[npc_id].type = WAR;
			clients[npc_id].movetype = ROAMING;
			clients[npc_id].level = 10;
			clients[npc_id].hp = 120;
			clients[npc_id].exp = clients[npc_id].level * 5;
			clients[npc_id].atk = 30;
		}

		lua_getglobal(L, "init");
		lua_pushnumber(L, npc_id);
		lua_pushnumber(L, clients[npc_id].x);
		lua_pushnumber(L, clients[npc_id].y);
		// level
		lua_pushnumber(L, clients[npc_id].level);
		// hp
		lua_pushnumber(L, clients[npc_id].hp);
		// exp
		lua_pushnumber(L, clients[npc_id].exp);
		// atk
		lua_pushnumber(L, clients[npc_id].atk);
		lua_pushnumber(L, clients[npc_id].obj_class);
		lua_pushnumber(L, clients[npc_id].type);
		lua_pcall(L, 9, 0, 0);

		// API함수 등록
		lua_register(L, "API_get_x", API_get_x);
		lua_register(L, "API_get_y", API_get_y);
		lua_register(L, "API_SetIsAttack", API_SetIsAttack);

		clients[npc_id].in_use = true;
		clients[npc_id].is_sleeping = true;
		clients[npc_id].isAttack = false;
		if(ROAMING == clients[npc_id].movetype)
			add_timer(npc_id, EV_MOVE, high_resolution_clock::now() + 1s);
	}
	printf("NPC초기화 완료\n");
}

void attack_player(int client, int obj_id, int atk)
{
	if (false == clients[client].in_use)
		return;
	if ((clients[client].x == clients[obj_id].x - 1 && clients[client].y == clients[obj_id].y) || 
		(clients[client].x == clients[obj_id].x + 1 && clients[client].y == clients[obj_id].y) ||
		(clients[client].x == clients[obj_id].x && clients[client].y == clients[obj_id].y - 1) ||
		(clients[client].x == clients[obj_id].x && clients[client].y == clients[obj_id].y + 1))
	{
		clients[client].hp -= atk;
		if (client < MAX_USER)
		{
			if (false == clients[client].isHealing)
			{
				OVER_EX *ex_over = new OVER_EX;
				ex_over->event_t = EV_HEAL;
				PostQueuedCompletionStatus(g_iocp, 1, client, &ex_over->over);
				clients[client].isHealing = true;
			}
		}
		cout << obj_id << "가 " << client << "를 때려서 " << atk << "의 데미지를 입혔습니다\n";
		if (clients[client].hp <= 0)
		{
			if (false == is_NPC(client))
			{
				cout << client << "가 " << obj_id << "에 의해 사망했습니다.\n";
				clients[client].vl.lock();
				auto old_vl = clients[client].view_list;
				clients[client].vl.unlock();

				clients[client].hp = 100;
				clients[client].exp /= 2;
				clients[client].x = clients[client].y = 9;

				clients[obj_id].isAttack = false;

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
					clients[pl].vl.lock();
					if (0 < clients[pl].view_list.count(client))
					{
						clients[pl].vl.unlock();
						send_pos_packet(pl, client);
					}
					else {	// 상대 viewlist에 내가 없을 때
						clients[pl].view_list.insert(client);
						clients[pl].vl.unlock();
						send_put_player_packet(pl, client);
					}
				}

				// 2. old_vl에 없고 new_vl에만 있는 플레이어(새로만난애들)
				for (auto pl : new_vl)
				{
					if (0 < old_vl.count(pl)) continue;
					// 내 viewlist에 상대 추가 후 put player
					clients[client].vl.lock();
					clients[client].view_list.insert(pl);
					clients[client].vl.unlock();
					send_put_player_packet(client, pl);
					if (true == is_NPC(pl)) {
						wakeup_NPC(pl);
						continue;
					}
					// 내가 상대 viewlist에 없다면
					clients[pl].vl.lock();
					if (0 == clients[pl].view_list.count(client))
					{
						clients[pl].view_list.insert(client);
						clients[pl].vl.unlock();
						send_put_player_packet(pl, client);
					}
					else // 있다면
					{
						clients[pl].vl.unlock();
						send_pos_packet(pl, client);
					}
				}

				// 3. old_vl에 있고 new_vl에는 없는 플레이어
				for (auto pl : old_vl)
				{
					if (0 < new_vl.count(pl)) continue;
					clients[client].vl.lock();
					clients[client].view_list.erase(pl);
					clients[client].vl.unlock();
					send_remove_player_packet(client, pl);
					if (true == is_NPC(pl)) continue;
					clients[pl].vl.lock();
					if (0 == clients[pl].view_list.count(pl))
					{
						clients[pl].view_list.erase(client);
						clients[pl].vl.unlock();
						send_remove_player_packet(pl, client);
					}
					else
						clients[pl].vl.unlock();

				}
				// 시야에 있는 몬스터들에게 플레이어의 이동을 알린다.
				for (auto monster : new_vl)
				{
					if (false == is_NPC(monster)) continue;
					if (WAR != clients[monster].type) continue;

					OVER_EX *ex_over = new OVER_EX;
					ex_over->event_t = EV_DETECT_PLAYER_MOVE;
					ex_over->target_client = client;
					PostQueuedCompletionStatus(g_iocp, 1, monster, &ex_over->over);
				}
			}
			else
			{
				clients[client].isAttack = false;
				int getExp = clients[client].exp;
				if (ROAMING == clients[client].movetype)
					getExp *= 2;
				if (WAR == clients[client].obj_class)
					getExp *= 2;
				clients[obj_id].exp += getExp;
				if (clients[obj_id].exp >= clients[obj_id].level * (200))
				{
					clients[obj_id].exp -= clients[obj_id].level * (200);
					clients[obj_id].level++;
				}

				for (int i = 0; i < MAX_USER; ++i)
				{
					if (false == clients[i].in_use) continue;
					
					clients[i].vl.lock();
					if (0 < clients[i].view_list.count(client))
					{
						clients[i].view_list.erase(client);
						clients[i].vl.unlock();
						send_remove_player_packet(i, client);
					}
					else
						clients[i].vl.unlock();

					send_stat_change(i, obj_id);
				}

				clients[client].in_use = false;
				cout << obj_id << "가 " << client << "를 무찔러서 " << getExp << "의 경험치를 얻었습니다.\n";
				// addtimer로 30초 센 후 initialize_NPC() 호출하기
				add_timer(client, EV_REVIVE, high_resolution_clock::now() + 30s);
			}
		}/*
		wstring s1 = L"";
		wstring s2 = L"";
		switch (clients[client].obj_class)
		{
		case PLAYER:
			s1 = L"PLAYER가";
			break;
		case SLIME:
			s1 = L"SLIME이";
			break;
		case EYEBALL:
			s1 = L"EYEBALL이";
			break;
		case BIGWORM:
			s1 = L"BIGWORM이";
			break;
		case GHOST:
			s1 = L"GHOST가";
			break;
		}

		switch (clients[obj_id].obj_class)
		{
		case PLAYER:
			s1 = L"PLAYER가";
			break;
		case SLIME:
			s1 = L"SLIME이";
			break;
		case EYEBALL:
			s1 = L"EYEBALL이";
			break;
		case BIGWORM:
			s1 = L"BIGWORM이";
			break;
		case GHOST:
			s1 = L"GHOST가";
			break;
		}*/

		if (false == is_NPC(client))
			send_stat_change(client, client);
		clients[client].vl.lock();
		auto tmp_vl = clients[client].view_list;
		clients[client].vl.unlock();

		for (auto obj : tmp_vl)
		{
			if (true == is_NPC(obj)) continue;

			send_stat_change(obj, client);
			//send_chat_packet(obj,client,"")
		}
		
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

void do_recv(int id)
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
	sc_packet_position packet;
	packet.id = player;
	packet.size = sizeof(packet);
	packet.type = SC_POSITION;
	packet.x = clients[player].x;
	packet.y = clients[player].y;

	send_packet(client, &packet);
}

void send_stat_change(int client, int obj_id)
{
	sc_packet_stat_change packet;
	packet.size = sizeof(packet);
	packet.type = SC_STAT_CHANGE;
	packet.id = obj_id;
	packet.LEVEL = clients[obj_id].level;
	packet.HP = clients[obj_id].hp;
	packet.EXP = clients[obj_id].exp;
	
	send_packet(client, &packet);
}

void send_login_ok_packet(int new_id)
{
	sc_packet_login_ok packet;
	packet.id = new_id;
	packet.x = clients[new_id].x;
	packet.y = clients[new_id].y;
	packet.HP = clients[new_id].hp;
	packet.EXP = clients[new_id].exp;
	packet.LEVEL = clients[new_id].level;
	packet.size = sizeof(packet);
	packet.type = SC_LOGIN_OK;

	send_packet(new_id, &packet);
}

void send_login_fail_packet(int new_id)
{
	sc_packet_login_fail packet;
	packet.size = sizeof(packet);
	packet.type = SC_LOGIN_FAIL;

	send_packet(new_id, &packet);
}

void send_put_player_packet(int client, int new_id)
{
	sc_packet_add_object packet;
	packet.id = new_id;
	packet.size = sizeof(packet);
	packet.type = SC_ADD_OBJECT;
	packet.x = clients[new_id].x;
	packet.y = clients[new_id].y;
	packet.HP = clients[new_id].hp;
	packet.EXP = clients[new_id].exp;
	packet.LEVEL = clients[new_id].level;
	packet.obj_class = clients[new_id].obj_class;

	send_packet(client, &packet);
}

void send_chat_packet(int client, int from_id, wchar_t *mess)
{
	sc_packet_chat packet;
	packet.id = from_id;
	packet.size = sizeof(packet);
	packet.type = SC_CHAT;
	// wchar_t이므로 strcpy가 아니라 wcscpy사용
	wcscpy_s(packet.message, mess);

	send_packet(client, &packet);
}

void send_remove_player_packet(int client, int id)
{
	sc_packet_remove_object packet;
	packet.id = id;
	packet.size = sizeof(packet);
	packet.type = SC_REMOVE_OBJECT;

	send_packet(client, &packet);
}

void move_player(int client, char* packet)
{
	cs_packet_move *p = reinterpret_cast<cs_packet_move *>(packet);
	int x = clients[client].x;
	int y = clients[client].y;

	clients[client].vl.lock();
	auto old_vl = clients[client].view_list;
	clients[client].vl.unlock();
	// 0번은 사이즈, 1번이 패킷타입
	switch (p->direction)
	{
	case DIR_UP:
		if (y > 0)
			y--;
		if (false == mapData[x][y])
		{
			y++;
		}
		break;
	case DIR_DOWN:
		if (y < (WORLD_HEIGHT - 1))
			y++;
		if (false == mapData[x][y])
		{
			y--;
		}
		break;
	case DIR_LEFT:
		if (x > 0)
			x--;
		if (false == mapData[x][y])
		{
			x++;
		}
		break;
	case DIR_RIGHT:
		if (x < (WORLD_WIDTH - 1))
			x++;
		if (false == mapData[x][y])
		{
			x--;
		}
		break;
	default:
		wcout << L"정의되지 않은 MOVE패킷 도착 오류!!\n";
		while (true);
	}
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
		if (false == clients[i].in_use) continue;
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
		clients[pl].vl.lock();
		if (0 < clients[pl].view_list.count(client))
		{
			clients[pl].vl.unlock();
			send_pos_packet(pl, client);
		}
		else {	// 상대 viewlist에 내가 없을 때
			clients[pl].view_list.insert(client);
			clients[pl].vl.unlock();
			send_put_player_packet(pl, client);
		}
	}

	// 2. old_vl에 없고 new_vl에만 있는 플레이어(새로만난애들)
	for (auto pl : new_vl)
	{
		if (0 < old_vl.count(pl)) continue;
		// 내 viewlist에 상대 추가 후 put player
		clients[client].vl.lock();
		clients[client].view_list.insert(pl);
		clients[client].vl.unlock();
		send_put_player_packet(client, pl);
		if (true == is_NPC(pl)) {
			wakeup_NPC(pl);
			continue;
		}
		// 내가 상대 viewlist에 없다면
		clients[pl].vl.lock();
		if (0 == clients[pl].view_list.count(client))
		{
			clients[pl].view_list.insert(client);
			clients[pl].vl.unlock();
			send_put_player_packet(pl, client);
		}
		else // 있다면
		{
			clients[pl].vl.unlock();
			send_pos_packet(pl, client);
		}
	}

	// 3. old_vl에 있고 new_vl에는 없는 플레이어
	for (auto pl : old_vl)
	{
		if (0 < new_vl.count(pl)) continue;
		clients[client].vl.lock();
		clients[client].view_list.erase(pl);
		clients[client].vl.unlock();
		send_remove_player_packet(client, pl);
		if (true == is_NPC(pl)) continue;
		clients[pl].vl.lock();
		if (0 == clients[pl].view_list.count(pl))
		{
			clients[pl].view_list.erase(client);
			clients[pl].vl.unlock();
			send_remove_player_packet(pl, client);
		}
		else
			clients[pl].vl.unlock();

	}
	// 시야에 있는 몬스터들에게 플레이어의 이동을 알린다.
	for (auto monster : new_vl)
	{
		if (false == is_NPC(monster)) continue;
		if (WAR != clients[monster].type) continue;

		OVER_EX *ex_over = new OVER_EX;
		ex_over->event_t = EV_DETECT_PLAYER_MOVE;
		ex_over->target_client = client;
		PostQueuedCompletionStatus(g_iocp, 1, monster, &ex_over->over);
	}
}

void process_packet(int client, char* packet)
{
	switch (packet[1])
	{
	case CS_LOGIN:
	{
		cs_packet_login *p = reinterpret_cast<cs_packet_login *>(packet);
		// DB연결시 아이디 비교 필요
		/*if (false == compare_id(client, p->id))
		{
			send_login_fail_packet(client);
			disconnect_client(client);
			return;
		}*/
		WCHAR tmpwc[10];
		wcsncpy_s(tmpwc, p->player_id, 10);
		wprintf_s(L"%s\n", tmpwc);

		send_login_ok_packet(client);
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

			clients[i].vl.lock();
			clients[i].view_list.insert(client);
			clients[i].vl.unlock();
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
			clients[client].vl.lock();
			clients[client].view_list.insert(i);
			clients[client].vl.unlock();
			send_put_player_packet(client, i);
		}
		break;
	}
	case CS_MOVE:
	{
		move_player(client, packet);
		break;
	}
	case CS_ATTACK:
	{
		cout << client << " 플레이어 attack!!\n";
		clients[client].vl.lock();
		auto tmp_vl = clients[client].view_list;
		clients[client].vl.unlock();

		for (auto obj : tmp_vl)
		{
			attack_player(obj, client, 100);
		}
		break;
	}
	case CS_LOGOUT:
	{
		cout << client << " 접속종료\n";
		disconnect_client(client);
		break;
	}
	default:
	{	
		wcout << L"정의되지 않은 패킷 도착 오류!!\n";
		while (true);
	}
	}
}

void disconnect_client(int id)
{
	for (int i = 0; i < MAX_USER; ++i)
	{
		if (false == clients[i].in_use)
			continue;
		if (i == id)
			continue;
		clients[i].vl.lock();
		if (0 == clients[i].view_list.count(id))
		{
			clients[i].vl.unlock();
			continue;
		}
		else
			clients[i].vl.unlock();

		// i가 나를 보고 있다면 지워줘야
		clients[i].vl.lock();
		clients[i].view_list.erase(id);
		clients[i].vl.unlock();
		send_remove_player_packet(i, id);
	}
	closesocket(clients[id].socket);
	clients[id].in_use = false;
	clients[id].vl.lock();
	clients[id].view_list.clear();
	clients[id].vl.unlock();
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
	if (false == clients[id].isAttack)
	{
		switch (rand() % 4)
		{
		case 0: 
			if (x > 0 && x > clients[id].move_minx) 
				x--; 
			if (false == mapData[x][y])
			{
				x++;
			}
			break;
		case 1: 
			if (x < (WORLD_WIDTH - 1) && x < clients[id].move_maxx) 
				x++; 
			if (false == mapData[x][y])
			{
				x--;
			}
			break;
		case 2: 
			if (y > 0 && y > clients[id].move_miny)
				y--; 
			if (false == mapData[x][y])
			{
				y++;
			}
			break;
		case 3: 
			if (y < (WORLD_HEIGHT - 1) && y < clients[id].move_maxy)
				y++; 
			if (false == mapData[x][y])
			{
				y--;
			}
			break;
		}
		clients[id].x = x;
		clients[id].y = y;
	}
	else
	{
		if (clients[clients[id].target_id].x - clients[id].x != 0)
		{
			clients[id].x += (clients[clients[id].target_id].x - clients[id].x) / abs(clients[clients[id].target_id].x - clients[id].x);
		}
		else if (clients[clients[id].target_id].y - clients[id].y != 0)
		{
			clients[id].y += (clients[clients[id].target_id].y - clients[id].y) / abs(clients[clients[id].target_id].y - clients[id].y);
		}
	}
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
		clients[pl].vl.lock();
		if (0 == clients[pl].view_list.count(id))
		{
			// 새로만났으니 플레이어에게도 알림
			clients[pl].view_list.insert(id);
			clients[pl].vl.unlock();
			send_put_player_packet(pl, id);
		}
		else {
			clients[pl].vl.unlock();
			send_put_player_packet(pl, id);
		}
	}
	// 헤어진 플레이어 처리
	for (auto pl : old_vl)
	{
		if (0 == new_vl.count(pl))
		{
			// 상대 플레이어 뷰리스트에 내가 아직 지워지지 않았다면
			clients[pl].vl.lock();
			if (0 < clients[pl].view_list.count(id))
			{
				clients[pl].view_list.erase(id);
				clients[pl].vl.unlock();
			}
			else
				clients[pl].vl.unlock();

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
			if (ROAMING != clients[ev.obj_id].movetype) continue;
			player_is_near = true;
			break;
		}

		// 그러나 한 번 멈춘 NPC들이 다시 깨어나지 않는다.
		// 플레이어가 주위 NPC들을 깨워줘야 한다.
		if (player_is_near) {
			if (false == clients[ev.obj_id].in_use) return;
			random_move_NPC(ev.obj_id);
			add_timer(ev.obj_id, EV_MOVE, high_resolution_clock::now() + 1s);
		}
		else
		{
			clients[ev.obj_id].is_sleeping = true;
		}
		break;
	}
	case EV_DETECT_PLAYER_MOVE:
	{
		lua_State *L = clients[ev.obj_id].L;
		// 확장구조체에 이동한 플레이어 id가 포함되어야 한다.
		int player_id = ev.target_id;
		// 루아 함수호출
		// 루아 가상머신은 동시 호출이 불가능한 언어. 꼬일 수 있다.
		// 멀티스레드에서 동시 호출할 수 있으므로 lock()필요.
		clients[ev.obj_id].lua.lock();
		lua_getglobal(L, "event_player_move");
		lua_pushnumber(L, player_id);
		lua_pcall(L, 1, 0, 0);
		clients[ev.obj_id].lua.unlock();
		break;
	}
	case EV_ATTACK:
	{
		if (ev.target_id <= NPC_ID_START)
		{
			attack_player(ev.target_id, ev.obj_id, clients[ev.obj_id].atk);
			if (true == clients[ev.obj_id].isAttack && true == clients[ev.obj_id].in_use)
				add_timer(ev.obj_id, EV_ATTACK, high_resolution_clock::now() + 1s);
		}
		break;
	}
	case EV_REVIVE:
	{
		int npc_id = ev.obj_id;
		while (true)
		{
			clients[npc_id].x = rand() % WORLD_WIDTH;
			clients[npc_id].y = rand() % WORLD_HEIGHT;
			if (true == mapData[clients[npc_id].y][clients[npc_id].x])
				break;
		}
		clients[npc_id].move_minx = clients[npc_id].x - 10;
		clients[npc_id].move_miny = clients[npc_id].y - 10;
		clients[npc_id].move_maxx = clients[npc_id].x + 10;
		clients[npc_id].move_maxy = clients[npc_id].y + 10;

		clients[npc_id].hp = 50;
		lua_State *L = clients[npc_id].L;
		lua_getglobal(L, "set_hp");
		lua_pushnumber(L, clients[npc_id].hp);
		lua_pcall(L, 1, 0, 0);

		clients[npc_id].in_use = true;
		clients[npc_id].is_sleeping = true;
		clients[npc_id].isAttack = false;
		add_timer(npc_id, EV_MOVE, high_resolution_clock::now() + 1s);
		break;
	}
	case EV_HEAL:
	{
		if (clients[ev.obj_id].hp >= 100)
		{
			clients[ev.obj_id].isHealing = false;
			return;
		}
		clients[ev.obj_id].hp += 20;
		if (clients[ev.obj_id].hp > 100)
			clients[ev.obj_id].hp = 100;
		send_stat_change(ev.obj_id, ev.obj_id);
		add_timer(ev.obj_id, EV_HEAL, high_resolution_clock::now() + 5s);
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
			//wcout << "Packet from Client: " << key << "\n";
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
		else if (EV_DETECT_PLAYER_MOVE == over_ex->event_t)
		{
			EVENT_ST ev;
			ev.obj_id = key;
			ev.start_time = high_resolution_clock::now() + 1s;
			ev.type = EV_DETECT_PLAYER_MOVE;
			ev.target_id = over_ex->target_client;
			process_event(ev);
			delete over_ex;
		}
		else if (EV_ATTACK == over_ex->event_t)
		{
			EVENT_ST ev;
			ev.obj_id = key;
			ev.start_time = high_resolution_clock::now() + 1s;
			ev.type = EV_ATTACK;
			ev.target_id = over_ex->target_client;
			clients[key].target_id = over_ex->target_client;
			process_event(ev);
			delete over_ex;
		}
		else if (EV_REVIVE == over_ex->event_t)
		{
			EVENT_ST ev;
			ev.obj_id = key;
			ev.start_time = high_resolution_clock::now() + 1s;
			ev.type = EV_REVIVE;
			process_event(ev);
			delete over_ex;
		}
		else if (EV_HEAL == over_ex->event_t)
		{
			EVENT_ST ev;
			ev.obj_id = key;
			ev.start_time = high_resolution_clock::now() + 5s;
			ev.type = EV_HEAL;
			process_event(ev);
			delete over_ex;
		}
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
		// 임시객체를 복사생성하므로 날아가버림
		//clients[new_id] = SOCKETINFO{clientSocket};
		// Solution 1. emplace로 생성 : 될 수도 있고 안 될 수도 있다. (복사할 수도 안 할 수도)
		// Solution 2. over_ex를 new로 생성 : 코드가 복잡해짐.
		// Solution 3. clients를 map이 아닌 배열로 관리 : null 생성자를 추가해야 한다.

		// 마지막 방법 사용
		// 이전에 접속하고 나간애가 쓰던 길이가 남아있으면 안되기 때문에 초기화
		clients[new_id].socket = clientSocket;
		clients[new_id].prev_size = 0;
		clients[new_id].x = clients[new_id].y = 9;
		clients[new_id].hp = 100;
		clients[new_id].exp = 0;
		clients[new_id].level = 1;
		clients[new_id].obj_class = PLAYER;
		clients[new_id].vl.lock();
		clients[new_id].view_list.clear();
		clients[new_id].vl.unlock();
		ZeroMemory(&clients[new_id].over_ex.over, sizeof(clients[new_id].over_ex.over));

		flags = 0;

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), g_iocp, new_id, 0);

		// 얘만 늦게하는 이유는 true해주는 순간 send가 막 날아가는데
		// IOCP연결 이전이므로 IOCP 콜백을 받지 못 해 메모리 누수가 일어나기 때문
		// 따라서 IOCP를 통해 통신할 수 있도록 CICP함수 이후에 true로 설정
		// CICP이전에 해주면 영원히 recv못 받음
		clients[new_id].in_use = true;

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
			over_ex->event_t = ev.type;
			over_ex->target_client = clients[ev.obj_id].target_id;
			PostQueuedCompletionStatus(g_iocp, 1, ev.obj_id, &over_ex->over);
			// lock을 하고 필요가 없어지면 바로 unlock해줘야 함. 
			// 중간에 break로 탈출해버리면 lock이 잡힌채로 끝나게 된다. 교착상태
		}
	}
}

int main()
{
	vector <thread> worker_threads;

	// error_display를 위함
	wcout.imbue(locale("korean"));

	Initialize_Map();

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
	//thread AI_thread{ do_AI };
	//AI_thread.join();
	thread timer_thread{ do_timer };
	timer_thread.join();
	accept_thread.join();
	for (auto &th : worker_threads) th.join();
}