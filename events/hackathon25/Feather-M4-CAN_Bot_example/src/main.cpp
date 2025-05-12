#include <Arduino.h>
#include <CAN.h>
#include "Hackathon25.h"
#include <queue>
#include <string.h>
#include <math.h>

// Globale Variablen
const uint32_t hardware_ID = (*(RoReg *)0x008061FCUL);
uint8_t player_ID = 0;
uint8_t game_ID = 0;
uint8_t currentDirection = UP; // Standardrichtung: UP
bool isDead = false;
const int dx[5] = {0, 0, 1, 0, -1};
const int dy[5] = {0, 1, 0, -1, 0};
uint8_t game_map[GRID_HEIGHT][GRID_WIDTH] = {0};
bool player_alive[MAX_PLAYERS] = {true};
int player_x = -1;
int player_y = -1;
int floodFillLimit = 50;

// Funktionsprototypen
void send_Join();
void rcv_Player();
void rcv_Game();
void send_Name(const char *name);
void rcv_GameState();
void send_Move(uint8_t direction);
void rcv_Die();
void rcv_Gamefinish();
void rcv_Error();
int floodFillArea(const uint8_t map[GRID_HEIGHT][GRID_WIDTH], int startX, int startY, int limit);
uint8_t choose_direction(int x, int y);
void apply_gamestate(const MSG_State &state);
void send_GameAck();
int wrap(int coord);
float torus_dist(int x1, int y1, int x2, int y2);

void onReceive(int packetSize) {
  if (packetSize) {
    switch (CAN.packetId()) {
      case Player: rcv_Player(); break;
      case Game: rcv_Game(); break;
      case Gamestate: rcv_GameState(); break;
      case Die: rcv_Die(); break;
      case Gamefinish: rcv_Gamefinish(); break;
      case Error: rcv_Error(); break;
      default: Serial.println("CAN: Received unknown packet"); break;
    }
  }
}

bool setupCan(long baudRate) {
  pinMode(PIN_CAN_STANDBY, OUTPUT);
  digitalWrite(PIN_CAN_STANDBY, false);
  pinMode(PIN_CAN_BOOSTEN, OUTPUT);
  digitalWrite(PIN_CAN_BOOSTEN, true);
  return CAN.begin(baudRate);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing CAN bus...");
  if (!setupCan(500000)) {
    Serial.println("Error: CAN initialization failed!");
    while (1);
  }
  Serial.println("CAN bus initialized successfully.");
  CAN.onReceive(onReceive);
  delay(1000);
  send_Join();
  // Karte initial mit -1 (= frei) belegen
}

void loop() {}

void rcv_Die() {
  uint8_t deadPlayer;
  CAN.readBytes(&deadPlayer, 1);
  if (deadPlayer < MAX_PLAYERS) player_alive[deadPlayer] = false;
  if (deadPlayer == player_ID) {
    Serial.println("You have died.");
    isDead = true;
  } else {
    Serial.printf("Player %u has died.\n", deadPlayer);
  }
  player_alive[deadPlayer - 1] = false;
}

void send_Join() {
  MSG_Join msg_join;
  msg_join.HardwareID = hardware_ID;
  CAN.beginPacket(Join);
  CAN.write((uint8_t *)&msg_join, sizeof(MSG_Join));
  CAN.endPacket();
  Serial.printf("JOIN packet sent (Hardware ID: %u)\n", hardware_ID);
}

void rcv_Player() {
  MSG_Player msg_player;
  CAN.readBytes((uint8_t *)&msg_player, sizeof(MSG_Player));
  if (msg_player.HardwareID == hardware_ID) {
    player_ID = msg_player.PlayerID;
    send_Name("Team Rocket");
  }
}

void rcv_Game() {
  MSG_Game msg_game;
  CAN.readBytes((uint8_t *)&msg_game, sizeof(MSG_Game));
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (msg_game.playerIDs[i] == player_ID) {
      send_GameAck();
      return;
    }
  }
}

void send_GameAck() {
  CAN.beginPacket(Gameack);
  CAN.write(player_ID);
  CAN.endPacket();
  Serial.printf("GameACK sent from Player ID: %u\n", player_ID);
}

void rcv_GameState() {
  MSG_State msg_state;
  CAN.readBytes((uint8_t *)&msg_state, sizeof(MSG_State));
  apply_gamestate(msg_state);
  uint8_t direction = choose_direction(player_x, player_y);
  send_Move(direction);
}

void apply_gamestate(const MSG_State &state) {
  memset(game_map, 0, sizeof(game_map));
  for (int i = 0; i < MAX_PLAYERS; i++) {
    int x = state.player[i][0];
    int y = state.player[i][1];
    if (x >= 0 && x < GRID_WIDTH && y >= 0 && y < GRID_HEIGHT) {
      game_map[y][x] = i + 1;
      if (i == player_ID) {
        player_x = x;
        player_y = y;
      }
    }
  }
}

uint8_t choose_direction(int x, int y) {
  uint8_t best_dir = currentDirection;
  int best_score = -1;

  for (uint8_t dir = UP; dir <= LEFT; dir++) {
    if ((currentDirection == UP && dir == DOWN) ||
        (currentDirection == DOWN && dir == UP) ||
        (currentDirection == LEFT && dir == RIGHT) ||
        (currentDirection == RIGHT && dir == LEFT)) {
      continue; // keine Rückwärtsbewegung
    }

    int nx = wrap(x + dx[dir]);
    int ny = wrap(y + dy[dir]);
    if (game_map[ny][nx] != 0) continue; // besetzt

    int area = floodFillArea(game_map, nx, ny, floodFillLimit);
    float min_enemy_dist = 30.0f;
    for (int i = 0; i < MAX_PLAYERS; i++) {
      if (i == player_ID || !player_alive[i]) continue;
      for (int y2 = 0; y2 < GRID_HEIGHT; y2++) {
        for (int x2 = 0; x2 < GRID_WIDTH; x2++) {
          if (game_map[y2][x2] == i + 1) {
            float d = torus_dist(nx, ny, x2, y2);
            if (d < min_enemy_dist) min_enemy_dist = d;
          }
        }
      }
    }
    int score = area * 3 + (int)(min_enemy_dist * 2);
    if (score > best_score) {
      best_score = score;
      best_dir = dir;
    }
  }

  // Sicherheitsprüfung
  int checkX = wrap(x + dx[best_dir]);
  int checkY = wrap(y + dy[best_dir]);
  if (game_map[checkY][checkX] != 0) {
    // aktuelle Richtung ist blockiert – suche irgendeine freie Richtung
    for (uint8_t dir = UP; dir <= LEFT; dir++) {
      if ((currentDirection == UP && dir == DOWN) ||
          (currentDirection == DOWN && dir == UP) ||
          (currentDirection == LEFT && dir == RIGHT) ||
          (currentDirection == RIGHT && dir == LEFT)) {
        continue;
      }
      int nx = wrap(x + dx[dir]);
      int ny = wrap(y + dy[dir]);
      if (game_map[ny][nx] == 0) {
        return dir;
      }
    }
    // keine freie Richtung -> Standardrichtung
    return currentDirection; 
  }
  return best_dir;

}




int floodFillArea(const uint8_t map[GRID_HEIGHT][GRID_WIDTH], int startX, int startY, int limit) {
  bool visited[GRID_HEIGHT][GRID_WIDTH] = {false};
  std::queue<std::pair<int, int>> q;
  q.push({startX, startY});
  visited[startY][startX] = true;
  int count = 0;
  while (!q.empty() && count < limit) {
    auto [x, y] = q.front(); q.pop();
    count++;
    for (int dir = 1; dir <= 4; dir++) {
      int nx = wrap(x + dx[dir]);
      int ny = wrap(y + dy[dir]);
      if (!visited[ny][nx] && map[ny][nx] == 0) {
        visited[ny][nx] = true;
        q.push({nx, ny});
      }
    }
  }
  return count;
}

int wrap(int coord) {
  return (coord + GRID_WIDTH) % GRID_WIDTH;
}

float torus_dist(int x1, int y1, int x2, int y2) {
  int dx = abs(x1 - x2);
  int dy = abs(y1 - y2);
  dx = std::min(dx, GRID_WIDTH - dx);
  dy = std::min(dy, GRID_HEIGHT - dy);
  return sqrtf(dx * dx + dy * dy);
}

void send_Move(uint8_t direction) {
  if ((currentDirection == UP && direction == DOWN) ||
      (currentDirection == DOWN && direction == UP) ||
      (currentDirection == LEFT && direction == RIGHT) ||
      (currentDirection == RIGHT && direction == LEFT)) {
    Serial.println("Invalid move: backward movement is ignored.");
    return;
  }
  CAN.beginPacket(Move);
  CAN.write(player_ID);
  CAN.write(direction);
  CAN.endPacket();
  currentDirection = direction;
}

void rcv_Gamefinish() {
  MSG_Gamefinish msg_gamefinish;
  CAN.readBytes((uint8_t *)&msg_gamefinish, sizeof(MSG_Gamefinish));
  Serial.printf("Game is finished.\n");
  for (int i = 0; i < 8; i += 2) {
    Serial.printf("Player %u has %u points.\n", msg_gamefinish.Player_IDs[i], msg_gamefinish.Player_IDs[i + 1]);
  }
}

void rcv_Error() {
  MSG_Error msg_error;
  CAN.readBytes((uint8_t *)&msg_error, sizeof(MSG_Error));
  Serial.printf("Player %u has received an error (code):%u\n", msg_error.Player_ID, msg_error.Error_Code);
}

void send_Name(const char *name) {
  uint8_t length = strlen(name);
  if (length > 20) length = 20;
  struct __attribute__((packed)) RenamePacket {
    uint8_t playerID;
    uint8_t length;
    char first6[6];
  } renamePacket;
  renamePacket.playerID = player_ID;
  renamePacket.length = length;
  memset(renamePacket.first6, ' ', 6);
  strncpy(renamePacket.first6, name, 6);
  CAN.beginPacket(Name);
  CAN.write((uint8_t *)&renamePacket, sizeof(renamePacket));
  CAN.endPacket();
  delay(10);

  const char *ptr = name + 6;
  uint8_t remaining = length > 6 ? length - 6 : 0;
  while (remaining > 0) {
    struct __attribute__((packed)) RenameFollowPacket {
      uint8_t playerID;
      char next7[7];
    } followPacket;
    followPacket.playerID = player_ID;
    memset(followPacket.next7, ' ', 7);
    strncpy(followPacket.next7, ptr, 7);
    CAN.beginPacket(0x510);
    CAN.write((uint8_t *)&followPacket, sizeof(followPacket));
    CAN.endPacket();
    ptr += 7;
    remaining = remaining > 7 ? remaining - 7 : 0;
    delay(10);
  }
}
