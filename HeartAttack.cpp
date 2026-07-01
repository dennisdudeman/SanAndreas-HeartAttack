// ============================================================================
// CJ Heart Attack ASI Plugin
// For Grand Theft Auto: San Andreas — ORIGINAL 2005 PC RELEASE (v1.0 US) ONLY.
//
// v1.4 changelog:
//   - REMOVED: an earlier draft of this plugin briefly added a custom
//     on-screen overlay message window. That's gone — this version only
//     touches game memory (reads/writes) and plays audio; it draws nothing.
//   - CHANGED: pain sounds for "Heart Palpitations" are now compiled
//     directly into this .asi as Windows resources (see PainSounds.rc),
//     rather than read from a folder of loose .wav files at runtime. A
//     random one is picked and played via the standard, documented
//     PlaySoundA(..., SND_RESOURCE) API against this module's own resource
//     table — no files needed next to the .asi at all, no unverified
//     addresses, no game-engine hooking.
//
// v1.3 changelog:
//   - ADDED: "Heart Palpitations" — a non-lethal counterpart to the heart
//     attack. On its own configurable chance, CJ loses a random chunk of
//     health (never enough to kill him — see PalpitationMinHealthAfter),
//     and a random pain sound effect plays.
//   - ON THE PAIN SOUND, SPECIFICALLY: I looked for a verified, community-
//     confirmed memory address for the actual internal engine function that
//     plays ped pain audio (CPed::Say). I could only confirm its *call
//     signature* (via the community-maintained plugin-sdk project's
//     headers), not a safe, verified raw address for it in the v1.0
//     executable. Calling an unverified address as a function pointer risks
//     crashing the game outright — a worse mistake than simply not having
//     this exact behavior — so I deliberately did not do that. Playing a
//     provided .wav via the standard Windows sound API (see v1.4 above for
//     how this now works) sidesteps that risk entirely.
//
// v1.2 changelog:
//   - FIXED: the "OnlyOnFoot" check previously tested the ped state byte
//     for the value 50 ("driving"), which was only ever confirmed against
//     cars. Replaced with the game's own GLOBAL "current vehicle" pointer
//     (0xBA18FC), documented generically as "0 = on-foot, >0 = in-car" —
//     covers cars, motorbikes, bicycles, boats, planes, helicopters, trains.
//
// v1.1 changelog:
//   - FIXED: the ini path was resolved from the game EXE's location instead
//     of this plugin's own location, so the ini was silently read/written
//     in the wrong folder whenever the .asi lived in a "scripts" subfolder.
//   - FIXED: config loading used GetPrivateProfileString /
//     WritePrivateProfileString, which cache in memory and aren't guaranteed
//     to flush to disk unless the process exits cleanly. Replaced with
//     plain, unbuffered fopen/fclose I/O.
//   - ADDED: dynamic chance scaling while sprinting (SprintChanceMultiplier).
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <time.h>
#include <vector>
#include <stdint.h>
#include <string.h>

#include "PainSoundCount.h" // PAIN_SOUND_COUNT — keep in sync with PainSounds.rc

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "version.lib")

// ----------------------------------------------------------------------------
// Memory addresses — GTA San Andreas PC, v1.0 (US), gta_sa.exe
// Community-verified (GTAMods Wiki "Memory Addresses (SA)"), confirmed for
// v1.0 only — NOT v2.0/Steam/Rockstar Games Launcher re-releases, and not
// relevant at all to the 2021 Definitive Edition remaster (different engine,
// can't load .asi plugins).
// ----------------------------------------------------------------------------
static const DWORD ADDR_PLAYER_PED_PTR   = 0x00B6F5F0; // dword: pointer to CPed (player)
static const DWORD ADDR_FAT_STAT         = 0x00B793D4; // float: CJ's Fat stat, range 0.0 - 1000.0
static const DWORD ADDR_GAME_PAUSED_FLAG = 0x00B7CB49; // byte : 0 = running, 1 = paused / in a menu

// Generic "is the player in a vehicle at all" pointer — documented as
// "0 = on-foot, >0 = in-car," described generically rather than per vehicle
// type. This is the address the engine itself relies on, so it correctly
// covers cars, motorbikes, bicycles, boats, planes, helicopters, and trains.
static const DWORD ADDR_CURRENT_VEHICLE_PTR = 0x00BA18FC; // dword: 0 = on-foot, non-zero = pointer to current CVehicle

static const DWORD OFFSET_PED_HEALTH   = 0x540; // float, relative to CPed*
static const DWORD OFFSET_PED_STATE    = 0x530; // dword, relative to CPed* — only value 55 (wasted) is used here
static const DWORD OFFSET_PED_RUNSTATE = 0x534; // byte,  relative to CPed* (7 = sprinting)

static const BYTE  RUNSTATE_SPRINTING = 7;
static const DWORD PEDSTATE_WASTED    = 55;

// ----------------------------------------------------------------------------
// Configuration — loaded from HeartAttack.ini next to THIS .asi file (not
// next to gta_sa.exe — see v1.1 changelog above for why that distinction
// matters). A default ini is (re)written immediately if one isn't found.
// ----------------------------------------------------------------------------
struct Config
{
    // --- Heart Attack (fatal) ---
    float fatThreshold;           // Fat value (0-1000) above which CJ is "at risk"
    float checkIntervalSecs;      // how often we roll the dice
    float baseChancePercent;      // chance per check at exactly fatThreshold
    float maxChancePercent;       // chance per check at max fat (1000)
    bool  onlyOnFoot;             // skip the check while in ANY vehicle
    float sprintChanceMultiplier; // multiplies the rolled chance while sprinting

    // --- Heart Palpitations (non-lethal) ---
    bool  palpitationsEnabled;
    float palpitationFatThreshold;
    float palpitationBaseChancePercent;
    float palpitationMaxChancePercent;
    float palpitationSprintChanceMultiplier;
    float palpitationMinHealthLoss;    // minimum health lost when one occurs
    float palpitationMaxHealthLoss;    // maximum health lost when one occurs
    float palpitationMinHealthAfter;   // health is never reduced below this
    bool  palpitationPlaySound;        // master on/off for the pain sound effect
};

static Config g_cfg;
static HMODULE g_hModule = nullptr;

static void SetConfigDefaults(Config& c)
{
    c.fatThreshold = 700.0f;
    c.checkIntervalSecs = 8.0f;
    c.baseChancePercent = 0.5f;
    c.maxChancePercent = 4.0f;
    c.onlyOnFoot = true;
    c.sprintChanceMultiplier = 2.0f;

    c.palpitationsEnabled = true;
    c.palpitationFatThreshold = 400.0f;
    c.palpitationBaseChancePercent = 1.0f;
    c.palpitationMaxChancePercent = 6.0f;
    c.palpitationSprintChanceMultiplier = 2.0f;
    c.palpitationMinHealthLoss = 5.0f;
    c.palpitationMaxHealthLoss = 25.0f;
    c.palpitationMinHealthAfter = 10.0f;
    c.palpitationPlaySound = true;
}

static void GetPluginDirectory(char* outPath, size_t outSize)
{
    char modulePath[MAX_PATH];
    GetModuleFileNameA(g_hModule, modulePath, MAX_PATH);
    char* lastSlash = strrchr(modulePath, '\\');
    if (lastSlash)
        *(lastSlash + 1) = '\0';
    else
        modulePath[0] = '\0';
    strncpy_s(outPath, outSize, modulePath, outSize - 1);
}

static void WriteDefaultIni(const char* iniPath)
{
    FILE* f = nullptr;
    if (fopen_s(&f, iniPath, "w") != 0 || f == nullptr)
        return; // folder not writable, or some other issue — nothing more we can do

    fprintf(f, "[HeartAttack]\n");
    fprintf(f, "; Fat value (0-1000) above which CJ is at risk of a fatal heart attack.\n");
    fprintf(f, "FatThreshold=%.1f\n", g_cfg.fatThreshold);
    fprintf(f, "; How often (in seconds) the game rolls the dice for BOTH heart attacks\n");
    fprintf(f, "; and heart palpitations.\n");
    fprintf(f, "CheckIntervalSeconds=%.1f\n", g_cfg.checkIntervalSecs);
    fprintf(f, "; Chance per check (percent) right at FatThreshold.\n");
    fprintf(f, "BaseChancePercent=%.2f\n", g_cfg.baseChancePercent);
    fprintf(f, "; Chance per check (percent) at maximum Fat (1000).\n");
    fprintf(f, "MaxChancePercent=%.2f\n", g_cfg.maxChancePercent);
    fprintf(f, "; If 1, skip the check entirely while in ANY vehicle (car, bike, bicycle,\n");
    fprintf(f, "; boat, plane, helicopter, or train). If 0, it can trigger in a vehicle too.\n");
    fprintf(f, "OnlyOnFoot=%d\n", g_cfg.onlyOnFoot ? 1 : 0);
    fprintf(f, "; The rolled heart-attack chance is multiplied by this while CJ is sprinting.\n");
    fprintf(f, "SprintChanceMultiplier=%.2f\n", g_cfg.sprintChanceMultiplier);
    fprintf(f, "\n");
    fprintf(f, "[HeartPalpitations]\n");
    fprintf(f, "; Master on/off switch for the non-lethal \"heart palpitation\" event.\n");
    fprintf(f, "Enabled=%d\n", g_cfg.palpitationsEnabled ? 1 : 0);
    fprintf(f, "; Fat value (0-1000) above which palpitations become possible. Usually\n");
    fprintf(f, "; lower than the fatal FatThreshold above, since this isn't lethal.\n");
    fprintf(f, "FatThreshold=%.1f\n", g_cfg.palpitationFatThreshold);
    fprintf(f, "; Chance per check (percent) right at FatThreshold.\n");
    fprintf(f, "BaseChancePercent=%.2f\n", g_cfg.palpitationBaseChancePercent);
    fprintf(f, "; Chance per check (percent) at maximum Fat (1000).\n");
    fprintf(f, "MaxChancePercent=%.2f\n", g_cfg.palpitationMaxChancePercent);
    fprintf(f, "; The rolled palpitation chance is multiplied by this while CJ is sprinting.\n");
    fprintf(f, "SprintChanceMultiplier=%.2f\n", g_cfg.palpitationSprintChanceMultiplier);
    fprintf(f, "; When a palpitation occurs, CJ loses a random amount of health between\n");
    fprintf(f, "; these two values (in health points, same scale as the game's own health).\n");
    fprintf(f, "MinHealthLoss=%.1f\n", g_cfg.palpitationMinHealthLoss);
    fprintf(f, "MaxHealthLoss=%.1f\n", g_cfg.palpitationMaxHealthLoss);
    fprintf(f, "; A palpitation will never reduce health below this floor (never lethal).\n");
    fprintf(f, "MinHealthAfter=%.1f\n", g_cfg.palpitationMinHealthAfter);
    fprintf(f, "; If 1, play a random CJ pain sound (compiled into this .asi) whenever a\n");
    fprintf(f, "; palpitation occurs. If 0, palpitations happen silently.\n");
    fprintf(f, "PlaySound=%d\n", g_cfg.palpitationPlaySound ? 1 : 0);

    fflush(f);
    fclose(f);
}

// Minimal, dependency-free "key=value" ini reader. Section headers are
// tracked so identically-named keys in different sections aren't confused.
static void LoadConfig()
{
    SetConfigDefaults(g_cfg);

    char pluginDir[MAX_PATH];
    GetPluginDirectory(pluginDir, sizeof(pluginDir));

    char iniPath[MAX_PATH];
    snprintf(iniPath, sizeof(iniPath), "%sHeartAttack.ini", pluginDir);

    FILE* f = nullptr;
    if (fopen_s(&f, iniPath, "r") != 0 || f == nullptr)
    {
        // No ini yet — write defaults out immediately, then use the
        // defaults already sitting in g_cfg for this session.
        WriteDefaultIni(iniPath);
        return;
    }

    char currentSection[64] = "";
    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0 || line[0] == ';' || line[0] == '#')
            continue;

        if (line[0] == '[')
        {
            char* end = strchr(line, ']');
            if (end) *end = '\0';
            strncpy_s(currentSection, sizeof(currentSection), line + 1, sizeof(currentSection) - 1);
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq = '\0';
        const char* key = line;
        const char* value = eq + 1;
        bool inPalpitationSection = (_stricmp(currentSection, "HeartPalpitations") == 0);

        if (!inPalpitationSection)
        {
            if (_stricmp(key, "FatThreshold") == 0)
                g_cfg.fatThreshold = (float)atof(value);
            else if (_stricmp(key, "CheckIntervalSeconds") == 0)
                g_cfg.checkIntervalSecs = (float)atof(value);
            else if (_stricmp(key, "BaseChancePercent") == 0)
                g_cfg.baseChancePercent = (float)atof(value);
            else if (_stricmp(key, "MaxChancePercent") == 0)
                g_cfg.maxChancePercent = (float)atof(value);
            else if (_stricmp(key, "OnlyOnFoot") == 0)
                g_cfg.onlyOnFoot = (atoi(value) != 0);
            else if (_stricmp(key, "SprintChanceMultiplier") == 0)
                g_cfg.sprintChanceMultiplier = (float)atof(value);
        }
        else
        {
            if (_stricmp(key, "Enabled") == 0)
                g_cfg.palpitationsEnabled = (atoi(value) != 0);
            else if (_stricmp(key, "FatThreshold") == 0)
                g_cfg.palpitationFatThreshold = (float)atof(value);
            else if (_stricmp(key, "BaseChancePercent") == 0)
                g_cfg.palpitationBaseChancePercent = (float)atof(value);
            else if (_stricmp(key, "MaxChancePercent") == 0)
                g_cfg.palpitationMaxChancePercent = (float)atof(value);
            else if (_stricmp(key, "SprintChanceMultiplier") == 0)
                g_cfg.palpitationSprintChanceMultiplier = (float)atof(value);
            else if (_stricmp(key, "MinHealthLoss") == 0)
                g_cfg.palpitationMinHealthLoss = (float)atof(value);
            else if (_stricmp(key, "MaxHealthLoss") == 0)
                g_cfg.palpitationMaxHealthLoss = (float)atof(value);
            else if (_stricmp(key, "MinHealthAfter") == 0)
                g_cfg.palpitationMinHealthAfter = (float)atof(value);
            else if (_stricmp(key, "PlaySound") == 0)
                g_cfg.palpitationPlaySound = (atoi(value) != 0);
        }
    }

    fclose(f);

    // Guard against nonsensical/inverted min/max values from a hand-edited
    // ini, so a typo can't produce undefined behavior (e.g. a negative
    // range, or a floor at or below zero).
    if (g_cfg.palpitationMaxHealthLoss < g_cfg.palpitationMinHealthLoss)
    {
        float tmp = g_cfg.palpitationMaxHealthLoss;
        g_cfg.palpitationMaxHealthLoss = g_cfg.palpitationMinHealthLoss;
        g_cfg.palpitationMinHealthLoss = tmp;
    }
    if (g_cfg.palpitationMinHealthLoss < 0.0f) g_cfg.palpitationMinHealthLoss = 0.0f;
    if (g_cfg.palpitationMinHealthAfter < 1.0f) g_cfg.palpitationMinHealthAfter = 1.0f; // never allow a floor at/below 0
}

// ----------------------------------------------------------------------------
// Version guard
// ----------------------------------------------------------------------------
static bool IsSupportedGameVersion()
{
    char exePath[MAX_PATH];
    if (!GetModuleFileNameA(NULL, exePath, MAX_PATH))
        return false;

    DWORD dummy = 0;
    DWORD verInfoSize = GetFileVersionInfoSizeA(exePath, &dummy);
    if (verInfoSize == 0)
        return true; // no version resource — typical of v1.0 EXEs; fall back to runtime checks

    std::vector<char> data(verInfoSize);
    if (!GetFileVersionInfoA(exePath, 0, verInfoSize, data.data()))
        return true;

    UINT len = 0;
    VS_FIXEDFILEINFO* ffi = nullptr;
    if (VerQueryValueA(data.data(), "\\", (LPVOID*)&ffi, &len) && ffi)
    {
        WORD major = HIWORD(ffi->dwFileVersionMS);
        if (major > 1)
            return false; // clearly a newer/different build; refuse to guess at its layout
    }

    return true;
}

// ----------------------------------------------------------------------------
// Safe memory access helpers — all reads/writes are wrapped in SEH so a
// wrong address fails harmlessly instead of crashing the game.
// ----------------------------------------------------------------------------
static bool TryReadCPed(BYTE** outPed)
{
    __try
    {
        BYTE* ped = *reinterpret_cast<BYTE**>(ADDR_PLAYER_PED_PTR);
        if (ped == nullptr) return false;
        if (reinterpret_cast<uintptr_t>(ped) < 0x00010000) return false;
        *outPed = ped;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool TryReadFloat(DWORD address, float* outVal)
{
    __try
    {
        *outVal = *reinterpret_cast<float*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool TryWriteFloat(DWORD address, float val)
{
    __try
    {
        *reinterpret_cast<float*>(address) = val;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool TryReadByte(DWORD address, BYTE* outVal)
{
    __try
    {
        *outVal = *reinterpret_cast<BYTE*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool TryReadDword(DWORD address, DWORD* outVal)
{
    __try
    {
        *outVal = *reinterpret_cast<DWORD*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Returns true if the player is currently in ANY vehicle — car, motorbike,
// bicycle, boat, plane, helicopter, or train — via the game's own generic
// "current vehicle" pointer rather than a car-specific ped state value.
static bool IsPlayerInAnyVehicle()
{
    DWORD vehiclePtr = 0;
    if (!TryReadDword(ADDR_CURRENT_VEHICLE_PTR, &vehiclePtr))
        return false; // if we can't read it, fail toward "on-foot" (allow the check)
    return vehiclePtr != 0;
}

// ----------------------------------------------------------------------------
// Random pain sound playback.
//
// The 40 CJ pain clips are compiled directly into this .asi as numbered
// "WAVE" resources (see PainSounds.rc / sounds/*.wav). A random one is
// picked and played via the standard, documented PlaySoundA(SND_RESOURCE)
// API against this module's own resource table. No files on disk are
// needed at runtime, no game-engine hooking, no unverified addresses.
// ----------------------------------------------------------------------------
static void PlayRandomPainSound()
{
    if (PAIN_SOUND_COUNT <= 0)
        return;

    int id = 1 + (rand() % PAIN_SOUND_COUNT); // resource IDs in PainSounds.rc start at 1

    // SND_NODEFAULT: if playback fails for any reason (e.g. a resource ID
    // mismatch), stay silent rather than falling back to the Windows
    // default beep. SND_ASYNC: don't block this thread waiting for
    // playback to finish.
    PlaySoundA(MAKEINTRESOURCEA(id), g_hModule, SND_RESOURCE | SND_ASYNC | SND_NODEFAULT);
}

static void KillPlayerWithHeartAttack(BYTE* ped)
{
    // Zeroing health lets the game's own death/respawn logic run exactly as
    // it would for any other cause of death, on foot or in any vehicle.
    TryWriteFloat(reinterpret_cast<DWORD>(ped) + OFFSET_PED_HEALTH, 0.0f);
}

// Chance scaling helper, shared by both the heart-attack and palpitation
// checks: 0% below `threshold`, linearly interpolating from `basePct` at
// `threshold` up to `maxPct` at `scaleMax` (the max Fat value, 1000).
static float ScaleChancePercent(float value, float threshold, float scaleMax, float basePct, float maxPct)
{
    if (value < threshold)
        return 0.0f;
    float range = scaleMax - threshold;
    float t = (range > 0.0f) ? (value - threshold) / range : 1.0f;
    if (t > 1.0f) t = 1.0f;
    if (t < 0.0f) t = 0.0f;
    return basePct + t * (maxPct - basePct);
}

// ----------------------------------------------------------------------------
// Background polling thread.
// ----------------------------------------------------------------------------
static DWORD WINAPI HeartAttackThreadProc(LPVOID)
{
    srand((unsigned int)time(nullptr));
    LoadConfig();

    if (!IsSupportedGameVersion())
        return 0;

    for (;;)
    {
        Sleep((DWORD)(g_cfg.checkIntervalSecs * 1000.0f));

        BYTE pausedFlag = 0;
        if (TryReadByte(ADDR_GAME_PAUSED_FLAG, &pausedFlag) && pausedFlag != 0)
            continue;

        BYTE* ped = nullptr;
        if (!TryReadCPed(&ped))
            continue;

        float health = 0.0f;
        if (!TryReadFloat(reinterpret_cast<DWORD>(ped) + OFFSET_PED_HEALTH, &health))
            continue;
        if (health <= 0.0f)
            continue;

        DWORD pedState = 0;
        __try { pedState = *reinterpret_cast<DWORD*>(ped + OFFSET_PED_STATE); }
        __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (pedState == PEDSTATE_WASTED)
            continue;

        // Covers ALL vehicle types (car, motorbike, bicycle, boat, plane,
        // helicopter, train) — see v1.2 changelog.
        bool isInVehicle = IsPlayerInAnyVehicle();
        if (g_cfg.onlyOnFoot && isInVehicle)
            continue;

        BYTE runState = 0;
        bool isSprinting = false;
        if (TryReadByte(reinterpret_cast<DWORD>(ped) + OFFSET_PED_RUNSTATE, &runState))
            isSprinting = (runState == RUNSTATE_SPRINTING);

        float fat = 0.0f;
        if (!TryReadFloat(ADDR_FAT_STAT, &fat))
            continue;

        bool triggeredHeartAttack = false;

        // --- Heart Attack (fatal) ---
        {
            float chancePercent = ScaleChancePercent(fat, g_cfg.fatThreshold, 1000.0f,
                                                       g_cfg.baseChancePercent, g_cfg.maxChancePercent);
            if (chancePercent > 0.0f)
            {
                if (isSprinting)
                    chancePercent *= g_cfg.sprintChanceMultiplier;
                if (chancePercent > 100.0f)
                    chancePercent = 100.0f;

                float roll = ((float)rand() / (float)RAND_MAX) * 100.0f;
                if (roll <= chancePercent)
                {
                    KillPlayerWithHeartAttack(ped);
                    triggeredHeartAttack = true;
                }
            }
        }

        // --- Heart Palpitations (non-lethal) ---
        // Skipped this tick if a fatal heart attack already happened, if the
        // feature is disabled, or if health is already at/below the
        // configured floor (nothing safe left to take away).
        if (!triggeredHeartAttack && g_cfg.palpitationsEnabled && health > g_cfg.palpitationMinHealthAfter)
        {
            float chancePercent = ScaleChancePercent(fat, g_cfg.palpitationFatThreshold, 1000.0f,
                                                       g_cfg.palpitationBaseChancePercent,
                                                       g_cfg.palpitationMaxChancePercent);
            if (chancePercent > 0.0f)
            {
                if (isSprinting)
                    chancePercent *= g_cfg.palpitationSprintChanceMultiplier;
                if (chancePercent > 100.0f)
                    chancePercent = 100.0f;

                float roll = ((float)rand() / (float)RAND_MAX) * 100.0f;
                if (roll <= chancePercent)
                {
                    float lossRange = g_cfg.palpitationMaxHealthLoss - g_cfg.palpitationMinHealthLoss;
                    float loss = g_cfg.palpitationMinHealthLoss +
                                  ((float)rand() / (float)RAND_MAX) * (lossRange > 0.0f ? lossRange : 0.0f);

                    float newHealth = health - loss;
                    if (newHealth < g_cfg.palpitationMinHealthAfter)
                        newHealth = g_cfg.palpitationMinHealthAfter;

                    if (newHealth < health) // only act if it's an actual reduction
                    {
                        TryWriteFloat(reinterpret_cast<DWORD>(ped) + OFFSET_PED_HEALTH, newHealth);
                        if (g_cfg.palpitationPlaySound)
                            PlayRandomPainSound();
                    }
                }
            }
        }
    }

    return 0;
}

// ----------------------------------------------------------------------------
// DLL entry point
// ----------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reasonForCall, LPVOID)
{
    switch (reasonForCall)
    {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, HeartAttackThreadProc, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
