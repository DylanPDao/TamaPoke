#include "linknow.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

// ESP-NOW transport. See linknow.h: this is the one piece that cannot be
// tested without two boards.
//
// Pairing starts as a broadcast, because pairing on a screen with no keyboard
// would otherwise mean showing and typing MAC addresses. But it does not STAY a
// broadcast: the first peer we accept a hello from is locked in by its MAC, and
// from then on every packet goes to that address alone and anything from any
// other device is dropped. That is what stops two pairs of players in one room
// from joining each other's battles, and it buys a real transmit-status
// callback as well -- a broadcast always reports success, so a unicast is the
// only way to learn that a frame did not land.

static Link *gLink = nullptr;
static bool gUp = false;
static bool gLocked = false;
static bool gPeerAdded = false;
static uint8_t gPeer[6];
static LinkNowStats gStats;
static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// A received packet is parked here and handled on the main loop. The callback
// runs on the WiFi task: touching Link from there would race the render loop
// that reads it, and calling back into the protocol would mean sending from
// inside the receive callback, which is exactly what ESP-IDF asks you not to do.
#define RING_SLOTS 8
#define RING_SLOT_BYTES (sizeof(LinkMon) + 16)
struct RingSlot { uint8_t len; uint8_t buf[RING_SLOT_BYTES]; };
static RingSlot gRing[RING_SLOTS];
static volatile uint8_t gHead = 0, gTail = 0;   // producer: ISR-ish; consumer: loop

static bool sameMac(const uint8_t *a, const uint8_t *b) {
  return memcmp(a, b, 6) == 0;
}

// Signature note: this core passes wifi_tx_info_t*, not the bare MAC that older
// ESP-NOW examples show.
static void onSent(const wifi_tx_info_t *info, esp_now_send_status_t st) {
  (void)info;
  if (st == ESP_NOW_SEND_SUCCESS) gStats.tx++;
  else gStats.txFail++;
}

static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!gUp || len <= 0 || (size_t)len > RING_SLOT_BYTES) return;
  // Once we have a peer, everyone else is somebody else's game.
  if (gLocked && info && info->src_addr && !sameMac(info->src_addr, gPeer)) {
    gStats.foreign++;
    return;
  }
  uint8_t h = gHead, next = (uint8_t)((h + 1) % RING_SLOTS);
  if (next == gTail) { gStats.overflow++; return; }   // full: drop, never block
  gRing[h].len = (uint8_t)len;
  memcpy(gRing[h].buf, data, len);
  // The peer is remembered here rather than in the poll, because the source
  // address only exists inside this callback.
  if (!gLocked && info && info->src_addr) {
    memcpy(gPeer, info->src_addr, 6);
    gLocked = true;
  }
  gHead = next;                 // published last: the slot is complete first
  gStats.rx++;
}

static void nowSend(void *, const uint8_t *buf, uint8_t len) {
  if (!gUp) return;
  esp_now_send(gLocked ? gPeer : BROADCAST, buf, len);
}

// Adding the locked peer is deferred to the main loop for the same reason the
// packets are: esp_now_add_peer() from inside the receive callback is asking
// for trouble.
static void lockPeer() {
  if (!gLocked || gPeerAdded) return;
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, gPeer, 6);
  p.channel = 1;
  p.encrypt = false;
  if (esp_now_add_peer(&p) == ESP_OK) {
    gPeerAdded = true;
    Serial.printf("ESP-NOW peer %02X:%02X:%02X:%02X:%02X:%02X\n",
                  gPeer[0], gPeer[1], gPeer[2], gPeer[3], gPeer[4], gPeer[5]);
  }
}

void linkNowPoll() {
  if (!gUp || !gLink) return;
  lockPeer();
  while (gTail != gHead) {
    RingSlot &s = gRing[gTail];
    gLink->onPacket(s.buf, s.len);      // safe here: we are on the main loop
    gTail = (uint8_t)((gTail + 1) % RING_SLOTS);
  }
}

bool linkNowBegin(Link *l) {
  if (gUp) return true;
  gLink = l;
  gLocked = false;
  gPeerAdded = false;
  gHead = gTail = 0;
  gStats = LinkNowStats();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  // A fixed channel, or two devices on different channels never hear each other
  // and it looks like a dead link rather than a mismatch.
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init fallo");
    WiFi.mode(WIFI_OFF);
    return false;
  }
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = 1;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("ESP-NOW peer fallo");
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
    return false;
  }
  // Our own MAC is the identity that settles a two-host tie, and it is already
  // unique per device -- there is nothing to generate or store.
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  l->id = (uint16_t)((mac[4] << 8) | mac[5]);
  gUp = true;
  l->send = nowSend;
  l->ctx = nullptr;
  Serial.printf("ESP-NOW listo, id %04X\n", l->id);
  return true;
}

void linkNowEnd() {
  if (!gUp) return;
  gUp = false;                  // before deinit, so a late callback does nothing
  esp_now_unregister_recv_cb();
  esp_now_unregister_send_cb();
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);          // the radio is not free: turn it off when done
  Serial.printf("ESP-NOW fin: rx %u tx %u fallos %u ajenos %u desborde %u\n",
                gStats.rx, gStats.tx, gStats.txFail, gStats.foreign,
                gStats.overflow);
  gLocked = false;
  gLink = nullptr;
}

bool linkNowUp() { return gUp; }
const LinkNowStats &linkNowStats() { return gStats; }
