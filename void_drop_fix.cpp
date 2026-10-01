#include <D2RLPlugin/api.h>
#include <D2RLPlugin/logging.h>
#include <algorithm>
#include <array>
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
    .id          = "d2rl-voiddropfix",
    .name        = "D2R Void Drop Fix",
    .version     = "1.0.0",
    .author      = "D2RLoader Community",
    .description = "Prevents items and gold from being destroyed when monsters die over void/abyss tiles by finding the nearest solid ground.",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

// ============================================================================
// Configuration & State
// ============================================================================
struct Config {
    bool enabled         = true;
    int32_t searchRadius = 64; // Generous search radius (in tiles) to guarantee finding valid ground
};

static Config g_config;
static const D2RL::PluginContext* g_context = nullptr;
static std::atomic<uint64_t> g_totalCallsCount{0};
static std::atomic<uint64_t> g_groundDropsCount{0};
static std::atomic<uint64_t> g_voidShiftedDropsCount{0};
static std::atomic<uint64_t> g_deepVoidRescuesCount{0};
static std::atomic<int32_t>  g_lastRadiusSeen{0};
static std::atomic<uint32_t> g_lastMaskSeen{0};
static std::atomic<int32_t>  g_lastDistanceSeen{0};
static std::atomic<int32_t>  g_maxDistanceSeen{0};

// Target RVA in Diablo II: Resurrected for COLLISION_GetFreeCoordinates
// Base address: 0x140000000 | Ghidra function: 0x140364e90
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
// Config File Helpers (TOML parsing conforming to D2RLoader guidelines)
// ============================================================================
static constexpr const char* kDefaultConfigToml =
    "# D2RLoader Void Drop Fix Configuration\n"
    "# Prevents items, runes, and gold from vanishing when monsters die over void/abyss tiles.\n\n"
    "[general]\n"
    "enabled = true\n"
    "search_radius = 64\n";

static std::string Trim(std::string_view sv) {
    const auto first = sv.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = sv.find_last_not_of(" \t\r\n");
    return std::string(sv.substr(first, last - first + 1));
}

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static void LoadConfiguration(const D2RL::PluginContext* context) {
    if (!context) return;

    if (context->EnsureConfig(kDefaultConfigToml)) {
        std::array<char, 4096> tomlBuffer{};
        uint32_t requiredSize = 0;
        if (context->ReadConfig(tomlBuffer.data(), static_cast<uint32_t>(tomlBuffer.size()), &requiredSize) && requiredSize > 0) {
            std::string_view content(tomlBuffer.data(), std::min<size_t>(requiredSize, tomlBuffer.size()));
            size_t start = 0;
            while (start < content.size()) {
                size_t end = content.find('\n', start);
                if (end == std::string_view::npos) end = content.size();

                std::string line = Trim(content.substr(start, end - start));
                start = end + 1;

                if (line.empty() || line.starts_with('#') || line.starts_with('[')) {
                    continue;
                }

                const size_t eq = line.find('=');
                if (eq == std::string_view::npos) continue;

                std::string key = ToLower(Trim(line.substr(0, eq)));
                std::string val = ToLower(Trim(line.substr(eq + 1)));

                if (key == "enabled") {
                    g_config.enabled = (val == "true" || val == "1");
                } else if (key == "search_radius" || key == "wider_search_radius") {
                    try {
                        g_config.searchRadius = std::clamp(std::stoi(val), 8, 256);
                    } catch (...) {}
                }
            }
        }
    }
}

// ============================================================================
// The Core Hook: Safe Ground Search Recovery
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
    int32_t   nStep
) {
    const auto original = g_originalGetFreeCoords;
    if (!original) {
        return 0;
    }

    g_totalCallsCount.fetch_add(1, std::memory_order_relaxed);
    g_lastRadiusSeen.store(nRadius, std::memory_order_relaxed);
    g_lastMaskSeen.store(nMask, std::memory_order_relaxed);

    // Step 1: Let the vanilla engine search with its standard radius.
    // If the monster is already on or near valid walkable ground, this succeeds immediately.
    int64_t resultRoom = original(
        pRoom, pTargetCoords, pOriginCoords, nMask, nField, bCheckLOS, bIncludeOrigin, nRadius, nStep
    );

    // If a valid ground tile was found, check distance moved
    if (resultRoom != 0) {
        int32_t dist = 0;
        if (pTargetCoords && pOriginCoords) {
            dist = std::max(std::abs(pTargetCoords[0] - pOriginCoords[0]), std::abs(pTargetCoords[1] - pOriginCoords[1]));
            g_lastDistanceSeen.store(dist, std::memory_order_relaxed);

            int32_t currentMax = g_maxDistanceSeen.load(std::memory_order_relaxed);
            while (dist > currentMax && !g_maxDistanceSeen.compare_exchange_weak(currentMax, dist, std::memory_order_relaxed)) {}
        }

        if (dist > 3) {
            // Repositioned across void/abyss to the nearest solid ground/walkway (> 3 tiles)
            g_voidShiftedDropsCount.fetch_add(1, std::memory_order_relaxed);
        } else {
            // Dropped directly on solid ground (including normal scatter up to 3 tiles)
            g_groundDropsCount.fetch_add(1, std::memory_order_relaxed);
        }

        return resultRoom;
    }

    if (!g_config.enabled || !pTargetCoords) {
        return 0;
    }

    // Step 2: Monster died out over deep void/abyss (Step 1 failed).
    // Perform an expanded radius search to find the nearest walkable ground/ledge.
    if (pRoom != 0) {
        const int32_t expandedRadius = std::max(nRadius, g_config.searchRadius);
        resultRoom = original(
            pRoom, pTargetCoords, pOriginCoords, nMask, nField, bCheckLOS, bIncludeOrigin, expandedRadius, nStep
        );

        if (resultRoom != 0) {
            g_deepVoidRescuesCount.fetch_add(1, std::memory_order_relaxed);
            if (pTargetCoords && pOriginCoords) {
                const int32_t dist = std::max(std::abs(pTargetCoords[0] - pOriginCoords[0]), std::abs(pTargetCoords[1] - pOriginCoords[1]));
                g_lastDistanceSeen.store(dist, std::memory_order_relaxed);

                int32_t currentMax = g_maxDistanceSeen.load(std::memory_order_relaxed);
                while (dist > currentMax && !g_maxDistanceSeen.compare_exchange_weak(currentMax, dist, std::memory_order_relaxed)) {}
            }
            return resultRoom;
        }
    }

    return 0;
}

// ============================================================================
// In-Game Console Command Handler
// ============================================================================
static auto VoidDropFixCommand(
    D2R::Game::Client* client,
    const D2RL::ConsoleCommandContext* cmd,
    void* userData
) noexcept -> D2RL::ConsoleCommandResult {
    (void)client;
    (void)userData;

    if (!cmd || !cmd->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }

    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
        "[VoidDropFix] Status: %s | Search Radius: %d tiles",
        g_config.enabled ? "ENABLED" : "DISABLED",
        g_config.searchRadius
    );
    cmd->plugin->WriteConsoleMessage(buffer);

    std::snprintf(buffer, sizeof(buffer),
        "[VoidDropFix] Total Calls: %llu | Ground: %llu | Void-Shifted: %llu | Deep-Rescued: %llu",
        static_cast<unsigned long long>(g_totalCallsCount.load()),
        static_cast<unsigned long long>(g_groundDropsCount.load()),
        static_cast<unsigned long long>(g_voidShiftedDropsCount.load()),
        static_cast<unsigned long long>(g_deepVoidRescuesCount.load())
    );
    cmd->plugin->WriteConsoleMessage(buffer);

    std::snprintf(buffer, sizeof(buffer),
        "[VoidDropFix] Last Seen: Radius=%d | Mask=0x%X | DistanceMoved=%d tiles (Max: %d)",
        g_lastRadiusSeen.load(),
        g_lastMaskSeen.load(),
        g_lastDistanceSeen.load(),
        g_maxDistanceSeen.load()
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

    context->LogInfo("[VoidDropFix] D2R Void Drop Fix Plugin v1.0.0 successfully initialized.");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    g_context = nullptr;
    g_originalGetFreeCoords = nullptr;
}
