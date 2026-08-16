#include "link.h"
#include <string.h>

// Packets are [type][len][payload]. Small and fixed so a truncated or
// oversized frame is dropped rather than half-parsed.
#define HDR 2

void linkMonFrom(LinkMon &out, const Combatant &c) {
  memset(&out, 0, sizeof(out));
  out.dex = c.dex;
  out.level = c.level;
  out.maxHp = c.maxHp;
  for (int i = 0; i < SI_COUNT; i++) out.base[i] = c.base[i];
  for (int i = 0; i < MOVE_SLOTS; i++) out.moves[i] = c.moves[i];
  out.shiny = c.shiny ? 1 : 0;
  snprintf(out.name, sizeof(out.name), "%s", c.name);
}

void linkMonTo(Combatant &out, const LinkMon &m) {
  out = Combatant();
  out.dex = m.dex;
  out.level = m.level;
  out.maxHp = m.maxHp ? m.maxHp : 1;
  out.hp = out.maxHp;
  for (int i = 0; i < SI_COUNT; i++) out.base[i] = m.base[i];
  for (int i = 0; i < MOVE_SLOTS; i++) out.moves[i] = m.moves[i];
  out.shiny = m.shiny != 0;
  snprintf(out.name, sizeof(out.name), "%s", m.name);
}

static void put(Link &l, uint8_t type, const uint8_t *body, uint8_t n) {
  if (!l.send || n > LINK_MAX_PAYLOAD) return;
  uint8_t pkt[HDR + LINK_MAX_PAYLOAD];
  pkt[0] = type;
  pkt[1] = n;
  if (n) memcpy(pkt + HDR, body, n);
  l.send(l.ctx, pkt, (uint8_t)(HDR + n));
}

void Link::begin(bool host, const char *myName) {
  state = LINK_LISTENING;
  isHost = host;
  protoTheirs = 0;
  mineN = theirsN = 0;
  pendingMove = 0;
  youWon = false;
  peerName[0] = 0;
  snprintf(peerName, sizeof(peerName), "%s", "");
  (void)myName;
}

void Link::addMon(const LinkMon &m) {
  if (mineN < TRAINER_TEAM_MAX) mine[mineN++] = m;
}

void Link::start() {
  if (state == LINK_OFF) return;
  // State FIRST, then send. put() may deliver synchronously -- it does in the
  // tests, and a fast radio can re-enter too -- so a reply can arrive before
  // this function returns. Sending first left us still LISTENING when the
  // answer landed, and each side answered the other's hello forever.
  state = LINK_HANDSHAKE;
  uint8_t body[1 + LINK_NAME_LEN];
  body[0] = LINK_PROTO;
  memcpy(body + 1, peerName, LINK_NAME_LEN);
  put(*this, LM_HELLO, body, 1 + LINK_NAME_LEN);
}

void Link::sendMove(uint8_t slot) {
  if (state != LINK_READY) return;
  put(*this, LM_MOVE, &slot, 1);
  if (!isHost) state = LINK_WAITING;
}

void Link::sendResult(const uint8_t *blob, uint8_t len) {
  if (!isHost) return;                 // only the host resolves anything
  put(*this, LM_RESULT, blob, len);
}

void Link::sendEnd(bool hostWon) {
  uint8_t b = hostWon ? 1 : 0;
  put(*this, LM_END, &b, 1);
  youWon = isHost ? hostWon : !hostWon;
  state = LINK_DONE;
}

void Link::onPacket(const uint8_t *buf, uint8_t len) {
  if (len < HDR) return;               // runt
  uint8_t type = buf[0], n = buf[1];
  if (n > LINK_MAX_PAYLOAD || (uint16_t)HDR + n > len) return;   // truncated
  const uint8_t *body = buf + HDR;

  switch (type) {
    case LM_HELLO: {
      if (n < 1) return;
      protoTheirs = body[0];
      // A version mismatch is REFUSED, loudly. A silent desync mid-battle is
      // far worse than not connecting: both sides would render different fights.
      if (protoTheirs != LINK_PROTO) { state = LINK_REFUSED; return; }
      if (n >= 1 + LINK_NAME_LEN) {
        memcpy(peerName, body + 1, LINK_NAME_LEN);
        peerName[LINK_NAME_LEN - 1] = 0;
      }
      if (state == LINK_SQUADS || state == LINK_READY) return;  // already greeted
      bool answer = (state == LINK_LISTENING);   // they spoke first
      state = LINK_SQUADS;                       // before sending, see start()
      if (answer) {
        uint8_t reply[1 + LINK_NAME_LEN];
        reply[0] = LINK_PROTO;
        memset(reply + 1, 0, LINK_NAME_LEN);
        put(*this, LM_HELLO, reply, 1 + LINK_NAME_LEN);
      }
      // our squad follows immediately; there is nothing to negotiate
      for (uint8_t i = 0; i < mineN; i++) {
        uint8_t b[1 + sizeof(LinkMon)];
        b[0] = i;
        memcpy(b + 1, &mine[i], sizeof(LinkMon));
        put(*this, LM_SQUAD, b, (uint8_t)(1 + sizeof(LinkMon)));
      }
      return;
    }
    case LM_SQUAD: {
      if (state == LINK_REFUSED) return;
      if (n < 1 + sizeof(LinkMon)) return;
      uint8_t idx = body[0];
      if (idx >= TRAINER_TEAM_MAX) return;
      memcpy(&theirs[idx], body + 1, sizeof(LinkMon));
      if (idx + 1 > theirsN) theirsN = idx + 1;
      if (theirsN && mineN) state = LINK_READY;
      return;
    }
    case LM_MOVE:
      if (!isHost || n < 1) return;    // only the host acts on a move
      pendingMove = body[0] + 1;       // +1 so 0 keeps meaning "nothing yet"
      return;
    case LM_RESULT:
      if (isHost) return;              // the host never receives results
      state = LINK_READY;              // the guest may choose again
      return;
    case LM_END:
      if (n < 1) return;
      youWon = isHost ? (body[0] != 0) : (body[0] == 0);
      state = LINK_DONE;
      return;
    default:
      return;                          // unknown type: ignore, do not desync
  }
}
