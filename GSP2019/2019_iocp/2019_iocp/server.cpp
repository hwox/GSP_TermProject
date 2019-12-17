#include <iostream>
#include <map>
#include <thread>
#include <set>
#include <mutex>
#include <chrono>
#include <queue>


extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

using namespace std;
using namespace chrono;
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "lua53.lib")

#include "protocol.h"
#include "DataBase.h"
#include "StateCtrl.h"

#define MAX_BUFFER        1024
constexpr auto VIEW_RANGE = 3;

enum EVENT_TYPE { EV_RECV, EV_SEND, EV_MOVE, EV_MOVE_TARGET, EV_ATTACK, EV_HEAL, EV_RUN, EV_IDLE, EV_DIE };

struct OVER_EX {
	WSAOVERLAPPED over;
	WSABUF	wsabuf[1];
	char	net_buf[MAX_BUFFER];
	EVENT_TYPE	event_type;

	char notifiedPlayer;
	int repeat;
};

struct SOCKETINFO
{
	OVER_EX	recv_over;
	SOCKET	socket;
	int		id;

	bool is_active;
	short	x, y;
	set <int> near_id;
	mutex near_lock;
	lua_State *L;

	int name; // 그 db에 있는 값 저장할 id
	int level;
	int hp;
	int exp;


	bool die;
	char m_type;
	int state;
};

struct EVENT {
	int obj_id;
	high_resolution_clock::time_point wakeup_time;
	int event_type;
	int target_obj;

	bool stop;
	constexpr bool operator < (const EVENT& left) const
	{
		return wakeup_time > left.wakeup_time;
	}
};

priority_queue <EVENT> timer_queue;
//priority_queue <pair<EVENT, EVENT_TYPE>> timer_queue;
mutex  timer_lock;
DataBase db;
StateCtrl state;


map <int, SOCKETINFO *> clients;
HANDLE	g_iocp;

int new_user_id = 0;
void HEAL_repeat(int player_id);
void LOGIN(int key, int id);
void DIE_Check(int player_id);


void error_display(const char *msg, int err_no)
{
	WCHAR *lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	cout << msg;
	wcout << L"에러 " << lpMsgBuf << endl;
	while (true);
	LocalFree(lpMsgBuf);
}

void add_timer(EVENT &ev)
{
	timer_lock.lock();
	//timer_queue.push(make_pair(ev, _type));
	timer_queue.push(ev);
	timer_lock.unlock();
}

bool Is_NPC(int id)
{
	return id >= NPC_ID_START;
}

bool Is_Active(int npc_id)
{
	return clients[npc_id]->is_active;
}

bool is_near(int a, int b)
{
	if (VIEW_RANGE < abs(clients[a]->x - clients[b]->x)) return false;
	if (VIEW_RANGE < abs(clients[a]->y - clients[b]->y)) return false;
	return true;
}

bool is_near_NPC(int a, int b)
{
	if (VIEW_RANGE + VIEW_RANGE < abs(clients[a]->x - clients[b]->x)) return false;
	if (VIEW_RANGE + VIEW_RANGE < abs(clients[a]->y - clients[b]->y)) return false;
	return true;
}

bool is_near_attack(int a, int b)
{
	// x+1 , y
	if (((clients[a]->x + 1) == (clients[b]->x)) && (clients[a]->y == clients[b]->y))
		return true;
	// x, y-1
	if (((clients[a]->x) == (clients[b]->x)) && ((clients[a]->y - 1) == clients[b]->y))
		return true;
	// x-1. y
	if (((clients[a]->x - 1) == (clients[b]->x)) && ((clients[a]->y) == clients[b]->y))
		return true;
	// x, y+1
	if (((clients[a]->x) == (clients[b]->x)) && ((clients[a]->y + 1) == clients[b]->y))
		return true;

	return false;
}
void send_packet(int id, void *buff)
{
	char *packet = reinterpret_cast<char *>(buff);
	int packet_size = packet[0];
	OVER_EX *send_over = new OVER_EX;
	memset(send_over, 0x00, sizeof(OVER_EX));
	send_over->event_type = EV_SEND;
	memcpy(send_over->net_buf, packet, packet_size);
	send_over->wsabuf[0].buf = send_over->net_buf;
	send_over->wsabuf[0].len = packet_size;
	int ret = WSASend(clients[id]->socket, send_over->wsabuf, 1, 0, 0, &send_over->over, 0);
	if (0 != ret) {
		int err_no = WSAGetLastError();
		if (WSA_IO_PENDING != err_no)
			error_display("WSARecv Error :", err_no);
	}
}

void send_login_ok_packet(int id)
{
	sc_packet_login_ok packet;
	packet.id = id;
	packet.size = sizeof(packet);
	packet.type = SC_LOGIN_OK;
	send_packet(id, &packet);
}

void send_login_fail_packet(int id)
{
	sc_packet_login_fail packet;
	packet.size = sizeof(packet);
	packet.type = SC_LOGIN_FAIL;
	send_packet(id, &packet);
}

void send_put_object_packet(int client, int new_id)
{
	sc_packet_put_object packet;
	packet.id = new_id;
	packet.size = sizeof(packet);
	packet.type = SC_PUT_OBJECT;
	packet.x = clients[new_id]->x;
	packet.y = clients[new_id]->y;
	packet.npc_type = clients[new_id]->m_type;

	packet.hp = clients[new_id]->hp;
	packet.level = clients[new_id]->level;
	packet.exp = clients[new_id]->exp;

	send_packet(client, &packet);

	if (client == new_id) return;
	lock_guard<mutex>lg{ clients[client]->near_lock };
	clients[client]->near_id.insert(new_id);
}

void send_pos_packet(int client, int mover)
{
	sc_packet_pos packet;
	packet.id = mover;
	packet.size = sizeof(packet);
	packet.type = SC_POS;
	packet.x = clients[mover]->x;
	packet.y = clients[mover]->y;

	clients[client]->near_lock.lock();
	if (0 != clients[client]->near_id.count(mover)) {
		clients[client]->near_lock.unlock();
		send_packet(client, &packet);
	}
	else {
		clients[client]->near_lock.unlock();
		send_put_object_packet(client, mover);
	}
}

void send_remove_player_packet(int client, int leaver)
{
	sc_packet_remove_object packet;
	packet.id = leaver;
	packet.size = sizeof(packet);
	packet.type = SC_REMOVE_OBJECT;
	send_packet(client, &packet);

	lock_guard<mutex>lg{ clients[client]->near_lock };
	clients[client]->near_id.erase(leaver);
}

void send_chat_player_packet(int client, int chatter, char mess[])
{
	sc_packet_chat packet;
	packet.id = chatter;
	packet.size = sizeof(packet);
	packet.type = SC_CHAT;
	strcpy_s(packet.chat, mess);
	send_packet(client, &packet);
}
void send_state_change_packet(int id, short hp, short level, int exp)
{
	sc_packet_stat_change packet;
	packet.size = sizeof(packet);
	packet.type = SC_STAT_CHANGE;
	packet.hp = hp;
	packet.level = level;
	packet.exp = exp;

	send_packet(id, &packet);

}
void send_notice_packet(int client, int chatter, char mess[])
{
	sc_packet_chat packet;
	packet.size = sizeof(packet);
	packet.type = SC_NOTICE;
	packet.id = chatter;
	strcpy_s(packet.chat, mess);
	send_packet(client, &packet);
}
bool is_near_id(int player, int other)
{
	lock_guard <mutex> gl{ clients[player]->near_lock };
	return (0 != clients[player]->near_id.count(other));
}

//void send_die_packet(int client)
//{
//	sc_packet_die packet;
//	packet.size = sizeof(packet);
//	packet.type = SC_DIE;
//	packet.id = client;
//	send_packet(client, &packet);
//}

void Attack(int id)
{
	clients[id]->near_lock.lock();
	auto vl_list = clients[id]->near_id;
	clients[id]->near_lock.unlock();

	for (auto &obj : vl_list)
	{
		if (obj == id) continue;
		if (true == Is_NPC(obj) && is_near_attack(id, obj))
		{
			clients[obj]->hp -= (clients[id]->level * 5);
			if (clients[obj]->hp <= 0)
			{
				clients[id]->exp += clients[obj]->exp;

				send_state_change_packet(id, clients[id]->hp, clients[id]->level, clients[id]->exp);

				clients[obj]->die = true;
				// 사망 여기! 
				//send_die_packet(id);
			}
			send_put_object_packet(id, obj);

			char hit[5];
			sprintf_s(hit, "hit");
			send_chat_player_packet(id, obj, hit);


			set <int> new_vl;
			for (auto &cl : clients) {
				int other = cl.second->id;
				if (!clients[obj]->die) {
					if (id == other)
					{
						char text[50];
						sprintf_s(text, "Number %d Player Hit %d", id, obj);
						send_notice_packet(other, id, text);
						continue;
					}
					if (false == Is_NPC(other))
					{
						char text[50];
						sprintf_s(text, "Number %d Player Hit %d", id, obj);
						send_put_object_packet(other, obj);
						send_notice_packet(other, id, text);
					}
				}
				else {
					if (false == Is_NPC(other))
					{
						int exp = clients[obj]->exp;
						char text[50];
						sprintf_s(text, "Number %d Player Get %d Exp", id, exp);
						//send_notice_packet(obj, id, text);
						send_notice_packet(other, id, text);
					}
				}
			}

		}
	}

}

void Set_NPC_StandEV(int key, int id)
{
	switch (clients[key]->m_type)
	{
	case MST_PIECE:
	{
		OVER_EX  *over_ex = new OVER_EX;
		over_ex->event_type = EV_IDLE;
		*(int *)(over_ex->net_buf) = id;
		PostQueuedCompletionStatus(g_iocp, 1, key, &over_ex->over);
		break;
	}
	case MST_WAR:
	{
		OVER_EX  *over_ex = new OVER_EX;
		over_ex->event_type = EV_MOVE;
		*(int *)(over_ex->net_buf) = id;
		PostQueuedCompletionStatus(g_iocp, 1, key, &over_ex->over);
		break;
	}
	case MST_STOP:
	{
		OVER_EX  *over_ex = new OVER_EX;
		over_ex->event_type = EV_IDLE;
		*(int *)(over_ex->net_buf) = id;
		PostQueuedCompletionStatus(g_iocp, 1, key, &over_ex->over);
		break;
	}
	case MST_LOAMING:
	{
		OVER_EX  *over_ex = new OVER_EX;
		over_ex->event_type = EV_MOVE;
		*(int *)(over_ex->net_buf) = id;
		PostQueuedCompletionStatus(g_iocp, 1, key, &over_ex->over);
		break;
	}
	}
}

void Set_NPC_TimeEV(int key, int id)
{
	switch (clients[key]->m_type)
	{
	case MST_PIECE:
	{
		EVENT ev{ key, high_resolution_clock::now() + 1s, EV_IDLE, 0 };
		add_timer(ev);
		break;
	}
	case MST_WAR:
	{
		EVENT ev{ key, high_resolution_clock::now() + 1s, EV_MOVE, 0 };
		add_timer(ev);
		break;
	}
	case MST_STOP:
	{
		EVENT ev{ key, high_resolution_clock::now() + 1s, EV_IDLE, 0 };
		add_timer(ev);
		break;
	}
	case MST_LOAMING:
	{
		EVENT ev{ key, high_resolution_clock::now() + 1s, EV_MOVE, 0 };
		add_timer(ev);
		break;
	}
	}
}
void ProcessPacket(int id, void *buff)
{
	char *packet = reinterpret_cast<char *>(buff);


	short x = clients[id]->x;
	short y = clients[id]->y;
	clients[id]->near_lock.lock();
	auto old_vl = clients[id]->near_id;
	clients[id]->near_lock.unlock();
	switch (packet[1])
	{
	case CS_MOVE:
	{
		switch (packet[2]) {
		case KEY_UP:
			if (y > 0) y--;
			break;
		case KEY_DOWN: if (y < WORLD_HEIGHT - 1) y++;
			break;
		case KEY_LEFT: if (x > 0) x--;
			break;
		case KEY_RIGHT: if (x < WORLD_WIDTH - 1) x++;
			break;
		default:
			cout << "Invalid Packet Type Error\n";
			while (true);
		}
		break;
	}
	case CS_HEAL:
	{
		HEAL_repeat(id);
		db.saveDB(clients[id]->name, clients[id]->x, clients[id]->y, clients[id]->level, clients[id]->exp, clients[id]->hp);
		break;
	}
	case CS_ATTACK:
	{
		Attack(id);
	}
	break;
	case CS_LOGIN:
	{
		cout << "LOGIN" << endl;

		char input_id[10];
		for (int i = 0; i < 10; i++)
		{
			input_id[i] = packet[i + 2];
		}
		int temp_id = atoi(input_id);
		LOGIN(id, temp_id);
		x = clients[id]->x;
		y = clients[id]->y;
		break;
	}
	case CS_STATE_CHANGE:
		//서버에서 레벨 바뀌거나 경험치 바뀔 때 

		break;
	default:
		break;
	}

	clients[id]->x = x;
	clients[id]->y = y;

	set <int> new_vl;
	for (auto &cl : clients) {
		int other = cl.second->id;
		if (id == other) continue;
		if (true == is_near_NPC(id, other)) {
			if (true == Is_NPC(other) && (false == Is_Active(other))) {
				clients[other]->is_active = true;
				Set_NPC_TimeEV(other, other);
			}
			if (true == is_near(id, other)) {
				new_vl.insert(other);
				if (true == Is_NPC(other)) {
					OVER_EX *over_ex = new OVER_EX;
					over_ex->event_type = EV_MOVE_TARGET;
					*(int *)(over_ex->net_buf) = id;
					PostQueuedCompletionStatus(g_iocp, 1, other, &over_ex->over);
				}
			}
		}
	}

	send_pos_packet(id, id);

	for (auto cl : old_vl) {
		if (0 != new_vl.count(cl)) {
			if (false == Is_NPC(cl))
				send_pos_packet(cl, id);
		}
		else
		{
			send_remove_player_packet(id, cl);
			if (false == Is_NPC(cl))
				send_remove_player_packet(cl, id);
		}
	}
	for (auto cl : new_vl) {
		if (0 == old_vl.count(cl)) {
			send_put_object_packet(id, cl);
			if (false == Is_NPC(cl))
				send_put_object_packet(cl, id);
		}
	}

}
void DIE_Check(int player_id)
{
	if (false == Is_NPC(player_id)) {
		if (clients[player_id]->hp <= 0)
		{
			cout << "DIE test " << endl;
			if (clients[player_id]->exp != 0) {
				clients[player_id]->exp /= 2;
			}
			clients[player_id]->x = INITPOS;
			clients[player_id]->y = INITPOS;
			clients[player_id]->hp = HP_MAX;
			send_state_change_packet(player_id, clients[player_id]->hp, clients[player_id]->level, clients[player_id]->exp);
		}

	}
}

void LEVEL_Up_Check(int player_id)
{
	if (false == Is_NPC(player_id))
	{
		if (clients[player_id]->exp >= (pow((double)2, (double)(clients[player_id]->level - 1)) * 100))
		{
			cout << "Level test " << endl;
			clients[player_id]->exp = 0;
			clients[player_id]->level += 1;

			send_state_change_packet(player_id, clients[player_id]->hp, clients[player_id]->level, clients[player_id]->exp);
		}
	}
}

void HEAL_repeat(int player_id)
{
	if (false == Is_NPC(player_id)) {
		sc_packet_stat_change packet;

		if (clients[player_id]->hp < HP_MAX && clients[player_id]->hp > 0)
		{
			clients[player_id]->hp += 10;


			if (clients[player_id]->hp > HP_MAX)
			{
				clients[player_id]->hp = 100;
			}
			send_state_change_packet(player_id, clients[player_id]->hp, clients[player_id]->level, clients[player_id]->exp);
		}
	}
}

void LOGIN(int key, int id)
{
	while (true)
	{
		bool result = db.ForLoginCheckID(id);
		if (result)
		{
			db.GetInitInfo(id, &clients[key]->x, &clients[key]->y, &clients[key]->level, &clients[key]->exp, &clients[key]->hp);
			send_login_ok_packet(key);
			send_state_change_packet(key, clients[key]->hp, clients[key]->level, clients[key]->exp);

			clients[key]->name = id;

			break;
		}
		else {
			send_login_fail_packet(key);
		}
	}
}

void do_random_move(int npc_id)
{
	if (0 == clients.count(npc_id)) {
		cout << "NPC: " << npc_id << " Does not EXIST!\n";
		while (true);
	}

	if (false == Is_NPC(npc_id)) {
		cout << "ID :" << npc_id << " is not NPC!!\n";
		while (true);
	}

	// 플레이어가 주변에 있는지 확인하고 있으면 true
	bool player_exists = false;
	for (int i = 0; i < NPC_ID_START; i++) {
		if (0 == clients.count(i)) continue;
		if (true == is_near_NPC(i, npc_id)) {
			player_exists = true;
			break;
		}
	}
	// 플레이어가 주변에 없는거니까 active를 끄고 move를 안시킴 
	if (false == player_exists) {
		clients[npc_id]->is_active = false;
		return;
	}

	SOCKETINFO *npc = clients[npc_id];
	int x = npc->x;
	int y = npc->y;
	set <int> old_view_list;

	// 현재 상황 (이동 전 상황에서) 주변에 clients가 몇이나 있는지 보고 view list에 넣어놓음
	for (auto &obj : clients) {
		if (true == is_near(npc->id, obj.second->id))
			old_view_list.insert(obj.second->id);
	}

	// 이동
	switch (rand() % 4) {
	case 0: if (y > 0) y--; break;
	case 1: if (y < (WORLD_HEIGHT - 1)) y++; break;
	case 2: if (x > 0) x--; break;
	case 3: if (x < (WORLD_WIDTH - 1)) x++; break;
	}

	npc->x = x;
	npc->y = y;

	// 이동 후 새로운 리스트로 주위에 얼마나 있는지 확인하고 new_view_list에 넣음
	set <int> new_view_list;
	for (auto &obj : clients) {
		if (true == is_near(npc->id, obj.second->id))
			new_view_list.insert(obj.second->id);
	}

	// 그리고 npc id가 아닌 플레이어이고 주변에 있는 player들에게 packet을 보낸다. 
	for (auto &pc : clients) {
		if (true == Is_NPC(pc.second->id)) continue;
		if (false == is_near(pc.second->id, npc->id)) continue;
		send_pos_packet(pc.second->id, npc->id);
	}

	// 그리고 MOVE상태로 
	EVENT new_ev{ npc_id, high_resolution_clock::now() + 1s, EV_MOVE, 0 };
	add_timer(new_ev);

}

void do_worker()
{
	while (true) {
		DWORD num_byte;
		ULONGLONG key64;
		PULONG_PTR p_key = &key64;
		WSAOVERLAPPED *p_over;

		GetQueuedCompletionStatus(g_iocp, &num_byte, p_key, &p_over, INFINITE);
		unsigned int key = static_cast<unsigned>(key64);
		SOCKET client_s = clients[key]->socket;
		if (num_byte == 0) {
			closesocket(client_s);
			clients.erase(key);
			for (auto &cl : clients) {
				if (false == Is_NPC(cl.first))
					send_remove_player_packet(cl.first, key);
			}
			continue;
		}
		OVER_EX *over_ex = reinterpret_cast<OVER_EX *> (p_over);

		if (EV_RECV == over_ex->event_type) {

			ProcessPacket(key, over_ex->net_buf);

			DWORD flags = 0;
			memset(&over_ex->over, 0x00, sizeof(WSAOVERLAPPED));
			WSARecv(client_s, over_ex->wsabuf, 1, 0, &flags, &over_ex->over, 0);
		}
		else if (EV_SEND == over_ex->event_type) {
			delete over_ex;
		}
		else if (EV_MOVE == over_ex->event_type) {
			if (clients[key]->m_type == MST_LOAMING ||
				clients[key]->m_type == MST_WAR)
			{
				do_random_move(key);
			}
			//else {
			//	int player_id = *(int *)(over_ex->net_buf);
			//	do_monster_run_move(key, player_id, over_ex->repeat);
			//}
			delete over_ex;
		}
		else if (EV_IDLE == over_ex->event_type)
		{
			//cout << key << " : IDLE" << endl;
			delete over_ex;
		}
		else if (EV_HEAL == over_ex->event_type)
		{

		}
		else if (EV_MOVE_TARGET == over_ex->event_type)
		{
			int player_id = *(int *)(over_ex->net_buf);

			//lua_State *L = clients[key]->L;

			//lua_getglobal(L, "player_Conflict");	// 이 함수 호출
			//lua_pushnumber(L, player_id);
			//lua_pushnumber(L, clients[player_id]->x);
			//lua_pushnumber(L, clients[player_id]->y);
			//lua_pcall(L, 3, 1, 0);

			//int result = (int)lua_tonumber(L, -1);
			//lua_pop(L, 1);

			//if (result == 2)
			//{
			//	clients[player_id]->hp -= 3 * clients[key]->level;
			//	send_state_change_packet(player_id, clients[player_id]->hp, clients[player_id]->level, clients[player_id]->exp);
			//	DIE_Check(player_id);

			//}

			if (clients[player_id]->x == clients[key]->x
				&& clients[player_id]->y == clients[key]->y)
			{
				cout << "충돌" << endl;

				clients[player_id]->hp -= 3;
				send_state_change_packet(player_id, clients[player_id]->hp, clients[player_id]->level, clients[player_id]->exp);
				DIE_Check(player_id);
			}
		}
		else if (EV_DIE == over_ex->event_type)
		{
			// 죽으면 30초 후에 부활


			cout << " DIE 에 들어왔어요 " << endl;

			EVENT new_ev{ key, high_resolution_clock::now() + 5s, EV_DIE, 0, false };
			add_timer(new_ev);
		}
		else {
			cout << "Unknown Event Type :" << over_ex->event_type << endl;
			while (true);
		}
	}
}

int lua_get_x_position(lua_State *L)
{
	int npc_id = (int)lua_tonumber(L, -1);
	lua_pop(L, 2);
	int x = clients[npc_id]->x;
	lua_pushnumber(L, x);
	return 1;
}

int lua_get_y_position(lua_State *L)
{
	int npc_id = (int)lua_tonumber(L, -1);
	lua_pop(L, 2);
	int y = clients[npc_id]->y;
	lua_pushnumber(L, y);
	return 1;
}

int lua_send_chat_packet(lua_State *L)
{
	int player_id = (int)lua_tonumber(L, -3);
	int chatter_id = (int)lua_tonumber(L, -2);
	char *mess = (char *)lua_tostring(L, -1);
	lua_pop(L, 4);

	send_chat_player_packet(player_id, chatter_id, mess);	//
	return 0;
}

int lua_setting_init_npc(lua_State *L)
{
	int npc_id = (int)lua_tonumber(L, -9);
	clients[npc_id]->x = (int)lua_tonumber(L, -8);
	clients[npc_id]->y = (int)lua_tonumber(L, -7);
	clients[npc_id]->m_type = (int)lua_tonumber(L, -6);
	clients[npc_id]->level = (int)lua_tonumber(L, -5);
	clients[npc_id]->hp = (int)lua_tonumber(L, -4);
	clients[npc_id]->exp = (int)lua_tonumber(L, -3);
	clients[npc_id]->is_active = (int)lua_tonumber(L, -2);
	clients[npc_id]->socket = (int)lua_tonumber(L, -1);
	lua_pop(L, 10);

	return 1;
}

void lua_error(lua_State *L, const char *fmt, ...) {
	va_list argp;
	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);
	lua_close(L);
	exit(EXIT_FAILURE);
}
void Create_NPC()
{
	cout << "Initializing NPC \n";
	for (int npc_id = NPC_ID_START; npc_id < NPC_ID_START + NUM_NPC; ++npc_id) {

		clients[npc_id] = new SOCKETINFO;
		clients[npc_id]->id = npc_id;
		lua_State *L = luaL_newstate();
		clients[npc_id]->L = L;
		luaL_openlibs(L);
		luaL_loadfile(L, "monster.lua");
		lua_pcall(L, 0, 0, 0);
		lua_getglobal(L, "set_npc_id");
		lua_pushnumber(L, npc_id);
		lua_pcall(L, 1, 0, 0);		// 가상 머신 초기화
		lua_register(L, "API_setting_Init_NPC", lua_setting_init_npc);

		lua_getglobal(L, "init_set_npc_infor");	// 이 함수 호출
		lua_pushnumber(L, npc_id);
		lua_pcall(L, 1, 0, 0);

		lua_register(L, "API_get_x_position", lua_get_x_position);
		lua_register(L, "API_get_y_position", lua_get_y_position);
		lua_register(L, "API_send_chat_packet", lua_send_chat_packet);

	}
	cout << "NPC initializaion finished. \n";
}

void do_ai()
{
	int count = 0;
	while (true) {
		auto ai_start = high_resolution_clock::now();
		//cout << "AI Loop %d" << count++ << endl;
		for (auto &npc : clients) {
			if (false == Is_NPC(npc.second->id)) continue;

			bool player_exists = false;
			for (int i = 0; i < NPC_ID_START; i++) {
				if (0 == clients.count(i)) continue;
				if (true == is_near_NPC(i, npc.second->id)) {
					player_exists = true;
					break;
				}
			}

			if (false == player_exists) continue;

			int x = npc.second->x;
			int y = npc.second->y;
			set <int> old_view_list;
			for (auto &obj : clients) {
				if (true == is_near(npc.second->id, obj.second->id))
					old_view_list.insert(obj.second->id);
			}
			switch (rand() % 4) {
			case 0: if (y > 0) y--; break;
			case 1: if (y < (WORLD_HEIGHT - 1)) y++; break;
			case 2: if (x > 0) x--; break;
			case 3: if (x < (WORLD_WIDTH - 1)) x++; break;
			}
			npc.second->x = x;
			npc.second->y = y;
			set <int> new_view_list;
			for (auto &obj : clients) {
				if (true == is_near(npc.second->id, obj.second->id))
					new_view_list.insert(obj.second->id);
			}
			for (auto &pc : clients) {
				if (true == Is_NPC(pc.second->id)) continue;
				if (false == is_near(pc.second->id, npc.second->id)) continue;
				send_pos_packet(pc.second->id, npc.second->id);
			}
		}
		auto ai_end = high_resolution_clock::now();
		this_thread::sleep_for(1s - (ai_end - ai_start));
	}
}

void do_timer()
{
	while (true) {
		timer_lock.lock();
		while (true == timer_queue.empty()) {
			timer_lock.unlock();
			this_thread::sleep_for(10ms);
			timer_lock.lock();
		}
		const EVENT &ev = timer_queue.top();

		if (ev.wakeup_time > high_resolution_clock::now()) {
			timer_lock.unlock();
			this_thread::sleep_for(10ms);
			continue;
		}
		EVENT p_ev = ev;
		timer_queue.pop();
		timer_lock.unlock();

		if (!p_ev.stop) {
			Set_NPC_StandEV(p_ev.obj_id, p_ev.obj_id);
		}
		else {
			cout << "Stop" << endl;
		}
		//for (auto &obj : clients)
		//{
		//	DIE_Check(obj.second->id);
		//	LEVEL_Up_Check(obj.second->id);
		//}
	}
}

int main()
{
	wcout.imbue(std::locale("korean"));

	srand((unsigned)time(NULL));
	Create_NPC();

	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	::bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
	listen(listenSocket, 5);
	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(SOCKADDR_IN);
	memset(&clientAddr, 0, addrLen);
	SOCKET clientSocket;
	DWORD flags;

	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	thread worker_thread{ do_worker };
	thread worker_thread2{ do_worker };
	thread worker_thread3{ do_worker };
	//	thread ai_thread{ do_ai };
	thread timer_thread{ do_timer };

	while (true) {
		clientSocket = accept(listenSocket, (struct sockaddr *)&clientAddr, &addrLen);
		int user_id = new_user_id++;
		clients[user_id] = new SOCKETINFO;
		//memset(clients[user_id], 0, sizeof(SOCKETINFO));
		clients[user_id]->id = user_id;
		clients[user_id]->socket = clientSocket;
		clients[user_id]->recv_over.wsabuf[0].len = MAX_BUFFER;
		clients[user_id]->recv_over.wsabuf[0].buf = clients[user_id]->recv_over.net_buf;
		clients[user_id]->recv_over.event_type = EV_RECV;
		//clients[user_id]->recv_over.event_type = EV_RECV;
		flags = 0;
		CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), g_iocp, user_id, 0);


		OVER_EX  *over_ex = new OVER_EX;
		over_ex->event_type = EV_HEAL;
		*(int *)(over_ex->net_buf) = user_id;
		PostQueuedCompletionStatus(g_iocp, 1, user_id, &over_ex->over);

		//clients[user_id]->x = 4;
		//clients[user_id]->y = 4;
		for (auto &cl : clients) {
			int other_player = cl.first;
			if ((true == Is_NPC(other_player))
				&& (false == Is_Active(other_player))
				&& (true == is_near_NPC(other_player, user_id))) {
				clients[other_player]->is_active = true;
				//	EVENT ev{ other_player, high_resolution_clock::now() + 1s, EV_MOVE, 0 };
				EVENT ev{ other_player, high_resolution_clock::now() + 1s, EV_IDLE, 0 };
				add_timer(ev);
			}
			if (true == is_near(other_player, user_id)) {
				if (false == Is_NPC(other_player))
					send_put_object_packet(other_player, user_id);
				if (other_player != user_id) {
					send_put_object_packet(user_id, other_player);
				}
			}
		}
		memset(&clients[user_id]->recv_over.over, 0, sizeof(clients[user_id]->recv_over.over));
		int ret = WSARecv(clientSocket, clients[user_id]->recv_over.wsabuf, 1, NULL,
			&flags, &(clients[user_id]->recv_over.over), NULL);
		if (0 != ret) {
			int err_no = WSAGetLastError();
			if (WSA_IO_PENDING != err_no)
				error_display("WSARecv Error :", err_no);
		}
	}
	worker_thread.join();
	//	ai_thread.join();
	timer_thread.join();
	closesocket(listenSocket);
	WSACleanup();
}

