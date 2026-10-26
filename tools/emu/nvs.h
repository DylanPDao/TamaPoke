#pragma once
// Stub of ESP-IDF's nvs.h, for the one call the sketch makes: HEALTH reports
// how many NVS entries are in use. The emulator's Preferences keeps a map, not
// flash pages, so there is nothing real to count -- it reports 0/0, and the
// figure only means something on a board.
#include <cstddef>
#ifndef ESP_OK
#define ESP_OK 0
#endif
typedef int esp_err_t;
typedef struct {
  size_t used_entries, free_entries, available_entries, total_entries, namespace_count;
} nvs_stats_t;
inline esp_err_t nvs_get_stats(const char *, nvs_stats_t *s) {
  *s = nvs_stats_t{0, 0, 0, 0, 0};
  return ESP_OK;
}
