#ifndef HACKATHON25_H
#define HACKATHON25_H

#include <stdint.h>

enum CAN_MSGs {
    Join = 0x100, // client → server: join with HardwareID
    Leave = 0x101, // client → server: optional leave
    Player = 0x110, // server → client: assigned PlayerID
    Game = 0x040, // server → clients: new game with 4 players
    Gameack = 0x120 // client → server: confirm game participation
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

#endif