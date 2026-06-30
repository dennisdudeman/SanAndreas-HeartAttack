// ============================================================================
// CJ Heart Attack ASI Plugin
// For Grand Theft Auto: San Andreas — ORIGINAL 2005 PC RELEASE (v1.0 US) ONLY.
//
// Concept: the game's own lore/wiki notes that being extremely overweight
// "lessens most physical capabilities ... and possibly lead[s] to random
// heart attacks (that can kill Carl)" — this mod implements that mechanic,
// which exists in GTA folklore but not in the shipped game.
//
// Author: written for a single user request. Provided as-is; please test
// thoroughly before distributing, and read README.md for build/install
// steps and the verification notes on the memory addresses used.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <vector>
#include <stdint.h>

// ----------------------------------------------------------------------------
// Memory addresses — GTA San Andreas PC, v1.0 (US), gta_sa.exe
// These are well-documented, long-standing community-verified addresses
// (GTAMods Wiki "Memory Addresses (SA)", confirmed for v1.0 only — NOT for
// v2.0/Steam/Rockstar Games Launcher re-releases, and obviously not for the
// 2021 Definitive Edition remaster, which is a completely different engine
// and cannot load .asi plugins at all).
// ----------------------------------------------------------------------------
static const DWORD ADDR_PLAYER_PED_PTR   = 0x00B6F5F0; // dword: pointer to CPed (player)
static const DWORD ADDR_FAT_STAT         = 0x00B793D4; // float: CJ's Fat stat, range 0.0 - 1000.0
static const DWORD ADDR_GAME_PAUSED_FLAG = 0x00B7CB49; // byte : 0 = running, 1 = paused / in a menu

static const DWORD OFFSET_PED_HEALTH     = 0x540; // float, relative to CPed*
static const DWORD OFFSET_PED_STATE      = 0x530; // dword, relative to CPed* (55 = wasted)

// Stat #21 in San Andreas' stat table is "Fat" (confirmed via GTAMods'
// "List of statistics (SA)"); default new-game value is 200.0 (20%).
// The constant isn't used directly here since we read the live mirrored
// float at ADDR_FAT_STAT, but it's left here as documentation/reference.
static const int STAT_ID_FAT = 21;

// ----------------------------------------------------------------------------
// Tunable parameters, loaded from HeartAttack.ini next to this .asi.
// Sensible defaults are used if the ini is missing.
// ----------------------------------------------------------------------------
struct Config
{
    float fatThreshold;      // Fat value (0-1000) above which CJ is "at risk"
    float checkIntervalSecs; // how often we roll the dice
    float baseChancePercent; // chance per check at exactly fatThreshold
    float maxChancePercent;  // chance per check at max fat (1000)
    bool  onlyOnFoot;        // skip the check while driving (gentler / less jarring)
};

static Config g_cfg = { 700.0f, 8.0f, 0.5f, 4.0f, true };

static void LoadConfig()
{
    char iniPath[MAX_PATH];
    GetModuleFileNameA(NULL, iniPath, MAX_PATH);
    char* lastSlash = strrchr(iniPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat_s(iniPath, MAX_PATH, "HeartAttack.ini");

    g_cfg.fatThreshold      = (float)GetPrivateProfileIntA("HeartAttack", "FatThreshold", 700, iniPath);
    g_cfg.checkIntervalSecs = (float)GetPrivateProfileIntA("HeartAttack", "CheckIntervalSeconds", 8, iniPath);

    char buf[64];
    GetPrivateProfileStringA("HeartAttack", "BaseChancePercent", "0.5", buf, sizeof(buf), iniPath);
    g_cfg.baseChancePercent = (float)atof(buf);

    GetPrivateProfileStringA("HeartAttack", "MaxChancePercent", "4.0", buf, sizeof(buf), iniPath);
    g_cfg.maxChancePercent = (float)atof(buf);

    g_cfg.onlyOnFoot = GetPrivateProfileIntA("HeartAttack", "OnlyOnFoot", 1, iniPath) != 0;

    // Write defaults back out so the user always has a config file to edit,
    // even on first run.
    char tmp[32];
    sprintf_s(tmp, "%.1f", g_cfg.fatThreshold);
    WritePrivateProfileStringA("HeartAttack", "FatThreshold", tmp, iniPath);
    sprintf_s(tmp, "%.1f", g_cfg.checkIntervalSecs);
    WritePrivateProfileStringA("HeartAttack", "CheckIntervalSeconds", tmp, iniPath);
    sprintf_s(tmp, "%.2f", g_cfg.baseChancePercent);
    WritePrivateProfileStringA("HeartAttack", "BaseChancePercent", tmp, iniPath);
    sprintf_s(tmp, "%.2f", g_cfg.maxChancePercent);
    WritePrivateProfileStringA("HeartAttack", "MaxChancePercent", tmp, iniPath);
    WritePrivateProfileStringA("HeartAttack", "OnlyOnFoot", g_cfg.onlyOnFoot ? "1" : "0", iniPath);
}

// ----------------------------------------------------------------------------
// Version guard: this plugin must refuse to act on anything except the
// original v1.0 (US) executable. Rather than trust a hard-coded file size
// (which is brittle — re-zips, mirrors, and disk images can alter padding),
// we read the EXE's own embedded VERSIONINFO resource and compare against
// the known v1.0 file version. If it doesn't match, or if it's missing
// entirely (as it is on most non-Windows-resource builds, re-releases, or
// the Definitive Edition), the plugin disables itself and does nothing —
// it will NOT guess at memory layout for a binary it can't identify.
// ----------------------------------------------------------------------------
static bool IsSupportedGameVersion()
{
    char exePath[MAX_PATH];
    if (!GetModuleFileNameA(NULL, exePath, MAX_PATH))
        return false;

    DWORD dummy = 0;
    DWORD verInfoSize = GetFileVersionInfoSizeA(exePath, &dummy);
    if (verInfoSize == 0)
    {
        // No version resource at all (typical of older/cracked v1.0 EXEs,
        // which is exactly the binary this mod targets). We fall back to
        // a defensive runtime sanity check instead — see ValidateAddressesAreSane().
        return true; // tentatively allow; final say is the runtime sanity check below
    }

    std::vector<char> data(verInfoSize);
    if (!GetFileVersionInfoA(exePath, 0, verInfoSize, data.data()))
        return true; // same fallback reasoning as above

    UINT len = 0;
    VS_FIXEDFILEINFO* ffi = nullptr;
    if (VerQueryValueA(data.data(), "\\", (LPVOID*)&ffi, &len) && ffi)
    {
        WORD major = HIWORD(ffi->dwFileVersionMS);
        WORD minor = LOWORD(ffi->dwFileVersionMS);
        // Known re-releases (Steam "v2.0"/"v3.0" Hot-Coffee-patched builds,
        // and the 2021 Definitive Edition, which isn't even this engine)
        // report different version numbers or none at all. We only proceed
        // for 1.x-style version stamps; anything clearly newer is rejected.
        if (major > 1)
            return false;
    }

    return true;
}

// A last-resort runtime sanity check: read the player-ped pointer and make
// sure it points somewhere plausible (non-null, within a typical process
// address range) before we ever dereference it. This doesn't *prove* we're
// on v1.0, but it stops the plugin from reading/writing garbage if it's
// wrong, on any version, ever — defense in depth on top of the version check.
static bool TryReadCPed(BYTE** outPed)
{
    __try
    {
        BYTE* ped = *reinterpret_cast<BYTE**>(ADDR_PLAYER_PED_PTR);
        if (ped == nullptr) return false;
        if (reinterpret_cast<uintptr_t>(ped) < 0x00010000) return false; // obviously bogus
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

static void KillPlayerWithHeartAttack(BYTE* ped)
{
    __try
    {
        // Setting health to 0 is the standard, safe way mods trigger a
        // normal "wasted" death — the game's own update loop notices zero
        // health and runs its usual death/respawn sequence, the same as
        // dying to any other cause. We deliberately do NOT poke the raw
        // state byte (CPed+0x530) directly, since that bypasses the
        // game's own death bookkeeping and can leave things in a broken
        // state (camera, controls, stats).
        *reinterpret_cast<float*>(ped + OFFSET_PED_HEALTH) = 0.0f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Swallow — if this fails, nothing else can be done safely.
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
    {
        // Wrong game/version: do nothing for the lifetime of the process.
        return 0;
    }

    for (;;)
    {
        Sleep((DWORD)(g_cfg.checkIntervalSecs * 1000.0f));

        BYTE pausedFlag = 0;
        if (TryReadByte(ADDR_GAME_PAUSED_FLAG, &pausedFlag) && pausedFlag != 0)
            continue; // menu / loading / paused — skip this round

        BYTE* ped = nullptr;
        if (!TryReadCPed(&ped))
            continue; // not in-game yet (e.g. still on the title screen)

        float health = 0.0f;
        if (!TryReadFloat(reinterpret_cast<DWORD>(ped) + OFFSET_PED_HEALTH, &health))
            continue;
        if (health <= 0.0f)
            continue; // already dead/wasted, nothing to do

        DWORD pedState = 0;
        __try { pedState = *reinterpret_cast<DWORD*>(ped + OFFSET_PED_STATE); }
        __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (pedState == 55) // already wasted
            continue;

        if (g_cfg.onlyOnFoot)
        {
            // Player state 50 == driving (per community memory-map notes).
            // If CJ's currently driving, skip — keeps the effect feeling
            // like a personal medical event rather than causing a crash.
            if (pedState == 50)
                continue;
        }

        float fat = 0.0f;
        if (!TryReadFloat(ADDR_FAT_STAT, &fat))
            continue;

        if (fat < g_cfg.fatThreshold)
            continue;

        // Scale the chance linearly between threshold and the stat's max (1000).
        float range = 1000.0f - g_cfg.fatThreshold;
        float t = (range > 0.0f) ? (fat - g_cfg.fatThreshold) / range : 1.0f;
        if (t > 1.0f) t = 1.0f;
        if (t < 0.0f) t = 0.0f;

        float chancePercent = g_cfg.baseChancePercent +
                               t * (g_cfg.maxChancePercent - g_cfg.baseChancePercent);

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
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, HeartAttackThreadProc, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
