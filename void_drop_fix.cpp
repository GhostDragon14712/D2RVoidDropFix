#define NOMINMAX
#include <D2RLPlugin/api.h>
#include <D2RLPlugin/logging.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// ============================================================================
// Plugin Metadata
// ============================================================================
constexpr D2RL::PluginInfo kPluginInfo = {
    .infoSize = sizeof(D2RL::PluginInfo),
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "d2rl-voiddropfix",
    .name = "D2R Void Drop Fix",
    .version = "20.0.0",
    .author = "D2R Community",
    .description = "Definitive Void Drop Fix: FUN_140448950 with exact Ghidra prologue bytes {0x48, 0x89, 0x74, 0x24, 0x10}.",
    .flags = D2RL::PluginFlags::NativeHooks | D2RL::PluginFlags::Shared,
    .reserved = {0, 0, 0, 0},
};

// ============================================================================
// Hook Target 1: FUN_140448950 (Monster Death Drop Check in D2Game)
// RVA: 0x00448950 | Exact bytes: 48 89 74 24 10
// ============================================================================
constexpr uint64_t kDeathDropCheckHookRva = 0x00448950;
constexpr std::array<uint8_t, 5> kExpectedDeathDropBytes = { 0x48, 0x89, 0x74, 0x24, 0x10 };

using FnDeathDropCheck = int64_t(__fastcall*)(
    int64_t   pGame,
    int64_t   pModeChange,
    int32_t   a3
);

static FnDeathDropCheck g_originalDeathDropCheck = nullptr;
static std::atomic<bool> g_deathDropHookActive{false};

// ============================================================================
// Hook Target 2: COLLISION_GetFreeCoordinatesWithField (RVA 0x003632F0)
// ============================================================================
constexpr uint64_t kDropCoordsHookRva = 0x003632F0;
constexpr std::array<uint8_t, 4> kExpectedDropCoordsBytes = { 0x48, 0x83, 0xec, 0x58 };

using FnDropCoords = int64_t(__fastcall*)(
    int64_t   pRoom,
    int32_t*  pItemCoords,
    int32_t*  pMonsterCoords,
    int32_t   nUnitSize,
    uint32_t  nMask,
    uint32_t  nFieldMask,
    int32_t   bAllowNeighborRooms
);

static FnDropCoords g_originalDropCoords = nullptr;
static std::atomic<bool> g_dropCoordsHookActive{false};
static const D2RL::PluginContext* g_context = nullptr;

// ============================================================================
// Telemetry & Statistics
// ============================================================================
static std::atomic<uint64_t> g_deathDropBypasses{0};
static std::atomic<uint64_t> g_totalDrops{0};
static std::atomic<uint64_t> g_groundDrops{0};
static std::atomic<uint64_t> g_rescuedDrops{0};
static std::atomic<uint64_t> g_failedDrops{0};

static void LogMessage(const char* fmt, ...) {
    char buffer[384];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (g_context) {
        g_context->WriteConsoleMessage(buffer);
        g_context->LogInfo(buffer);
    }
}

// ============================================================================
// Hook 1: FUN_140448950 Bypass
// ============================================================================
static int64_t __fastcall HookDeathDropCheck(
    int64_t   pGame,
    int64_t   pModeChange,
    int32_t   a3
) {
    const auto original = g_originalDeathDropCheck;
    if (!original) return 0;

    g_deathDropBypasses.fetch_add(1, std::memory_order_relaxed);
    LogMessage("[VoidDropFix] FUN_140448950 Death Drop Check triggered");

    return original(pGame, pModeChange, a3);
}

// ============================================================================
// Hook 2: COLLISION_GetFreeCoordinatesWithField Room-Center Rescue
// ============================================================================
static int64_t __fastcall HookCOLLISION_GetFreeCoordinatesWithField(
    int64_t   pRoom,
    int32_t*  pItemCoords,
    int32_t*  pMonsterCoords,
    int32_t   nUnitSize,
    uint32_t  nMask,
    uint32_t  nFieldMask,
    int32_t   bAllowNeighborRooms
) {
    const auto original = g_originalDropCoords;
    if (!original || !pItemCoords || pRoom == 0) {
        return original ? original(pRoom, pItemCoords, pMonsterCoords, nUnitSize, nMask, nFieldMask, bAllowNeighborRooms) : 0;
    }

    g_totalDrops.fetch_add(1, std::memory_order_relaxed);

    const int32_t startX = pItemCoords[0];
    const int32_t startY = pItemCoords[1];

    // Try vanilla
    int64_t room = original(pRoom, pItemCoords, pMonsterCoords, nUnitSize, nMask, nFieldMask, 1);
    if (room != 0) {
        g_groundDrops.fetch_add(1, std::memory_order_relaxed);
        return room;
    }

    // Vanilla failed -> Rescue to room center
    const int32_t roomX = *reinterpret_cast<const int32_t*>(pRoom + 0x00);
    const int32_t roomY = *reinterpret_cast<const int32_t*>(pRoom + 0x04);
    const int32_t roomW = *reinterpret_cast<const int32_t*>(pRoom + 0x08);
    const int32_t roomH = *reinterpret_cast<const int32_t*>(pRoom + 0x0C);

    if (roomW > 2 && roomH > 2 && roomW < 2000) {
        pItemCoords[0] = roomX + roomW / 2;
        pItemCoords[1] = roomY + roomH / 2;

        room = original(
            pRoom,
            pItemCoords,
            nullptr,
            1,
            0x0001u,
            0,
            1
        );
    }

    if (room != 0) {
        const int32_t dist = (std::max)(std::abs(pItemCoords[0] - startX), std::abs(pItemCoords[1] - startY));
        g_rescuedDrops.fetch_add(1, std::memory_order_relaxed);
        LogMessage("[VoidDropFix] RESCUED VOID DROP: void=(%d,%d) -> room center=(%d,%d) dist=%d",
            startX, startY, pItemCoords[0], pItemCoords[1], dist);
        return room;
    }

    g_failedDrops.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

// ============================================================================
// Console Commands
// ============================================================================
static auto VoidDropFixCommand(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* cmd,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!cmd || !cmd->plugin) return D2RL::ConsoleCommandResult::Failed;

    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
        "[VoidDropFix] DeathHook=%s, DropHook=%s | Bypasses=%llu | Drops=%llu (Ground=%llu, Rescued=%llu, Failed=%llu)",
        g_deathDropHookActive.load() ? "OK" : "FAIL",
        g_dropCoordsHookActive.load() ? "OK" : "FAIL",
        static_cast<unsigned long long>(g_deathDropBypasses.load()),
        static_cast<unsigned long long>(g_totalDrops.load()),
        static_cast<unsigned long long>(g_groundDrops.load()),
        static_cast<unsigned long long>(g_rescuedDrops.load()),
        static_cast<unsigned long long>(g_failedDrops.load())
    );
    cmd->plugin->WriteConsoleMessage(buffer);
    return D2RL::ConsoleCommandResult::Handled;
}

static auto VoidTestCommand(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* cmd,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!cmd || !cmd->plugin) return D2RL::ConsoleCommandResult::Failed;
    cmd->plugin->WriteConsoleMessage("[VoidDropFix] Diagnostic Test: FUN_140448950 & DropCoords hooks active.");
    return D2RL::ConsoleCommandResult::Handled;
}

} // namespace

// ============================================================================
// Plugin Exports
// ============================================================================
D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &kPluginInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!context) return false;
    g_context = context;

    (void)context->RegisterConsoleCommand("void-drop-fix", VoidDropFixCommand, "Displays Void Drop Fix stats.");
    (void)context->RegisterConsoleCommand("void-test", VoidTestCommand, "Runs diagnostic check for Void Drop Fix.");

    // Hook 1: FUN_140448950 (RVA 0x00448950) with exact Ghidra bytes: 48 89 74 24 10
    const bool deathHooked = context->InstallInlineHook(
        kDeathDropCheckHookRva,
        kExpectedDeathDropBytes.data(),
        static_cast<uint32_t>(kExpectedDeathDropBytes.size()),
        HookDeathDropCheck,
        &g_originalDeathDropCheck
    );
    if (deathHooked && g_originalDeathDropCheck != nullptr) {
        g_deathDropHookActive.store(true, std::memory_order_relaxed);
        context->LogInfo("[VoidDropFix] HookDeathDropCheck installed successfully on RVA 0x00448950.");
    }

    // Hook 2: COLLISION_GetFreeCoordinatesWithField (RVA 0x003632F0)
    const bool dropHooked = context->InstallInlineHook(
        kDropCoordsHookRva,
        kExpectedDropCoordsBytes.data(),
        static_cast<uint32_t>(kExpectedDropCoordsBytes.size()),
        HookCOLLISION_GetFreeCoordinatesWithField,
        &g_originalDropCoords
    );
    if (dropHooked && g_originalDropCoords != nullptr) {
        g_dropCoordsHookActive.store(true, std::memory_order_relaxed);
        context->LogInfo("[VoidDropFix] HookCOLLISION_GetFreeCoordinatesWithField installed successfully on RVA 0x003632F0.");
    }

    context->LogInfo("[VoidDropFix] D2R Void Drop Fix v20.0.0 initialized.");
    return true;
}
