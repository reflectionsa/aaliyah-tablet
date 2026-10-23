// Detours with MinHook (built with -DECHO_MOD_MINHOOK=ON; add "hooks-engine" to capabilities).
//
// The rules nEVR needs from a mod that hooks:
// - Hook only from Start (never DllMain), and check the bytes first: Hook() refuses when the
//   function doesn't start with what you expect, so a different build is left alone.
// - Never hook a function nEVR hooks itself. nEVR checks its own hooks after every plugin's init,
//   and on a game server a hook laid over one of them ends the game.
// - Never call MH_Uninitialize or MH_DisableHook(MH_ALL_HOOKS): End() removes only your hooks.
#pragma once

#include <cstddef>
#include <cstdint>

namespace mod::hooks {

// Called by plugin.cpp before Start / after Stop.
bool Begin();
void End();

// Hooks the function at game base + `rva`, if its first bytes are `prologue`. `original` gets
// the trampoline to call the game's own code. Returns false (and logs why) otherwise.
bool Hook(uintptr_t rva, const uint8_t* prologue, size_t prologue_size, void* detour, void** original);

// The same with the function's type kept: Hook(0x123450, kPrologue, sizeof(kPrologue), &MyDetour, &g_original).
template <typename F>
bool Hook(uintptr_t rva, const uint8_t* prologue, size_t prologue_size, F* detour, F** original) {
    return Hook(rva, prologue, prologue_size, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
}

}  // namespace mod::hooks
