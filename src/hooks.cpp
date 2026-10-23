// MinHook for the mod's detours. Each DLL has its own MinHook copy; this one only ever touches
// the hooks it created itself.
#include <windows.h>

#include <vector>

#include <MinHook.h>

#include "hooks.h"
#include "mod.h"

namespace mod::hooks {

namespace {
std::vector<void*> g_targets;
bool g_ready = false;
}  // namespace

bool Begin() {
    MH_STATUS s = MH_Initialize();
    // Another copy in this process doesn't matter; this one is ours.
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        Error("MinHook didn't start: %s", MH_StatusToString(s));
        return false;
    }
    g_ready = true;
    return true;
}

void End() {
    for (void* target : g_targets) {
        MH_DisableHook(target);
        MH_RemoveHook(target);
    }
    g_targets.clear();
    // No MH_Uninitialize: it would undo hooks other code in the process relies on.
}

bool Hook(uintptr_t rva, const uint8_t* prologue, size_t prologue_size, void* detour, void** original) {
    if (!g_ready) return false;
    if (!BytesAt(rva, prologue, prologue_size)) {
        Warn("not hooking 0x%llx: its first bytes aren't the ones expected", static_cast<unsigned long long>(rva));
        return false;
    }
    void* target = GameBase() + rva;
    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s == MH_OK) s = MH_EnableHook(target);
    if (s != MH_OK) {
        Warn("hooking 0x%llx failed: %s", static_cast<unsigned long long>(rva), MH_StatusToString(s));
        MH_RemoveHook(target);
        return false;
    }
    g_targets.push_back(target);
    return true;
}

}  // namespace mod::hooks
