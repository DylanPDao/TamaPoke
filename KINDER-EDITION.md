# TamaPoke – Kinder-Edition (Fork-Notizen)

Stand: 11.09.2026 · Basis: DylanPDao/TamaPoke `main` (v3.11) · Branch `feat/kids-mode` · FW `3.11-kids1`

Alles hier ist im Desktop-Emulator gebaut und getestet (38 Testsuiten, 37 grün;
`sprite_test` braucht nur das Sprite-Paket auf der Platte). **Auf dem Board lief
noch nichts** – siehe „Auf dem Board prüfen“.

## Was neu ist

### Kinder-Modus (`KINDER`-Schalter in den Einstellungen, standardmäßig AN)
- Keine Pflegefehler, keine verzögerte Entwicklung, kein Weglaufen, kein verfluchtes Ei.
  Die Buttons dafür erscheinen nicht, die „Patzer“-Zeile auf der Karte ist weg.
- Untergrenze 50 % für alle vier Werte – live, im Schlaf, beim Offline-Nachrechnen.
  Höchstens ein Häufchen auf dem Bildschirm.
- Werte sinken nur, wenn gespielt wird (Bildschirm an + Berührung in den letzten 90 s).
  Abgelegt döst das Pokémon: nichts sinkt, Energie kommt zurück, es wächst weiter.
- Statusmeldungen sind Vorschläge („Ein Snack waere schoen“, „Lust zu spielen?“).
- Zur Ruhe setzen ist immer kostenlos und landet immer im Team.
- Verlorener Kampf: kleiner Trainingsbonus + „Gut gekaempft! Nochmal?“.
- Beim Einschalten des Modus (auch beim Laden eines alten Spielstands) werden
  Altlasten vergeben und die Balken auf 50 % gehoben.
- Konstanten in `pet.h`: `KIDS_FLOOR`, `KIDS_POOP_MAX`, `KIDS_POOP_PCT`, `KIDS_MODE_DEFAULT`.

### Bootscreen und Begrüßung
- Eigenes Motiv (Nachthimmel, Ei, Titel), 1,8 s gehalten: `drawSplash()` in `TamaPoke.ino`.
  Für ein eigenes Bild einfach den Funktionskörper ersetzen (466×466, Radius 231).
- „Guten Morgen/Hallo/Guten Abend, Sophie!“ + „X freut sich!“ nach dem Start und
  wenn das Gerät nach >20 min wieder angefasst wird. Name = Trainerpass; solange
  keiner gesetzt ist: `KIDS_DEFAULT_NAME` in `pet.h` (steht auf „Sophie“).
- Deutsch ist Standardsprache (`LANG_DEFAULT` in `i18n.h`).

### Hoopa + Halloween
- **Deko** (Kürbisse, Fledermäuse, „Frohes Halloween!“): jedes Jahr 24.10.–7.11.
  (`HALLOWEEN_FROM_*`/`HALLOWEEN_TO_*` in `pet.h`).
- **Hoopa**: schaltet sich einmalig frei, sobald die Uhr ≥ 31.10.2026 00:00 zeigt
  (`HOOPA_UNLOCK_EPOCH`), und bleibt dann. Banner „HOOPA ist da!“, goldener Ring.
  Wartet gerade ein Ei, wird es sofort das Hoopa-Ei (Region-Pille ausgeblendet);
  lebt gerade ein Pokémon, wartet der Ring oben rechts und das *nächste* Ei ist Hoopa.
- **Beide Formen**: ab Level 3 und etwas Bindung erscheint der goldene Button
  „ENTFESSELN“ (bzw. „BINDEN“) an der Stelle des Entwicklungs-Buttons. Entfesselt
  kämpft Hoopa als Psycho/Unlicht mit den echten Werten (160 Angriff, 170 Sp.-Angriff);
  Dex-Nummer, Name und Pokédex bleiben 720. Umschalten geht beliebig oft.
- Sprites: `python3 tools/pack_pmd.py hoopa` erzeugt `p720.bin`, `ps720.bin`,
  `pu720.bin`, `pus720.bin` (SpriteCollab hat beide Formen komplett). Fehlt die
  Unbound-Datei, fällt die Firmware auf die gebundene Form zurück.
- Konsole (115200): `HOOPA` schaltet das Event sofort frei, `FORM` wechselt die Form.

## Bauen und flashen
1. Repo auf GitHub pushen, unter *Settings → Pages* „GitHub Actions“ als Quelle wählen.
   `.github/workflows/build.yml` baut die Firmware und veröffentlicht den Web-Installer
   unter `https://<user>.github.io/TamaPoke/web/`. (Der Workflow ist ungetestet –
   diese Umgebung hat keinen ESP32-Toolchain-Zugang; bei Fehlern das Log ansehen,
   meist ist eine Library-Version zu pinnen.)
2. Oder lokal wie bisher: `bash tools/flash.sh --monitor` bzw. Arduino IDE
   (ESP32S3 Dev Module, 16 MB, OPI PSRAM, Partition `app3M_fat9M_16MB`, USB CDC on).
3. Sprites: `python3 tools/pack_pmd.py kanto hoopa` (Kanto + beide Hoopa-Formen)
   und `python3 tools/send_sd.py`, oder die Pakete über den Installer laden.

## Auf dem Board prüfen (Checkliste)
- Bootscreen sichtbar, danach Begrüßung mit Name.
- Einstellungen: `KINDER`-Pille schaltet und bleibt nach Neustart erhalten.
- Gerät einen Tag liegen lassen: Balken nicht unter 50 %, kein Button „fühlt sich verlassen“.
- Konsole `HOOPA`: Banner, Ring, Ei schlüpft als Hoopa; `FORM` bzw. Button ab Lv. 3.
- Uhr auf 31.10. stellen: Kürbisse/Fledermäuse, Event feuert von selbst.

## Emulator
`bash tools/emu/build.sh && tools/emu/tamapoke-emu --scale 2` – neue Screenshots:
`--shot splash|greet|hoopaegg|hoopa|hoopaform`. Tests: `bash tools/emu/tests/run.sh`
(`kids_test`, `hoopa_test` sind neu).
