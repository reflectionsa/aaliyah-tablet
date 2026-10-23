// The nEVR exports. You normally don't change this file: write your mod in mod.cpp, and set its
// name, version and capabilities in echo-mod.json (CMake passes them in as MOD_* macros).
#include <windows.h>

#include "mod.h"

#ifdef ECHO_MOD_MINHOOK
#include "hooks.h"
#endif

namespace {

bool g_started = false;  // Start ran and returned true
bool g_tried = false;    // InitEx or Init was called: the host calls only one, but be safe

int Init(const NvrGameContext* ctx, const char* args_json) {
    if (g_tried) return g_started ? 0 : 1;
    g_tried = true;
    mod::OpenLog(ctx);
    mod::Log("%s %s", MOD_STEM, MOD_VERSION);
    mod::SetArgs(args_json ? args_json : "{}");

    uint32_t stamp = 0;
    if (!mod::IsLiveBuild(&stamp)) {
        mod::Log("not echovr.exe 631547 (PE timestamp 0x%08X, wants 0x%08X): not loaded",
                 static_cast<unsigned>(stamp), static_cast<unsigned>(mod::kLiveBuildStamp));
        return 1;
    }
#ifdef ECHO_MOD_MINHOOK
    if (!mod::hooks::Begin()) return 1;
#endif
    g_started = mod::Start(ctx);
    if (!g_started) {
        mod::Log("Start returned false: not loaded");
        return 1;
    }
    return 0;
}

}  // namespace

extern "C" {

NvrPluginInfo NvrPluginGetInfo(void) {
    NvrPluginInfo info = {};
    info.name = MOD_ID;
    info.description = MOD_SUMMARY;
    info.version_major = MOD_VERSION_MAJOR;
    info.version_minor = MOD_VERSION_MINOR;
    info.version_patch = MOD_VERSION_PATCH;
    return info;
}

// The API version from echo-mod.json ("api"): 5 runs on every nEVR the launcher ships without a
// warning. The v6 fields (ctx->log) are used only where the host has them either way.
uint32_t NvrPluginGetApiVersion(void) { return MOD_API; }

uint32_t NvrPluginGetCapabilities(void) { return MOD_CAPABILITIES; }

int NvrPluginInitEx(const NvrGameContext* ctx, const char* args_json) { return Init(ctx, args_json); }

int NvrPluginInit(const NvrGameContext* ctx) { return Init(ctx, "{}"); }

void NvrPluginOnFrame(const NvrGameContext* ctx) {
    if (g_started) mod::Frame(ctx);
}

void NvrPluginOnGameStateChange(const NvrGameContext* ctx, uint32_t old_state, uint32_t new_state) {
    if (g_started) mod::StateChanged(ctx, old_state, new_state);
}

void NvrPluginShutdown(void) {
    if (g_started) mod::Stop();
    g_started = false;
#ifdef ECHO_MOD_MINHOOK
    mod::hooks::End();
#endif
    mod::CloseLog();
}

}  // extern "C"

// Nothing else belongs here: the loader lock is held, so hooks, threads and files wait for Init.
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
