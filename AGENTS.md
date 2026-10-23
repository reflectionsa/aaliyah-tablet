# AGENTS.md: building an Echo VR mod from this template

This file is for coding agents, and for anyone who wants the exact contract. The README is the
short version for people.

A **mod** is an x64 Windows DLL that nEVR (the game's mod runtime, `bin/win10/BugSplat64.dll`)
loads into `echovr.exe` build 631547. The Echo VR launcher installs mods, writes nEVR's config,
and only loads mods it can verify. Everything below has to hold, or the launcher won't take the
mod, or nEVR won't load it.

## Repo layout

| Path | What it is | Edit? |
|---|---|---|
| `echo-mod.json` | The manifest: the single source of the mod's id, name, file, version, capabilities | yes |
| `src/mod.cpp` | The mod itself: `Start`, `Frame`, `StateChanged`, `Stop` | yes |
| `src/mod.h` | What the template gives the mod: logging, args, game checks, runtime exports | add to it |
| `src/plugin.cpp` | The nEVR exports; calls `mod::*` | rarely |
| `src/log.cpp`, `src/args.cpp`, `src/game.cpp` | The helpers from `mod.h` | rarely |
| `src/hooks.cpp`, `src/hooks.h` | MinHook detours, built only with `-DECHO_MOD_MINHOOK=ON` | rarely |
| `src/exports.def` | Exactly the exports the DLL has | no |
| `include/nevr_plugin_interface.h` | The nEVR plugin ABI (v6 layout, works with v5 hosts) | **never** |
| `<Stem>.plugin.json` | The settings the launcher shows for the mod | yes |
| `tools/check_mod.py` | The checks the release and the echo-mods review run | **never** weaken |
| `tools/package.py` | Packs a release into `dist/` | no |
| `tools/ci-build.sh` | Build + Wine smoke test + checks | no |
| `tests/smoke_host.c` | Loads the DLL like nEVR and calls its exports | no |
| `.github/workflows/release.yml` | Builds and publishes releases with a build attestation | **never** |
| `.github/workflows/build.yml` | Builds and checks every push | no |

The launcher only takes release files built and attested by this repo's unmodified
`release.yml`. Don't edit it, don't rename it, don't attach files to a release by hand.

## Commands

```sh
cmake --preset mingw && cmake --build --preset mingw          # build/mingw/<file>.dll
python3 tools/check_mod.py --dll build/mingw/<file>.dll        # manifest + DLL checks
tools/ci-build.sh                                              # all of it, with the Wine smoke test (Linux)
cmake -S . -B build/mh -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-toolchain.cmake -DECHO_MOD_MINHOOK=ON
```

A change is done when `tools/ci-build.sh` passes (or the **build** workflow on GitHub).

## echo-mod.json

```json
{
  "schema": 1,
  "id": "example-mod",
  "name": "Example mod",
  "summary": "One line for the launcher's list.",
  "author": "Your name",
  "version": "0.1.0",
  "kind": "mod",
  "file": "ExampleMod.dll",
  "api": 5,
  "capabilities": ["observes-only"],
  "target": "client",
  "visibility": "public",
  "requires": { "nevr": ">=4.1.0", "mods": [] },
  "conflicts": [],
  "license": "MIT",
  "homepage": "",
  "args": {},
  "settings": "ExampleMod.plugin.json"
}
```

| Field | Rule |
|---|---|
| `schema` | `1` |
| `id` | `^[a-z0-9][a-z0-9._-]{0,63}$`, unique among all mods, never changes after approval. Reserved: the launcher's own mods (`asset-patches`, `xmlhttp-fix`, `gpu-rating`, `content-overlay`, `missing-textures`, `server-patches`, `echocombat`) and everything starting with `nevr`, `echoxr` or `echotools` |
| `name`, `summary`, `author` | Non-empty text |
| `version` | A [semantic version](https://semver.org/): `MAJOR.MINOR.PATCH`, optional `-prerelease`, optional `+build`. No `v` here. A prerelease part makes the release a prerelease. Build metadata doesn't count when versions are compared, so `1.0.0+a` and `1.0.0+b` are the same version |
| `kind` | `mod` (one DLL); `pack` (DLL + content, below) isn't published yet |
| `file` | `^[A-Za-z0-9._-]{1,64}\.dll$`, unique among all mods, never changes after approval (the maintainers can rename it). Reserved: `BugSplat64.dll`, `dbgcore.dll`, `nevr.dll`, `log_filter.dll`, the game's own DLLs, and every name starting with `Nvr`, `nEVR` or `EchoXR` (any case) |
| `api` | The plugin API version reported to nEVR: `5` (default, runs on every nEVR the launcher ships without a warning) or `6` |
| `capabilities` | At least one of `observes-only`, `cosmetic`, `alters-gameplay`, `alters-rules`, `network`, `hooks-engine`. `observes-only` stands alone. Importing a network DLL (`ws2_32`, `wininet`, `winhttp`, ...) needs `network`; installing hooks needs `hooks-engine` |
| `target` | `client` or `both` (`server`, game servers only, isn't published yet) |
| `visibility` | `public` (listed), `unlisted` (only by id: More mods → Add by id) or `private` (only the author's EchoVRCE account and the account IDs in their `access.json`). The newest release that isn't a prerelease decides |
| `requires.nevr` | A version range of nEVR: comparators `>=`, `>`, `<=`, `<`, `=`, `^`, `~`, space-separated (`>=4.1.0 <6.0.0`), or `*` |
| `requires.mods` | `[{"id": "other-mod", "version": ">=1.2.0"}]`: mods that must be on too |
| `conflicts` | Ids of mods that must not be on at the same time |
| `license` | An SPDX id of an open-source license (`MIT`, `Apache-2.0`, `GPL-3.0-only`, ...), matching `LICENSE` |
| `homepage` | Empty or `https://...` (the launcher links the repo itself) |
| `args` | Default options, an object (values become strings) |
| `settings` | Path of the `<Stem>.plugin.json` describing the mod's settings, or omitted |
| `required`, `shipped` | Not allowed: only for the launcher's own mods |

CMake reads `id`, `name`, `summary`, `version`, `file`, `api` and `capabilities` from this file
and passes them to the code as `MOD_ID`, `MOD_SUMMARY`, `MOD_VERSION`, `MOD_VERSION_MAJOR/MINOR/PATCH`,
`MOD_STEM` (file without `.dll`), `MOD_API` and `MOD_CAPABILITIES`. Never hard-code these in
the source: `check_mod.py` compares what the DLL reports with the manifest.

## The plugin ABI

nEVR loads a plugin only when the launcher lists it in `_local/config.yaml`; the file goes to
`bin/win10/plugins/<file>`. It loads it with
`LoadLibraryExA(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)`,
looks up the exports by name, loads every plugin first, then calls the inits ordered by
capability (undeclared, observes-only, cosmetic, alters-gameplay, alters-rules, network,
hooks-engine), config order within each.

Exports (unmangled, `extern "C"`, listed in `src/exports.def`):

| Export | When | Contract |
|---|---|---|
| `NvrPluginInfo NvrPluginGetInfo(void)` | load | Required. Static strings only. `name` = the mod's id |
| `uint32_t NvrPluginGetApiVersion(void)` | load | `MOD_API`. Absent means 1 |
| `uint32_t NvrPluginGetCapabilities(void)` | load | The capability bits: observes-only 0x01, cosmetic 0x02, alters-gameplay 0x04, alters-rules 0x08, network 0x10, hooks-engine 0x20 |
| `int NvrPluginInitEx(const NvrGameContext*, const char* args_json)` | once, after all plugins loaded | 0 = keep me, non-zero = unload me. `args_json` is a flat JSON object of strings, `"{}"` when empty, never null |
| `int NvrPluginInit(const NvrGameContext*)` | once, only if there's no InitEx | Same, without args |
| `void NvrPluginOnFrame(const NvrGameContext*)` | every frame, game thread | Short, never blocks |
| `void NvrPluginOnGameStateChange(const NvrGameContext*, uint32_t old, uint32_t new)` | state changes | `ctx->net_game` valid once `flags & NEVR_HOST_HAS_NETGAME` |
| `void NvrPluginShutdown(void)` | clean unload, and right after a failed init | Not guaranteed at process exit. Without it, nEVR keeps a failed plugin loaded |

`NvrGameContext` grows only at its end. Read a field past `flags` only after
`NEVR_CTX_HAS(ctx, field)`:

| Fields | Since | nEVR |
|---|---|---|
| `base_addr`, `net_game`, `game_state`, `flags` | v1 | all |
| `ctx_size`, `get_plugin_count`, `get_plugin_info` | v5 | 4.1.0 (live servers), 5.2.x (launcher Beta) |
| `plugin_id`, `log` | v6 | 5.3.0 and newer |

Hosts before v5 aren't supported (no launcher ships one). Host flags: `HAS_NETGAME` 0x01,
`IS_SERVER` 0x02, `IS_CLIENT` 0x04, `COMBAT_MODE` 0x08, `IS_HEADLESS` 0x10.

EchoLoader 2 (used for EchoRelay servers and event builds) hosts the same ABI at v5, calls init
before the game's entry point (`net_game` always null, `game_state` 0), and copies the mod
beside its own plugins. A mod should work there too or return non-zero from init.

## Rules (all of them are checked or reviewed)

1. **`DllMain` only calls `DisableThreadLibraryCalls`.** No hooks, threads, files or logging
   there: the loader lock is held. Everything starts in `mod::Start`.
2. **Check the game build first.** `plugin.cpp` already returns non-zero unless `echovr.exe`'s PE
   timestamp is `0x6452DFF6` (build 631547). Keep that. Outside the game (the smoke host) init
   must return non-zero, cleanly.
3. **Check the bytes before patching or hooking** (`mod::BytesAt`, `hooks::Hook` does it). Leave
   anything that doesn't match alone, log it, and keep running or return false from `Start`.
4. **Never hook a function nEVR hooks itself.** nEVR re-checks its own hooks after every plugin's
   init; on a game server a hook over one of them ends the process. To change something nEVR
   already hooks, use what it offers (resource overrides below) instead.
5. **MinHook:** each DLL has its own copy. Treat `MH_ERROR_ALREADY_INITIALIZED` as success.
   Never call `MH_Uninitialize` or `MH_DisableHook(MH_ALL_HOOKS)`; remove only your own hooks
   (`hooks::End` does).
6. **Everything linked statically.** The DLL may import only Windows' own DLLs (`check_mod.py`
   has the list); no `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`,
   `vcruntime*.dll`, `msvcp*.dll`. MinGW: the toolchain file's `-static`. MSVC: `/MT` (set in
   `CMakeLists.txt`). No other DLLs beside it.
7. **Only the `Nvr*` exports** leave the DLL (`exports.def`, `--exclude-all-symbols`).
8. **Never log option values or secrets.** Log what you made of them (`log_states on`).
9. **Threads:** callbacks run on the game's thread. Slow work (files, network) goes on a thread
   started in `Start`, and stops in `Stop`.
10. **Log to `plugin_logs/<Stem>/<Stem>.log`** through `mod::Log`/`Warn`/`Error`; on v6 hosts it
    also reaches nEVR's log. That's where the launcher's Logs button and log upload look.
11. **Capabilities are honest** and match what the code does (rule above for network and hooks).
12. **No cheats, no data collection,** no network use beyond what the mod is for.
13. **Never write into the game's folders** except the mod's own log folder and a settings file
    the `.plugin.json` names. Never touch other plugins' files, `config.yaml`, or `BugSplat64.dll`.

## Options and settings

The launcher passes `args` from `config.yaml` to `NvrPluginInitEx`: a flat object, all values
strings, nested keys as `a.b`, lists joined with commas. Read them with `mod::Arg`,
`mod::ArgBool`, `mod::ArgNumber`. The launcher refuses `${...}` in values.

`<Stem>.plugin.json` describes the settings the launcher draws (switches, sliders, choices). Its
format is the launcher's
[docs/plugins/settings.md](https://github.com/marshmallow-mia/EchoVR_Launcher/blob/main/docs/plugins/settings.md)
(schema: `docs/plugins/plugin.schema.json`). With `"store": {"kind": "args"}` the values arrive as
args. The release puts its `settings` object into the published `echo-mod.json`.

## nEVR's runtime exports

`mod::RuntimeExport(name)` finds an nEVR export in `nevr.dll`, then `BugSplat64.dll`, then
`dbgcore.dll` (nEVR is moving its runtime into `nevr.dll`). Null when there is none: handle that.

| Export | Signature |
|---|---|
| `NEVR_RegisterResourceOverride` | `void (uint64_t type_hash, uint64_t name_hash, const void* data, uint64_t size, const char* label)` |
| `NEVR_DeregisterResourceOverrides` | `void (const void* start, const void* end)` |
| `NEVR_ResetResourceOverrides` | `void (void)` |

Resource overrides replace a game resource as it is read. `data` must stay valid while the
override is registered.

## Content packs (`kind: "pack"`)

**Not published yet:** `check_mod.py` refuses `kind: "pack"` ("content packs come later"), so
a pack can't be submitted or released to players for now. Build a plain `mod` (one DLL).
What follows is how packs will work.

A pack is the DLL plus game content that NvrContentOverlay serves:

```
content/<content id>/content.json      (and what it lists)
echo-mod.json: "kind": "pack", "pack": { "content": "<content id>" }
```

The release zips `plugins/<file>`, `plugins/<Stem>.plugin.json` and `content/<content id>/`. The
launcher installs NvrContentOverlay itself, and packs need an nEVR with the early load pass
(launcher Beta). The `content.json` format is in the launcher's
[docs/launcher/mods.md](https://github.com/marshmallow-mia/EchoVR_Launcher/blob/main/docs/launcher/mods.md#content-packs).

## Releasing

1. Bump `version` in `echo-mod.json` (semver: a breaking change for players or other mods bumps
   MAJOR, a feature MINOR, a fix PATCH; a test build gets a prerelease part: `1.3.0-beta.1`).
2. Commit, tag `v<version>` on that commit, push the tag. Nothing else.
3. `release.yml` builds, smoke-tests, checks the tag against the manifest, packs `dist/`
   (`<file>` or the pack zip, `echo-mod.json` with `version`, `prerelease`, `sha256`, `size`,
   `settings` and `source` filled in, `SHA256SUMS`), checks `dist/` with the template's
   current `tools/check_mod.py --published` (the same check the launcher's catalogue applies),
   attests the files and creates the GitHub release (a prerelease when the version has a
   prerelease part).
4. Within about 5 minutes the catalogue has it; the result ("live", or why not) is a comment
   on the mod's submission issue.

Published versions are immutable: never move or reuse a tag, never replace release files. A
broken release is fixed by a new version. A published version that must not be used any more
is pulled by the maintainers (ask on the submission issue, or the **Pull a version** form in
echo-mods): launchers go back to the version before.

Check a release locally like CI does: `python3 tools/package.py --dll build/mingw/<file>`, then
`python3 tools/check_mod.py --published dist --tag v<version>`. `python3 tests/test_check_mod.py`
tests the checks themselves.

## Distribution, briefly

- A mod reaches the launcher after its first submission at
  [EchoTools/echo-mods](https://github.com/EchoTools/echo-mods/issues/new/choose) is approved.
  From then on every release of it is picked up automatically, if its checks and attestation hold.
- Prereleases are offered only to players who turn on prereleases for that mod.
- After the first approval the author has a dev channel (SFTP with the GitHub account's SSH keys,
  the code in `DEV-CODE.txt`) for test builds. In the launcher the code opens it for the author's
  EchoVRCE account and the accounts in `access.json` in that folder:
  `{"schema": 1, "accounts": ["<EchoVRCE account ID>", ...]}` (at most 50; account IDs only,
  copied on the launcher's EchoVRCE page; usernames are refused). The same list opens the
  author's `private` mods. `ACCESS-STATUS.txt` there says what was found.

## Before you say it's done

- [ ] `echo-mod.json` has the real `id`, `name`, `summary`, `author`, `file`, `license`, and a semver `version`
- [ ] `<Stem>.plugin.json` renamed to match `file`, `settings` points at it (or both removed)
- [ ] capabilities match the code
- [ ] `tools/ci-build.sh` (or the build workflow) passes with no errors
- [ ] tried in the game: the launcher shows the mod as **Loaded**, and its log shows `Start` ran
