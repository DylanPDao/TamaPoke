#include "sdmon.h"
#include "noart.h"
#include "pin_config.h"
#include "pet.h"   // gRegionArt, REGIONS -- the mask this narrows
#include <FS.h>
#include <SD_MMC.h>
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"

bool sdReady = false;
bool sdNeedsFormat = false;
bool sdDirty = false;
bool sdArtDirty = false;
SdThumbs thumbs;

bool PmdMon::load(int16_t dexNum, bool shiny) {
  // int16_t, NOT uint8_t. The dex reached 386 and this did not follow, so
  // everything from 256 up wrapped into Kanto: MARSHTOMP (258) opened
  // p002.bin and drew an IVYSAUR. Same trap that caught DexEntry::evolvesTo
  // and TrainerMon::dex when the expansion landed.
  if (dexNum < 1 || dexNum > 999) return false;
  unload();
  if (!sdReady) return false;

  char path[28];
  snprintf(path, sizeof(path), "/mons/p%s%03u.bin", shiny ? "s" : "", (unsigned)dexNum);
  File f = SD_MMC.open(path, FILE_READ);
  if (!f && shiny) {  // sin shiny PMD: usa el normal
    snprintf(path, sizeof(path), "/mons/p%03u.bin", (unsigned)dexNum);
    f = SD_MMC.open(path, FILE_READ);
  }
  if (!f) return false;

  uint32_t size = f.size();
  if (size < 7 || size > 3UL * 1024 * 1024) { f.close(); return false; }
  blob = (uint8_t *)ps_malloc(size);
  if (!blob || f.read(blob, size) != size || memcmp(blob, "TPK2", 4) != 0) {
    if (blob) { free(blob); blob = nullptr; }
    f.close();
    return false;
  }
  f.close();

  dex = dexNum;                  // what is actually in here, for the tests
  uint8_t nActs = blob[4];
  memcpy(&palCount, blob + 5, 2);
  if (palCount > 256 || (uint32_t)7 + palCount * 2 > size) { unload(); return false; }
  memcpy(pal, blob + 7, palCount * 2);

  const uint8_t *p = blob + 7 + palCount * 2;
  const uint8_t *end = blob + size;
  for (uint8_t i = 0; i < nActs && p + 4 <= end; i++) {
    uint8_t id = p[0], w = p[1], h = p[2], nf = p[3];
    p += 4;
    if (id >= PMD_NACTS || nf > 24) { unload(); return false; }
    // valida que ms[] y los datos del frame caben en el blob (archivo truncado)
    uint32_t bytes = (uint32_t)nf * 2 + (uint32_t)w * h * nf;
    if (w == 0 || h == 0 || nf == 0 || p + bytes > end) { unload(); return false; }
    PmdAct &a = acts[id];
    a.w = w;
    a.h = h;
    a.frames = nf;
    for (uint8_t k = 0; k < nf; k++) {
      a.ms[k] = p[0] | (p[1] << 8);
      p += 2;
    }
    if (!pmdActTimed(a)) { unload(); return false; }   // every frame 0 ms: corrupt
    a.data = p;
    p += (uint32_t)w * h * nf;
    // fila mas baja con contenido en cualquier frame: anclar por los pies
    uint8_t base = 1;
    for (uint8_t f = 0; f < nf; f++) {
      const uint8_t *fr = a.data + (uint32_t)f * w * h;
      for (int r = h - 1; r >= 0; r--) {
        bool any = false;
        for (int c = 0; c < w && !any; c++)
          if (fr[r * w + c] != 0xFF) any = true;
        if (any) { if (r + 1 > base) base = r + 1; break; }
      }
    }
    a.base = base;
  }
  loaded = true;
  Serial.printf("cargado %s (%u KB)\n", path, size / 1024);
  return true;
}

void PmdMon::unload() {
  if (blob) {
    free(blob);
    blob = nullptr;
  }
  for (auto &a : acts) {
    a.w = a.h = a.frames = a.base = 0;
    a.data = nullptr;
  }
  loaded = false;
}

bool SdThumbs::load() {
  if (!sdReady) return false;
  File f = SD_MMC.open("/mons/thumbs.bin", FILE_READ);
  if (!f) {
    Serial.println("sin thumbs.bin (galeria sin miniaturas)");
    return false;
  }
  // Loaded again after the card is formatted, so drop any previous image first.
  if (data) { free(data); data = nullptr; }
  loaded = false;
  count = 0;
  size = f.size();
  // The size is checked BEFORE the magic is compared: an empty or three-byte
  // file (an upload that died at once) made memcmp read past the buffer.
  if (size < 6) data = nullptr;
  else data = (uint8_t *)ps_malloc(size);
  if (!data || f.read(data, size) != size || !sdThumbsHeader(data, size, &count)) {
    Serial.println("thumbs.bin invalido");
    if (data) { free(data); data = nullptr; }
    size = 0;
    count = 0;
    f.close();
    return false;
  }
  f.close();
  loaded = true;
  Serial.printf("miniaturas cargadas: %u (%u KB)\n", count, size / 1024);
  return true;
}

const uint8_t *SdThumbs::get(int16_t dex) const {
  if (!loaded) return nullptr;
  return sdThumbAt(data, size, count, dex);   // nullptr past the end of a cut file
}

// Which regions actually have their sprite pack on the card.
//
// Three probes per region, not one: the realistic failure is a half-finished
// copy of a 100 MB pack, and a single probe would call that region present.
// Start, middle and end, so a copy that stopped partway fails the check.
//
// This NARROWS gRegionArt, which starts as everything. A board with no SD is
// therefore untouched and keeps today's behaviour, which is the documented
// requirement -- an empty game would be a worse answer than a graceful one.
void sdScanRegionArt(bool verbose) {
  if (!sdReady) return;                 // no card: leave every region enabled
  uint16_t mask = 0;
  for (uint8_t r = 0; r < REGION_COUNT; r++) {
    if (r == REGION_ALL) continue;      // derived from the others, never probed
    const RegionInfo &rg = REGIONS[r];
    const int16_t probe[3] = { sdRegionProbe(r, 0), sdRegionProbe(r, 1),
                               sdRegionProbe(r, 2) };
    bool all = true;
    for (int i = 0; i < 3 && all; i++) {
      char path[28];
      snprintf(path, sizeof(path), "/mons/p%03u.bin", (unsigned)probe[i]);
      File f = SD_MMC.open(path, FILE_READ);
      if (!f) all = false; else f.close();
    }
    if (all) mask |= (uint16_t)(1u << r);
    if (verbose)
      Serial.printf("art: %-6s %s\n", rg.name, all ? "si" : "NO (falta el pack)");
  }
  gRegionArt = mask;
}

// What follows a successful mount, in ONE place: sdBegin() and sdFormat() both
// end here, so a freshly formatted card is set up exactly like one found at boot.
static void sdAfterMount() {
  Serial.printf("SD montada: %llu MB\n", SD_MMC.cardSize() / (1024ULL * 1024ULL));
  SD_MMC.mkdir("/mons");
  sdScanRegionArt();
}

// Telling "no card" from "a card whose filesystem will not mount".
//
// SD_MMC.begin() returns false for both and does not expose why. The ESP-IDF
// call underneath it does: esp_vfs_fat_sdmmc_mount() returns ESP_FAIL only
// AFTER sdmmc_card_init() succeeded -- the card answered, then f_mount() failed
// -- while an empty slot fails card init with ESP_ERR_TIMEOUT /
// ESP_ERR_INVALID_RESPONSE / ESP_ERR_NOT_FOUND and never reaches the FS. So when
// SD_MMC.begin() fails, this repeats the mount through the IDF directly, on its
// own mount point and with the SAME host and slot SD_MMC builds (slot 1, 1-bit,
// pins through the GPIO matrix), reads the code, and unmounts again. Everything
// else keeps using SD_MMC exactly as before; this exists only to ask the card.
//
// With `format` set it is also how the card gets formatted: the IDF formats
// only when f_mount fails again, so a card that turns out to mount after all
// is left untouched. That path is used instead of SD_MMC.begin(.., true)
// because SD_MMC hardcodes allocation_unit_size 0 -- one-sector clusters --
// which on a 128 GB card means a FAT of about a gigabyte to write. 32 KB
// clusters are the usual FAT32 choice for large cards and format in seconds.
#define SD_PROBE_PATH "/sdprobe"
static esp_err_t sdProbe(bool format) {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_1;
  host.flags = SDMMC_HOST_FLAG_1BIT;
  host.max_freq_khz = BOARD_MAX_SDMMC_FREQ;   // SD_MMC.begin()'s own default
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk = (gpio_num_t)SDMMC_CLK;
  slot.cmd = (gpio_num_t)SDMMC_CMD;
  slot.d0 = (gpio_num_t)SDMMC_DATA;
  slot.d1 = slot.d2 = slot.d3 = GPIO_NUM_NC;
  slot.width = 1;
  esp_vfs_fat_sdmmc_mount_config_t cfg = {};
  cfg.format_if_mount_failed = format;
  cfg.max_files = 1;
  cfg.allocation_unit_size = 32 * 1024;
  sdmmc_card_t *card = nullptr;
  esp_err_t r = esp_vfs_fat_sdmmc_mount(SD_PROBE_PATH, &host, &slot, &cfg, &card);
  if (r == ESP_OK) esp_vfs_fat_sdcard_unmount(SD_PROBE_PATH, card);
  return r;
}

bool sdBegin() {
  SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
  sdNeedsFormat = false;
  // NEVER format here. This used to pass format_if_mount_failed = true, which
  // silently wiped any card it could not read -- an exFAT card, an NTFS one,
  // one with a glitch -- along with everything the player kept on it.
  sdReady = SD_MMC.begin("/sdcard", true /* modo 1-bit */, false /* never format unasked */);
  if (!sdReady) {
    esp_err_t r = sdProbe(false);
    if (r == ESP_OK) {
      // It mounted the second time (a slow card or a marginal contact): hand it
      // back to SD_MMC, which everything else reads through.
      sdReady = SD_MMC.begin("/sdcard", true, false);
    } else if (r == ESP_FAIL) {
      sdNeedsFormat = true;
    }
    if (!sdReady && !sdNeedsFormat) Serial.printf("SD: init 0x%x\n", (unsigned)r);
  }
  if (sdReady) {
    sdAfterMount();
  } else if (sdNeedsFormat) {
    Serial.println("SD present but unreadable: asking before formatting it");
  } else {
    Serial.println("SD no detectada (el juego usa los sprites de flash)");
  }
  return sdReady;
}

bool sdFormat() {
  if (sdReady) return true;          // mounted already: there is nothing to erase
  esp_err_t r = sdProbe(true);
  if (r != ESP_OK) {
    Serial.printf("SD: format failed (0x%x)\n", (unsigned)r);
    return false;
  }
  sdReady = SD_MMC.begin("/sdcard", true, false);
  if (!sdReady) {
    Serial.println("SD: formatted but it does not mount");
    return false;
  }
  sdNeedsFormat = false;
  Serial.println("SD formatted");
  sdAfterMount();
  return true;
}

bool SdMon::load(int16_t dexNum, bool shiny) {
  if (dexNum < 1 || dexNum > 999) return false;
  unload();
  if (!sdReady) return false;

  char path[24];
  snprintf(path, sizeof(path), "/mons/%s%03u.bin", shiny ? "s" : "", (unsigned)dexNum);
  File f = SD_MMC.open(path, FILE_READ);
  if (!f && shiny) {  // sin variante shiny: usa la normal
    snprintf(path, sizeof(path), "/mons/%03u.bin", (unsigned)dexNum);
    f = SD_MMC.open(path, FILE_READ);
  }
  if (!f) {
    Serial.printf("no existe %s\n", path);
    return false;
  }

  char magic[4];
  uint16_t header[4];
  if (f.read((uint8_t *)magic, 4) != 4 || memcmp(magic, "TPK1", 4) != 0 ||
      f.read((uint8_t *)header, 8) != 8) {
    f.close();
    return false;
  }
  w = header[0];
  h = header[1];
  frames = header[2];
  frameMs = header[3];
  // acota dimensiones: evita size desbordado o absurdo con archivo corrupto
  if (f.read((uint8_t *)&palCount, 2) != 2 || palCount > 256 ||
      w == 0 || w > 256 || h == 0 || h > 256 || frames == 0 || frames > 64) {
    f.close();
    return false;
  }
  if (f.read((uint8_t *)pal, palCount * 2) != palCount * 2) {
    f.close();
    return false;
  }

  uint32_t size = (uint32_t)w * h * frames;
  data = (uint8_t *)ps_malloc(size);
  if (!data) {
    Serial.println("sin PSRAM para el sprite");
    f.close();
    return false;
  }
  uint32_t got = f.read(data, size);
  f.close();
  if (got != size) {
    Serial.printf("%s truncado (%u de %u)\n", path, got, size);
    unload();
    return false;
  }

  // zoom entero para que el bicho mida ~200 px de alto en pantalla
  scale = 200 / h;
  if (scale < 2) scale = 2;
  if (scale > 5) scale = 5;

  Serial.printf("cargado %s: %ux%u x%u frames @%ums, escala %u\n",
                path, w, h, frames, frameMs, scale);
  loaded = true;
  return true;
}

void SdMon::unload() {
  if (data) {
    free(data);
    data = nullptr;
  }
  loaded = false;
}

// ---------------------------------------------------------------------------
// Protocolo de carga por USB (para llenar la SD sin sacarla de la placa):
//   PUT <ruta> <bytes>\n  + datos crudos   -> "OK" ... "DONE"
//   LS\n                                   -> listado de /mons
// Usar con tools/send_sd.py
// ---------------------------------------------------------------------------

bool sdSerialCommand(const String &line) {
  if (line.startsWith("PUT ")) {
    int sp = line.lastIndexOf(' ');
    String path = line.substring(4, sp);
    uint32_t size = line.substring(sp + 1).toInt();
    if (!sdReady || size == 0 || size > 4 * 1024 * 1024) {
      Serial.println("ERR");
      return true;
    }
    if (!path.startsWith("/")) path = "/" + path;
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) {
      Serial.println("ERR");
      return true;
    }
    Serial.println("OK");
    static uint8_t buf[2048];
    uint32_t remaining = size;
    Serial.setTimeout(5000);
    while (remaining > 0) {
      size_t want = remaining > sizeof(buf) ? sizeof(buf) : remaining;
      size_t n = Serial.readBytes(buf, want);
      if (n == 0) break;  // timeout
      f.write(buf, n);
      remaining -= n;
      Serial.println("#");  // ack: listo para el siguiente bloque
    }
    f.close();
    Serial.setTimeout(1000);
    // What a finished file is worth reloading -- the pet's sprite, the region
    // scan -- is decided by the sketch, which knows the creature on screen.
    // Nothing is reloaded here: this runs between the last data block and the
    // DONE the host is waiting on. See sdPutLanded().
    if (remaining == 0) sdPutLanded(path.c_str());
    Serial.println(remaining == 0 ? "DONE" : "ERR");
    return true;
  } else if (line == "LS") {
    File dir = SD_MMC.open("/mons");
    if (dir) {
      File e;
      while ((e = dir.openNextFile())) {
        Serial.printf("%s %u\n", e.name(), (uint32_t)e.size());
        e.close();
      }
      dir.close();
    }
    Serial.println("DONE");
    return true;
  }
  return false;
}
