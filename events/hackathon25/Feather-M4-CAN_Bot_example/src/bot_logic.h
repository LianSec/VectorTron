#ifndef BOT_LOGIC_H
#define BOT_LOGIC_H

#include <stdint.h>
#include <stdbool.h>

#define GRID_WIDTH 64
#define GRID_HEIGHT 64
#define MAX_PLAYERS 4

enum Direction { UP = 0, RIGHT = 1, DOWN = 2, LEFT = 3 };
int dx[] = { 0, 1, 0, -1 };
int dy[] = { -1, 0, 1, 0 };

uint8_t 		choose_direction(int x, int y);
void 			update_game_state(int tick_positions[MAX_PLAYERS][2]);

#endif
