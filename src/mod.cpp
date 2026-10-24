// Aaliyah's tablet mod, step 2: PAINT the tablet.
// When the game loads a screen ("canvas"), we write a list of its pieces to the log (step 1).
// When the screen is the tablet, we then set its coloured box (a "panel", type 2) to the colour
// from the launcher option "tablet_color" (a hex string like #2ad4d4). Needs two things first:
//   1. echo-mod.json: "capabilities": ["cosmetic", "hooks-engine"]  ("observes-only" must stand alone)
//   2. the build must have ECHO_MOD_MINHOOK=ON (CMakeLists.txt: option(ECHO_MOD_MINHOOK ... ON))
#include <windows.h>

#include <cctype>
#include <cstdlib>
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

// The colour Aaliyah picked (red, green, blue, each 0 to 1). The alpha (see-through amount) is NOT
// part of the choice: we keep whatever the game had (step-1 log: 0.500 for the tablet's panel).
float g_rgb[3] = {0, 0, 0};
bool g_haveColor = false;

// "#rrggbb" (the # is optional) -> three numbers from 0 to 1. False if it is not exactly 6 hex digits.
bool ParseHexColor(const std::string& text, float* rgb) {
    size_t start = (!text.empty() && text[0] == '#') ? 1 : 0;
    if (text.size() - start != 6) return false;
    for (size_t i = start; i < text.size(); i++) {
        if (!isxdigit(static_cast<unsigned char>(text[i]))) return false;
    }
    unsigned long v = strtoul(text.c_str() + start, nullptr, 16);
    rgb[0] = ((v >> 16) & 0xff) / 255.0f;
    rgb[1] = ((v >> 8) & 0xff) / 255.0f;
    rgb[2] = (v & 0xff) / 255.0f;
    return true;
}

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

// Only write where the memory really is writable (a wrong guess must not crash the game).
bool Writable(void* p, size_t n) {
    MEMORY_BASIC_INFORMATION info;
    if (!p || !VirtualQuery(p, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    if (!(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
    if (info.Protect & PAGE_GUARD) return false;
    return static_cast<uint8_t*>(p) + n <= static_cast<uint8_t*>(info.BaseAddress) + info.RegionSize;
}

// Does this look like a colour? All four numbers from 0 to 1 (a name hash read as floats almost never does).
bool LooksLikeColor(const float* c) {
    for (int i = 0; i < 4; i++) {
        if (!(c[i] >= 0.0f && c[i] <= 1.0f)) return false;  // also false for "not a number"
    }
    return true;
}

// ---- the list ----

void DumpCanvas(unsigned count, uint8_t* elements) {
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

        // STEP 2: paint. Only a panel (type 2) whose four numbers look like a colour. In the step-1
        // log the tablet has exactly one: piece 6.
        if (g_haveColor && type == 2) {
            uint8_t* at = elements + static_cast<size_t>(i) * kElementSize + kElementColor;
            if (!LooksLikeColor(c)) {
                Log("piece %u: not painted, the numbers do not look like a colour", i);
            } else if (!Writable(at, sizeof(float) * 4)) {
                Log("piece %u: not painted, the memory is not writable", i);
            } else {
                float painted[4] = {g_rgb[0], g_rgb[1], g_rgb[2], c[3]};  // keep the old alpha
                memcpy(at, painted, sizeof(painted));
                Log("piece %u: PAINTED before %.3f %.3f %.3f %.3f -> after %.3f %.3f %.3f %.3f", i, c[0], c[1], c[2],
                    c[3], painted[0], painted[1], painted[2], painted[3]);
            }
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
    uint8_t* elements = nullptr;
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
        bool isTablet = haveName && (name == kTabletCanvas || name == 0x51a76b94d9710516ULL ||
                        name == 0x7d87d9944d1564dcULL || name == 0xfdea8ae53c32407eULL ||
                        name == 0xfdea8aeb3a0f4862ULL || name == 0x703455302d126f3aULL ||
                        name == 0x73fb81bebf1da3eeULL);
        if (isTablet) DumpCanvas(static_cast<unsigned>(count), elements);
    }
    return result;
}

}  // namespace

bool Start(const NvrGameContext*) {
    // The colour from the launcher: tablet_color, like "#2ad4d4". Log what we made of it, never more.
    std::string text = Arg("tablet_color", "#2ad4d4");
    g_haveColor = ParseHexColor(text, g_rgb);
    if (g_haveColor) {
        Log("tablet colour: %.3f %.3f %.3f", g_rgb[0], g_rgb[1], g_rgb[2]);
    } else {
        Warn("tablet_color is not like #rrggbb: the tablet keeps its own colour");
    }

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
