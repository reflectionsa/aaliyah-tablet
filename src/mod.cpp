// Aaliyah's tablet mod, step 1: LOOK before we paint.
// When the game loads a screen ("canvas"), we write a list of its pieces to the log. We only
// look; we change nothing. Needs two things first (see the notes with this file):
//   1. echo-mod.json: capabilities must be ["hooks-engine"] on its own (the template refuses "observes-only" together with another one)
//   2. the build must have ECHO_MOD_MINHOOK=ON (CMakeLists.txt: option(ECHO_MOD_MINHOOK ... ON))
#include <windows.h>

#include <cstring>

#include "hooks.h"
#include "mod.h"

namespace mod {

namespace {

// ---- facts we were given (sources in the comments) ----

// The game function "CUICanvasInstance::OnLoaded" runs every time a screen finishes loading.
// 0x14071fc90 minus the game's start address 0x140000000 (Probing NOTES.md:62 and :232; Doom
// runtime.cpp:547 hooks exe+0x71fc90).
constexpr uintptr_t kOnLoadedRva = 0x71fc90;
// Its first 16 bytes, so Hook() refuses on a different game build (Doom generated/bindings.h:4).
const uint8_t kOnLoadedPrologue[16] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x55, 0x56, 0x57,
                                       0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};

// The tablet's screen name (hash of "ui_ar_mp_tablet"), from Doom's ROOT_CANVAS
// (EchoVr-Tablet-Probing/build_tools_tab.py:19).
constexpr uint64_t kTabletCanvas = 0x30b4d30bbcb8d444ull;

// Where things sit inside a loaded screen ("instance"):
constexpr size_t kInstanceResource = 0x08;  // pointer to the screen's data (NOTES.md:62)
constexpr size_t kInstanceElements = 0x90;  // pointer to the list of pieces (NOTES.md:62; Doom runtime.cpp:102)
constexpr size_t kInstanceCount = 0xc0;     // how many pieces (NOTES.md:62; Doom runtime.cpp:102)
// One piece ("element") is 0xe0 = 224 bytes (NOTES.md:75 and :82; Doom runtime.cpp:107 uses 224):
constexpr size_t kElementSize = 0xe0;
constexpr size_t kElementType = 0x08;  // 1 text, 2 panel (a coloured box), 3 sprite (a picture) ... (NOTES.md:87)
constexpr size_t kElementColor = 0x78;  // 4 floats red,green,blue,alpha, for panels (Doom build_tools_tab.py:66,
                                        // build_question_tab.py:35; Probing canvas_edit.py:113-114)

// The screen's NAME lives in the game's "resource" object that instance +0x08 points to (revault
// echovr.exe@0x1407216e0 CUICanvasInstance::Load stores the resource at instance+8; OnLoaded
// 0x14071fc90 reads it back at the same place). On a resource the name hash is at +0x38 and the type
// hash at +0x28 (nEVR runtime src/runtime/patch/resource_override.cpp:126-128). Offset 0 is a
// pointer (a "vtable"), which is why the first version printed the same number for every screen.
constexpr size_t kResourceName = 0x38;
constexpr size_t kResourceType = 0x28;
// A screen's type hash is the hash of "CUICanvasResource" (revault echovr.exe@0x1407216e0).
constexpr uint64_t kCanvasType = 0x46222c5d2afeb3ffull;

// TODO (fill in after the first run, from the "canvas" lines): the tablet's piece count.
// Doom finds screens by their piece count (runtime.cpp:102). 0 = not used.
constexpr unsigned kTabletElementCount = 0;

// Max pieces we will print (so a bad guess can't flood the log).
constexpr unsigned kMaxPrint = 400;

// ---- safe reading of game memory (a wrong guess must not crash the game) ----

bool Readable(const void* p, size_t n) {
    if (!p) return false;
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(p, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    if (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return static_cast<const uint8_t*>(p) + n <= static_cast<const uint8_t*>(info.BaseAddress) + info.RegionSize;
}

template <typename T>
bool Read(const void* base, size_t offset, T* out) {
    const uint8_t* at = static_cast<const uint8_t*>(base) + offset;
    if (!Readable(at, sizeof(T))) return false;
    memcpy(out, at, sizeof(T));
    return true;
}

// ---- the list ----

void DumpCanvas(unsigned count, const uint8_t* elements) {
    Log("TABLET FOUND: %u pieces", count);
    for (unsigned i = 0; i < count && i < kMaxPrint; i++) {
        const uint8_t* e = elements + static_cast<size_t>(i) * kElementSize;
        uint32_t type = 0;  // we read only the low half; the real width is not in the sources
        float c[4] = {0, 0, 0, 0};
        uint64_t sym = 0;
        if (!Read(e, kElementType, &type) || !Read(e, kElementColor, &c) || !Read(e, kElementColor, &sym)) {
            Log("piece %u: unreadable", i);
            continue;
        }
        if (type == 4 || type == 7) {
            // Mask and child-screen pieces keep a name hash at +0x78, not colours (NOTES.md:92 and the
            // newer copy of it in Doom: EchoVr-Tablet-Probing/ECHOVR_TABLET_UI_NOTES.md:104).
            Log("piece %u: type %u, +0x78 is a name hash 0x%llx (not a colour)", i, type, static_cast<unsigned long long>(sym));
        } else {
            Log("piece %u: type %u, colour %.3f %.3f %.3f %.3f%s", i, type, c[0], c[1], c[2], c[3],
                type == 2 ? "" : "  (only panels, type 2, are sure to use these as a colour)");
        }
    }
}

// Our replacement for OnLoaded. First we let the game do its own work, then we look.
// (The signature matches Doom's runtime.cpp:441: this-pointer plus three values we just pass on.)
uint64_t (*g_original)(void*, uint64_t, uint64_t, uint64_t) = nullptr;

uint64_t OnLoadedDetour(void* self, uint64_t a, uint64_t b, uint64_t c) {
    uint64_t result = g_original(self, a, b, c);

    const uint8_t* instance = static_cast<const uint8_t*>(self);
    const uint8_t* resource = nullptr;
    const uint8_t* elements = nullptr;
    uint64_t count = 0;
    uint64_t name = 0;
    uint64_t type = 0;
    bool haveName = false;
    if (Read(instance, kInstanceElements, &elements) && Read(instance, kInstanceCount, &count) &&
        Read(instance, kInstanceResource, &resource) && resource) {
        haveName = Read(resource, kResourceName, &name) && Read(resource, kResourceType, &type);
    }

    if (elements && count > 0 && count < 100000) {
        // One line per screen so you can see what the game loads. Delete these two lines when you are done.
        Log("canvas: name 0x%llx type 0x%llx%s, %llu pieces", static_cast<unsigned long long>(name),
            static_cast<unsigned long long>(type), (haveName && type == kCanvasType) ? "" : " (type is not a canvas: check)",
            static_cast<unsigned long long>(count));
        bool isTablet = (haveName && name == kTabletCanvas) ||
                        (kTabletElementCount != 0 && count == kTabletElementCount);
        if (isTablet) DumpCanvas(static_cast<unsigned>(count), elements);
    }
    return result;
}

}  // namespace

bool Start(const NvrGameContext*) {
    // Hooks may only be set up here in Start (hooks.h:3-9). Hook() checks the 16 bytes first.
    if (!hooks::Hook(kOnLoadedRva, kOnLoadedPrologue, sizeof(kOnLoadedPrologue), &OnLoadedDetour, &g_original)) {
        Error("could not hook OnLoaded (wrong game build, or nEVR already hooks it)");
        return false;
    }
    Log("watching screens as they load");
    return true;
}

void Frame(const NvrGameContext*) {}
void StateChanged(const NvrGameContext*, uint32_t, uint32_t) {}
void Stop() {}

}  // namespace mod
