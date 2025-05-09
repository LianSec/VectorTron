#include <Arduino.h>
#include <CAN.h>
#include "Hackathon25.h"

// Global variables
const uint32_t hardware_ID = (*(RoReg *)0x008061FCUL);
uint8_t player_ID = 0;
uint8_t game_ID = 0;
uint8_t currentDirection = 1; // UP by default
bool isDead = false;

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
      Serial.println("CAN: Received unknown packet");
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
}

// Loop remains empty, logic is event-driven via CAN callback
void loop()
{
  if (isDead)
    return;
  if (Serial.available())
  {
    char key = Serial.read();
    switch (key)
    {
    case 'w':
      send_Move(UP);
      break;
    case 'd':
      send_Move(RIGHT);
      break;
    case 's':
      send_Move(DOWN);
      break;
    case 'a':
      send_Move(LEFT);
      break;
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
     // Serial.println("I'm part of this game – sending gameack.");
      send_GameAck();
      return;
    }
  }
  //Serial.println("Not part of this game.");
}

void rcv_GameState() // sets player positions
{
  MSG_State msg_gamestate;
  CAN.readBytes((uint8_t *)&msg_gamestate, sizeof(msg_gamestate));
  for (int i = 0; i < 4; i++)
  {
    uint8_t x = msg_gamestate.player[i][0];
    uint8_t y = msg_gamestate.player[i][1];

  }
}

void send_Name(const char *name)
{
  uint8_t length = strlen(name);
  if (length > 20)
    length = 20; // max. 20 Zeichen

  // Rename-Paket (erste 6 Zeichen)
  struct __attribute__((packed)) RenamePacket
  {
    uint8_t playerID;
    uint8_t length;
    char first6[6];
  } 
  
  renamePacket;
  renamePacket.playerID = player_ID;
  renamePacket.length = length;
  memset(renamePacket.first6, ' ', 6);
  strncpy(renamePacket.first6, name, 6);

  CAN.beginPacket(Name);
  CAN.write((uint8_t *)&renamePacket, sizeof(renamePacket));
  CAN.endPacket();
  delay(10); // kurze Pause, um Puffer zu schonen

  // Folgepakete (je 7 Zeichen)
  const char *ptr = name + 6;
  uint8_t remaining = length > 6 ? length - 6 : 0;

  while (remaining > 0)
  {
    struct __attribute__((packed)) RenameFollowPacket
    {
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
  //Serial.printf("Sent move | Player ID: %u | Direction: %u\n", player_ID, direction);
}

void rcv_Gamefinish(){
  MSG_Gamefinish msg_gamefinish;
  CAN.readBytes((uint8_t *)&msg_gamefinish, sizeof(MSG_Gamefinish));
  Serial.printf("Game is finished.\n");
  for(int i = 0; i < 8; i+=2){
    Serial.printf("Player %u has %u points.\n", msg_gamefinish.Player_IDs[i], msg_gamefinish.Player_IDs[i+1]);
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