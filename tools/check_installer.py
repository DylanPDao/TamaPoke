#!/usr/bin/env python3
"""Proves the web installer cannot erase a player's pet.

The save lives in the NVS partition, and TWO separate things were destroying it.
Both are checked here because fixing either one alone still loses the save.

1. WHAT IS WRITTEN. web/firmware/tamapoke.bin was a single image starting at
   offset 0, and esptool's merge-bin pads the gaps, so it wrote 0xFF straight
   over NVS at 0x9000. The manifest must ship the four parts at their own
   offsets instead, leaving 0x9000..0xE000 alone.

2. WHETHER THE CHIP IS ERASED FIRST, which no arrangement of parts can survive.
   In esp-web-tools' no-Improv path -- ours, this firmware speaks no Improv --
   the Install button is:

       new_install_prompt_erase ? state = "ASK_ERASE" : _startInstall(true)

   The name is the exact opposite of what it does. FALSE means "do not ask,
   just erase", and _startInstall(true) calls eraseFlash(), a whole-chip erase.
   TRUE shows a screen with an "Erase device" checkbox that starts UNCHECKED.
   It was set false deliberately, reading the name at face value, and that
   destroyed two real saves.

The partition table is read out of the build itself rather than hardcoded, so a
partition scheme change moves the check with it.

    python3 tools/check_installer.py            # checks web/manifest.json

Run by build_web.sh on every build, so neither cause can come back.
"""
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")


def partitions(path):
    """The partition table as (label, offset, size), read from the .bin."""
    out = []
    with open(path, "rb") as f:
        blob = f.read()
    for i in range(0, len(blob), 32):
        e = blob[i:i + 32]
        if len(e) < 32 or e[:2] != b"\xAA\x50":
            break
        off, size = struct.unpack("<II", e[4:12])
        label = e[12:28].rstrip(b"\x00").decode("ascii", "replace")
        out.append((label, off, size))
    return out


def main():
    manifest_path = os.path.join(WEB, "manifest.json")
    manifest = json.load(open(manifest_path))

    parts = []
    for build in manifest.get("builds", []):
        for p in build.get("parts", []):
            path = os.path.join(WEB, p["path"])
            if not os.path.exists(path):
                print("MISSING: %s (named by the manifest)" % p["path"])
                return 1
            parts.append((p["path"], int(p["offset"]), os.path.getsize(path)))

    if not parts:
        print("the manifest lists no parts at all")
        return 1

    # The partition table is one of the parts we ship, so the layout is read
    # from the same bytes the board will be given.
    table = None
    for path, off, _ in parts:
        if off == 0x8000:
            table = partitions(os.path.join(WEB, path))
    if table is None:
        # a single merged image carries the table inside it at 0x8000
        for path, off, size in parts:
            if off == 0 and size > 0x9000:
                blob = open(os.path.join(WEB, path), "rb").read()
                tmp = os.path.join(WEB, ".ptable.tmp")
                open(tmp, "wb").write(blob[0x8000:0x9000])
                table = partitions(tmp)
                os.remove(tmp)
    if not table:
        print("could not find a partition table in anything the manifest ships")
        return 1

    keep = [(l, o, s) for (l, o, s) in table if l in ("nvs", "ffat")]
    print("what the installer writes:")
    for path, off, size in sorted(parts, key=lambda p: p[1]):
        print("  0x%06X..0x%06X  %s" % (off, off + size, path))
    print("what must survive it:")
    for label, off, size in keep:
        print("  0x%06X..0x%06X  %s" % (off, off + size, label))

    bad = 0
    for label, poff, psize in keep:
        for path, off, size in parts:
            if off < poff + psize and poff < off + size:
                print("\nFAIL: %s (0x%06X..0x%06X) is written over by %s "
                      "(0x%06X..0x%06X)" % (label, poff, poff + psize, path,
                                            off, off + size))
                if label == "nvs":
                    print("      That is the save. Every update would destroy "
                          "the player's pet.")
                bad += 1

    # The name lies. In esp-web-tools' no-Improv path (ours), the Install button
    # is `new_install_prompt_erase ? ASK_ERASE : _startInstall(true)` -- so
    # FALSE means erase the whole chip without asking, and TRUE means show a
    # checkbox that defaults to not erasing. Setting it false to "stop it
    # erasing" is what destroyed two real saves.
    if not manifest.get("new_install_prompt_erase"):
        print("\nFAIL: new_install_prompt_erase is false, which in the "
              "no-Improv path means a SILENT full chip erase -- it must be "
              "true, which offers an unchecked 'Erase device' box instead")
        bad += 1

    print("\n%s" % ("FAILURES" if bad else "the save is out of the blast radius"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
