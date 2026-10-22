#ifndef FPS_HUD_MANAGER_H
#define FPS_HUD_MANAGER_H

#include <stdint.h>
#include <string>
#include <mutex>
#include "FileLogger.h"
#include "Macros.h"
#include "Utils.h"

namespace FPS_Offsets {
    // RVAs from dump.cs (libil2cpp.so)
    constexpr uintptr_t BattleManager_CustomUpdate            = 0x2513A7C; // Line 15025
    constexpr uintptr_t BattleManager_get_ThirdLookPlayersCount = 0x250FEE4; // Line 14899
    constexpr uintptr_t BattleManagerAI_CustomUpdate          = 0x25C5CF0; // Line 163012
    constexpr uintptr_t BattleManagerAI_get_AI                = 0x25C5A48; // Line 162999
    constexpr uintptr_t BattleAI_get_Count                    = 0x25C5544; // Line 162937
    constexpr uintptr_t GameScore_GetTeamPlayersCount         = 0x2142DF0; // Line 40079
    constexpr uintptr_t BattleServerConnectionController_getInstance = 0x21EE570; // Line 54418

    // Struct field offsets from dump.cs
    constexpr size_t BattleManager_worldState        = 0xB8;  // WorldStateType
    constexpr size_t BattleManager_gameMode          = 0xE8;  // BaseGameMode
    constexpr size_t BattleManager_localPlayer       = 0x120; // IFirstLookBattlePlayer
    constexpr size_t BattleManager_thirdLookPlayers  = 0x148; // Dictionary<int, ThirdLookBattlePlayer>
    constexpr size_t BattleManagerAI_battleAI        = 0x1B8; // BattleAI
    constexpr size_t BaseGameMode_gameScore          = 0x28;  // GameScore
    constexpr size_t BSCC_battleManager              = 0x100; // BattleManager
}

struct HUDStatusInfo {
    bool enabled = true;
    bool showBots = true;
    bool showReal = true;
    bool showTeams = true;
    bool inBattle = false;
    int totalPlayers = 0;
    int botCount = 0;
    int humanPlayers = 0;
    int swatCount = 0;
    int terrorCount = 0;
    int worldState = 0; // 0=None, 1=Load, 2=Finish, 3=Play
    uint64_t lastUpdateTimestamp = 0;
};

class FPS_HUD_Manager {
private:
    static FPS_HUD_Manager* s_Instance;
    HUDStatusInfo m_Info;
    std::mutex m_Mutex;
    void* m_pCurrentBattleManager = nullptr;
    uintptr_t m_LibBase = 0;
    bool m_HooksInstalled = false;

    // Resolved Il2Cpp function pointers
    int (*m_pfn_get_ThirdLookPlayersCount)(void* instance) = nullptr;
    void* (*m_pfn_BattleManagerAI_get_AI)(void* instance) = nullptr;
    int (*m_pfn_BattleAI_get_Count)(void* instance) = nullptr;
    int (*m_pfn_GameScore_GetTeamPlayersCount)(void* instance, uint8_t team, bool onlyNotDead) = nullptr;
    void* (*m_pfn_BSCC_getInstance)() = nullptr;

    // Original hooked functions
    void (*m_orig_BattleManager_CustomUpdate)(void* instance) = nullptr;
    void (*m_orig_BattleManagerAI_CustomUpdate)(void* instance) = nullptr;

public:
    static FPS_HUD_Manager* GetInstance() {
        if (!s_Instance) {
            s_Instance = new FPS_HUD_Manager();
        }
        return s_Instance;
    }

    void SetLibBase(uintptr_t base) {
        m_LibBase = base;
        g_TargetLibBase = base;
    }

    uintptr_t GetLibBase() const {
        return m_LibBase;
    }

    bool AreHooksInstalled() const {
        return m_HooksInstalled;
    }

    void SetEnabled(bool enabled) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Info.enabled = enabled;
        LOG_FILE_I("HUD Status Panel toggled: %s", enabled ? "ENABLED" : "DISABLED");
    }

    bool IsEnabled() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Info.enabled;
    }

    void SetShowBots(bool show) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Info.showBots = show;
    }

    void SetShowReal(bool show) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Info.showReal = show;
    }

    void SetShowTeams(bool show) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Info.showTeams = show;
    }

    HUDStatusInfo GetStatusInfo() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Info;
    }

    // Initialize offsets and install inline hooks
    bool InitializeHooks(const char* libName) {
        LOG_FILE_I("Initializing FPS HUD Manager hooks for target library: %s", libName);

        if (m_LibBase == 0) {
            m_LibBase = findLibrary(libName);
            g_TargetLibBase = m_LibBase;
        }

        if (m_LibBase == 0) {
            LOG_FILE_E("Failed to find base address of library %s in /proc/self/maps", libName);
            return false;
        }

        LOG_FILE_I("Found %s base address at: 0x%lx", libName, (unsigned long)m_LibBase);

        // Resolve Il2Cpp function pointers
        m_pfn_get_ThirdLookPlayersCount = (int (*)(void*))(m_LibBase + FPS_Offsets::BattleManager_get_ThirdLookPlayersCount);
        m_pfn_BattleManagerAI_get_AI = (void* (*)(void*))(m_LibBase + FPS_Offsets::BattleManagerAI_get_AI);
        m_pfn_BattleAI_get_Count = (int (*)(void*))(m_LibBase + FPS_Offsets::BattleAI_get_Count);
        m_pfn_GameScore_GetTeamPlayersCount = (int (*)(void*, uint8_t, bool))(m_LibBase + FPS_Offsets::GameScore_GetTeamPlayersCount);
        m_pfn_BSCC_getInstance = (void* (*)())(m_LibBase + FPS_Offsets::BattleServerConnectionController_getInstance);

        LOG_FILE_I("Resolved get_ThirdLookPlayersCount at 0x%lx", (unsigned long)m_pfn_get_ThirdLookPlayersCount);
        LOG_FILE_I("Resolved BattleAI_get_Count at 0x%lx", (unsigned long)m_pfn_BattleAI_get_Count);

#if defined(__aarch64__)
        void* addr_BM_CustomUpdate = (void*)(m_LibBase + FPS_Offsets::BattleManager_CustomUpdate);
        void* addr_BMAI_CustomUpdate = (void*)(m_LibBase + FPS_Offsets::BattleManagerAI_CustomUpdate);

        LOG_FILE_I("Hooking BattleManager::CustomUpdate at 0x%lx", (unsigned long)addr_BM_CustomUpdate);
        A64HookFunction(addr_BM_CustomUpdate, (void*)Hooked_BattleManager_CustomUpdate, (void**)&m_orig_BattleManager_CustomUpdate);

        LOG_FILE_I("Hooking BattleManagerAI::CustomUpdate at 0x%lx", (unsigned long)addr_BMAI_CustomUpdate);
        A64HookFunction(addr_BMAI_CustomUpdate, (void*)Hooked_BattleManagerAI_CustomUpdate, (void**)&m_orig_BattleManagerAI_CustomUpdate);

        m_HooksInstalled = true;
        LOG_FILE_I("All HUD hooks successfully installed!");
        return true;
#else
        LOG_FILE_W("Warning: Not compiling for aarch64. Inline hooks skipped.");
        return false;
#endif
    }

    // Called during BattleManager / BattleManagerAI update
    void OnBattleManagerUpdate(void* instance, bool isAI) {
        if (!instance) return;

        m_pCurrentBattleManager = instance;

        // Extract game state and counts safely
        int totalPlayers = 0;
        int botCount = 0;
        int worldState = 0;
        int swatCount = 0;
        int terrorCount = 0;

        // 1. Read WorldState
        uint8_t* pWs = (uint8_t*)((uintptr_t)instance + FPS_Offsets::BattleManager_worldState);
        if (pWs) {
            worldState = (int)(*pWs);
        }

        // 2. Count ThirdLookPlayers
        int thirdLookCount = 0;
        if (m_pfn_get_ThirdLookPlayersCount) {
            try {
                thirdLookCount = m_pfn_get_ThirdLookPlayersCount(instance);
            } catch (...) {
                thirdLookCount = 0;
            }
        }

        // 3. Check LocalPlayer
        void* localPlayer = *(void**)((uintptr_t)instance + FPS_Offsets::BattleManager_localPlayer);
        totalPlayers = thirdLookCount + (localPlayer != nullptr ? 1 : 0);

        // 4. Count Bots
        void* battleAI = *(void**)((uintptr_t)instance + FPS_Offsets::BattleManagerAI_battleAI);
        if (!battleAI && m_pfn_BattleManagerAI_get_AI) {
            try {
                battleAI = m_pfn_BattleManagerAI_get_AI(instance);
            } catch (...) {
                battleAI = nullptr;
            }
        }

        if (battleAI && m_pfn_BattleAI_get_Count) {
            try {
                botCount = m_pfn_BattleAI_get_Count(battleAI);
            } catch (...) {
                botCount = 0;
            }
        }

        int humanPlayers = (totalPlayers >= botCount) ? (totalPlayers - botCount) : totalPlayers;

        // 5. Read Team scores / counts from GameScore
        void* gameMode = *(void**)((uintptr_t)instance + FPS_Offsets::BattleManager_gameMode);
        if (gameMode) {
            void* gameScore = *(void**)((uintptr_t)gameMode + FPS_Offsets::BaseGameMode_gameScore);
            if (gameScore && m_pfn_GameScore_GetTeamPlayersCount) {
                try {
                    swatCount = m_pfn_GameScore_GetTeamPlayersCount(gameScore, 1 /* Swat */, false);
                    terrorCount = m_pfn_GameScore_GetTeamPlayersCount(gameScore, 2 /* Terror */, false);
                } catch (...) {
                    swatCount = 0;
                    terrorCount = 0;
                }
            }
        }

        // Update state
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Info.inBattle = (worldState == 3 /* Play */) || (totalPlayers > 0);
            m_Info.totalPlayers = totalPlayers;
            m_Info.botCount = botCount;
            m_Info.humanPlayers = humanPlayers;
            m_Info.worldState = worldState;
            m_Info.swatCount = swatCount;
            m_Info.terrorCount = terrorCount;
            m_Info.lastUpdateTimestamp = (uint64_t)time(NULL);
        }
    }

    // Static Hook Trampolines
    static void Hooked_BattleManager_CustomUpdate(void* instance) {
        if (s_Instance) {
            s_Instance->OnBattleManagerUpdate(instance, false);
            if (s_Instance->m_orig_BattleManager_CustomUpdate) {
                s_Instance->m_orig_BattleManager_CustomUpdate(instance);
            }
        }
    }

    static void Hooked_BattleManagerAI_CustomUpdate(void* instance) {
        if (s_Instance) {
            s_Instance->OnBattleManagerUpdate(instance, true);
            if (s_Instance->m_orig_BattleManagerAI_CustomUpdate) {
                s_Instance->m_orig_BattleManagerAI_CustomUpdate(instance);
            }
        }
    }

    // Format display string for UI
    std::string GetFormattedStatusText() {
        std::lock_guard<std::mutex> lock(m_Mutex);

        char buf[256];
        const char* stateStr = "LOBBY";
        if (m_Info.worldState == 1) stateStr = "LOADING";
        else if (m_Info.worldState == 2) stateStr = "FINISHED";
        else if (m_Info.worldState == 3) stateStr = "PLAYING";
        else if (m_Info.inBattle) stateStr = "IN MATCH";

        if (m_Info.showTeams && (m_Info.swatCount > 0 || m_Info.terrorCount > 0)) {
            snprintf(buf, sizeof(buf),
                     "👥 Players: %d (Real: %d) | 🤖 Bots: %d\n🛡️ SWAT: %d  vs  💣 TR: %d | [%s]",
                     m_Info.totalPlayers, m_Info.humanPlayers, m_Info.botCount,
                     m_Info.swatCount, m_Info.terrorCount, stateStr);
        } else {
            snprintf(buf, sizeof(buf),
                     "👥 Players: %d (Real: %d) | 🤖 Bots: %d | [%s]",
                     m_Info.totalPlayers, m_Info.humanPlayers, m_Info.botCount, stateStr);
        }

        return std::string(buf);
    }
};

FPS_HUD_Manager* FPS_HUD_Manager::s_Instance = nullptr;

#endif // FPS_HUD_MANAGER_H
