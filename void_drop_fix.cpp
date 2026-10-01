// ============================================================================
// D2RVoidDropFix - Diablo II: Resurrected Plugin
//
// Prevents item and gold destruction when monsters die over void / abyss tiles
// (Arcane Sanctuary, River of Flame, Chaos Sanctuary).
//
// Acknowledgements & Credits:
//   - Dimentio: Creator of D2RLoader and the D2RLoader Plugin SDK.
//   - D2MOO Project: Extensive Diablo II engine collision reverse-engineering.
// ============================================================================

#include <D2RLPlugin/api.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

// ============================================================================
// Plugin Metadata
// ============================================================================
static constexpr D2RL::PluginInfo kPluginInfo{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "d2rl-void-drop-fix",
    .name        = "D2R Void Drop Fix",
    .version     = "1.0.1",
    .author      = "D2RLoader Community",
    .description = "Prevents items and gold from being destroyed when monsters die over void/abyss tiles. (Credits: Dimentio, D2MOO)",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

// ============================================================================
// Configuration & State
// ============================================================================
struct Config {
    bool enabled                 = true;
    bool logRecoveries           = true;
    bool searchWiderRadiusFirst  = true;
    int32_t widerSearchRadius    = 24;
    bool fallbackToPlayer        = true;
};

static Config g_config;
static std::atomic<uint64_t> g_totalCallsCount{0};
static std::atomic<uint64_t> g_recoveredDropsCount{0};
static const D2RL::PluginContext* g_context = nullptr;

// Target RVA in Diablo II: Resurrected for COLLISION_GetFreeCoordinates
// Base address: 0x140000000 | Function offset: 0x140364e90
static constexpr uint64_t kCollisionGetFreeCoordsRva = 0x00364E90;

// Safety check bytes required by D2RLoader to verify the binary function prologue:
// 140364e90 48 8b c4        MOV  RAX, RSP
// 140364e93 44 89 48 20     MOV  dword ptr [RAX + 0x20], R9D (nMask)
// 140364e97 4c 89 40 18     MOV  qword ptr [RAX + 0x18], R8 (pOriginCoords)
// 140364e9b 53              PUSH RBX
// 140364e9c 55              PUSH RBP
// 140364e9d 56              PUSH RSI
// 140364e9e 57              PUSH RDI
static constexpr uint8_t kExpectedGetFreeCoordsBytes[]{
    0x48, 0x8B, 0xC4,
    0x44, 0x89, 0x48, 0x20,
    0x4C, 0x89, 0x40, 0x18,
    0x53,
    0x55,
    0x56,
    0x57
};

// Type definition matching the engine's COLLISION_GetFreeCoordinates signature
using COLLISION_GetFreeCoordinatesFn = int64_t(__fastcall*)(
    int64_t   pRoom,
    int32_t*  pTargetCoords,
    int32_t*  pOriginCoords,
    uint32_t  nMask,
    uint32_t  nField,
    uint32_t  bCheckLOS,
    int32_t   bIncludeOrigin,
    int32_t   nRadius,
    int32_t   nStep
);

static COLLISION_GetFreeCoordinatesFn g_originalGetFreeCoords = nullptr;

// ============================================================================
// Helper Utilities
// ============================================================================
static auto Trim(std::string_view sv) -> std::string {
    const auto first = sv.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = sv.find_last_not_of(" \t\r\n");
    return std::string(sv.substr(first, (last - first + 1)));
}

static auto ToLower(std::string s) -> std::string {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static void NotifyLog(const char* message) {
    if (!g_context) return;
    // Log to d2rloader.log
    g_context->LogInfo(message);
    // Also print to in-game console (`~`)
    g_context->WriteConsoleMessage(message, D2RL::ConsoleMessageKind::Output);
}

static void LoadConfiguration(const D2RL::PluginContext* context) {
    if (!context) return;

    if (context->ReadConfig) {
        uint32_t requiredSize = 0;
        if (context->ReadConfig(nullptr, 0, &requiredSize) && requiredSize > 0) {
            std::string tomlBuffer(requiredSize, '\0');
            if (context->ReadConfig(tomlBuffer.data(), requiredSize, &requiredSize)) {
                size_t start = 0;
                while (start < tomlBuffer.size()) {
                    size_t end = tomlBuffer.find('\n', start);
                    if (end == std::string::npos) end = tomlBuffer.size();

                    std::string line = Trim(tomlBuffer.substr(start, end - start));
                    start = end + 1;

                    if (line.empty() || line.starts_with('#') || line.starts_with('[')) {
                        continue;
                    }

                    const size_t eq = line.find('=');
                    if (eq == std::string::npos) continue;

                    std::string key = ToLower(Trim(line.substr(0, eq)));
                    std::string val = ToLower(Trim(line.substr(eq + 1)));

                    if (key == "enabled") {
                        g_config.enabled = (val == "true" || val == "1");
                    } else if (key == "log_recoveries") {
                        g_config.logRecoveries = (val == "true" || val == "1");
                    } else if (key == "search_wider_radius_first") {
                        g_config.searchWiderRadiusFirst = (val == "true" || val == "1");
                    } else if (key == "wider_search_radius") {
                        try {
                            g_config.widerSearchRadius = std::clamp(std::stoi(val), 4, 100);
                        } catch (...) {}
                    } else if (key == "fallback_to_player") {
                        g_config.fallbackToPlayer = (val == "true" || val == "1");
                    }
                }
            }
        }
    }
}

// ============================================================================
// The Core Hook: Safe Loot Recovery
// ============================================================================
static int64_t __fastcall HookCOLLISION_GetFreeCoordinates(
    int64_t   pRoom,
    int32_t*  pTargetCoords,
    int32_t*  pOriginCoords,
    uint32_t  nMask,
    uint32_t  nField,
    uint32_t  bCheckLOS,
    int32_t   bIncludeOrigin,
    int32_t   nRadius,
    int32_t   nStep) {

    const auto original = g_originalGetFreeCoords;
    if (!original) {
        return 0;
    }

    g_totalCallsCount.fetch_add(1, std::memory_order_relaxed);

    // Step 1: Let the vanilla engine search with its standard radius first.
    // If the monster is on valid walkable ground, this succeeds immediately.
    int64_t resultRoom = original(
        pRoom, pTargetCoords, pOriginCoords, nMask, nField, bCheckLOS, bIncludeOrigin, nRadius, nStep
    );

    // If valid ground was found, or the plugin is disabled, or no target buffer, return vanilla result
    if (resultRoom != 0 || !g_config.enabled || !pTargetCoords) {
        return resultRoom;
    }

    // Step 2: ATTEMPT A - Search with an expanded radius for the nearest walkable walkway/ledge
    if (g_config.searchWiderRadiusFirst && pRoom != 0) {
        const int32_t expandedRadius = std::max(nRadius, g_config.widerSearchRadius);
        resultRoom = original(
            pRoom, pTargetCoords, pOriginCoords, nMask, nField, bCheckLOS, bIncludeOrigin, expandedRadius, nStep
        );

        if (resultRoom != 0) {
            const auto count = g_recoveredDropsCount.fetch_add(1, std::memory_order_relaxed) + 1;
            if (g_config.logRecoveries) {
                char msg[256];
                std::snprintf(msg, sizeof(msg),
                    "[VoidDropFix] Loot recovered to nearest ledge at (%d, %d) [mask=0x%X, radius=%d]. Total recovered: %llu",
                    pTargetCoords[0], pTargetCoords[1], nMask, expandedRadius,
                    static_cast<unsigned long long>(count));
                NotifyLog(msg);
            }
            return resultRoom;
        }
    }

    // Step 3: ATTEMPT B - Fallback to origin coordinates (player or monster kill position)
    if (g_config.fallbackToPlayer && pOriginCoords != nullptr && pRoom != 0) {
        // Snap target coordinates to origin coords
        pTargetCoords[0] = pOriginCoords[0];
        pTargetCoords[1] = pOriginCoords[1];

        // Re-check with radius 8 centered directly on the origin coordinates
        resultRoom = original(
            pRoom, pTargetCoords, pOriginCoords, nMask, nField, bCheckLOS, 1 /* include origin */, 8, nStep
        );

        if (resultRoom != 0) {
            const auto count = g_recoveredDropsCount.fetch_add(1, std::memory_order_relaxed) + 1;
            if (g_config.logRecoveries) {
                char msg[256];
                std::snprintf(msg, sizeof(msg),
                    "[VoidDropFix] Loot recovered to origin/killer position at (%d, %d) [mask=0x%X]. Total recovered: %llu",
                    pTargetCoords[0], pTargetCoords[1], nMask,
                    static_cast<unsigned long long>(count));
                NotifyLog(msg);
            }
            return resultRoom;
        }

        // Final safety net: return the room handle so the calling drop function does not abort
        const auto count = g_recoveredDropsCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (g_config.logRecoveries) {
            char msg[256];
            std::snprintf(msg, sizeof(msg),
                "[VoidDropFix] Loot forced to origin room at (%d, %d). Total recovered: %llu",
                pTargetCoords[0], pTargetCoords[1],
                static_cast<unsigned long long>(count));
            NotifyLog(msg);
        }
        return pRoom;
    }

    return 0;
}

// ============================================================================
// In-Game Console Command Handler
// ============================================================================
static auto VoidDropFixCommand(
    D2R::Game::Client* client,
    const D2RL::ConsoleCommandContext* cmd,
    void* userData) noexcept -> D2RL::ConsoleCommandResult {

    (void)client;
    (void)userData;

    if (!cmd || !cmd->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }

    char buffer[384];
    std::snprintf(buffer, sizeof(buffer),
        "=== [D2R Void Drop Fix v1.0.1] ===\n"
        "  Hook Status:   %s\n"
        "  Plugin State:  %s\n"
        "  Total Calls:   %llu\n"
        "  Recovered:     %llu drops\n"
        "  Search Radius: %d tiles\n"
        "  Origin Fallback: %s\n"
        "  Credits: Dimentio (D2RLoader), D2MOO",
        g_originalGetFreeCoords != nullptr ? "ACTIVE (Hooked)" : "FAILED TO HOOK",
        g_config.enabled ? "ENABLED" : "DISABLED",
        static_cast<unsigned long long>(g_totalCallsCount.load()),
        static_cast<unsigned long long>(g_recoveredDropsCount.load()),
        g_config.widerSearchRadius,
        g_config.fallbackToPlayer ? "YES" : "NO"
    );
    cmd->plugin->WriteConsoleMessage(buffer);

    return D2RL::ConsoleCommandResult::Handled;
}

// ============================================================================
// Safe Hook Installation
// ============================================================================
static auto InstallVoidDropHook(const D2RL::PluginContext* context) noexcept -> bool {
    if (!context) return false;

    // Use D2RLoader's tracked inline hook installation with verified safety check bytes
    const bool success = context->InstallInlineHook(
        kCollisionGetFreeCoordsRva,
        kExpectedGetFreeCoordsBytes,
        static_cast<uint32_t>(sizeof(kExpectedGetFreeCoordsBytes)),
        HookCOLLISION_GetFreeCoordinates,
        &g_originalGetFreeCoords
    );

    if (success) {
        context->LogInfo("[VoidDropFix] HookCOLLISION_GetFreeCoordinates installed successfully.");
    } else {
        context->LogError("[VoidDropFix] Failed to install inline hook on COLLISION_GetFreeCoordinates.");
    }

    return success;
}

} // namespace

// ============================================================================
// D2RLoader Export Functions
// ============================================================================
D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &kPluginInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!context) {
        return false;
    }

    g_context = context;
    LoadConfiguration(context);

    if (!context->RegisterConsoleCommand("void-drop-fix", VoidDropFixCommand, "Displays Void Drop Fix status and recovery stats.")) {
        context->LogWarn("[VoidDropFix] Console command 'void-drop-fix' could not be registered.");
    }

    if (!InstallVoidDropHook(context)) {
        return false;
    }

    context->LogInfo("[VoidDropFix] D2R Void Drop Fix Plugin v1.0.1 successfully initialized.");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    g_context = nullptr;
    g_originalGetFreeCoords = nullptr;
}
