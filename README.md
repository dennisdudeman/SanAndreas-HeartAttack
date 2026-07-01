# Heart Attack - an .ASI plugin for Grand Theft Auto: San Andreas.

- **Heart Attack** (fatal) — above a configurable Fat threshold, there's a
  small per-check chance CJ simply drops dead from a heart attack. This is a
  normal "wasted" death, the same as any other cause of death in the game.
- **Heart Palpitations** (non-lethal) — a lesser, more frequent event at a
  lower Fat threshold: CJ loses a random chunk of health (never enough to
  kill him) and a random CJ pain sound plays.

## Compatibility

This mod was only tested on the US 1.0 version of the game.

## Building

You'll need Visual Studio 2022 or newer with the "Desktop
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

Make sure you have an .asi loader installed. Drop the .asi file and the .ini file into your "scripts" folder.

## Configuration (`HeartAttack.ini`)

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

