# CJ Heart Attack — ASI plugin for GTA San Andreas (PC, original 2005 release)

If CJ's Fat stat gets too high, there's now a small, configurable chance per
check that he keels over with a heart attack — a normal "wasted" death, the
same as any other cause of death in the game.

## Compatibility — READ THIS FIRST

This plugin pokes at the **original v1.0 `gta_sa.exe`** released in 2005, using
memory addresses that are specific to that build. It will **not** work
correctly on:

- The Steam / Rockstar Games Launcher re-releases (commonly called "v2.0"/"v3.0",
  patched after the Hot Coffee controversy) — different memory layout.
- **GTA: San Andreas – The Definitive Edition** (2021 remaster) — this runs on
  Unreal Engine and can't load legacy `.asi`/CLEO plugins at all, so this is a
  non-issue in practice, but is called out explicitly per your request.

The plugin includes a version guard (`IsSupportedGameVersion`) that checks the
EXE's embedded version resource and a runtime pointer sanity check before it
ever touches memory, so on an unsupported build it should simply do nothing
rather than misbehave — but I have **not** been able to compile or test this
against a live game session, since I don't have a Windows/MSVC toolchain or
the game binary available in this environment. Please test it yourself before
relying on it, ideally with a save you don't mind risking.

## How it works

- CJ's "Fat" stat (one of the game's built-in stats, range 0–1000, default 200)
  is read from the well-documented v1.0 memory address `0xB793D4`.
- A background thread checks it periodically (default: every 8 seconds).
- Above a configurable threshold (default: 700/1000, i.e. quite overweight),
  there's a chance per check of a "heart attack" — implemented as setting the
  player's health to 0, which lets the game's own death/respawn logic run
  exactly as it would for any other cause of death.
- The chance scales from `BaseChancePercent` at the threshold up to
  `MaxChancePercent` at maximum fat (1000).
- Checks are skipped while the game is paused/in a menu, while already
  "wasted," and (by default) while driving, so it doesn't feel like a bug.

## Building

You'll need Visual Studio (2019 or newer is fine) with the "Desktop
development with C++" workload.

1. Open `HeartAttack.vcxproj` in Visual Studio (or generate a new Win32 DLL
   project and add `HeartAttack.cpp` to it if you'd rather not use the
   provided project file).
2. Make sure the platform is set to **Win32 (x86)** — GTA SA is a 32-bit game,
   this will not work as x64.
3. Build in **Release** configuration. The project is set up to produce
   `HeartAttack.asi` directly (via `<TargetExt>.asi</TargetExt>`); if you're
   using your own project, just build the DLL normally and rename
   `HeartAttack.dll` to `HeartAttack.asi` afterward.

## Installing

1. You need an ASI loader. The original game doesn't load `.asi` files on its
   own — most people use **CLEO** (which includes one) or a standalone
   loader like Ultimate ASI Loader. Drop the loader's `dinput8.dll` (or
   equivalent) into your GTA SA install folder per its instructions.
2. Copy `HeartAttack.asi` into the same folder as `gta_sa.exe` (typically
   inside a `scripts` folder if you're using CLEO — check your loader's docs).
3. Launch the game normally. A `HeartAttack.ini` will be created automatically
   next to the `.asi` on first run if one doesn't already exist.

## Configuration (`HeartAttack.ini`)

```ini
[HeartAttack]
FatThreshold=700
CheckIntervalSeconds=8
BaseChancePercent=0.5
MaxChancePercent=4.0
OnlyOnFoot=1
```

- `FatThreshold` — Fat value (0–1000) above which CJ is "at risk."
- `CheckIntervalSeconds` — how often the dice get rolled.
- `BaseChancePercent` / `MaxChancePercent` — chance per check at the threshold
  vs. at maximum fat (1000), linearly interpolated in between.
- `OnlyOnFoot` — if `1`, skips the check while driving.

## Notes on the memory addresses used

These are taken from the long-standing, community-maintained GTAMods Wiki
"Memory Addresses (SA)" page, which explicitly states its addresses are
"confirmed for GTA San Andreas (GTA_SA.EXE) version 1.0" and not valid for
v2.0/Steam or later:

- `0xB6F5F0` — pointer to the player's `CPed`
- `CPed + 0x540` — player health (float)
- `CPed + 0x530` — player state (`55` = wasted)
- `0xB793D4` — Fat stat (float, 0–1000)
- `0xB7CB49` — paused/menu flag (byte)

If you find these don't line up on your specific copy of the game (some
"1.0" downloads online are actually slightly patched or cracked builds with
small offset shifts), the plugin's `__try`/`__except` guards should at worst
make it silently do nothing rather than crash — but again, I'd treat this as
a solid first draft to test and adjust rather than a guaranteed-correct
final build.
