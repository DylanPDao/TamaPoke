#include "link.h"
#include "moves.h"
#include "dex.h"
#include "pet.h"
#include <string.h>

// Packets are [type][len][payload]. Small and fixed so a truncated or
// oversized frame is dropped rather than half-parsed.
#define HDR 2

// Hello body. Laid out once, here, because every field in it is something the
// two sides must agree on before a single turn is resolved.
#define HB_PROTO 0
#define HB_FLAGS 1        // bit0: I am offering to host
#define HB_COUNT 2        // how many creatures my squad has
#define HB_ID 3           // 2 bytes, breaks a two-host tie
#define HB_BUILD 5        // 2 bytes, table compatibility
#define HB_NAME 7         // LINK_NAME_LEN bytes
#define HB_LEN (HB_NAME + LINK_NAME_LEN)

uint16_t linkBuildTag() {
  // Deliberately made of sizes and counts, not a version string: these are
  // exactly the things that change how a packet is READ. Two builds agreeing on
  // all of them can narrate the same fight even if they differ elsewhere.
  uint32_t h = 2166136261u;
  const uint16_t bits[] = {
    (uint16_t)MOVE_COUNT, (uint16_t)DEX_COUNT, (uint16_t)MOVE_SLOTS,
    (uint16_t)TRAINER_TEAM_MAX, (uint16_t)sizeof(LinkMon),
    (uint16_t)sizeof(LinkResult), (uint16_t)SI_COUNT,
  };
  for (uint16_t b : bits) { h ^= b; h *= 16777619u; }
  return (uint16_t)(h ^ (h >> 16));
}

uint8_t linkSafeMove(uint8_t m) { return m < MOVE_COUNT ? m : 0; }

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

// Everything here is untrusted. A creature off the air indexes DEX_TBL and
// MOVE_TBL and looks up sprites, so a stray byte from a mismatched build --
// or from nothing in particular, since this is a broadcast -- would be an
// out-of-bounds read rather than merely a strange-looking opponent.
void linkMonTo(Combatant &out, const LinkMon &m) {
  out = Combatant();
  out.dex = (m.dex >= 1 && m.dex <= DEX_COUNT) ? m.dex : 1;
  out.level = m.level < 1 ? 1 : (m.level > MAX_LEVEL ? MAX_LEVEL : m.level);
  out.maxHp = m.maxHp ? m.maxHp : 1;
  out.hp = out.maxHp;
  for (int i = 0; i < SI_COUNT; i++) out.base[i] = m.base[i] ? m.base[i] : 1;
  for (int i = 0; i < MOVE_SLOTS; i++) out.moves[i] = linkSafeMove(m.moves[i]);
  out.shiny = m.shiny != 0;
  // NOT snprintf("%s"): a name off the wire need not be terminated, and reading
  // it as a C string would run off the end of the struct.
  uint8_t n = sizeof(out.name) - 1;
  if (n > LINK_NAME_LEN) n = LINK_NAME_LEN;
  memcpy(out.name, m.name, n);
  out.name[n] = 0;
}

static void rawSend(Link &l, const uint8_t *pkt, uint8_t n) {
  if (l.send) l.send(l.ctx, pkt, n);
}

// `keep` marks a packet worth repeating until something supersedes it. Hello
// and squad are not kept individually -- tick() re-sends those as a set, since
// losing one of six squad packets must not leave the other five unrepeated.
static void put(Link &l, uint8_t type, const uint8_t *body, uint8_t n,
                bool keep = false) {
  if (!l.send || n > LINK_MAX_PAYLOAD) return;
  uint8_t pkt[HDR + LINK_MAX_PAYLOAD];
  pkt[0] = type;
  pkt[1] = n;
  if (n) memcpy(pkt + HDR, body, n);
  if (keep) {
    memcpy(l.tx, pkt, HDR + n);
    l.txN = (uint8_t)(HDR + n);
    l.txLive = true;
  }
  rawSend(l, pkt, (uint8_t)(HDR + n));
}

static void sendHello(Link &l) {
  uint8_t b[HB_LEN];
  memset(b, 0, sizeof(b));
  b[HB_PROTO] = LINK_PROTO;
  b[HB_FLAGS] = l.isHost ? 1 : 0;
  b[HB_COUNT] = l.mineN;
  b[HB_ID] = (uint8_t)(l.id & 0xFF);
  b[HB_ID + 1] = (uint8_t)(l.id >> 8);
  uint16_t tag = linkBuildTag();
  b[HB_BUILD] = (uint8_t)(tag & 0xFF);
  b[HB_BUILD + 1] = (uint8_t)(tag >> 8);
  memcpy(b + HB_NAME, l.peerName, LINK_NAME_LEN);
  put(l, LM_HELLO, b, HB_LEN);
}

// The order ROTATES between attempts, and that is load-bearing. A repeated
// exchange settles into a burst of a fixed length; if something drops packets
// on any period that divides it, the same POSITION in the burst dies every time
// and resending never helps. The lossy test reproduces it exactly: a 6-frame
// pairing burst against one-in-three loss destroyed squad slot 0 on all ten
// attempts. Rotating means no slot can stay unlucky. Jittering the timer does
// not fix this on its own -- the loss is per packet, not per millisecond.
static void sendSquad(Link &l, uint8_t rot = 0) {
  if (!l.mineN) return;
  for (uint8_t k = 0; k < l.mineN; k++) {
    uint8_t i = (uint8_t)((k + rot) % l.mineN);
    uint8_t b[1 + sizeof(LinkMon)];
    b[0] = i;
    memcpy(b + 1, &l.mine[i], sizeof(LinkMon));
    put(l, LM_SQUAD, b, (uint8_t)(1 + sizeof(LinkMon)));
  }
}

void Link::begin(bool host, const char *myName) {
  state = LINK_LISTENING;
  isHost = host;
  protoTheirs = 0;
  buildTheirs = 0;
  peerId = 0;
  mineN = theirsN = 0;
  theirsExpected = 0;
  theirsGot = 0;
  turn = 0;
  pendingAct = 0;
  resultN = 0;
  resultNew = false;
  youWon = false;
  txN = 0;
  txLive = false;
  txAt = 0;
  resendSeq = 0;
  lastRx = 0;
  sawRx = false;
  armed = false;
  peerName[0] = 0;
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
  sendHello(*this);
}

void Link::sendAct(uint8_t act) {
  if (state != LINK_READY || !act) return;
  uint8_t b[2] = { turn, act };
  put(*this, LM_ACT, b, 2, true);   // kept: the host may never have heard it
  if (!isHost) state = LINK_WAITING;
}

void Link::sendResult(const uint8_t *blob, uint8_t len) {
  if (!isHost) return;                 // only the host resolves anything
  if (len > LINK_MAX_PAYLOAD - 1) return;
  uint8_t b[LINK_MAX_PAYLOAD];
  b[0] = turn;
  memcpy(b + 1, blob, len);
  put(*this, LM_RESULT, b, (uint8_t)(len + 1), true);
  turn++;                              // this exchange is finished
  pendingAct = 0;
}

void Link::sendEnd(bool hostWon) {
  uint8_t b = hostWon ? 1 : 0;
  put(*this, LM_END, &b, 1, true);
  youWon = isHost ? hostWon : !hostWon;
  state = LINK_DONE;
}

void Link::sendBye() {
  if (state == LINK_OFF) return;
  put(*this, LM_BYE, nullptr, 0);
  txLive = false;
  state = LINK_LOST;
}

void Link::sendRematch() {
  if (!theirsN || !mineN) return;
  put(*this, LM_REMATCH, nullptr, 0);
  rearm();
}

// Same two squads, fresh fight. Nothing is re-exchanged: both sides already
// hold each other's teams, and re-sending them would only be another chance to
// lose a packet.
void Link::rearm() {
  turn = 0;
  pendingAct = 0;
  resultN = 0;
  resultNew = false;
  youWon = false;
  txLive = false;
  state = LINK_READY;
}

// Resend what we last said, and give up on a peer that has gone quiet. Called
// once a frame; `now` is passed in so a test can drive time directly.
void Link::tick(uint32_t now) {
  if (!live() || state == LINK_DONE) return;
  if (!armed) { armed = true; lastRx = now; txAt = now; }
  if (sawRx) { sawRx = false; lastRx = now; }

  uint32_t limit = (state == LINK_READY || state == LINK_WAITING)
                     ? LINK_BATTLE_TIMEOUT_MS : LINK_PAIR_TIMEOUT_MS;
  if (now - lastRx > limit) {
    // A peer that walked out of range, powered off, or was never there at all.
    // Reported, never waited on forever -- the old behaviour was a hang, which
    // is indistinguishable from a crash.
    state = LINK_LOST;
    txLive = false;
    return;
  }

  // Derived from our id, so the two sides never settle into the same rhythm as
  // each other or as whatever is stepping on the channel.
  uint32_t wait = LINK_RESEND_MS + ((uint32_t)(id + resendSeq * 37u) % LINK_JITTER_MS);
  if (now - txAt < wait) return;
  txAt = now;
  resendSeq++;
  if (state == LINK_HANDSHAKE) {
    sendHello(*this);
  } else if (state == LINK_SQUADS) {
    sendHello(*this);       // as a set: a squad is only useful complete
    sendSquad(*this, resendSeq);
  } else if (txLive) {
    rawSend(*this, tx, txN);
  }
}

void Link::onPacket(const uint8_t *buf, uint8_t len) {
  if (len < HDR) return;               // runt
  uint8_t type = buf[0], n = buf[1];
  if (n > LINK_MAX_PAYLOAD || (uint16_t)HDR + n > len) return;   // truncated
  const uint8_t *body = buf + HDR;
  if (state == LINK_REFUSED) return;   // an incompatible peer stays refused
  sawRx = true;    // proof of life. onPacket has no clock, so tick() stamps it

  switch (type) {
    case LM_HELLO: {
      if (n < HB_LEN) return;
      protoTheirs = body[HB_PROTO];
      // A version mismatch is REFUSED, loudly. A silent desync mid-battle is
      // far worse than not connecting: both sides would render different fights.
      if (protoTheirs != LINK_PROTO) { state = LINK_REFUSED; return; }
      buildTheirs = (uint16_t)body[HB_BUILD] | ((uint16_t)body[HB_BUILD + 1] << 8);
      // Same proto, different tables: the move index in a result would name a
      // different move on each screen. Also refused.
      if (buildTheirs != linkBuildTag()) { state = LINK_REFUSED; return; }
      peerId = (uint16_t)body[HB_ID] | ((uint16_t)body[HB_ID + 1] << 8);
      bool theyHost = (body[HB_FLAGS] & 1) != 0;
      theirsExpected = body[HB_COUNT] > TRAINER_TEAM_MAX ? TRAINER_TEAM_MAX
                                                        : body[HB_COUNT];
      memcpy(peerName, body + HB_NAME, LINK_NAME_LEN);
      peerName[LINK_NAME_LEN - 1] = 0;

      // Two hosts would resolve the same battle twice and disagree about all of
      // it; two guests would sit in silence forever. Settled by id rather than
      // by asking the players again -- the higher one hosts. Identical ids
      // cannot be told apart, so that is refused rather than guessed.
      if (isHost == theyHost) {
        if (peerId == id) { state = LINK_REFUSED; return; }
        isHost = (id > peerId);
      }

      // Already met -- but they are still saying hello, which means they are
      // still missing part of our squad. Only the side that is behind resends,
      // and a side that has finished stops listening for it, so without this
      // one lost SQUAD packet deadlocks the pair permanently. Answer with the
      // squad and NOT another hello, or the two would volley forever.
      if (state == LINK_READY || state == LINK_WAITING || state == LINK_DONE) {
        sendSquad(*this, resendSeq++);
        return;
      }
      bool answer = (state == LINK_LISTENING);   // they spoke first
      state = LINK_SQUADS;                       // before sending, see start()
      if (answer) sendHello(*this);
      sendSquad(*this, resendSeq++);             // nothing to negotiate
      return;
    }
    case LM_SQUAD: {
      if (n < 1 + sizeof(LinkMon)) return;
      uint8_t idx = body[0];
      if (idx >= TRAINER_TEAM_MAX) return;
      memcpy(&theirs[idx], body + 1, sizeof(LinkMon));
      theirsGot |= (uint16_t)1 << idx;
      if (idx + 1 > theirsN) theirsN = idx + 1;
      // Only ready once the WHOLE squad is here. A lost SQUAD packet used to
      // leave a gap that read back as a real creature with dex 0.
      if (mineN && theirsExpected) {
        uint16_t want = (uint16_t)((1u << theirsExpected) - 1);
        if ((theirsGot & want) == want) {
          theirsN = theirsExpected;
          state = LINK_READY;
          txLive = false;
        }
      }
      return;
    }
    case LM_ACT:
      if (!isHost || n < 2) return;    // only the host acts on an action
      // A resend of a turn already resolved is not a new choice. Without this
      // the retransmissions that make the link reliable would themselves
      // desync it -- every repeat would spend another turn.
      if (body[0] != turn) return;
      if (!body[1]) return;            // 0 is not an action, it means silence
      pendingAct = body[1];
      return;
    case LM_RESULT: {
      if (isHost || n < 1) return;     // the host never receives results
      uint8_t t = body[0];
      // Absolute state, so a result from a LATER turn is still safe to apply:
      // we simply missed one, and skip its narration. An earlier one is a
      // duplicate and is dropped.
      if ((uint8_t)(t - turn) > 128) return;
      resultN = (uint8_t)(n - 1);
      if (resultN) memcpy(result, body + 1, resultN);
      resultNew = true;
      turn = (uint8_t)(t + 1);
      txLive = false;                  // our action got through
      state = LINK_READY;              // and we may choose again
      return;
    }
    case LM_END:
      if (n < 1) return;
      youWon = isHost ? (body[0] != 0) : (body[0] == 0);
      txLive = false;
      state = LINK_DONE;
      return;
    case LM_BYE:
      txLive = false;
      state = LINK_LOST;
      return;
    case LM_REMATCH:
      if (!theirsN || !mineN) return;
      rearm();
      return;
    default:
      return;                          // unknown type: ignore, do not desync
  }
}
