#pragma once
#include "link.h"

// The radio half of the peer-to-peer link, kept apart from link.cpp on purpose:
// link.cpp is pure protocol and compiles anywhere, including the desktop
// emulator, so it can be tested. Everything that touches ESP-NOW lives here and
// is stubbed out for the emulator, which has no radio at all.
//
// UNTESTED. Nothing in this file has run on hardware -- it needs two boards.
// Treat the first bring-up as debugging, not as verification.

bool linkNowBegin(Link *l);   // brings up WiFi + ESP-NOW; false if either fails
void linkNowEnd();            // frees the radio again -- it costs real current
bool linkNowUp();

// Hands received packets to the Link, on the MAIN loop. The ESP-NOW callback
// runs on the WiFi task and must not touch protocol state directly, so it only
// parks bytes in a ring that this drains. Call once a frame, alongside tick().
void linkNowPoll();

// Diagnostics for the first bring-up, since none of this has ever run:
// how many packets arrived, how many were sent, how many the radio reported
// as undelivered, and how many were dropped for coming from another device.
struct LinkNowStats {
  uint32_t rx, tx, txFail, foreign, overflow;
};
const LinkNowStats &linkNowStats();
