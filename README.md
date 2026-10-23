# Echo VR mod template

Start here to make a mod for Echo VR that players can get from the
[Echo VR launcher](https://github.com/marshmallow-mia/EchoVR_Launcher). A mod is a small
Windows DLL that the game's mod runtime, [nEVR](https://github.com/EchoTools/nevr-runtime),
loads when Echo VR starts. On Linux the same DLL runs under Proton.

This template gives you a mod that already loads and logs, a build that works from Linux,
macOS and Windows, and GitHub Actions that check every push and publish your releases.

> Working with an AI agent? Point it at [AGENTS.md](AGENTS.md): it has the exact rules and
> formats. This README only covers the basics.

## 1. Make it yours

1. Click **Use this template** on GitHub, then clone your new repo.
2. Open `echo-mod.json` and change:
   - `id`: a short lowercase name, unique among all mods (`my-cool-mod`)
   - `name`, `summary`, `author`
   - `file`: your DLL's name (`MyCoolMod.dll`). Rename `ExampleMod.plugin.json` to match
     (`MyCoolMod.plugin.json`) and change `settings` to that name.
   - `capabilities`: what your mod does (see below). Be honest: players see them.
   - `license`: keep MIT or pick another open-source license, and change `LICENSE` to match
     (put your name in it).
3. Write your mod in `src/mod.cpp`. The rest of `src/` is plumbing you rarely need to touch.

Everything else (the DLL's name, its version, what it tells nEVR) comes from `echo-mod.json`,
so that file is the only one to edit when you bump the version. Pick `id` and `file` with
care: once the mod is approved they stay (a new name needs the maintainers, on your
submission issue).

Does your mod need another one, or can't it run beside one? Say so in `requires` and
`conflicts` ([AGENTS.md](AGENTS.md#echo-modjson) has the format): the launcher installs what
it needs and keeps conflicting mods apart.

Before you push, `python3 tools/check_mod.py` checks `echo-mod.json` the way the launcher's
catalogue will.

### Capabilities

| Capability | Your mod... |
|---|---|
| `observes-only` | only reads the game (stats, overlays that show data) |
| `cosmetic` | changes looks or sound only |
| `alters-gameplay` | changes physics, weapons, movement |
| `alters-rules` | changes match rules, scoring or the game mode |
| `network` | talks to the internet or opens sockets |
| `hooks-engine` | hooks the game's functions itself (needed with MinHook) |

## 2. Build it

**Easiest: let GitHub build it.** Every push runs the **build** workflow. Its result (the
**Artifacts** of the run, under **Actions**) is your DLL.

**On your PC** you need CMake 3.20+, Ninja and MinGW-w64:

- Linux: install `mingw-w64`, `cmake`, `ninja` (Ubuntu: `sudo apt install mingw-w64 cmake ninja-build`)
- macOS: `brew install mingw-w64 cmake ninja`
- Windows: [MSYS2](https://www.msys2.org/) with `mingw-w64-x86_64-toolchain`, `cmake`, `ninja`,
  or Visual Studio (use the `msvc` preset from a *x64 Native Tools* prompt)

```sh
cmake --preset mingw
cmake --build --preset mingw        # -> build/mingw/ExampleMod.dll
```

On Linux, `tools/ci-build.sh` also runs the same checks as GitHub (it needs Wine).

## 3. Try it in the game

Testing goes in steps, each reaching more people:

1. **On your PC:** turn on local plugins for the version you test with (the launcher's guide:
   [Writing a plugin and loading it from your PC](https://github.com/marshmallow-mia/EchoVR_Launcher/blob/main/docs/plugins/local-plugins.md#4-loading-it-with-the-launcher)),
   then **Mods → Installed mods → Add DLL**, pick your DLL (with its `.plugin.json` beside
   it), start with **PLAY**. Your mod's state (Loaded, Failed and why) is on the Mods page;
   **Logs** opens its log. After a rebuild, add the DLL again.
2. **Your testers:** once your first mod is approved you have a **dev channel** (below):
   upload a test build there, and your testers see it in their launcher.
3. **Everyone who wants to:** release a prerelease (`1.5.0-beta.1`). Players get it only if
   they turn on prereleases for your mod.
4. **Everyone:** release it.

## 4. Publish it

**Releases come from GitHub, built by this repo's release workflow.** You don't upload DLLs
yourself.

1. Set `version` in `echo-mod.json`. Versions are [semantic versions](https://semver.org/):
   `1.4.2`. A version with a prerelease part (`1.5.0-beta.1`) is published as a prerelease:
   players only get it if they choose to try your mod's prereleases.
2. Commit, then tag that commit with the version, prefixed with `v`, and push the tag:
   ```sh
   git tag v1.4.2 && git push origin v1.4.2
   ```
3. The **release** workflow builds, checks and publishes the GitHub release. It checks the
   release by the launcher's current rules first: if something's off (the tag doesn't match
   `echo-mod.json`, say), it stops and tells you, and nothing is published.

A version, once published, never changes: fix things with a new version. Within about 5
minutes it's live in the launcher, and the result (live, or why not) is posted on your
submission issue. Something seriously wrong with a published version? Comment on your
submission issue (or use the **Pull a version** form in echo-mods): it can be turned off for
everyone, and players go back to the version before.

### Getting into the launcher

- **Your first mod:** open a submission at
  [EchoTools/echo-mods](https://github.com/EchoTools/echo-mods/issues/new/choose) once your repo
  has a release. The form asks for your EchoVRCE username, so the maintainers know who you are
  there: like everything in the issue, it's public. An automatic check runs and comments on it. After the mod is approved, every new
  release of it reaches players on its own.
- **After the first approval** you also get a **dev channel**: a folder you upload test builds
  to over SFTP (with your GitHub account's SSH keys), and a code (in `DEV-CODE.txt` there).
  In the launcher the code works for your EchoVRCE account and the testers you list in
  `access.json` in that folder by their **EchoVRCE account IDs** (they copy theirs on the
  launcher's EchoVRCE page, **Copy ID**; usernames don't count, as names can change):
  `{"schema": 1, "accounts": ["8f0c…-…", "…"]}`. `ACCESS-STATUS.txt` beside it says what was found.
- **Each new mod** of yours is submitted once the same way.

### Who sees your mod

Set `visibility` in `echo-mod.json`:

- `public` (the default): listed in the launcher for everyone
- `unlisted`: not listed; anyone who knows its id adds it (More mods → **Add by id**)
- `private`: like unlisted, but only for you and the accounts in your dev channel's
  `access.json` (above), signed in with EchoVRCE

The newest release (not a prerelease) decides.

## Rules

- No cheats, and nothing that gives an unfair advantage in public matches.
- No collecting players' data. Network use is declared with the `network` capability.
- Capabilities are honest.
- The source is public and under an open-source license: the launcher redistributes your
  files, so it needs the right to.
- Mods are for Echo VR on PC (Windows, and Linux through Proton).

Antivirus programs sometimes flag new, unknown DLLs. If yours is, tell us; don't ask players
to turn their antivirus off.

## Need help?

Ask in the Echo VR community Discord, or open an issue in
[EchoTools/echo-mods](https://github.com/EchoTools/echo-mods/issues).
