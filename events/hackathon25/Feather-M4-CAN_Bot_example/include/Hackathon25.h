#ifndef HACKATHON25_H
#define HACKATHON25_H

#include <stdint.h>
#include <stdbool.h>

// ========== SPIELKONSTANTEN ==========
#define GRID_WIDTH 64
#define GRID_HEIGHT 64
#define MAX_PLAYERS 4

// ========== BEWEGUNGSRICHTUNGEN ==========
enum Direction { UP = 1, RIGHT = 2, DOWN = 3, LEFT = 4 };

// ========== BOT-LOGIK FUNKTIONSPROTOTYPEN ==========
uint8_t choose_direction(int x, int y);
uint8_t update_game_state(int tick_positions[MAX_PLAYERS][2]);

// ========== CAN-NACHRICHTENTYPEN ==========
enum CAN_MSGs {
    Join = 0x100, // client → server: join with HardwareID
    Leave = 0x101, // client → server: optional leave
    Player = 0x110, // server → client: assigned PlayerID
    Game = 0x040, // server → clients: new game with 4 players
    Name = 0x500, //  client → server: update name
    Gameack = 0x120, // client → server: confirm game participation
    Gamestate = 0x050, // server → client: 
    Move = 0x090, // client → server:
    Dead = 0x080,
    Gamefinish = 0x070, // client → server: confirms game is over 
    Error = 0x020 // client → server: receive error if error occurs
};

struct __attribute__((packed)) MSG_Join {
    uint32_t HardwareID;
};

struct __attribute__((packed)) MSG_Player {
    uint32_t HardwareID;
    uint8_t PlayerID;
};

struct __attribute__((packed)) MSG_Game {
    uint8_t playerIDs[MAX_PLAYERS];
};

struct __attribute__((packed)) MSG_State {
    uint8_t player[4][2]; // [4 Spieler][x,y]
  };  

struct __attribute__((packed)) MSG_Move {
    uint8_t Player_ID;
    uint8_t Direction; // 1=UP, 2=RIGHT, 3=DOWN, 4=LEFT
};
struct __attribute__((packed)) MSG_Gamefinish {
    uint8_t Player_IDs[8];
};
struct __attribute__((packed)) MSG_Error{
    uint8_t Player_ID;
    uint8_t Error_Code;
};
#endif
