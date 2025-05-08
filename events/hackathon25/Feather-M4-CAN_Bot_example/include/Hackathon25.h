#ifndef HACKATHON25_H
#define HACKATHON25_H

#include <stdint.h>

enum CAN_MSGs {
    Join = 0x100,
    Leave = 0x101,
    Player = 0x110,
    Game = 0x040,
    Gameack = 0x120
};

struct __attribute__((packed)) MSG_Join {
    uint32_t HardwareID;
};

struct __attribute__((packed)) MSG_Player {
    uint32_t HardwareID;
    uint8_t PlayerID;
};

struct __attribute__((packed)) MSG_Game {
    uint8_t Player1;
    uint8_t Player2;
    uint8_t Player3;
    uint8_t Player4;
};


#endif