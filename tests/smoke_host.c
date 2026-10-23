/*
 * smoke_host.exe <mod.dll> - loads a mod the way nEVR does and calls its exports, outside the
 * game (under Wine on Linux, or on Windows). It prints what it found as one JSON line, which
 * tools/check_mod.py compares with echo-mod.json.
 *
 * What it checks:
 *   - the DLL loads with nEVR's restricted search path (everything linked statically);
 *   - NvrPluginGetInfo is there and gives a name;
 *   - init refuses cleanly (non-zero) here, because this isn't echovr.exe 631547 -- once with a
 *     v6 context and once with a v5 one whose v6 fields hold garbage;
 *   - Shutdown and unloading don't crash.
 * Exit code 0 when all of that holds.
 */
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char* name;
    const char* description;
    uint32_t version_major, version_minor, version_patch;
} Info;

typedef struct {
    uintptr_t base_addr;
    void* net_game;
    uint32_t game_state;
    uint32_t flags;
    uint32_t ctx_size;
    int (*get_plugin_count)(void);
    const void* (*get_plugin_info)(int);
    const char* plugin_id;
    void (*log)(const char*, uint32_t, const char*, const void*, uint32_t);
} Context;

typedef Info (*GetInfoFn)(void);
typedef uint32_t (*U32Fn)(void);
typedef int (*InitExFn)(const Context*, const char*);
typedef int (*InitFn)(const Context*);
typedef void (*ShutdownFn)(void);

static int host_log_lines = 0;

static int plugin_count(void) { return 0; }
static const void* plugin_info(int index) { (void)index; return NULL; }
static void host_log(const char* id, uint32_t level, const char* message, const void* fields, uint32_t n) {
    (void)fields; (void)n;
    host_log_lines++;
    fprintf(stderr, "[host log] %s level %u: %s\n", id ? id : "?", level, message ? message : "");
}

/* JSON string escape for the few strings printed. */
static void put_json(const char* s) {
    putchar('"');
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') printf("\\%c", c);
        else if (c < 0x20) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}

/* One load, init with `ctx`, shutdown, unload. Returns init's result, or -1000 when it can't run. */
static int round_trip(const wchar_t* path, const Context* ctx, int print) {
    HMODULE dll = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) {
        fprintf(stderr, "can't load the DLL (error %lu): it needs a DLL that isn't beside it or in System32, "
                        "or it isn't 64-bit\n", GetLastError());
        return -1000;
    }
    GetInfoFn get_info = (GetInfoFn)GetProcAddress(dll, "NvrPluginGetInfo");
    U32Fn get_api = (U32Fn)GetProcAddress(dll, "NvrPluginGetApiVersion");
    U32Fn get_caps = (U32Fn)GetProcAddress(dll, "NvrPluginGetCapabilities");
    InitExFn init_ex = (InitExFn)GetProcAddress(dll, "NvrPluginInitEx");
    InitFn init = (InitFn)GetProcAddress(dll, "NvrPluginInit");
    ShutdownFn shutdown = (ShutdownFn)GetProcAddress(dll, "NvrPluginShutdown");
    if (!get_info) {
        fprintf(stderr, "no NvrPluginGetInfo export\n");
        FreeLibrary(dll);
        return -1000;
    }
    Info info = get_info();
    uint32_t api = get_api ? get_api() : 1;
    uint32_t caps = get_caps ? get_caps() : 0;
    int result = init_ex ? init_ex(ctx, "{\"log_states\":\"true\"}") : init ? init(ctx) : 0;
    if (shutdown) shutdown();

    /* The strings live in the DLL: print them before it is unloaded. */
    int named = info.name != NULL;
    if (print) {
        printf("{\"name\":");
        put_json(info.name);
        printf(",\"description\":");
        put_json(info.description);
        printf(",\"version\":\"%u.%u.%u\",\"api\":%u,\"capabilities\":%u,\"init\":%d,"
               "\"has_init_ex\":%s,\"has_init\":%s,\"has_shutdown\":%s}\n",
               info.version_major, info.version_minor, info.version_patch, api, caps, result,
               init_ex ? "true" : "false", init ? "true" : "false", shutdown ? "true" : "false");
        fflush(stdout);
    }
    FreeLibrary(dll);
    return named ? result : -1000;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: smoke_host.exe <mod.dll>\n");
        return 2;
    }
    wchar_t path[MAX_PATH];
    if (!GetFullPathNameW(argv[1], MAX_PATH, path, NULL)) return 2;

    Context v6;
    memset(&v6, 0, sizeof(v6));
    v6.base_addr = (uintptr_t)GetModuleHandleW(NULL);
    v6.flags = 0x04; /* NEVR_HOST_IS_CLIENT */
    v6.ctx_size = sizeof(Context);
    v6.get_plugin_count = plugin_count;
    v6.get_plugin_info = plugin_info;
    v6.plugin_id = "smoke";
    v6.log = host_log;

    /* A v5 host's context: 48 bytes. What lies past them here is garbage the mod must not use. */
    Context v5 = v6;
    v5.ctx_size = 48;
    v5.plugin_id = (const char*)(uintptr_t)0xDEADBEEF;
    v5.log = (void (*)(const char*, uint32_t, const char*, const void*, uint32_t))(uintptr_t)0xDEADBEEF;

    int first = round_trip(path, &v6, 1);
    if (first == -1000) return 1;
    if (first == 0) {
        fprintf(stderr, "init returned 0 outside echovr.exe 631547: check the game build first and return "
                        "non-zero when it isn't the one you know\n");
        return 1;
    }
    int second = round_trip(path, &v5, 0);
    if (second == -1000) return 1;
    if (second == 0) {
        fprintf(stderr, "init returned 0 on the second load outside echovr.exe 631547\n");
        return 1;
    }
    fprintf(stderr, "ok: loads, refuses this build cleanly with v6 and v5 contexts, unloads (%d host log lines)\n",
            host_log_lines);
    return 0;
}
