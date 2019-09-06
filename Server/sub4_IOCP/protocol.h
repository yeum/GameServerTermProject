#pragma once

constexpr int MAX_USER = 20000;
constexpr int MAX_STR_LEN = 50;

constexpr int NPC_ID_START = 20000;
// bat 500개
constexpr int BAT_ID_START = 20000;
// slime 2500개
constexpr int SLIME_ID_START = 20500;
// eyeball 1000천개
constexpr int EYEBALL_ID_START = 23000;
// bigworm 800개
constexpr int BIGWORM_ID_START = 24000;
// ghost 200개
constexpr int GHOST_ID_START = 24800;
constexpr int NUM_NPC = 25000;

#define WORLD_WIDTH		300
#define WORLD_HEIGHT	300

#define VIEW_RADIUS		10

#define SERVER_PORT		3500

#define DIR_UP 0
#define DIR_DOWN 1
#define DIR_RIGHT 2
#define DIR_LEFT 3


#define CS_LOGIN		1
#define CS_MOVE			2
#define CS_ATTACK		3
#define CS_CHAT			4
#define CS_LOGOUT		5

#define SC_LOGIN_OK			1
#define SC_LOGIN_FAIL		2
#define SC_POSITION			3
#define SC_CHAT				4
#define SC_STAT_CHANGE		5
#define SC_REMOVE_OBJECT	6
#define SC_ADD_OBJECT		7

#pragma pack(push ,1)

struct sc_packet_login_ok {
	char size;
	char type;
	int id;
	short	x, y;
	int	HP, LEVEL, EXP;
};

struct sc_packet_login_fail {
	char size;
	char type;
};

struct sc_packet_position {
	char size;
	char type;
	int id;
	char x, y;
};

struct sc_packet_chat {
	char size;
	char type;
	int	id;
	wchar_t	message[MAX_STR_LEN];
};

struct sc_packet_stat_change {
	char size;
	char type;
	int	id;
	int	HP, LEVEL, EXP;
};


struct sc_packet_remove_object {
	char size;
	char type;
	int id;
};

struct sc_packet_add_object {
	char size;
	char type;
	int id;
	int obj_class;		// 1: PLAYER,    2:ORC,  3:Dragon, …..
	short x, y;
	int	HP, LEVEL, EXP;

};


struct cs_packet_login {
	char	size;
	char	type;
	wchar_t player_id[10];
};

struct cs_packet_move {
	char	size;
	char	type;
	char	direction;		// 0:Up, 1:Down, 2:Left, 3:Right
};

struct cs_packet_attack {
	char	size;
	char	type;
};

struct cs_packet_chat {
	char	size;
	char	type;
	wchar_t	message[MAX_STR_LEN];
};

struct cs_packet_logout {
	char	size;
	char	type;
};

#pragma pack (pop)