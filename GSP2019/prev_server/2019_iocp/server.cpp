#include <iostream>
#include <map>
#include <thread>
#include <set>
#include <mutex>
#include <chrono>
#include <queue>
#include <sqlext.h>  

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

#define MAX_BUFFER        1024
constexpr auto VIEW_RANGE = 3;
constexpr auto NAME_LEN = 11;

enum EVENT_TYPE { EV_RECV, EV_SEND, EV_MOVE, EV_PLAYER_MOVE_NOTIFY, EV_PLAYER_BYE_NOTIFY, EV_MOVE_TARGET, EV_ATTACK, EV_HEAL, EV_RUN, EV_IDLE };

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

	int level;
	int hp;
	int exp;
};

struct EVENT {
	int obj_id;
	high_resolution_clock::time_point wakeup_time;
	int event_type;
	int target_obj;

	int repeat;
	constexpr bool operator < (const EVENT& left) const
	{
		return wakeup_time > left.wakeup_time;
	}
};

class Database
{
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;
	SQLINTEGER nID, nX, nY;
	SQLWCHAR szName[NAME_LEN];
	SQLLEN cbName = 0, cbID = 0, cbX = 0, cbY = 0;

	bool id_find = false;


public:
	Database()
	{
		setlocale(LC_ALL, "korean");
	}
	~Database()
	{
		SQLDisconnect(hdbc);
		SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}

	void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
	{
		SQLSMALLINT iRec = 0;  SQLINTEGER	iError;
		WCHAR	wszMessage[1000];
		WCHAR	wszState[SQL_SQLSTATE_SIZE + 1];

		if (RetCode == SQL_INVALID_HANDLE) {
			fwprintf(stderr, L"Invalid handle!\n");  return;
		}
		while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage, (SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT *)NULL) == SQL_SUCCESS) {
			// Hide data truncated..
			if (wcsncmp(wszState, L"01004", 5)) {
				fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
			}
		}
	}

	void saveDB_Player(const string& id, short x, short y, char lv, short hp, short exp)
	{
		//statement 처리에 대 한 상태 정보와 같은 statement 정보에 대한 액세스를 제공 합니다.
		retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

		//SQL 명령어 전송 받는건 다른명령어 사용해서. --> SQL PATCH써서. 그냥 패치 쓰면 실행결과 하나씩 꺼내고 끝임. 허공중에 꺼내서 버림. 
		//얘는 오버랩드마냥 hstmt는 스레드마다 별도로 가지고있어야함. 
		cout << "디비에 저장하는 값 x : " << x << " y: " << y << endl;
		SQLLEN posX = x, posY = y;
		//cout << "디비에 저장하는 값 x : " << posX << " y: " << y << endl;

		wchar_t text[100];
		TCHAR* wchar_id = (TCHAR*)id.c_str();
		// 과제 5 TODO
		// 1. 여기서 데이터를 입력받아 업데이트를 시킨다. 
		wsprintf(text, L"EXEC SetPos %S, %d, %d, %d, %d, %d", wchar_id, x, y, lv, hp, exp);

		retcode = SQLExecDirect(hstmt, (SQLWCHAR*)text, SQL_NTS);

		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
		{
			//꺼낸 데이터를 어디다가 연결을 해줘라!허공에 뿌리지마라
			// Fetch and print each row of data. On an error, display a message and exit.  
			//셀렉트 시 날아오는 데이터 수 알 수 없음. 루프돌면서하나하나 받아야함. 패치하는데 석세스가 아닐 때까지.
			// LI거나 데이터가 없거나. LI면 에러난다. 에러아니면 석세스면 읽어서 화면에 출력 아니면 다읽었다하고 빠져나감. 
			cout << "업데이트 내역: " << endl;
			cout << szName << "디비업뎃 x: " << x << ", " << y << endl;

			if (retcode == SQL_ERROR)
			{
				cout << "업데이트 오류: " << endl;

				HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
			}
		}
		else
		{
			//cout << "업데이트중 데이터 없음: " << endl;
			HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);

		}
		// Process data  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			SQLCancel(hstmt);
			SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
		}
		cout << "업데이트 완료!" << endl;

	}
	void saveDB_Monster(const string& id, short x, short y, char lv, short hp, short exp)
	{
		//statement 처리에 대 한 상태 정보와 같은 statement 정보에 대한 액세스를 제공 합니다.
		retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

		//SQL 명령어 전송 받는건 다른명령어 사용해서. --> SQL PATCH써서. 그냥 패치 쓰면 실행결과 하나씩 꺼내고 끝임. 허공중에 꺼내서 버림. 
		//얘는 오버랩드마냥 hstmt는 스레드마다 별도로 가지고있어야함. 
		cout << "디비에 저장하는 값 x : " << x << " y: " << y << endl;
		SQLLEN posX = x, posY = y;
		//cout << "디비에 저장하는 값 x : " << posX << " y: " << y << endl;

		wchar_t text[100];
		TCHAR* wchar_id = (TCHAR*)id.c_str();
		// 과제 5 TODO
		// 1. 여기서 데이터를 입력받아 업데이트를 시킨다. 
		wsprintf(text, L"EXEC SetPos %S, %d, %d, %d, %d, %d", wchar_id, x, y, lv, hp, exp);

		retcode = SQLExecDirect(hstmt, (SQLWCHAR*)text, SQL_NTS);

		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
		{
			//꺼낸 데이터를 어디다가 연결을 해줘라!허공에 뿌리지마라
			// Fetch and print each row of data. On an error, display a message and exit.  
			//셀렉트 시 날아오는 데이터 수 알 수 없음. 루프돌면서하나하나 받아야함. 패치하는데 석세스가 아닐 때까지.
			// LI거나 데이터가 없거나. LI면 에러난다. 에러아니면 석세스면 읽어서 화면에 출력 아니면 다읽었다하고 빠져나감. 
			cout << "업데이트 내역: " << endl;
			cout << szName << "디비업뎃 x: " << x << ", " << y << endl;

			if (retcode == SQL_ERROR)
			{
				cout << "업데이트 오류: " << endl;

				HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
			}
		}
		else
		{
			//cout << "업데이트중 데이터 없음: " << endl;
			HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);

		}
		// Process data  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			SQLCancel(hstmt);
			SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
		}
		cout << "업데이트 완료!" << endl;

	}
};

priority_queue <EVENT> timer_queue;
mutex  timer_lock;
Database db;
map <int, SOCKETINFO *> clients;
HANDLE	g_iocp;

int new_user_id = 0;

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

void send_put_player_packet(int client, int new_id)
{
	sc_packet_put_object packet;
	packet.id = new_id;
	packet.size = sizeof(packet);
	packet.type = SC_PUT_OBJECT;
	packet.x = clients[new_id]->x;
	packet.y = clients[new_id]->y;
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
		send_put_player_packet(client, mover);
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

bool is_near_id(int player, int other)
{
	lock_guard <mutex> gl{ clients[player]->near_lock };
	return (0 != clients[player]->near_id.count(other));
}

void ProcessPacket(int id, void *buff)
{
	char *packet = reinterpret_cast<char *>(buff);
	short x = clients[id]->x;
	short y = clients[id]->y;
	clients[id]->near_lock.lock();
	auto old_vl = clients[id]->near_id;
	clients[id]->near_lock.unlock();
	switch (packet[1]) {
	case KEY_UP: if (y > 0) y--;
		break;
	case KEY_DOWN: if (y < WORLD_HEIGHT - 1) y++;
		break;
	case KEY_LEFT: if (x > 0) x--;
		break;
	case KEY_RIGHT: if (x < WORLD_WIDTH - 1) x++;
		break;
	default: cout << "Invalid Packet Type Error\n";
		while (true);
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
				//EVENT ev{ other, high_resolution_clock::now() + 1s, EV_MOVE, 0 };
				EVENT ev{ other, high_resolution_clock::now() + 1s, EV_IDLE, 0 };
				add_timer(ev);
			}
			if (true == is_near(id, other)) {
				new_vl.insert(other);
				if (true == Is_NPC(other)) {
					OVER_EX  *over_ex = new OVER_EX;
					over_ex->event_type = EV_PLAYER_MOVE_NOTIFY;
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
			send_put_player_packet(id, cl);
			if (false == Is_NPC(cl))
				send_put_player_packet(cl, id);
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
	EVENT new_ev{ npc_id, high_resolution_clock::now() + 1s, EV_MOVE, 0 , 1 };
	add_timer(new_ev);

}


void do_monster_run_move(int npc_id, int player_id, int remain_repeat)
{

	SOCKETINFO *npc = clients[npc_id];
	int x = npc->x;
	int y = npc->y;

	switch (rand() % 4) {
	case 0: if (y > 0) y--; break;
	case 1: if (y < (WORLD_HEIGHT - 1)) y++; break;
	case 2: if (x > 0) x--; break;
	case 3: if (x < (WORLD_WIDTH - 1)) x++; break;
	}

	cout << npc_id << "번째가 x로는 " << npc->x << "에서 " << x << "로 y로는 " << npc->y << "에서 " << y << "로 이동" << endl;

	npc->x = x;
	npc->y = y;

	for (auto &pc : clients) {
		if (true == Is_NPC(pc.second->id)) continue;
		if (false == is_near(pc.second->id, npc->id)) continue;
		send_pos_packet(pc.second->id, npc->id);
	}
	cout << "남은 횟수 " << remain_repeat << endl;

	if (remain_repeat == 0)
	{
		cout << "아니 bye하라고 " << endl;
		OVER_EX  *over_ex = new OVER_EX;
		over_ex->event_type = EV_PLAYER_BYE_NOTIFY;
		*(int *)(over_ex->net_buf) = player_id;
		PostQueuedCompletionStatus(g_iocp, 1, npc_id, &over_ex->over);
		//EVENT new_ev{ npc_id, high_resolution_clock::now() + 1s, EV_PLAYER_BYE_NOTIFY, 0 };
		//add_timer(new_ev);
	}
	else
	{
		EVENT new_ev{ npc_id, high_resolution_clock::now() + 1s, EV_MOVE, 0 ,remain_repeat };
		add_timer(new_ev);
	}


	//EVENT new_ev{ npc_id, high_resolution_clock::now() + 1s, EV_MOVE, 0 ,remain_repeat };
	//add_timer(new_ev);
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
			int player_id = *(int *)(over_ex->net_buf);
			do_monster_run_move(key, player_id, over_ex->repeat);

			delete over_ex;
		}
		else if (EV_IDLE == over_ex->event_type)
		{
			cout << key << " : IDLE" << endl;
		}
		else if (EV_PLAYER_MOVE_NOTIFY == over_ex->event_type) {
			int player_id = *(int *)(over_ex->net_buf);
			//	cout << "MOVE_EVENT from :" << player_id << " TO :" << key << endl;
			lua_State *L = clients[key]->L;

			// 주위에 있는 애들 모두 다 이리로 들어와서 notify 호출하고 
			// 만약에 x== y== 이면! 그 때 hello 호출하는거임 

			lua_getglobal(L, "event_player_move_notify");	// 이 함수 호출
			lua_pushnumber(L, player_id);
			lua_pushnumber(L, clients[player_id]->x);
			lua_pushnumber(L, clients[player_id]->y);
			lua_pcall(L, 3, 1, 0);

			int result = (int)lua_tonumber(L, -1);
			lua_pop(L, 1);

			if (result == 2)
			{
				cout << "result 2에서 monster_run_move \n";
				//do_monster_run_move(key);

				OVER_EX  *over_ex = new OVER_EX;
				over_ex->event_type = EV_MOVE;
				over_ex->repeat = 2;
				*(int *)(over_ex->net_buf) = player_id;
				PostQueuedCompletionStatus(g_iocp, 1, key, &over_ex->over);
			}
		}
		else if (EV_PLAYER_BYE_NOTIFY == over_ex->event_type) {

			int player_id = *(int *)(over_ex->net_buf);
			lua_State *L = clients[key]->L;

			cout << "BYE" << endl;
			lua_getglobal(L, "event_monster_bye_notify");	// 이 함수 호출
			lua_pushnumber(L, player_id);
			lua_pcall(L, 1, 0, 0);
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
	cout << "CHAT : from = " << chatter_id << ", to= " << player_id << " , [ " << mess << "]\n";
	send_chat_player_packet(player_id, chatter_id, mess);	//
	return 0;
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
		clients[npc_id]->x = rand() % WORLD_WIDTH;
		clients[npc_id]->y = rand() % WORLD_HEIGHT;
		clients[npc_id]->socket = -1;
		clients[npc_id]->is_active = false;

		lua_State *L = luaL_newstate();
		clients[npc_id]->L = L;
		luaL_openlibs(L);
		luaL_loadfile(L, "monster.lua");
		lua_pcall(L, 0, 0, 0);
		lua_getglobal(L, "set_npc_id");
		lua_pushnumber(L, npc_id);
		lua_pcall(L, 1, 0, 0);		// 가상 머신 초기화
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
		cout << "AI Loop %d" << count++ << endl;
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
		//const EVENT &ev = timer_queue.top().first;
		//const EVENT_TYPE et = timer_queue.top().second;
		const EVENT &ev = timer_queue.top();

		if (ev.wakeup_time > high_resolution_clock::now()) {
			timer_lock.unlock();
			this_thread::sleep_for(10ms);
			continue;
		}
		EVENT p_ev = ev;

		//	EVENT_TYPE p_et = et;
		timer_queue.pop();
		timer_lock.unlock();

		if (p_ev.repeat > 0) {
			cout << "여긴 안들어와?" << endl;
			OVER_EX *over_ex = new OVER_EX;
			switch (ev.event_type)
			{
			case EV_IDLE:
				over_ex->event_type = EV_IDLE;
				break;
			case EV_MOVE:
				over_ex->event_type = EV_MOVE;
				break;
			case EV_PLAYER_BYE_NOTIFY:
				over_ex->event_type = EV_PLAYER_BYE_NOTIFY;
				break;
			default:
				cout << "Error~" << endl;
				break;
			}	
			p_ev.repeat--;
			over_ex->repeat = p_ev.repeat;
			PostQueuedCompletionStatus(g_iocp, 1, p_ev.obj_id, &over_ex->over);
		}
		else
		{
			OVER_EX *over_ex = new OVER_EX;
			over_ex->event_type = EV_IDLE;
			PostQueuedCompletionStatus(g_iocp, 1, p_ev.obj_id, &over_ex->over);
		}
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
		flags = 0;
		CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), g_iocp, user_id, 0);

		send_login_ok_packet(user_id);
		clients[user_id]->x = 4;
		clients[user_id]->y = 4;
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
					send_put_player_packet(other_player, user_id);
				if (other_player != user_id) {
					send_put_player_packet(user_id, other_player);
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

