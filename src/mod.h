// What the template gives your mod, and what your mod gives the template.
//
// plugin.cpp holds the nEVR exports and calls the four functions below; write your mod in
// mod.cpp. The helpers (log, args, game) are yours to use from anywhere.
#pragma once

#include <cstdint>
#include <string>

#include "nevr_plugin_interface.h"

namespace mod {

// ---- your mod (mod.cpp) ----

// Once, when nEVR starts the plugin. Return true to stay loaded, false to be unloaded (log why
// first). On a game build the mod doesn't know, plugin.cpp returns before this is called.
bool Start(const NvrGameContext* ctx);

// Each game frame, on the game's thread: keep it short, never block.
void Frame(const NvrGameContext* ctx);

// When the game's state changes. ctx->net_game is set once ctx->flags has NEVR_HOST_HAS_NETGAME.
void StateChanged(const NvrGameContext* ctx, uint32_t old_state, uint32_t new_state);

// Before a clean unload, and right after a failed Start. Not guaranteed when the game exits:
// don't depend on it to save anything.
void Stop();

// ---- logging (log.cpp) ----

// One line to bin/win10/plugin_logs/<file stem>/<file stem>.log, and to nEVR's own log where the
// host offers it (API v6). printf-style. Never log the args: they can hold passwords.
void Log(const char* format, ...);
void Warn(const char* format, ...);
void Error(const char* format, ...);

// Called by plugin.cpp: opens the log file and remembers the host's log, if any.
void OpenLog(const NvrGameContext* ctx);
void CloseLog();

// ---- arguments (args.cpp) ----

// The plugin's options from the launcher (config.yaml's args), as nEVR passes them: a flat JSON
// object of strings. Nested keys arrive as dotted paths ("limits.max"), lists joined with commas.
void SetArgs(const char* args_json);
std::string Arg(const char* key, const char* fallback = "");
bool ArgBool(const char* key, bool fallback);
long long ArgNumber(const char* key, long long min, long long max, long long fallback);

// ---- the game (game.cpp) ----

// echovr.exe build 631547, the PC build every community install runs: PE timestamp 0x6452DFF6.
constexpr uint32_t kLiveBuildStamp = 0x6452DFF6;

// echovr.exe's base address (ctx->base_addr when the host gives it).
uint8_t* GameBase();

// Whether the running echovr.exe is build 631547. `stamp` gets the PE timestamp it has.
bool IsLiveBuild(uint32_t* stamp);

// Whether the game's command line has `flag` (case-insensitive, like the game reads it).
bool HasFlag(const wchar_t* flag);

// Whether the bytes at game base + rva are `expected`: check this before patching or hooking
// anything, and leave it alone when they differ.
bool BytesAt(uintptr_t rva, const uint8_t* expected, size_t size);

// One of nEVR's runtime exports (NEVR_RegisterResourceOverride, ...): looked up in nevr.dll, then
// BugSplat64.dll, then dbgcore.dll. Null when no loaded module has it (an older nEVR, EchoLoader).
void* RuntimeExport(const char* name);

}  // namespace mod
