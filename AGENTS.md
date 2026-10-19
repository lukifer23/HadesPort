# Rules for contributors and coding agents

Start with `HANDOFF.md` (how to resume) and `STATUS.md` (live state). If a private
`.local/private/LOCAL.md` exists, read it too (device identifiers and local notes; never commit).

## Goal and architecture

A native ARM64 Android engine for the original Hades (2020) that runs the game's own Lua
scripts and assets from the user's legally owned PC copy. Compatibility layers (Wine,
GameNative, CPU translation, launchers) are rejected. No ads, accounts or network
permission. Don't call the prototype a complete game; progress is proven on a device, not
by host or synthetic tests.

## Repository hygiene (public repo)

- Work on `main`; commit small, coherent changes and push to `origin main`.
- **Never commit game files or anything derived from them**: originals, exported packs,
  decompiled listings, captures and reports all live under the ignored `.local/`.
  `native/analysis/*-contract.json` may record recovered facts (addresses, layouts,
  constants, field names) but not copied engine code or game content.
- No personal data: no absolute home paths, emails, device serials or display ids. Use
  `HADES_DEVICE_SERIAL` / `HADES_DISPLAY_ID`, `ANDROID_HOME`, `JAVA_HOME`, `REA_HOME`.
- Before every commit: `git status` and check the diff for the above.

## Engineering rules

- Reconstruct behaviour from `EngineWin64s.dll` + its matching PDB. Cite VA/RVA provenance
  in code comments; label approximations explicitly; fail loudly
  (`SCRIPT_STAGE_BLOCKED … detail=…`) on anything not rebuilt. No mocks or stand-ins in `native/`.
- Fix the class of problem (derived coverage over hand-picked lists).
- Settings mirror the PC game's real `ConfigOptions`; don't add toggles for features that
  aren't rebuilt.
- Use REA via `scripts/rea.py` (sibling checkout `../rea` or `REA_HOME`); run one
  decompile at a time; don't modify the REA checkout. Close native sessions you open.
- Asset exporters write fresh versioned dirs (`.local/native-assets/<name>-vN`);
  `scripts/build_native.py` defaults select the current versions.

## Testing

Sparing and deliberate: fast host builds (RelWithDebInfo, `-DHADES_SANITIZE=OFF`) in your
own temporary build dir, batch fixes, the shortest replay that reaches the change, one
sanitized (ASan/UBSan) run at the end. Delete temporary build dirs and scratch copies.

## Devices

Use only the designated device, always `adb -s "$HADES_DEVICE_SERIAL"`. If it is someone's
everyday phone: never change phone-wide airplane mode, Wi-Fi or mobile data, and don't
capture other apps' screens. One coordinator builds and installs the APK and records each
device result in `STATUS.md`.
