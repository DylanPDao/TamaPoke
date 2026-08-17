#pragma once
#include <stdint.h>
#include <stddef.h>

// A whole-save backup, over the serial console.
//
// A run is weeks of real time living in one NVS partition, and there is now a
// lot in it: the creature, the party, the box, both badge ladders, the Pokedex,
// the streak, the trainer name. Nothing could get it back out.
//
// The backup is KEY-DRIVEN, not a struct of fields. SAVE_FIELDS lists every key
// the firmware stores along with its type, and both directions walk that one
// table through the ordinary Preferences API -- which means the identical code
// runs on the board and in the emulator, so all of it is testable here. An
// explicit struct would have been a second description of the save that drifts
// the moment somebody adds a field; `save_test` compares the table against the
// keys actually present after a save, so forgetting to add one fails a test
// rather than silently dropping it from everyone's backup.

#define SAVE_MAGIC0 'T'
#define SAVE_MAGIC1 'K'
#define SAVE_MAGIC2 'P'
#define SAVE_MAGIC3 'S'
#define SAVE_VERSION 1
#define SAVE_HDR 8

enum SaveKind : uint8_t {
  SK_U8 = 1, SK_I8, SK_BOOL, SK_U16, SK_I16, SK_U32, SK_BYTES, SK_STR,
};

struct SaveField {
  const char *key;
  uint8_t kind;
};
extern const SaveField SAVE_FIELDS[];
extern const uint16_t SAVE_FIELD_COUNT;

// Serialises the live save. Returns the number of bytes written, or 0 if it
// would not fit in cap.
size_t saveExport(uint8_t *out, size_t cap);

// Restores one. VALIDATES THE WHOLE BLOB FIRST and only then touches NVS: a
// half-applied restore over a good save would be worse than no backup at all.
// Returns false and changes nothing if the blob is not ours, is the wrong
// version, or fails its checksum. The caller must reload afterwards.
bool saveImport(const uint8_t *in, size_t n);

// Roughly what saveExport needs, for sizing a buffer.
size_t saveExportSize();
