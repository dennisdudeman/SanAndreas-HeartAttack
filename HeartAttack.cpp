// ============================================================================
// CJ Heart Attack ASI Plugin
// For Grand Theft Auto: San Andreas — ORIGINAL 2005 PC RELEASE (v1.0 US) ONLY.
//
// v1.2 changelog:
//   - FIXED: the "OnlyOnFoot" check previously tested the ped state byte
//     for the value 50 ("driving"). That value is documented as "driving"
//     in general, but was only ever confirmed against cars — San Andreas'
//     own script language (SCM) has SEPARATE, distinct opcodes for checking
//     whether the player is in a car vs. a bike vs. a boat vs. a plane vs.
//     a helicopter (e.g. "driving_bike", "driving_boat", "driving_heli",
//     "driving_plane" are all different checks internally), which strongly
//     suggests the engine does NOT necessarily represent every vehicle type
//     with the same single state value. Relying on state==50 risked missing
//     bicycles, boats, planes, helicopters, and/or trains — CJ could die on
//     foot but not on a bike, for example, with no indication anything was
//     wrong.
//     Replaced with the game's own GLOBAL "current vehicle" pointer
//     (0xBA18FC), which is documented simply as "0 = on-foot, >0 = in-car"
//     — described generically, not per vehicle type. This is the same
//     pointer the engine itself relies on to know whether the player is in
//     ANY vehicle, so "in a vehicle" now correctly covers cars, motorbikes,
//     bicycles, boats, planes, helicopters, and trains alike.
//
// v1.1 changelog (fixes reported after first release):
//   - FIXED: ini path was resolved from the game EXE's location
//     (GetModuleFileName(NULL, ...)), not this plugin's own location. If your
//     ASI loader puts HeartAttack.asi in a "scripts" subfolder, the ini was
//     silently being read/written one level up, in the game's root folder —
//     so it looked like the ini was never generated and never respected.
//     Fixed by resolving paths relative to this DLL's own module handle.
//   - FIXED: config loading previously used GetPrivateProfileString /
//     WritePrivateProfileString. These Win32 APIs cache file contents in
//     memory and are not guaranteed to flush to disk until the *entire
//     process* using them exits cleanly — if the game is closed via Alt+F4,
//     crashes, or is killed from Task Manager, the ini file can simply never
//     get written. Replaced with plain, unbuffered file I/O (fopen/fclose),
//     which writes and flushes immediately.
//   - "Death in car" was never actually broken — OnlyOnFoot defaults to 1,
//     and because of the ini bug above, a manually-set OnlyOnFoot=0 was
//     never actually being read. Fixed as a side effect of the ini fix.
//   - ADDED: dynamic chance scaling while sprinting (see SprintChanceMultiplier
//     below), using the documented CPed running-state byte.
//   - NOT ADDED: dynamic scaling while "exercising" (treadmill/bike/weights).
//     I don't have a verified, community-confirmed memory address for a
//     "currently using gym equipment" flag, as opposed to sprinting, which
//     is documented. Rather than guess at an unverified offset — which is
//     exactly the kind of mistake that would silently do the wrong thing —
//     I've left this out. See README.md for details and options if you want
//     to pursue it further.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <vector>
#include <stdint.h>
#include <string.h>

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

// Generic "is the player in a vehicle at all" pointer. Documented simply as
// "0 = on-foot, >0 = in-car" — this is described generically (not per
// vehicle type), and is the address the game itself relies on, so it
// correctly covers cars, motorbikes, bicycles, boats, planes, helicopters,
// and trains alike. This is what makes "OnlyOnFoot" actually mean "on foot"
// rather than just "not in a car."
static const DWORD ADDR_CURRENT_VEHICLE_PTR = 0x00BA18FC; // dword: 0 = on-foot, non-zero = pointer to current CVehicle

static const DWORD OFFSET_PED_HEALTH     = 0x540; // float, relative to CPed*
static const DWORD OFFSET_PED_STATE      = 0x530; // dword, relative to CPed* — only value 55 (wasted) is
                                                    // relied on here now; see ADDR_CURRENT_VEHICLE_PTR for
                                                    // vehicle detection instead of this field's "driving" value.
static const DWORD OFFSET_PED_RUNSTATE   = 0x534; // byte,  relative to CPed*
                                                    //   0 = while driving, 1 = standing still,
                                                    //   4 = start to run, 6 = running,
                                                    //   7 = running fast / sprinting

static const BYTE  RUNSTATE_SPRINTING = 7;
static const DWORD PEDSTATE_WASTED    = 55;
// Note: there is deliberately no PEDSTATE_DRIVING constant here anymore.
// State 50 is documented as "driving" but was only ever confirmed for cars;
// "is the player in a vehicle" is now determined via IsPlayerInAnyVehicle()
// using the generic current-vehicle pointer instead. See v1.2 changelog.

// ----------------------------------------------------------------------------
// Tunable parameters, loaded from HeartAttack.ini next to THIS .asi file
// (not next to gta_sa.exe — see changelog above). Sensible defaults are
// used, and a default ini is (re)written immediately on load if missing.
// ----------------------------------------------------------------------------
struct Config
{
    float fatThreshold;           // Fat value (0-1000) above which CJ is "at risk"
    float checkIntervalSecs;      // how often we roll the dice
    float baseChancePercent;      // chance per check at exactly fatThreshold
    float maxChancePercent;       // chance per check at max fat (1000)
    bool  onlyOnFoot;             // skip the check while driving
    float sprintChanceMultiplier; // multiplies the rolled chance while sprinting
};

static Config g_cfg = { 700.0f, 8.0f, 0.5f, 4.0f, true, 2.0f };
static HMODULE g_hModule = nullptr;

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
    fprintf(f, "; Fat value (0-1000) above which CJ is at risk of a heart attack.\n");
    fprintf(f, "FatThreshold=%.1f\n", g_cfg.fatThreshold);
    fprintf(f, "; How often (in seconds) the game rolls the dice.\n");
    fprintf(f, "CheckIntervalSeconds=%.1f\n", g_cfg.checkIntervalSecs);
    fprintf(f, "; Chance per check (percent) right at FatThreshold.\n");
    fprintf(f, "BaseChancePercent=%.2f\n", g_cfg.baseChancePercent);
    fprintf(f, "; Chance per check (percent) at maximum Fat (1000).\n");
    fprintf(f, "MaxChancePercent=%.2f\n", g_cfg.maxChancePercent);
    fprintf(f, "; If 1, skip the check entirely while driving. If 0, it can happen in a car too.\n");
    fprintf(f, "OnlyOnFoot=%d\n", g_cfg.onlyOnFoot ? 1 : 0);
    fprintf(f, "; The rolled chance is multiplied by this while CJ is sprinting.\n");
    fprintf(f, "; e.g. 2.0 = twice as likely per check while sprinting.\n");
    fprintf(f, "SprintChanceMultiplier=%.2f\n", g_cfg.sprintChanceMultiplier);

    fflush(f);
    fclose(f);
}

// Minimal, dependency-free "key=value" ini reader. Ignores blank lines,
// lines starting with ';' or '#', and a leading "[Section]" line.
static void LoadConfig()
{
    char pluginDir[MAX_PATH];
    GetPluginDirectory(pluginDir, sizeof(pluginDir));

    char iniPath[MAX_PATH];
    snprintf(iniPath, sizeof(iniPath), "%sHeartAttack.ini", pluginDir);

    FILE* f = nullptr;
    if (fopen_s(&f, iniPath, "r") != 0 || f == nullptr)
    {
        // No ini yet — write defaults out immediately, then use the defaults
        // already sitting in g_cfg for this session.
        WriteDefaultIni(iniPath);
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        // Strip trailing newline/carriage return.
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0 || line[0] == ';' || line[0] == '#' || line[0] == '[')
            continue;

        char* eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq = '\0';
        const char* key = line;
        const char* value = eq + 1;

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

    fclose(f);
}

// ----------------------------------------------------------------------------
// Version guard (unchanged behavior from v1.0 of this plugin)
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
// bicycle, boat, plane, helicopter, or train — using the game's own generic
// "current vehicle" pointer rather than a car-specific ped state value.
static bool IsPlayerInAnyVehicle()
{
    DWORD vehiclePtr = 0;
    if (!TryReadDword(ADDR_CURRENT_VEHICLE_PTR, &vehiclePtr))
        return false; // if we can't read it, assume on-foot (fail toward allowing the check)
    return vehiclePtr != 0;
}

static void KillPlayerWithHeartAttack(BYTE* ped)
{
    __try
    {
        // Zeroing health lets the game's own death/respawn logic run exactly
        // as it would for any other cause of death, regardless of whether
        // CJ is on foot or in a vehicle at the time.
        *reinterpret_cast<float*>(ped + OFFSET_PED_HEALTH) = 0.0f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Nothing more we can safely do if even this fails.
    }
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

        // Covers ALL vehicle types — car, motorbike, bicycle, boat, plane,
        // helicopter, train — not just cars. See v1.2 changelog at the top
        // of this file for why this replaced a car-specific check.
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

        if (fat < g_cfg.fatThreshold)
            continue;

        float range = 1000.0f - g_cfg.fatThreshold;
        float t = (range > 0.0f) ? (fat - g_cfg.fatThreshold) / range : 1.0f;
        if (t > 1.0f) t = 1.0f;
        if (t < 0.0f) t = 0.0f;

        float chancePercent = g_cfg.baseChancePercent +
                               t * (g_cfg.maxChancePercent - g_cfg.baseChancePercent);

        if (isSprinting)
            chancePercent *= g_cfg.sprintChanceMultiplier;

        if (chancePercent > 100.0f)
            chancePercent = 100.0f;

        float roll = ((float)rand() / (float)RAND_MAX) * 100.0f;
        if (roll <= chancePercent)
        {
            KillPlayerWithHeartAttack(ped);
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
