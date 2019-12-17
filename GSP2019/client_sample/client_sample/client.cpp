#define SFML_STATIC 1
#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <unordered_map>
#include <chrono>


using namespace std;
using namespace chrono;

#ifdef _DEBUG
#pragma comment (lib, "lib/sfml-graphics-s-d.lib")
#pragma comment (lib, "lib/sfml-window-s-d.lib")
#pragma comment (lib, "lib/sfml-system-s-d.lib")
#pragma comment (lib, "lib/sfml-network-s-d.lib")
#else
#pragma comment (lib, "lib/sfml-graphics-s.lib")
#pragma comment (lib, "lib/sfml-window-s.lib")
#pragma comment (lib, "lib/sfml-system-s.lib")
#pragma comment (lib, "lib/sfml-network-s.lib")
#endif
#pragma comment (lib, "freetype.lib")
#pragma comment (lib, "opengl32.lib")
#pragma comment (lib, "winmm.lib")
#pragma comment (lib, "ws2_32.lib")

#include "..\..\2019_IOCP\2019_IOCP\protocol.h"

sf::TcpSocket socket;

constexpr auto SCREEN_WIDTH = 9;
constexpr auto SCREEN_HEIGHT = 9;

constexpr auto TILE_WIDTH = 65;
constexpr auto WINDOW_WIDTH = TILE_WIDTH * SCREEN_WIDTH + 10;   // size of window
constexpr auto WINDOW_HEIGHT = TILE_WIDTH * SCREEN_WIDTH + 10;
constexpr auto BUF_SIZE = 200;
constexpr auto MAX_USER = 10;

void send_heal_timer_packet(int inc);
void send_state_change_packet(int p_type, int hp, int exp, int level);
// 이거 UI에 띄워야될 것들.
// 서버에서 받아와서 client main에서 글씨로 띄울거임
class player {
	int hp;
	int level;
	int exp;
public:
	player() {}
	~player() {}

	void HpSet(int _hp)
	{
		hp = _hp;
	}

	int HpGet()
	{
		return hp;
	}

	void LevelSet(int _level)
	{
		level = _level;
	}
	int LevelGet()
	{
		return level;
	}
	void ExpSet(int _exp)
	{
		exp = _exp;
	}
	int ExpGet()
	{
		return exp;
	}
};


int g_left_x;
int g_top_y;
int g_myid;
player p;

char input_id[10];
bool login_ok;

sf::RenderWindow *g_window;
sf::Font g_font;
sf::Font pixel_font;

//sf::Text NOTICE;

class OBJECT {
private:
	bool m_showing;
	sf::Sprite m_sprite;
	char m_mess[MAX_STR_LEN];
	high_resolution_clock::time_point m_time_out;
	sf::Text m_text;
	sf::Text pos_text;
	sf::Text TYPE;

	high_resolution_clock::time_point die_time;

public:
	int m_x, m_y;
	bool die_check;
	OBJECT(sf::Texture &t, int x, int y, int x2, int y2) {
		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		m_time_out = high_resolution_clock::now();
		m_text.setFillColor(sf::Color::Black);
		TYPE.setFont(g_font);
	}
	OBJECT() {

		TYPE.setFont(g_font);
		m_showing = false;
		m_time_out = high_resolution_clock::now();
	}
	void show()
	{
		m_showing = true;
	}
	void hide()
	{
		m_showing = false;
	}
	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}
	void a_draw() {
		g_window->draw(m_sprite);
	}
	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}
	void draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_left_x) * 65.0f + 8;
		float ry = (m_y - g_top_y) * 65.0f + 8;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);

		if (high_resolution_clock::now() < m_time_out) {
			m_text.setPosition(rx - 5, ry - 10);
			g_window->draw(m_text);
		}
		else {
			TYPE.setPosition(rx - 20, ry - 10);
			TYPE.setScale(0.6f, 0.7f);
			TYPE.setFillColor(sf::Color::Blue);
			g_window->draw(TYPE);
		}
	}
	void add_chat(char chat[]) {
		m_text.setFont(g_font);
		m_text.setString(chat);
		m_time_out = high_resolution_clock::now() + 1s;
	}
	void set_IdType(int my_level, int my_hp)
	{
		float rx = (m_x - g_left_x) * 65.0f + 8;
		float ry = (m_y - g_top_y) * 65.0f + 8;
		char text[20];
		sprintf_s(text, "Lv:%d Hp:%d", my_level, my_hp);
		TYPE.setString(text);
		//g_window->draw(TYPE);
	}
	void object_die()
	{
		die_time = high_resolution_clock::now();
		die_check = true;
	}
	bool die_end_check()
	{
		if (die_check)
		{
			if (die_time + 30s < high_resolution_clock::now())
			{
				die_check = false;
				return true;
			}
			return false;
		}
	}
};
class STATE
{
	high_resolution_clock::time_point m_time_out;
	high_resolution_clock::time_point re_vive;
public:
	STATE() {
		m_time_out = high_resolution_clock::now() + 5s;
	}
	~STATE() {

	}

	void HEAL_REPEAT()
	{
		if (high_resolution_clock::now() > m_time_out) {
			cout << "5초 timer" << endl;
			m_time_out = high_resolution_clock::now() + 5s;
			// 그리고 heal하라고 보내기 
			send_heal_timer_packet(10);
		}
	}
};

class NOTICE {
	bool showing;
	sf::Text notice;
	high_resolution_clock::time_point m_time_out;
public:
	NOTICE() {
		notice.setFillColor(sf::Color::Magenta);
		notice.setPosition(100, 100);
		notice.setScale(0.7f, 0.7f);
		m_time_out = high_resolution_clock::now();
	}
	~NOTICE()
	{

	}
	void draw() {
		if (high_resolution_clock::now() < m_time_out && showing) {
			g_window->draw(notice);
		}
		else {
			showing = false;
		}
	}
	void add_chat(char chat[]) {
		notice.setFont(pixel_font);
		notice.setString(chat);
		m_time_out = high_resolution_clock::now() + 2s;
		showing = true;
	}
};
OBJECT avatar;
OBJECT players[MAX_USER];
STATE status;
NOTICE no;
//OBJECT npcs[NUM_NPC];
unordered_map <int, OBJECT> npcs;


OBJECT ground_grass;
sf::Texture *board;
sf::Texture *pieces;
sf::Texture* ground;
sf::Texture* enemy;


void client_initialize()
{
	board = new sf::Texture;
	pieces = new sf::Texture;
	ground = new sf::Texture;
	enemy = new sf::Texture;
	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		while (true);
	}
	if (false == pixel_font.loadFromFile("Pixel.ttf")) {
		cout << "Font Loading Error!\n";
		while (true);
	}
	board->loadFromFile("chessmap.bmp");
	pieces->loadFromFile("player_motion.png");
	ground->loadFromFile("ground.png");
	enemy->loadFromFile("enemy.png");

	ground_grass = OBJECT{ *ground, 0,0,TILE_WIDTH - 1, TILE_WIDTH - 1 };


	avatar = OBJECT{ *pieces, 0, 0, 65, 65 };
	avatar.move(10, 10);
	for (auto &pl : players) {
		pl = OBJECT{ *enemy, 0, 0, 65, 65 };
	}
}

void client_finish()
{
	delete board;
	delete pieces;
	delete ground;
	delete enemy;
}


void ProcessPacket(char *ptr)
{
	static bool first_time = true;
	switch (ptr[1])
	{
	case SC_LOGIN_OK:
	{
		cout << "login ok " << endl;
		sc_packet_login_ok *packet = reinterpret_cast<sc_packet_login_ok *>(ptr);
		g_myid = packet->id;
	}

	case SC_PUT_OBJECT:
	{
		sc_packet_put_object *my_packet = reinterpret_cast<sc_packet_put_object *>(ptr);
		int id = my_packet->id;
		int type = my_packet->npc_type;
		int hp = my_packet->hp;
		//cout << hp << endl;
		if (id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - 4;
			g_top_y = my_packet->y - 4;
			avatar.show();
		}
		else if (id < MAX_USER) {
			players[id].move(my_packet->x, my_packet->y);
			players[id].show();
		}
		else {

			switch (type)
			{
			case MST_PIECE:
				npcs[id] = OBJECT{ *enemy, 0, 0, 65, 65 };
				npcs[id].move(my_packet->x, my_packet->y);
				npcs[id].show();
				break;
			case MST_WAR:
				npcs[id] = OBJECT{ *enemy, 65, 0, 65, 65 };
				npcs[id].move(my_packet->x, my_packet->y);
				npcs[id].show();
				break;
			case MST_LOAMING:
				npcs[id] = OBJECT{ *enemy, 130, 0, 65, 65 };
				npcs[id].move(my_packet->x, my_packet->y);
				npcs[id].show();
				break;
			case MST_STOP:
				npcs[id] = OBJECT{ *enemy, 195, 0, 65, 65 };
				npcs[id].move(my_packet->x, my_packet->y);
				npcs[id].show();
				break;
			default:
				//cout << "Monster Type Set Error !" << endl;
				//npcs[id] = OBJECT{ *enemy, 0, 0, 65, 65 };
				//npcs[id].move(my_packet->x, my_packet->y);
				//npcs[id].show();
				break;
			}
			npcs[id].set_IdType(my_packet->level, my_packet->hp);

			//else {
			//	if (npcs[id].die_end_check())
			//	{
			//		// 이게 true면 패킷 보내기! 
			//		// hp 회복 시켜서! 
			//		hp = 100 + ((my_packet->level * 100) / 2);
			//		send_state_change_packet(type, hp, my_packet->exp, my_packet->level);
			//	}
			//}
		}
		break;
	}
	case SC_POS:
	{
		sc_packet_pos *my_packet = reinterpret_cast<sc_packet_pos *>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - 4;
			g_top_y = my_packet->y - 4;
		}
		else if (other_id < MAX_USER) {
			players[other_id].move(my_packet->x, my_packet->y);
		}
		else {
			if (0 != npcs.count(other_id))
				npcs[other_id].move(my_packet->x, my_packet->y);
		}

		break;
	}

	case SC_REMOVE_OBJECT:
	{
		sc_packet_remove_object *my_packet = reinterpret_cast<sc_packet_remove_object *>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hide();
		}
		else if (other_id < MAX_USER) {
			players[other_id].hide();
		}
		else {
			if (0 != npcs.count(other_id))
				npcs[other_id].hide();
		}
		break;
	}
	case SC_CHAT:
	{
		sc_packet_chat *my_packet = reinterpret_cast<sc_packet_chat *>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.add_chat(my_packet->chat);
		}
		else if (other_id < MAX_USER) {
			players[other_id].add_chat(my_packet->chat);
		}
		else {
			if (0 != npcs.count(other_id))
				npcs[other_id].add_chat(my_packet->chat);
		}
		break;
	}
	case SC_LOGIN_FAIL:
		cout << "Login FAIL" << endl;
		break;
	case SC_STAT_CHANGE:
	{
		sc_packet_stat_change *my_packet = reinterpret_cast<sc_packet_stat_change *>(ptr);
		p.HpSet(my_packet->hp);
		p.LevelSet(my_packet->level);
		p.ExpSet(my_packet->exp);

		break;
	}
	case SC_NOTICE:
	{
		sc_packet_chat *my_packet = reinterpret_cast<sc_packet_chat *>(ptr);
		no.add_chat(my_packet->chat);
		break;
	}
	//case SC_DIE:
	//{
	//	sc_packet_die *my_packet = reinterpret_cast<sc_packet_die *>(ptr);
	//	int id = my_packet->id;
	//	npcs[id].object_die();
	//	break;
	//}
	default:
		printf("Unknown PACKET type [%d]\n", ptr[1]);
	}
}

void process_data(char *net_buf, size_t io_byte)
{
	char *ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[BUF_SIZE];


	while (0 != io_byte) {
		if (0 == in_packet_size) in_packet_size = ptr[0];
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

void client_main()
{
	status.HEAL_REPEAT();

	char net_buf[BUF_SIZE];
	size_t	received;

	auto recv_result = socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!";
		while (true);
	}
	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);

	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_x = i + g_left_x;
			int tile_y = j + g_top_y;
			if ((tile_x < 0) || (tile_y < 0)) continue;

			ground_grass.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
			ground_grass.a_draw();
		}
	avatar.draw();
	for (auto &pl : players) pl.draw();
	for (auto &npc : npcs) npc.second.draw();
	sf::Text text;
	sf::Text p_text;
	sf::Text p_exptext;
	//text.setFont(g_font);
	text.setFont(pixel_font);
	//p_text.setFont(g_font);
	p_text.setFont(pixel_font);
	//p_exptext.setFont(g_font);
	p_exptext.setFont(pixel_font);

	char buf[30];
	sprintf_s(buf, "(%d, %d)", avatar.m_x, avatar.m_y);
	p_text.setPosition(10, 40);
	p_text.setString(buf);
	g_window->draw(p_text);

	char playerUI[40];
	sprintf_s(playerUI, "level : %d / hp : %d", p.LevelGet(), p.HpGet());
	text.setString(playerUI);
	text.setFillColor(sf::Color::Black);
	g_window->draw(text);

	char ShowEXP[20];
	sprintf_s(ShowEXP, "EXP : %d ", p.ExpGet());
	p_exptext.setPosition(10, 550);
	p_exptext.setString(ShowEXP);
	p_exptext.setFillColor(sf::Color::Red);
	g_window->draw(p_exptext);

	// 게임 공지
	no.draw();
}

void send_move_packet(int p_type)
{
	cs_packet_move packet;
	packet.size = sizeof(packet);
	packet.type = CS_MOVE;
	packet.direction = p_type;
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}
void send_heal_timer_packet(int inc)
{
	cs_packet_heal packet;
	packet.size = sizeof(packet);
	packet.type = CS_HEAL;
	packet.num = inc;

	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}
void send_state_change_packet(int p_type, int hp, int exp, int level)
{

	// 그럼 여기서는 5초마다 바꿔달라고 요청만할까? 
	sc_packet_stat_change packet;
	packet.size = sizeof(packet);
	packet.type = CS_STATE_CHANGE;
	packet.hp = hp;
	packet.exp = exp;
	packet.level = level;
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}

void send_attack_packet()
{
	cs_packet_attack packet;
	packet.size = sizeof(packet);
	packet.type = CS_ATTACK;
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}

void Input_PlayerID()
{
	cout << "ID 입력 : " << endl;
	cin >> input_id;

	cs_packet_login packet;
	packet.size = sizeof(packet);
	packet.type = CS_LOGIN;
	strcpy_s(packet.id, input_id);
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);

}
int main()
{
	wcout.imbue(locale("korean"));
	sf::Socket::Status status = socket.connect("127.0.0.1", SERVER_PORT);
	socket.setBlocking(false);

	if (status != sf::Socket::Done) {
		wcout << L"서버와 연결할 수 없습니다.\n";
		while (true);
	}

	// 여기서 로그인id받고 
	// 패킷 보내자 

	client_initialize();

	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2016182016GSP");
	g_window = &window;
	Input_PlayerID();
	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			// 아 여기는 진짜 단지 move의 패킷, 그리고 closed 닫는거 뿐이구나.
			// id실패하면 바로 닫을거면 Event::closed여기로 보내버리자 그냥 
			if (event.type == sf::Event::Closed)
				window.close();
			if (event.type == sf::Event::KeyPressed) {
				int p_type = -1;
				switch (event.key.code) {
				case sf::Keyboard::Left:
					p_type = KEY_LEFT;
					break;
				case sf::Keyboard::Right:
					p_type = KEY_RIGHT;
					break;
				case sf::Keyboard::Up:
					p_type = KEY_UP;
					break;
				case sf::Keyboard::Down:
					p_type = KEY_DOWN;
					break;
				case sf::Keyboard::Escape:
					window.close();
					break;
				case sf::Keyboard::A:
					send_attack_packet();
					continue;
					break;
				}
				if (-1 != p_type) send_move_packet(p_type);

			}
		}

		window.clear();
		client_main();
		window.display();
	}
	client_finish();

	return 0;
}