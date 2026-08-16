#pragma once
#include <stdint.h>
#include "battle.h"
#include "trainers.h"   // TRAINER_TEAM_MAX: a link team is the same size

// Peer-to-peer battles.
//
// ONE DEVICE IS AUTHORITATIVE. That is forced, not chosen: battle.cpp makes 11
// random() calls a turn -- crit, damage spread, accuracy, ailment procs,
// multi-hit count, confusion, thaw, wake, speed ties -- so two devices
// resolving independently desync inside a single turn. A shared seed only holds
// until the two builds differ by one call. The host runs battleAct(); the guest
// sends a move index and renders the TurnLogs it is handed, and therefore needs
// no game logic at all.
//
// The transport is a function pointer rather than a direct ESP-NOW call so the
// protocol can be tested: two Links cross-wired in one process exercise the
// whole handshake without a radio. Only the radio itself is unverifiable here.

#define LINK_PROTO 1        // bump on ANY wire change; a mismatch is refused
#define LINK_MAX_PAYLOAD 200
#define LINK_NAME_LEN 12

enum LinkState : uint8_t {
  LINK_OFF = 0,
  LINK_LISTENING,    // waiting for a peer
  LINK_HANDSHAKE,    // hello sent, waiting for theirs
  LINK_SQUADS,       // teams being exchanged
  LINK_READY,        // both known; the host may resolve a turn
  LINK_WAITING,      // guest: move sent, waiting for the result
  LINK_DONE,         // somebody won
  LINK_REFUSED,      // protocol mismatch -- loudly, never a silent desync
};

enum LinkMsg : uint8_t {
  LM_HELLO = 1,
  LM_SQUAD,
  LM_MOVE,
  LM_RESULT,
  LM_END,
};

// A creature on the wire. Deliberately not `Combatant` itself: that carries
// live battle state (stages, ailments, timers) which only the host owns.
struct LinkMon {
  int16_t dex;
  uint8_t level;
  uint16_t maxHp;
  uint16_t base[SI_COUNT];
  uint8_t moves[MOVE_SLOTS];
  uint8_t shiny;
  char name[LINK_NAME_LEN];
};

struct Link {
  uint8_t state = LINK_OFF;
  bool isHost = false;
  uint8_t protoTheirs = 0;
  char peerName[LINK_NAME_LEN] = "";

  LinkMon mine[TRAINER_TEAM_MAX];
  uint8_t mineN = 0;
  LinkMon theirs[TRAINER_TEAM_MAX];
  uint8_t theirsN = 0;

  uint8_t pendingMove = 0;   // host: the guest's chosen slot, 0 = none yet
  bool youWon = false;

  // set by whoever owns the radio; ctx lets a test route two Links to each other
  void (*send)(void *ctx, const uint8_t *buf, uint8_t len) = nullptr;
  void *ctx = nullptr;

  void begin(bool host, const char *myName);
  void addMon(const LinkMon &m);
  void start();                       // announce: hello, then squad
  void onPacket(const uint8_t *buf, uint8_t len);
  void sendMove(uint8_t slot);        // guest -> host
  void sendResult(const uint8_t *blob, uint8_t len);   // host -> guest
  void sendEnd(bool hostWon);
  bool ready() const { return state == LINK_READY; }
};

// Builds the wire form of a combatant, so both ends agree on what a creature is.
void linkMonFrom(LinkMon &out, const Combatant &c);
void linkMonTo(Combatant &out, const LinkMon &m);
