# San Andreas: Heart Attack — .ASI plugin for Grand Theft Auto: San Andreas.

If CJ's Fat stat gets too high, there's now a small chance that he keels over with a heart attack — a normal "wasted" death, the
same as any other cause of death in the game.

## Compatibility

This mod was only tested on the US 1.0 version of the game.

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
