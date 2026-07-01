# CJ Heart Attack — ASI plugin for GTA San Andreas (PC, original 2005 release)

Two related effects, both driven by CJ's in-game **Fat** stat:

- **Heart Attack** (fatal) — above a configurable Fat threshold, there's a
  small per-check chance CJ simply drops from a heart attack. This is a
  normal "wasted" death, the same as any other cause of death in the game.
- **Heart Palpitations** (non-lethal) — a lesser, more frequent event at a
  lower Fat threshold: CJ loses a random chunk of health (never enough to
  kill him) and a random CJ pain sound plays.

## Compatibility — READ THIS FIRST

This plugin pokes at the **original v1.0 `gta_sa.exe`** released in 2005, using
memory addresses specific to that build. It will **not** work correctly on:

- The Steam / Rockstar Games Launcher re-releases ("v2.0"/"v3.0") — different
  memory layout.
- **GTA: San Andreas – The Definitive Edition** (2021 remaster) — runs on
  Unreal Engine and can't load legacy `.asi`/CLEO plugins at all.

A version guard checks the EXE's embedded version resource and a runtime
pointer sanity check runs before any memory is touched, so on an unsupported
build the plugin should do nothing rather than misbehave — but I have not
been able to compile or run this against a live game session (no Windows/MSVC
toolchain or the game binary in the environment I write this in). Please test
on a save you don't mind risking before relying on it.

## How it works

- CJ's "Fat" stat (0–1000, default 200) is read from the documented v1.0
  address `0xB793D4` on a background timer (default: every 8 seconds).
- **Heart Attack**: above `FatThreshold`, there's a chance per check
  (`BaseChancePercent` at the threshold, scaling up to `MaxChancePercent` at
  max Fat) of setting health to 0 — letting the game's own death/respawn
  logic run exactly as it would for any other cause of death.
- **Heart Palpitations**: checked independently (and skipped if a heart
  attack already fired that tick), above its own, separately-configurable
  `FatThreshold`. When triggered, health drops by a random amount between
  `MinHealthLoss` and `MaxHealthLoss`, clamped so it can never fall below
  `MinHealthAfter` — it is structurally incapable of killing CJ. A random
  pain sound plays alongside it (see below).
- Both chances are multiplied by their own `SprintChanceMultiplier` while CJ
  is sprinting.
- Checks are skipped while the game is paused/in a menu, while already
  "wasted," and (for the heart attack only, via `OnlyOnFoot`) while in **any**
  vehicle — car, motorbike, bicycle, boat, plane, helicopter, or train.

## About the pain sounds

The 40 `.wav` clips in the `sounds/` folder (provided by you, extracted from
the game's `PAIN_A` audio bank) are **compiled directly into the `.asi`** as
Windows resources — see `PainSounds.rc`. At runtime, one is picked at random
and played with the standard, documented `PlaySoundA(..., SND_RESOURCE)` API
against the plugin's own resource table. There are no `.wav` files to manage
on disk once it's built — everything ships inside the single `.asi` file.

I deliberately did **not** try to hook the game's own internal audio-engine
function that plays pain sounds natively (e.g. `CPed::Say`). I could confirm
that function's call signature via the community-maintained plugin-sdk
project, but not a verified, safe raw memory address for it in the v1.0
executable — and calling an unverified address as a function pointer risks
crashing the game outright. Playing your provided clips through the standard
Windows sound API avoids that risk entirely while still delivering the actual
effect you asked for (a random CJ pain sound on each palpitation).

### Adding, removing, or replacing sounds

If you want to change the sound set later:

1. Add/remove/replace `.wav` files in the `sounds/` folder.
2. Regenerate `PainSounds.rc` so its numbered entries match the files present
   (each line is `<sequential id> WAVE "sounds\<filename>"`, IDs starting at 1
   with no gaps).
3. Update the constant in `PainSoundCount.h` to match the new total count.
4. Rebuild.

(There's nothing conceptually complex here — the numbering just has to stay
consistent between the two files, since the code picks a random number in
`[1, PAIN_SOUND_COUNT]` and asks the resource table for that exact ID.)

## Building

You'll need Visual Studio (2019 or newer) with the "Desktop development with
C++" workload.

1. Open `HeartAttack.vcxproj`.
2. Set the platform to **Win32 (x86)** — GTA SA is 32-bit; this will not work
   as x64.
3. Build in **Release**. The project produces `HeartAttack.asi` directly
   (via `<TargetExt>.asi</TargetExt>`), with the 40 pain sounds baked in.

If you're setting up your own project instead of using the provided
`.vcxproj`, make sure `PainSounds.rc` is added to the project as a resource
script (not just a text file) so the resource compiler picks it up.

## Installing

1. You need an ASI loader — most people use **CLEO** (which includes one) or
   a standalone loader like Ultimate ASI Loader.
2. Copy `HeartAttack.asi` into the same folder as `gta_sa.exe` (or a
   `scripts` subfolder, depending on your loader — check its docs). Nothing
   else needs to be copied; the sounds are inside the `.asi` itself.
3. Launch the game. A `HeartAttack.ini` is created automatically next to the
   `.asi` on first run if one doesn't already exist.

## Configuration (`HeartAttack.ini`)

This file lives next to `HeartAttack.asi` itself, wherever you placed it —
not necessarily next to `gta_sa.exe`.

```ini
[HeartAttack]
FatThreshold=700.0
CheckIntervalSeconds=8.0
BaseChancePercent=0.50
MaxChancePercent=4.00
OnlyOnFoot=1
SprintChanceMultiplier=2.00

[HeartPalpitations]
Enabled=1
FatThreshold=400.0
BaseChancePercent=1.00
MaxChancePercent=6.00
SprintChanceMultiplier=2.00
MinHealthLoss=5.0
MaxHealthLoss=25.0
MinHealthAfter=10.0
PlaySound=1
```

**`[HeartAttack]`** (fatal):
- `FatThreshold` — Fat value (0–1000) above which CJ is at risk.
- `CheckIntervalSeconds` — how often the dice are rolled, for both features.
- `BaseChancePercent` / `MaxChancePercent` — chance per check at the
  threshold vs. at max Fat (1000), linearly interpolated in between.
- `OnlyOnFoot` — if `1`, skips the heart-attack check while in any vehicle.
- `SprintChanceMultiplier` — multiplies the rolled chance while sprinting.

**`[HeartPalpitations]`** (non-lethal):
- `Enabled` — master on/off switch for this feature.
- `FatThreshold` — usually lower than the fatal one, since this isn't lethal.
- `BaseChancePercent` / `MaxChancePercent` — same interpolation as above.
- `SprintChanceMultiplier` — multiplies the rolled chance while sprinting.
- `MinHealthLoss` / `MaxHealthLoss` — a random amount of health in this range
  is lost each time one occurs.
- `MinHealthAfter` — health will never be pushed below this by a palpitation.
- `PlaySound` — if `1`, plays a random embedded pain sound each time.

## Notes on the memory addresses used

Community-verified via the GTAMods Wiki "Memory Addresses (SA)" page, which
explicitly states its addresses are "confirmed for GTA San Andreas
(GTA_SA.EXE) version 1.0" and not valid for v2.0/Steam or later:

- `0xB6F5F0` — pointer to the player's `CPed`
- `CPed + 0x540` — player health (float)
- `CPed + 0x530` — player state (only `55` = wasted is used)
- `CPed + 0x534` — running state (`7` = sprinting)
- `0xB793D4` — Fat stat (float, 0–1000)
- `0xB7CB49` — paused/menu flag (byte)
- `0xBA18FC` — current vehicle pointer (`0` = on-foot, non-zero = in any
  vehicle type)

If these don't line up on your specific copy of the game (some "1.0"
downloads online are subtly patched/cracked builds with small offset
shifts), the plugin's `__try`/`__except` guards should at worst make it
silently do nothing rather than crash — but treat this as a solid first
draft to test rather than a guaranteed-correct final build.
