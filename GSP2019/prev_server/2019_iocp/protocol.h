#pragma once

constexpr int MAX_STR_LEN = 50;

#define WORLD_WIDTH		800
#define WORLD_HEIGHT	800

#define NPC_ID_START	20000

#define SERVER_PORT		3500
#define NUM_NPC			100000

#define CS_LOGIN	1
#define CS_MOVE		2
#define CS_ATTACK	3
#define CS_CHAT		4
#define CS_LOGOUT	5
#define CS_TELEPORT 6


#define KEY_UP		1
#define KEY_DOWN	2
#define KEY_LEFT	3
#define KEY_RIGHT	4

#define SC_LOGIN_OK			1
#define SC_LOGIN_FAIL		2
#define SC_POS				3
#define SC_PUT_OBJECT		4
#define SC_REMOVE_OBJECT	5
#define SC_CHAT				6
#define SC_STAT_CHANGE		7

#pragma pack(push ,1)

struct sc_packet_login_ok {
	char size;
	char type;
	int id;
	short x, y;
	short hp;
	short level;
	int	exp;
};

struct sc_packet_login_fail {
	char size;
	char type;
};

struct sc_packet_pos {
	char size;
	char type;
	int id;
	short x, y;
};

struct sc_packet_put_object {
	char size;
	char type;
	int id;
	//	char type;
	short x, y;
	// 렌더링 정보, 종족, 성별, 착용 아이템, 캐릭터 외형, 이름, 길드....
};

struct sc_packet_remove_object {
	char size;
	char type;
	int id;
};

struct sc_packet_chat {
	char size;
	char type;
	int	 id;
	char chat[100];
};

struct sc_packet_stat_change {
	char size;
	char type;
	short hp;
	short level;
	int   exp;
};

struct cs_packet_login {
	char	size;
	char	type;
	char	id[50];
};

struct cs_packet_move {
	char	size;
	char	type;
	char	direction;
};

struct cs_packet_attack {
	char	size;
	char	type;
};

struct cs_packet_chat {
	char	size;
	char	type;
	char	chat_str[100];
};

struct cs_packet_logout {
	char	size;
	char	type;
};

struct cs_packet_teleport {
	char	size;
	char	type;
};

#pragma pack (pop)