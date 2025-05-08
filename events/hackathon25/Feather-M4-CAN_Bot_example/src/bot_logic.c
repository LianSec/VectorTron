// tron_bot_logic.c (you can split this into a separate file later)

#include "bot_logic.h"

// Global game board: 0 = free, >0 = occupied by player ID
uint8_t game_board[GRID_HEIGHT][GRID_WIDTH] = {0};

// Track last known positions of players
typedef struct {
    int x;
    int y;
    bool alive;
} PlayerState;

PlayerState players[MAX_PLAYERS];

int wrap_x(int x) { return (x + GRID_WIDTH) % GRID_WIDTH; }
int wrap_y(int y) { return (y + GRID_HEIGHT) % GRID_HEIGHT; }

bool is_free(int x, int y) {
    x = wrap_x(x);
    y = wrap_y(y);
    return game_board[y][x] == 0;
}

int flood_fill_area(int x, int y, uint8_t visited[GRID_HEIGHT][GRID_WIDTH]) {
    int area = 0;
    int stack[GRID_WIDTH * GRID_HEIGHT][2];
    int top = 0;

    x = wrap_x(x);
    y = wrap_y(y);

    if (!is_free(x, y) || visited[y][x]) return 0;

    stack[top][0] = x;
    stack[top][1] = y;
    top++;

    while (top > 0) {
        top--;
        int cx = wrap_x(stack[top][0]);
        int cy = wrap_y(stack[top][1]);

        if (!is_free(cx, cy) || visited[cy][cx]) continue;

        visited[cy][cx] = 1;
        area++;

        stack[top][0] = cx + 1; stack[top][1] = cy;     top++;
        stack[top][0] = cx - 1; stack[top][1] = cy;     top++;
        stack[top][0] = cx;     stack[top][1] = cy + 1; top++;
        stack[top][0] = cx;     stack[top][1] = cy - 1; top++;
    }

    return area;
}

// Main logic to pick best direction
uint8_t choose_direction(int x, int y) {
    int best_dir = UP;
    int max_area = -1;

    for (int dir = 0; dir < 4; dir++) {
        int nx = wrap_x(x + dx[dir]);
        int ny = wrap_y(y + dy[dir]);

        if (!is_free(nx, ny)) continue;

        uint8_t visited[GRID_HEIGHT][GRID_WIDTH] = {0};
        int area = flood_fill_area(nx, ny, visited);

        if (area > max_area) {
            max_area = area;
            best_dir = dir;
        }
    }

    return best_dir;
}

// Call this each tick with updated position of all players
void update_game_state(int tick_positions[MAX_PLAYERS][2]) {
    // First mark the new positions and detect inactive players
    for (int i = 0; i < MAX_PLAYERS; i++) {
        int new_x = tick_positions[i][0];
        int new_y = tick_positions[i][1];

        if (players[i].alive && new_x == players[i].x && new_y == players[i].y) {
            // Player didn't move => dead, clear their trail
            for (int y = 0; y < GRID_HEIGHT; y++) {
                for (int x = 0; x < GRID_WIDTH; x++) {
                    if (game_board[y][x] == (i + 1)) {
                        game_board[y][x] = 0;
                    }
                }
            }
            players[i].alive = false;
        } else {
            players[i].x = new_x;
            players[i].y = new_y;
            players[i].alive = true;
            game_board[wrap_y(new_y)][wrap_x(new_x)] = i + 1;
        }
    }
}

// Usage: after parsing TICK packet and updating player positions
// call update_game_state(tick_positions), then use choose_direction(my_x, my_y)
// to get your next move.
