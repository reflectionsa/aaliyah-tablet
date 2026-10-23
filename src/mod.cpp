// Your mod. This example only watches: it logs the game's state changes, unless the launcher
// passes the option "log_states" as false. Replace it with what your mod does.
#include "mod.h"

namespace mod {

namespace {
uint64_t g_frames = 0;
bool g_logStates = true;
}

bool Start(const NvrGameContext* ctx) {
    bool server = (ctx && (ctx->flags & NEVR_HOST_IS_SERVER)) || HasFlag(L"-server");
    Log("started on a %s", server ? "game server" : "client");

    // Options come from the launcher. Log what you made of them, never the raw values: an option
    // can be a password.
    g_logStates = ArgBool("log_states", true);
    Log("log_states %s", g_logStates ? "on" : "off");

    // Other plugins nEVR loaded (v5 hosts and newer).
    if (NEVR_CTX_HAS(ctx, get_plugin_info)) {
        int count = ctx->get_plugin_count();
        for (int i = 0; i < count; i++) {
            const NvrLoadedPluginInfo* p = ctx->get_plugin_info(i);
            if (p) Log("beside %s %u.%u.%u", p->name, p->version_major, p->version_minor, p->version_patch);
        }
    }
    return true;
}

void Frame(const NvrGameContext*) { g_frames++; }

void StateChanged(const NvrGameContext*, uint32_t old_state, uint32_t new_state) {
    if (g_logStates) Log("game state %u -> %u (frame %llu)", old_state, new_state, static_cast<unsigned long long>(g_frames));
}

void Stop() { Log("stopped after %llu frames", static_cast<unsigned long long>(g_frames)); }

}  // namespace mod
