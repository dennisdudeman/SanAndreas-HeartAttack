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

**Important:** this file lives next to `HeartAttack.asi` itself — wherever
you placed the `.asi` (often a `scripts` folder, depending on your loader),
that's where the ini goes too. It is **not** next to `gta_sa.exe` unless
that's also where you put the `.asi`. (Earlier versions of this plugin
incorrectly resolved paths from the game EXE instead of the plugin itself —
see the changelog at the top of `HeartAttack.cpp` if you're curious.)

```ini
[HeartAttack]
FatThreshold=700.0
CheckIntervalSeconds=8.0
BaseChancePercent=0.50
MaxChancePercent=4.00
OnlyOnFoot=1
SprintChanceMultiplier=2.00
```

- `FatThreshold` — Fat value (0–1000) above which CJ is "at risk."
- `CheckIntervalSeconds` — how often the dice get rolled.
- `BaseChancePercent` / `MaxChancePercent` — chance per check at the threshold
  vs. at maximum fat (1000), linearly interpolated in between.
- `OnlyOnFoot` — if `1`, skips the check while driving. Set to `0` if you
  want it to be able to trigger in a vehicle too.
- `SprintChanceMultiplier` — the rolled chance is multiplied by this while
  CJ is sprinting (detected via the game's own ped "running state" byte,
  value `7`). `2.0` means twice as likely per check while sprinting; `1.0`
  disables the effect.

### Why there's no "exercising" multiplier

I looked for a reliable memory flag for "CJ is currently using gym
equipment" (treadmill, exercise bike, bench press) and couldn't find one
that's documented and community-verified the way the sprinting byte is.
Gym animations are almost certainly tracked at the animation-blend level
rather than as a simple ped state byte, and I don't have verified offsets
into that structure. Rather than invent a plausible-looking address that
might silently read garbage (and either never trigger, or worse, trigger
on the wrong condition), I've left this out.

If you want to pursue it yourself, two safer paths forward:
1. Use a memory-scanning tool (Cheat Engine) while manually using gym
   equipment in-game to find a stable, dedicated flag/byte, if one exists.
2. Hook the animation-name comparison the game itself already does — the
   SCM opcode `0611` (`actor performing_animation`) proves the engine can
   check the currently-playing animation by name, so there's very likely a
   reachable "current animation name" pointer; finding its exact offset
   would need the same kind of verification the addresses above went
   through.

## Vehicle coverage (cars, bikes, bicycles, boats, planes, helicopters, trains)

`OnlyOnFoot` now correctly means *on foot* — it checks the game's own global
"current vehicle" pointer (`0xBA18FC`, documented as `0 = on-foot, >0 = in a
vehicle`), which the engine relies on internally regardless of vehicle type.
This covers cars, motorbikes, bicycles, boats, planes, helicopters, and
trains equally.

An earlier draft of this plugin instead checked a ped state value (`50`,
documented only as "driving") that had only ever been confirmed against
cars — San Andreas' own scripting language has separate opcodes for
checking whether the player is driving a car vs. a bike vs. a boat vs. a
plane vs. a helicopter, which was a strong signal that a single "driving"
state value might not reliably cover every vehicle type. Rather than leave
that ambiguity in place, the check was replaced with the generic pointer
described above, which is unambiguous about applying to vehicles in
general rather than to cars specifically.

## Notes on the memory addresses used

These are taken from the long-standing, community-maintained GTAMods Wiki
"Memory Addresses (SA)" page, which explicitly states its addresses are
"confirmed for GTA San Andreas (GTA_SA.EXE) version 1.0" and not valid for
v2.0/Steam or later:

- `0xB6F5F0` — pointer to the player's `CPed`
- `CPed + 0x540` — player health (float)
- `CPed + 0x530` — player state (only `55` = wasted is used)
- `CPed + 0x534` — running state (`7` = sprinting)
- `0xB793D4` — Fat stat (float, 0–1000)
- `0xB7CB49` — paused/menu flag (byte)
- `0xBA18FC` — current vehicle pointer (`0` = on-foot, non-zero = in any
  vehicle type: car, bike, bicycle, boat, plane, helicopter, or train)

If you find these don't line up on your specific copy of the game (some
"1.0" downloads online are actually slightly patched or cracked builds with
small offset shifts), the plugin's `__try`/`__except` guards should at worst
make it silently do nothing rather than crash — but again, I'd treat this as
a solid first draft to test and adjust rather than a guaranteed-correct
final build.
