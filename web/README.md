# TamaPoke web installer

A one-click page that flashes the firmware and loads the sprites from the browser
(Chrome/Edge), with no Arduino or drivers. It uses
[ESP Web Tools](https://esphome.github.io/esp-web-tools/) to flash and **Web
Serial** to push the sprites to the SD with the firmware's `PUT` protocol (the
same one as `tools/send_sd.py`).

## Contents

- `index.html` — the page (flashing + sprite loader).
- `manifest.json` — ESP Web Tools config (points at the firmware).
- `firmware/tamapoke.bin` — combined firmware, flashable at `0x0`.
- `sprites-kanto.pak`, `sprites-johto.pak`, `sprites-hoenn.pak` — the sprites bundled
  (TPAK) **one file per region**, so the page sends a region in one click.
  **Generated** by `tools/pack_bundle.py` (gitignored by default — see
  *Hosting the sprites* below).

## Regenerate

After changing the firmware or the sprites:

```bash
bash tools/build_web.sh        # recompiles -> firmware/tamapoke.bin AND rebuilds the region .paks
```

## Test locally

Web Serial and ESP Web Tools need a **secure context**: `https://` or
`http://localhost`. To test:

```bash
cd web && python3 -m http.server 8000
# open http://localhost:8000 in Chrome/Edge
```

## End-user flow

1. **Install TamaPoke** → flashes the firmware (pick the USB port; tick "Erase
   device" for a fresh board).
2. **Connect board** + a region button → downloads that region's `.pak` and copies it to
   the microSD over USB (progress bar, ~8–10 min). Close the step-1 install tab
   first: only one program can use the port at a time.
3. Restart (PWR button) → choose your starter and play.

A hidden "pick them manually" option lets advanced users send their own `.bin`.

## Hosting the sprites

Each region's `.pak` is ~27–40 MB and **gitignored** so it doesn't bloat the repo.

## Publishing the sprite bundles

The `.pak` files are **not committed** — each is ~40 MB and a firmware repo should
not make every clone carry 100+ MB of sprites forever. They are attached to a
**GitHub Release** instead, which lives outside the git history and serves
`Access-Control-Allow-Origin`, so the installer page can `fetch()` them.

```bash
bash tools/build_web.sh                  # rebuilds firmware + the region .paks
gh release create v2.4 web/sprites-*.pak --title "TamaPoke v2.4" --notes "Sprite bundles"
# later versions: gh release upload v2.5 web/sprites-*.pak
```

The page tries **same-origin first**, then the release. So for local testing you can
just leave the `.pak` files in `web/` and run `python3 -m http.server` — no edit
needed. If the buttons report "could not download", the usual cause is that no
release exists yet.

`PAK_RELEASE` at the top of the script block in `index.html` points at the repo;
change it if you fork.

**Why one file per region and not one big one:** all three together come to about
100 MB, which is exactly GitHub's hard per-file limit — a single bundle would be
uncommittable. Splitting also means most people can take Kanto (~40 MB) and stop,
and add a region later without re-sending what is already on the card. To make
the one-click sprite loader work on a real deployment, pick one:

- **Commit it** — add `web/sprites.pak` to git and serve it from Pages. Simple,
  but doubles the repo's sprite size.
- **Release asset** — attach `sprites.pak` to a GitHub Release and change the
  `fetch('sprites.pak')` URL in `index.html` to the release URL (keeps the repo
  small; watch out for CORS on the asset host).

All sprites are from PMD SpriteCollab, CC BY-NC (non-commercial sharing with
attribution is allowed); see [`../CREDITS.md`](../CREDITS.md).

## Deploy (GitHub Pages)

1. Repo settings → Pages → serve from `main`, folder `/web` (or move `web/` to
   `docs/`). Pages gives HTTPS automatically.
2. URL ends up at `https://<user>.github.io/<repo>/`.

> **Pages on private repos** needs GitHub Pro/Team. If you make the repo
> **public** to use Pages for free, decide about the sprites first (see above and
> CREDITS).

## Limitations

- Desktop **Chrome/Edge** only (Web Serial isn't in Firefox/Safari).
