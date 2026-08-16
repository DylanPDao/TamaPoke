#include "linknow.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ESP-NOW transport. See linknow.h: this is the one piece that cannot be
// tested without two boards.
//
// Everything is broadcast rather than paired to a MAC. Pairing on a screen with
// no keyboard would mean showing and typing MAC addresses, and the protocol
// already tolerates strangers: a wrong LINK_PROTO is refused, unknown types are
// ignored, and a second peer's squad simply overwrites an unfinished handshake
// rather than corrupting a running battle. If two pairs of devices are ever in
// one room, that is when this needs real addressing.

static Link *gLink = nullptr;
static bool gUp = false;
static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  (void)info;
  if (!gLink || len <= 0 || len > 250) return;
  // Straight into the protocol. onPacket does its own validation, and it is
  // deliberately re-entrancy safe (state before send) because this callback
  // runs on the WiFi task, not the main loop.
  gLink->onPacket(data, (uint8_t)len);
}

static void nowSend(void *, const uint8_t *buf, uint8_t len) {
  if (!gUp) return;
  esp_now_send(BROADCAST, buf, len);
}

bool linkNowBegin(Link *l) {
  if (gUp) return true;
  gLink = l;
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
  gUp = true;
  l->send = nowSend;
  l->ctx = nullptr;
  Serial.println("ESP-NOW listo");
  return true;
}

void linkNowEnd() {
  if (!gUp) return;
  esp_now_unregister_recv_cb();
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);      // the radio is not free: turn it off when done
  gUp = false;
  gLink = nullptr;
}

bool linkNowUp() { return gUp; }
