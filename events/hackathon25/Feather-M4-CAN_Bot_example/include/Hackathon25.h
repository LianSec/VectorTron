#ifndef HACKATHON25_H
#define HACKATHON25_H

# include <stdint.h>

typedef struct __attribute__((__packed__)) {
    uint8_t GameID;
    uint8_t PlayerX[MAX_PLAYERS]; // X positions for players 1–4
    uint8_t PlayerY[MAX_PLAYERS]; // Y positions for players 1–4
  } MSG_Tick;



enum CAN_MSGs {
    Join = 0x100, // client → server: join with HardwareID
    Leave = 0x101, // client → server: optional leave
    Player = 0x110, // server → client: assigned PlayerID
    Game = 0x040, // server → clients: new game with 4 players
    Gameack = 0x120, // client → server: confirm game participation
    Move = 0x090 // client → server: send direction to move
};

struct __attribute__((packed)) MSG_Join {
    uint32_t HardwareID;
};

struct __attribute__((packed)) MSG_Player {
    uint32_t HardwareID;
    uint8_t PlayerID;
};

struct __attribute__((packed)) MSG_Game {
    uint8_t playerIDs[4];
};

struct __attribute__((packed)) MSG_Move{
    uint8_t Player_ID;
    uint8_t Direction; // UP = 1, RIGHT = 2, DOWN = 3, LEFT = 4
};
#endif
