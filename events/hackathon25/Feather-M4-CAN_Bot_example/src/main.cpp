#include <Arduino.h>
#include <CAN.h>
#include "Hackathon25.h"

// Global variables
const uint32_t hardware_ID = (*(RoReg *)0x008061FCUL);
uint8_t player_ID = 0;
uint8_t game_ID = 0;
uint8_t currentDirection = 1; // UP by default
bool isDead = false;
bool preferLeft = true;
#define HISTORY_LEN 3
uint8_t move_history[HISTORY_LEN] = {0};
int move_index = 0;
#define WIDTH 64
#define HEIGHT 64
#define MAX_PLAYERS 4
unsigned long lastMoveTime = 0;
bool player_alive[MAX_PLAYERS] = { true, true, true, true };

int8_t grid[WIDTH][HEIGHT] = { -1 }; // -1 = frei, 0..3 = Trail von Spieler 1..4
uint8_t player_positions[MAX_PLAYERS][2] = {{0}};         // aktuelle Positionen der Spieler


// Function prototypes
void send_Join();
void rcv_Player();
void rcv_Game();
void send_Name(const char *name);
void rcv_GameState();
void send_Move(uint8_t direction);
void rcv_Die();
void rcv_Gamefinish();
void rcv_Error();
uint8_t choose_direction(uint8_t x, uint8_t y);
#define MAX_QUEUE_SIZE 1024
int flood_fill_score(uint8_t startX, uint8_t startY, int maxTiles);


struct Point {
  uint8_t x, y;
};

Point queue[MAX_QUEUE_SIZE];
int q_head = 0, q_tail = 0;

void q_clear() {
  q_head = q_tail = 0;
}

bool q_empty() {
  return q_head == q_tail;
}

bool q_push(Point p) {
  if ((q_tail + 1) % MAX_QUEUE_SIZE == q_head) return false; // overflow
  queue[q_tail] = p;
  q_tail = (q_tail + 1) % MAX_QUEUE_SIZE;
  return true;
}

Point q_pop() {
  Point p = queue[q_head];
  q_head = (q_head + 1) % MAX_QUEUE_SIZE;
  return p;
}
uint8_t choose_direction(uint8_t myX, uint8_t myY) {
  const uint8_t directions[4] = {UP, DOWN, LEFT, RIGHT};
  const int dx[4] = {0, 0, -1, 1};
  const int dy[4] = {-1, 1, 0, 0};
  int scores[4] = {-1, -1, -1, -1};

  // === Kreisbewegung erkennen ===
  bool sameTurn = true;
  if (move_index >= HISTORY_LEN) {
    uint8_t last = move_history[(move_index - 1) % HISTORY_LEN];
    for (int i = 2; i <= HISTORY_LEN; i++) {
      if (move_history[(move_index - i) % HISTORY_LEN] != last) {
        sameTurn = false;
        break;
      }
    }
  }

  // === Gefahr direkt voraus erkennen ===
  int dirIndex = -1;
  for (int i = 0; i < 4; i++) {
    if (directions[i] == currentDirection) {
      dirIndex = i;
      break;
    }
  }

  bool dangerAhead = false;
  if (dirIndex != -1) {
    int fx = myX + dx[dirIndex];
    int fy = myY + dy[dirIndex];

    if (fx < 0 || fx >= WIDTH || fy < 0 || fy >= HEIGHT) {
      dangerAhead = true;
    } else {
      int val = grid[fx][fy];
      if (val >= 0 && player_alive[val]) {
        dangerAhead = true;
      }
    }
  }

  if (dangerAhead) {
    int leftDir, rightDir;
    switch (currentDirection) {
      case UP:    leftDir = 2; rightDir = 3; break;
      case DOWN:  leftDir = 3; rightDir = 2; break;
      case LEFT:  leftDir = 1; rightDir = 0; break;
      case RIGHT: leftDir = 0; rightDir = 1; break;
      default:    leftDir = 0; rightDir = 1; break;
    }

    int tryDir = preferLeft ? leftDir : rightDir;
    preferLeft = !preferLeft;

    int nx = myX + dx[tryDir];
    int ny = myY + dy[tryDir];

    if (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT) {
      int val = grid[nx][ny];
      if (val == -1 || (val >= 0 && !player_alive[val])) {
        return directions[tryDir];
      }
    }
    return currentDirection;
  }

  // === Richtungen bewerten ===
  for (int i = 0; i < 4; i++) {
    if ((currentDirection == UP && directions[i] == DOWN) ||
        (currentDirection == DOWN && directions[i] == UP) ||
        (currentDirection == LEFT && directions[i] == RIGHT) ||
        (currentDirection == RIGHT && directions[i] == LEFT)) {
      continue;
    }

    int nx = myX + dx[i];
    int ny = myY + dy[i];

    if (nx < 0 || nx >= WIDTH || ny < 0 || ny >= HEIGHT) continue;
    int val = grid[nx][ny];
    if (val != -1 && player_alive[val]) continue;

    if (sameTurn && directions[i] == move_history[(move_index - 1) % HISTORY_LEN]) {
      continue; // gleiche Richtung vermeiden
    }

    // Tunnelprüfung
    bool safe = true;
    for (int step = 1; step <= 7; step++) {
      int fx = nx + dx[i] * step;
      int fy = ny + dy[i] * step;

      if (fx < 0 || fx >= WIDTH || fy < 0 || fy >= HEIGHT) {
        safe = false; break;
      }
      int trail = grid[fx][fy];
      if (trail >= 0 && player_alive[trail]) {
        safe = false; break;
      }
    }

    if (!safe) continue;

    int score = flood_fill_score(nx, ny, 50);
    if (score < 8) continue;
    scores[i] = score;
  }

  int bestScore = -1;
  int bestOptions[4];
  int bestCount = 0;

  for (int i = 0; i < 4; i++) {
    if (scores[i] > bestScore) {
      bestScore = scores[i];
      bestOptions[0] = i;
      bestCount = 1;
    } else if (scores[i] == bestScore && bestScore >= 0) {
      bestOptions[bestCount++] = i;
    }
  }

  if (bestCount > 0) {
    int chosen = bestOptions[random(bestCount)];
    return directions[chosen];
  } else {
    return currentDirection;
  }
}


int flood_fill_score(uint8_t startX, uint8_t startY, int maxTiles) {
  if (startX >= WIDTH || startY >= HEIGHT) return 0;

  int8_t val = grid[startX][startY];
  bool isDeadTrail = (val >= 0 && !player_alive[val]);

  if (val != -1 && !isDeadTrail) return 0; // Startfeld blockiert

  bool visited[WIDTH][HEIGHT] = { false };
  q_clear();

  q_push({startX, startY});
  visited[startX][startY] = true;

  const int dx[4] = {0, 0, -1, 1};
  const int dy[4] = {-1, 1, 0, 0};

  int score = 0;

  while (!q_empty() && score < maxTiles) {
    Point p = q_pop();
    score++;

    for (int i = 0; i < 4; i++) {
      int nx = p.x + dx[i];
      int ny = p.y + dy[i];

      if (nx < 0 || nx >= WIDTH || ny < 0 || ny >= HEIGHT) continue;
      if (visited[nx][ny]) continue;

      visited[nx][ny] = true;

      int8_t val = grid[nx][ny];
      bool isDeadTrail = (val >= 0 && !player_alive[val]);

      if (val == -1 || isDeadTrail) {
        q_push({(uint8_t)nx, (uint8_t)ny});
      }
    }
  }

  return score;
}

// CAN receive callback
void onReceive(int packetSize)
{
  if (packetSize)
  {
    switch (CAN.packetId())
    {
    case Player:
      // Serial.println("CAN: Received Player packet");
      rcv_Player();
      break;
    case Game:
     // Serial.println("CAN: Received Game packet");
      rcv_Game();
      break;
    case Gamestate:
     // Serial.println("CAN: Received Gamestate packet");
      rcv_GameState();
      break;
    case Die:
      // Serial.println("CAN: Received Die packet");
      rcv_Die();
      break;
    case Gamefinish:
     // Serial.println("CAN: Received Gamefinish packet");
      rcv_Gamefinish();
      break;
    case Error:
     // Serial.println("CAN: Received Error packet");
      rcv_Error();
      break;
    default:
     // Serial.println("CAN: Received unknown packet");
      break;
    }
  }
}

// CAN setup
bool setupCan(long baudRate)
{
  pinMode(PIN_CAN_STANDBY, OUTPUT);
  digitalWrite(PIN_CAN_STANDBY, false);
  pinMode(PIN_CAN_BOOSTEN, OUTPUT);
  digitalWrite(PIN_CAN_BOOSTEN, true);

  if (!CAN.begin(baudRate))
  {
    return false;
  }
  return true;
}

// Setup
void setup()
{
  Serial.begin(115200);
  while (!Serial)
    ;
   randomSeed(analogRead(A0));
  Serial.println("Initializing CAN bus...");
  if (!setupCan(500000))
  {
    Serial.println("Error: CAN initialization failed!");
    while (1)
      ;
  }
  Serial.println("CAN bus initialized successfully.");

  CAN.onReceive(onReceive);

  delay(1000);
  send_Join();
  // Karte initial mit -1 (= frei) belegen
  memset(grid, -1, sizeof(grid));
}

void loop()
{
 
}
void clear_dead_trail(uint8_t deadPlayerID) {
  int8_t trail_val = deadPlayerID - 1; // 1 → 0, 2 → 1, etc.

  for (int x = 0; x < WIDTH; x++) {
    for (int y = 0; y < HEIGHT; y++) {
      if (grid[x][y] == trail_val) {
        grid[x][y] = -1; // als frei markieren
      }
    }
  }
}

void rcv_Die() {
  uint8_t deadPlayer;
  CAN.readBytes(&deadPlayer, 1);

  if (deadPlayer == player_ID) {
    Serial.println("You have died.");
    isDead = true;  // blockiere Bewegung etc.
  } else {
    Serial.printf("Player %u has died.\n", deadPlayer);
  }
  player_alive[deadPlayer - 1] = false;
  clear_dead_trail(deadPlayer); // Spuren des toten Spielers entfernen
}

// Send JOIN packet via CAN
void send_Join()
{
  MSG_Join msg_join;
  msg_join.HardwareID = hardware_ID;

  CAN.beginPacket(Join);
  CAN.write((uint8_t *)&msg_join, sizeof(MSG_Join));
  CAN.endPacket();

  Serial.printf("JOIN packet sent (Hardware ID: %u)\n", hardware_ID);
}

// Receive player information
void rcv_Player()
{
  MSG_Player msg_player;
  CAN.readBytes((uint8_t *)&msg_player, sizeof(MSG_Player));

  if (msg_player.HardwareID == hardware_ID)
  {
    player_ID = msg_player.PlayerID;

    // Den Namen senden
    send_Name("Team Rocket");
  }
  //  else {
  //     player_ID = 0;
  // }

 // Serial.printf("Received Player packet | Player ID received: %u | Own Player ID: %u | Hardware ID received: %u | Own Hardware ID: %u\n",
                // msg_player.PlayerID, player_ID, msg_player.HardwareID, hardware_ID);
}

void send_GameAck()
{ // to send the ack message and start the game
  CAN.beginPacket(Gameack);
  CAN.write(player_ID);
  CAN.endPacket();
  Serial.printf("GameACK sent from Player ID: %u\n", player_ID);
}

void rcv_Game()
{
  MSG_Game msg_game;
  CAN.readBytes((uint8_t *)&msg_game, sizeof(MSG_Game));

  for (int i = 0; i < 4; i++)
  {
    if (msg_game.playerIDs[i] == player_ID)
    {
      memset(grid, -1, sizeof(grid));
      for (int j = 0; j < MAX_PLAYERS; j++) player_alive[j] = true;
      send_GameAck();
      return;
    }
  }
}



void rcv_GameState()
{
  MSG_State msg_gamestate;
  CAN.readBytes((uint8_t *)&msg_gamestate, sizeof(msg_gamestate));

  for (int i = 0; i < MAX_PLAYERS; i++)
  {
    uint8_t x = msg_gamestate.player[i][0];
    uint8_t y = msg_gamestate.player[i][1];
    uint8_t lastX = player_positions[i][0];
    uint8_t lastY = player_positions[i][1];

    // Nur Spieler mit gültiger Position und die noch leben
    if (player_alive[i] && x < WIDTH && y < HEIGHT && !(x == 255 && y == 255))
    {
      // Wenn sich Spieler bewegt hat → vorherige Position als Trail markieren
      if (lastX < WIDTH && lastY < HEIGHT && !(x == lastX && y == lastY)) {
        grid[lastX][lastY] = i; // i = 0..3 → Trail von Spieler i+1
      }

      // Position aktualisieren
      player_positions[i][0] = x;
      player_positions[i][1] = y;

      // Aktuelle Position markieren (optional, für bessere Übersicht)
      grid[x][y] = i;
    }
  }

  // === BOT-ENTSCHEIDUNG DIREKT NACH GAMESTATE ===
  if (!isDead && player_ID > 0)
  {
    uint8_t myX = player_positions[player_ID - 1][0];
    uint8_t myY = player_positions[player_ID - 1][1];

    if (myX != 255 && myY != 255)
    {
      uint8_t dir = choose_direction(myX, myY);
      send_Move(dir);
    }
  }
}

void send_Move(uint8_t direction)
{
  if ((currentDirection == UP && direction == DOWN) ||
      (currentDirection == DOWN && direction == UP) ||
      (currentDirection == LEFT && direction == RIGHT) ||
      (currentDirection == RIGHT && direction == LEFT))
  {
    Serial.println("Invalid move: backward movement is ignored.");
    return;
  }

  CAN.beginPacket(Move);
  CAN.write(player_ID);
  CAN.write(direction);
  CAN.endPacket();

  currentDirection = direction;

  move_history[move_index % HISTORY_LEN] = direction;
  move_index++;

  uint8_t x = player_positions[player_ID - 1][0];
  uint8_t y = player_positions[player_ID - 1][1];

  if (x < WIDTH && y < HEIGHT && x != 255 && y != 255)
  {
    grid[x][y] = player_ID - 1;
  }
}

void send_Move(uint8_t direction)
{
  if ((currentDirection == UP && direction == DOWN) ||
      (currentDirection == DOWN && direction == UP) ||
      (currentDirection == LEFT && direction == RIGHT) ||
      (currentDirection == RIGHT && direction == LEFT)) // check for backward movement
  {
    Serial.println("Invalid move: backward movement is ignored.");
    return;
  }

  CAN.beginPacket(Move);
  CAN.write(player_ID);
  CAN.write(direction);
  CAN.endPacket();

  currentDirection = direction;
   // === Eigenen Trail setzen ===
  uint8_t x = player_positions[player_ID - 1][0];
  uint8_t y = player_positions[player_ID - 1][1];

  if (x < WIDTH && y < HEIGHT && x != 255 && y != 255)
  {
    grid[x][y] = player_ID - 1; // Spieler-ID als Trail markieren
  }
}

void rcv_Gamefinish(){
  MSG_Gamefinish msg_gamefinish;
  CAN.readBytes((uint8_t *)&msg_gamefinish, sizeof(MSG_Gamefinish));
  Serial.printf("Game is finished.\n");
  for(int i = 0; i < 8; i+=2){
    Serial.printf("Player %u has %u points.\n", msg_gamefinish.Player_IDs[i], msg_gamefinish.Player_IDs[i+1]);234
  };
}

void rcv_Error(){
  MSG_Error msg_error;
  CAN.readBytes((uint8_t *)&msg_error, sizeof(MSG_Error));

  Serial.printf("Player %u has received an error (code):%u\n", msg_error.Player_ID, msg_error.Error_Code);

  switch (msg_error.Error_Code)
  {
  case 1: 
    Serial.println("Invalid Player ID."); 
    break;
  case 2:
    Serial.println("Unallowed Name.");
    break;
  case 3:
    Serial.println("You are not playing.");
    break;
  case 4:
    Serial.println("Unknown move.");
    break;
  default:
    Serial.println("Unknown Error Code!");
    break;
  }
}

