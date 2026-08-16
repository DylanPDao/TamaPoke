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
