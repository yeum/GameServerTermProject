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

// Overlapped����ü Ȯ��
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
	// ID�� ����, ID�� ������� ���� ������ unordered_set, �� ������ ������ ������ ������ unordered��
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
	// �� �̺�Ʈ�� � ��ü���� �߻��ؾ� �ϴ���
	int obj_id;
	// � �̺�Ʈ��
	EVENT_TYPE type;
	// ���� ����Ǿ�� �ϴ���
	high_resolution_clock::time_point start_time;
	int target_id;


	// ����� �켱���� ť���� �ð������� �����ϴ� ���� �𸥴�. operator�� �������� ��.
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
	wcout << L"���� [" << err_no << L"] " << lpMsgBuf << "\n";
	while (true);
	LocalFree(lpMsgBuf);
}

int API_get_x(lua_State *L)
{
	int obj_id = (int)lua_tonumber(L, -1);
	// �Ķ���� 1���� �ڽ��� ȣ���� �Լ��� pop�Ͽ� �� 2�� pop
	lua_pop(L, 2);
	int x = clients[obj_id].x;
	lua_pushnumber(L, x);

	// �����Ķ���� �� �� �ִ� �˸��� ����
	return 1;
}

int API_get_y(lua_State *L)
{
	int obj_id = (int)lua_tonumber(L, -1);
	// �Ķ���� 1���� �ڽ��� ȣ���� �Լ��� pop�Ͽ� �� 2�� pop
	lua_pop(L, 2);
	int y = clients[obj_id].y;
	lua_pushnumber(L, y);

	// �����Ķ���� �� �� �ִ� �˸��� ����
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
		// addtimer�� 1�ʿ� �� �� �� �����ϵ��� ����
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
		
		// ������ġ
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
		// ��� ���� �ʱ�ȭ
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

		// API�Լ� ���
		lua_register(L, "API_get_x", API_get_x);
		lua_register(L, "API_get_y", API_get_y);
		lua_register(L, "API_SetIsAttack", API_SetIsAttack);

		clients[npc_id].in_use = true;
		clients[npc_id].is_sleeping = true;
		clients[npc_id].isAttack = false;
		if(ROAMING == clients[npc_id].movetype)
			add_timer(npc_id, EV_MOVE, high_resolution_clock::now() + 1s);
	}
	printf("NPC�ʱ�ȭ �Ϸ�\n");
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
		cout << obj_id << "�� " << client << "�� ������ " << atk << "�� �������� �������ϴ�\n";
		if (clients[client].hp <= 0)
		{
			if (false == is_NPC(client))
			{
				cout << client << "�� " << obj_id << "�� ���� ����߽��ϴ�.\n";
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
					clients[pl].vl.lock();
					if (0 < clients[pl].view_list.count(client))
					{
						clients[pl].vl.unlock();
						send_pos_packet(pl, client);
					}
					else {	// ��� viewlist�� ���� ���� ��
						clients[pl].view_list.insert(client);
						clients[pl].vl.unlock();
						send_put_player_packet(pl, client);
					}
				}

				// 2. old_vl�� ���� new_vl���� �ִ� �÷��̾�(���θ����ֵ�)
				for (auto pl : new_vl)
				{
					if (0 < old_vl.count(pl)) continue;
					// �� viewlist�� ��� �߰� �� put player
					clients[client].vl.lock();
					clients[client].view_list.insert(pl);
					clients[client].vl.unlock();
					send_put_player_packet(client, pl);
					if (true == is_NPC(pl)) {
						wakeup_NPC(pl);
						continue;
					}
					// ���� ��� viewlist�� ���ٸ�
					clients[pl].vl.lock();
					if (0 == clients[pl].view_list.count(client))
					{
						clients[pl].view_list.insert(client);
						clients[pl].vl.unlock();
						send_put_player_packet(pl, client);
					}
					else // �ִٸ�
					{
						clients[pl].vl.unlock();
						send_pos_packet(pl, client);
					}
				}

				// 3. old_vl�� �ְ� new_vl���� ���� �÷��̾�
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
				// �þ߿� �ִ� ���͵鿡�� �÷��̾��� �̵��� �˸���.
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
				cout << obj_id << "�� " << client << "�� ���񷯼� " << getExp << "�� ����ġ�� ������ϴ�.\n";
				// addtimer�� 30�� �� �� initialize_NPC() ȣ���ϱ�
				add_timer(client, EV_REVIVE, high_resolution_clock::now() + 30s);
			}
		}/*
		wstring s1 = L"";
		wstring s2 = L"";
		switch (clients[client].obj_class)
		{
		case PLAYER:
			s1 = L"PLAYER��";
			break;
		case SLIME:
			s1 = L"SLIME��";
			break;
		case EYEBALL:
			s1 = L"EYEBALL��";
			break;
		case BIGWORM:
			s1 = L"BIGWORM��";
			break;
		case GHOST:
			s1 = L"GHOST��";
			break;
		}

		switch (clients[obj_id].obj_class)
		{
		case PLAYER:
			s1 = L"PLAYER��";
			break;
		case SLIME:
			s1 = L"SLIME��";
			break;
		case EYEBALL:
			s1 = L"EYEBALL��";
			break;
		case BIGWORM:
			s1 = L"BIGWORM��";
			break;
		case GHOST:
			s1 = L"GHOST��";
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
	// wchar_t�̹Ƿ� strcpy�� �ƴ϶� wcscpy���
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
	// 0���� ������, 1���� ��ŶŸ��
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
		wcout << L"���ǵ��� ���� MOVE��Ŷ ���� ����!!\n";
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

	// NPC������ viewList�� �ֱ�
	for (int i = MAX_USER; i < MAX_USER + NUM_NPC; ++i)
	{
		if (false == clients[i].in_use) continue;
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
		clients[pl].vl.lock();
		if (0 < clients[pl].view_list.count(client))
		{
			clients[pl].vl.unlock();
			send_pos_packet(pl, client);
		}
		else {	// ��� viewlist�� ���� ���� ��
			clients[pl].view_list.insert(client);
			clients[pl].vl.unlock();
			send_put_player_packet(pl, client);
		}
	}

	// 2. old_vl�� ���� new_vl���� �ִ� �÷��̾�(���θ����ֵ�)
	for (auto pl : new_vl)
	{
		if (0 < old_vl.count(pl)) continue;
		// �� viewlist�� ��� �߰� �� put player
		clients[client].vl.lock();
		clients[client].view_list.insert(pl);
		clients[client].vl.unlock();
		send_put_player_packet(client, pl);
		if (true == is_NPC(pl)) {
			wakeup_NPC(pl);
			continue;
		}
		// ���� ��� viewlist�� ���ٸ�
		clients[pl].vl.lock();
		if (0 == clients[pl].view_list.count(client))
		{
			clients[pl].view_list.insert(client);
			clients[pl].vl.unlock();
			send_put_player_packet(pl, client);
		}
		else // �ִٸ�
		{
			clients[pl].vl.unlock();
			send_pos_packet(pl, client);
		}
	}

	// 3. old_vl�� �ְ� new_vl���� ���� �÷��̾�
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
	// �þ߿� �ִ� ���͵鿡�� �÷��̾��� �̵��� �˸���.
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
		// DB����� ���̵� �� �ʿ�
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
		// ���� �����鿡�� ���� ������ ������ ���
		for (int i = 0; i < MAX_USER; ++i)
		{
			if (false == clients[i].in_use)
				continue;
			if (false == Is_Near_Object(client, i))		// ��ó�� �ִ� ��쿡�� ������
				continue;
			if (i == client)
				continue;

			clients[i].vl.lock();
			clients[i].view_list.insert(client);
			clients[i].vl.unlock();
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
		cout << client << " �÷��̾� attack!!\n";
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
		cout << client << " ��������\n";
		disconnect_client(client);
		break;
	}
	default:
	{	
		wcout << L"���ǵ��� ���� ��Ŷ ���� ����!!\n";
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

		// i�� ���� ���� �ִٸ� �������
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
	// �̵� �� �˻�
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
		clients[pl].vl.lock();
		if (0 == clients[pl].view_list.count(id))
		{
			// ���θ������� �÷��̾�Ե� �˸�
			clients[pl].view_list.insert(id);
			clients[pl].vl.unlock();
			send_put_player_packet(pl, id);
		}
		else {
			clients[pl].vl.unlock();
			send_put_player_packet(pl, id);
		}
	}
	// ����� �÷��̾� ó��
	for (auto pl : old_vl)
	{
		if (0 == new_vl.count(pl))
		{
			// ��� �÷��̾� �丮��Ʈ�� ���� ���� �������� �ʾҴٸ�
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
			// timer �����峪 �̺�Ʈ ť�� ������� ������ �����ϰ� ���� �� ������
			// 1�ʿ� �� ���� �ƴ϶� �� �ʿ� �� �� ������.
			// => �� �ʿ��� �ֵ鸸 �����̰� �ؾ�. => �÷��̾� ������ �ִ� NPC��
			if (false == clients[i].in_use) continue;
			if (false == Is_Near_Object(i, ev.obj_id)) continue;
			if (ROAMING != clients[ev.obj_id].movetype) continue;
			player_is_near = true;
			break;
		}

		// �׷��� �� �� ���� NPC���� �ٽ� ����� �ʴ´�.
		// �÷��̾ ���� NPC���� ������� �Ѵ�.
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
		// Ȯ�屸��ü�� �̵��� �÷��̾� id�� ���ԵǾ�� �Ѵ�.
		int player_id = ev.target_id;
		// ��� �Լ�ȣ��
		// ��� ����ӽ��� ���� ȣ���� �Ұ����� ���. ���� �� �ִ�.
		// ��Ƽ�����忡�� ���� ȣ���� �� �����Ƿ� lock()�ʿ�.
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
			//wcout << "Packet from Client: " << key << "\n";
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
		// �ӽð�ü�� ��������ϹǷ� ���ư�����
		//clients[new_id] = SOCKETINFO{clientSocket};
		// Solution 1. emplace�� ���� : �� ���� �ְ� �� �� ���� �ִ�. (������ ���� �� �� ����)
		// Solution 2. over_ex�� new�� ���� : �ڵ尡 ��������.
		// Solution 3. clients�� map�� �ƴ� �迭�� ���� : null �����ڸ� �߰��ؾ� �Ѵ�.

		// ������ ��� ���
		// ������ �����ϰ� �����ְ� ���� ���̰� ���������� �ȵǱ� ������ �ʱ�ȭ
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

		// �길 �ʰ��ϴ� ������ true���ִ� ���� send�� �� ���ư��µ�
		// IOCP���� �����̹Ƿ� IOCP �ݹ��� ���� �� �� �޸� ������ �Ͼ�� ����
		// ���� IOCP�� ���� ����� �� �ֵ��� CICP�Լ� ���Ŀ� true�� ����
		// CICP������ ���ָ� ������ recv�� ����
		clients[new_id].in_use = true;

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
			over_ex->event_t = ev.type;
			over_ex->target_client = clients[ev.obj_id].target_id;
			PostQueuedCompletionStatus(g_iocp, 1, ev.obj_id, &over_ex->over);
			// lock�� �ϰ� �ʿ䰡 �������� �ٷ� unlock����� ��. 
			// �߰��� break�� Ż���ع����� lock�� ����ä�� ������ �ȴ�. ��������
		}
	}
}

int main()
{
	vector <thread> worker_threads;

	// error_display�� ����
	wcout.imbue(locale("korean"));

	Initialize_Map();

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
	//thread AI_thread{ do_AI };
	//AI_thread.join();
	thread timer_thread{ do_timer };
	timer_thread.join();
	accept_thread.join();
	for (auto &th : worker_threads) th.join();
}