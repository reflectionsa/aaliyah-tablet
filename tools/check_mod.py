#!/usr/bin/env python3
"""Checks a mod the way the echo-mods review does: echo-mod.json, the DLL, and (optionally) what
the smoke host found when it loaded the DLL.

    python3 tools/check_mod.py                         # echo-mod.json only
    python3 tools/check_mod.py --dll build/mingw/ExampleMod.dll
    python3 tools/check_mod.py --dll ... --smoke smoke.json --tag v0.1.0
    python3 tools/check_mod.py --published dist --tag v0.1.0   # a release as package.py made it

--published checks what a release publishes (echo-mod.json, the DLL or the pack's zip,
SHA256SUMS) by the same rules the echo-mods check and the launcher's catalogue apply to every
release: the release workflow runs it before it publishes anything.

Exit code 0 when nothing is wrong. Warnings don't fail the check. Python 3.8+, no packages.
"""
import argparse
import atexit
import hashlib
import json
import re
import shutil
import struct
import sys
import tempfile
import zipfile
from pathlib import Path

# semver.org's own regex for a semantic version (2.0.0).
SEMVER = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-((?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?"
    r"(?:\+([0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$"
)
ID = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")
FILE = re.compile(r"^[A-Za-z0-9._-]{1,64}\.dll$")
RANGE_PART = re.compile(r"^(>=|<=|>|<|=|\^|~)?(\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?)$")

CAPABILITIES = {
    "observes-only": 0x01,
    "cosmetic": 0x02,
    "alters-gameplay": 0x04,
    "alters-rules": 0x08,
    "network": 0x10,
    "hooks-engine": 0x20,
}
KINDS = {"mod", "pack"}
TARGETS = {"client", "server", "both"}
VISIBILITY = {"public", "unlisted", "private"}

# File names a mod can never use: the loader slot, nEVR's own files, and the files the community
# update ships or the launcher manages. Compared case-insensitively.
RESERVED_FILES = {
    "bugsplat64.dll", "bugsplat64.nevr.dll", "dbgcore.dll", "nevr.dll", "log_filter.dll", "log-filter.dll",
    "echovr.exe", "pnsradgameserver.dll", "pnsrad.dll", "pnsovr.dll",
    "nvrassetpatches.dll", "nvrxmlhttpfix.dll", "nvrgpurating.dll", "nvrcontentoverlay.dll",
    "nvrmissingtextures.dll", "nvrserverpatches.dll",
    "echoxr.dll", "libovrrt64_1.dll", "echoxr-hands.dll", "handtracking.dll",
}
RESERVED_IDS = {
    "asset-patches", "xmlhttp-fix", "gpu-rating", "content-overlay", "missing-textures", "server-patches",
    "nevr", "nevr-runtime", "echoxr", "echoxr-hands", "echocombat",
}
# Whole families: nEVR's and EchoXR's names, and the launcher's own Nvr*.dll plugins.
RESERVED_ID_PREFIXES = ("nevr", "echoxr", "echotools")
RESERVED_FILE_PREFIXES = ("nvr", "nevr", "echoxr")
# Not published yet: mods for game servers.
UNPUBLISHED_TARGETS = {"server"}
# Content packs (game data beside the DLL) aren't published yet: players' launchers can't
# install them until the content overlay is in the launcher's own catalogue.
UNPUBLISHED_KINDS = {"pack"}
PACKS_LATER = "\"kind\": \"pack\" isn't published yet (content packs come later): a \"mod\" (one DLL)"

# Exports nEVR looks up. GetInfo is required; the template exports all of them.
EXPORTS_REQUIRED = {"NvrPluginGetInfo"}
EXPORTS_EXPECTED = {
    "NvrPluginGetApiVersion", "NvrPluginGetCapabilities", "NvrPluginInitEx", "NvrPluginInit", "NvrPluginShutdown",
}

# DLLs every Windows 10 has in System32 (and Wine/Proton provides). Anything else has to be linked
# in statically: nEVR loads plugins with a search path of their own folder and System32 only.
SYSTEM_DLLS = {
    "kernel32.dll", "kernelbase.dll", "ntdll.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "shell32.dll",
    "shlwapi.dll", "ole32.dll", "oleaut32.dll", "uuid.dll", "comdlg32.dll", "comctl32.dll", "version.dll",
    "psapi.dll", "dbghelp.dll", "imagehlp.dll", "bcrypt.dll", "crypt32.dll", "ncrypt.dll", "secur32.dll",
    "rpcrt4.dll", "setupapi.dll", "winmm.dll", "imm32.dll", "uxtheme.dll", "dwmapi.dll", "userenv.dll",
    "msvcrt.dll", "ucrtbase.dll", "d3d11.dll", "dxgi.dll", "d3dcompiler_47.dll", "d3d9.dll", "dinput8.dll",
    "xinput1_3.dll", "xinput1_4.dll", "xinput9_1_0.dll", "dsound.dll", "mmdevapi.dll", "avrt.dll",
    "ws2_32.dll", "mswsock.dll", "wininet.dll", "winhttp.dll", "iphlpapi.dll", "dnsapi.dll", "normaliz.dll",
    "wtsapi32.dll", "powrprof.dll", "hid.dll", "cfgmgr32.dll", "opengl32.dll", "vulkan-1.dll",
}
NETWORK_DLLS = {"ws2_32.dll", "mswsock.dll", "wininet.dll", "winhttp.dll", "iphlpapi.dll", "dnsapi.dll"}
# Runtimes that are never there: link them statically (-static / /MT).
RUNTIME_DLLS = re.compile(r"^(libstdc\+\+|libgcc_s|libwinpthread|vcruntime|msvcp|concrt|vcomp|ucrtbased|msvcr\d)",
                          re.I)


class Report:
    def __init__(self):
        self.errors = []
        self.warnings = []
        self.notes = []

    def error(self, text):
        self.errors.append(text)

    def warn(self, text):
        self.warnings.append(text)

    def note(self, text):
        self.notes.append(text)

    def print(self):
        for text in self.notes:
            print(f"  {text}")
        for text in self.warnings:
            print(f"warning: {text}")
        for text in self.errors:
            print(f"error: {text}")
        print("ok" if not self.errors else f"{len(self.errors)} error(s)")


def is_prerelease(version):
    m = SEMVER.match(version)
    return bool(m and m.group(4))


def check_range(text):
    """A version range: space-separated comparators (">=4.1.0 <6.0.0"), "^1.2.0", "~1.2.0" or "*"."""
    if text.strip() == "*":
        return True
    parts = text.split()
    return bool(parts) and all(RANGE_PART.match(p) for p in parts)


def check_manifest(m, report, root, published=False):
    """echo-mod.json as it is in the repo (`root` its folder), or, `published`, as package.py
    filled it in for a release (settings as an object, a pack's zip described in "pack")."""
    def need(key, kind):
        if not isinstance(m.get(key), kind):
            report.error(f"echo-mod.json: \"{key}\" is missing or not a {kind.__name__}")
            return None
        return m[key]

    if m.get("schema") != 1:
        report.error("echo-mod.json: \"schema\" must be 1")
    mod_id = need("id", str)
    if mod_id is not None:
        if not ID.match(mod_id):
            report.error(f"id \"{mod_id}\": lowercase letters, digits, '.', '-', '_' (at most 64), starting with a "
                         "letter or digit")
        elif mod_id in RESERVED_IDS or mod_id.startswith(RESERVED_ID_PREFIXES):
            report.error(f"id \"{mod_id}\" is reserved (nevr*, echoxr* and echotools* are the projects' own)")
    for key in ("name", "summary", "author"):
        value = need(key, str)
        if value is not None and not value.strip():
            report.error(f"\"{key}\" is empty")
    if m.get("author") == "Your name":
        report.warn("\"author\" is still the template's")
    if m.get("id") == "example-mod":
        report.warn("\"id\" is still the template's example-mod")
    version = need("version", str)
    if version is not None and not SEMVER.match(version):
        report.error(f"version \"{version}\" is not a semantic version (semver.org): MAJOR.MINOR.PATCH, optionally "
                     "-prerelease and +build")
    kind = m.get("kind", "mod")
    if kind not in KINDS:
        report.error(f"kind \"{kind}\": one of {sorted(KINDS)}")
    elif kind in UNPUBLISHED_KINDS:
        report.error(PACKS_LATER)
    file = need("file", str)
    if file is not None:
        if not FILE.match(file):
            report.error(f"file \"{file}\": letters, digits, '.', '-', '_', ending in .dll (at most 64)")
        elif file.lower() in RESERVED_FILES or file.lower().startswith(RESERVED_FILE_PREFIXES):
            report.error(f"file \"{file}\" is reserved (Nvr*, nEVR* and EchoXR* are the projects' own)")
    api = m.get("api", 5)
    if api not in (5, 6):
        report.error("\"api\": 5 or 6")
    caps = m.get("capabilities")
    if not isinstance(caps, list) or not caps:
        report.error("\"capabilities\": declare at least one of " + ", ".join(CAPABILITIES))
    else:
        for cap in caps:
            if cap not in CAPABILITIES:
                report.error(f"capability \"{cap}\": one of " + ", ".join(CAPABILITIES))
        if "observes-only" in caps and len(caps) > 1:
            report.error("\"observes-only\" can't go with other capabilities: a mod that changes something "
                         "doesn't only observe")
    if m.get("target", "client") not in TARGETS:
        report.error(f"\"target\": one of {sorted(TARGETS)}")
    elif m.get("target", "client") in UNPUBLISHED_TARGETS:
        report.error("\"target\": \"server\" isn't published yet (mods for game servers come later): "
                     "\"client\" or \"both\"")
    if m.get("visibility", "public") not in VISIBILITY:
        report.error(f"\"visibility\": one of {sorted(VISIBILITY)}")
    for flag in ("required", "shipped"):
        if m.get(flag):
            report.error(f"\"{flag}\" is only for the launcher's own mods")
    requires = m.get("requires", {})
    if not isinstance(requires, dict):
        report.error("\"requires\" must be an object")
        requires = {}
    if "nevr" in requires and not (isinstance(requires["nevr"], str) and check_range(requires["nevr"])):
        report.error("requires.nevr: a version range such as \">=4.1.0\"")
    for dep in requires.get("mods", []):
        if not (isinstance(dep, dict) and isinstance(dep.get("id"), str) and ID.match(dep["id"])
                and check_range(dep.get("version", "*"))):
            report.error(f"requires.mods: each one {{\"id\": \"...\", \"version\": \">=1.0.0\"}}, not {dep!r}")
        elif dep["id"] == mod_id:
            report.error("a mod can't require itself")
    for other in m.get("conflicts", []):
        if not (isinstance(other, str) and ID.match(other)):
            report.error(f"conflicts: mod ids, not {other!r}")
    if not isinstance(m.get("license"), str) or not m["license"].strip():
        report.error("\"license\": an SPDX id such as MIT, Apache-2.0, GPL-3.0-only")
    homepage = m.get("homepage", "")
    if homepage and not homepage.startswith("https://"):
        report.error("\"homepage\" must start with https://")
    if not isinstance(m.get("args", {}), dict):
        report.error("\"args\" must be an object")
    settings = m.get("settings")
    if settings and published:
        if not isinstance(settings, dict):
            report.error("\"settings\": the release's echo-mod.json carries the settings object")
    elif settings:
        if not isinstance(settings, str):
            report.error("\"settings\": the path of a <Stem>.plugin.json in the repo")
        else:
            path = root / settings
            try:
                doc = json.loads(path.read_text(encoding="utf-8"))
                if doc.get("schema") != 1 or not isinstance(doc.get("settings"), dict):
                    report.error(f"{settings}: needs \"schema\": 1 and a \"settings\" object (see the launcher's "
                                 "docs/plugins/settings.md)")
            except (OSError, ValueError) as e:
                report.error(f"{settings}: {e}")
    if kind == "pack":
        pack = m.get("pack")
        if not isinstance(pack, dict) or not isinstance(pack.get("content"), str) or not ID.match(pack["content"]):
            report.error("a pack needs \"pack\": {\"content\": \"<content id>\"} and content/<content id>/content.json")
        elif published:
            if not isinstance(pack.get("file"), str) or not re.match(r"^[A-Za-z0-9._+-]{1,96}\.zip$", pack["file"]):
                report.error("pack.file: the pack's zip (package.py names it)")
        elif not (root / "content" / pack["content"] / "content.json").is_file():
            report.error(f"content/{pack['content']}/content.json is missing")


def read_pe(data, report):
    """(exports, imports) of a PE file, or None when it isn't a 64-bit DLL."""
    if data[:2] != b"MZ":
        report.error("the DLL isn't a Windows PE file")
        return None
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        report.error("the DLL has no PE header")
        return None
    machine, sections, _, _, _, opt_size, characteristics = struct.unpack_from("<HHIIIHH", data, pe + 4)
    if machine != 0x8664:
        report.error(f"the DLL isn't x64 (machine 0x{machine:04x}): Echo VR is 64-bit")
        return None
    if not characteristics & 0x2000:
        report.error("the file is an executable, not a DLL")
        return None
    opt = pe + 24
    if struct.unpack_from("<H", data, opt)[0] != 0x20B:
        report.error("the DLL isn't PE32+")
        return None
    dirs = opt + 112
    export_rva, _ = struct.unpack_from("<II", data, dirs)
    import_rva, _ = struct.unpack_from("<II", data, dirs + 8)
    table = opt + opt_size
    secs = [struct.unpack_from("<8sIIII", data, table + 40 * i) for i in range(sections)]

    def offset(rva):
        for _, vsize, vaddr, rsize, raw in secs:
            if vaddr <= rva < vaddr + max(vsize, rsize):
                return raw + rva - vaddr
        return None

    def cstr(rva):
        o = offset(rva)
        if o is None:
            return ""
        return data[o:data.index(b"\0", o)].decode("ascii", "replace")

    exports = set()
    if export_rva:
        o = offset(export_rva)
        count, names_rva = struct.unpack_from("<I", data, o + 24)[0], struct.unpack_from("<I", data, o + 32)[0]
        names = offset(names_rva)
        for i in range(count):
            exports.add(cstr(struct.unpack_from("<I", data, names + 4 * i)[0]))
    imports = []
    if import_rva:
        o = offset(import_rva)
        while o is not None:
            name_rva = struct.unpack_from("<I", data, o + 12)[0]
            if not name_rva:
                break
            imports.append(cstr(name_rva))
            o += 20
    return exports, imports


def check_dll(path, manifest, report):
    data = Path(path).read_bytes()
    if Path(path).name != manifest.get("file"):
        report.error(f"the DLL is called {Path(path).name}, echo-mod.json says {manifest.get('file')}")
    pe = read_pe(data, report)
    if pe is None:
        return
    exports, imports = pe
    for name in sorted(EXPORTS_REQUIRED - exports):
        report.error(f"missing export {name}: nEVR can't load the DLL without it")
    for name in sorted(EXPORTS_EXPECTED - exports):
        report.warn(f"no export {name}")
    stray = sorted(e for e in exports if not e.startswith("Nvr"))
    if stray:
        report.warn("exports beyond the plugin's own: " + ", ".join(stray[:10]) + (" ..." if len(stray) > 10 else ""))
    report.note(f"{len(data)} bytes, imports: " + ", ".join(imports))
    caps = set(manifest.get("capabilities") or [])
    for dll in imports:
        low = dll.lower()
        if RUNTIME_DLLS.match(low):
            report.error(f"imports {dll}: link the C/C++ runtime statically (MinGW -static, MSVC /MT)")
        elif low not in SYSTEM_DLLS and not low.startswith(("api-ms-win-", "ext-ms-win-")):
            report.error(f"imports {dll}, which isn't part of Windows: link it statically or leave it out")
        if low in NETWORK_DLLS and "network" not in caps:
            report.error(f"imports {dll} (network) but doesn't declare the \"network\" capability")


def check_smoke(path, manifest, report):
    found = json.loads(Path(path).read_text(encoding="utf-8").strip().splitlines()[0])
    version = manifest.get("version", "")
    core = version.split("-")[0].split("+")[0]
    if found.get("version") != core:
        report.error(f"NvrPluginGetInfo says version {found.get('version')}, echo-mod.json {version}")
    if found.get("name") != manifest.get("id"):
        report.warn(f"NvrPluginGetInfo's name is {found.get('name')!r}, the id {manifest.get('id')!r}")
    want = 0
    for cap in manifest.get("capabilities") or []:
        want |= CAPABILITIES.get(cap, 0)
    if found.get("capabilities") != want:
        report.error(f"NvrPluginGetCapabilities gives 0x{found.get('capabilities', 0):02x}, echo-mod.json declares "
                     f"0x{want:02x}")
    if found.get("api") != manifest.get("api", 5):
        report.error(f"NvrPluginGetApiVersion gives {found.get('api')}, echo-mod.json says {manifest.get('api', 5)}")
    if not found.get("has_shutdown"):
        report.warn("no NvrPluginShutdown: nEVR then keeps a failed plugin loaded until the game exits")
    report.note(f"smoke: {found}")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main_file(manifest):
    """The file a release publishes: the DLL, or a pack's zip."""
    if manifest.get("kind", "mod") == "pack":
        return (manifest.get("pack") or {}).get("file")
    return manifest.get("file")


def check_published(work, tag, prerelease, report):
    """A release as the release workflow publishes it, in folder `work`: echo-mod.json, the DLL
    or the pack's zip, SHA256SUMS. `tag` is the release's tag, `prerelease` its GitHub flag.
    Returns (manifest, the DLL to load-test) -- the DLL None when there is none to test."""
    work = Path(work)
    try:
        manifest = json.loads((work / "echo-mod.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        report.error(f"echo-mod.json: {e}")
        return None, None
    if not isinstance(manifest, dict):
        report.error("echo-mod.json isn't an object")
        return None, None
    check_manifest(manifest, report, work, published=True)
    version = str(manifest.get("version", ""))
    if tag != f"v{version}":
        report.error(f"tag {tag} doesn't match echo-mod.json's version: tag v{version}")
    if SEMVER.match(version) and bool(prerelease) != is_prerelease(version):
        report.error("the GitHub prerelease flag doesn't match the version: a version with a prerelease part "
                     "(1.2.0-beta.1) is a prerelease, any other isn't")
    main = main_file(manifest)
    if not isinstance(main, str) or not (work / main).is_file():
        report.error(f"the release lacks {main}, the file its echo-mod.json names")
        return manifest, None
    sums = {}
    try:
        for line in (work / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
            parts = line.split()
            if len(parts) == 2:
                sums[parts[1].lstrip("*")] = parts[0]
    except OSError:
        report.error("the release has no SHA256SUMS")
    digest = sha256(work / main)
    for name, have in ((main, digest), ("echo-mod.json", sha256(work / "echo-mod.json"))):
        if sums and sums.get(name) != have:
            report.error(f"{name} doesn't match SHA256SUMS")
    want = manifest.get("sha256") if manifest.get("kind", "mod") == "mod" else (manifest.get("pack") or {}).get("sha256")
    if want != digest:
        report.error(f"{main} doesn't match the sha256 in echo-mod.json")
    if manifest.get("kind", "mod") != "pack":
        return manifest, work / main
    # A pack: plugins/<file> and content/<content id>/, nothing else, no path out of either.
    content = str((manifest.get("pack") or {}).get("content", ""))
    inner = f"plugins/{manifest.get('file')}"
    try:
        with zipfile.ZipFile(work / main) as zf:
            names = zf.namelist()
            bad = [n for n in names if n.startswith("/") or "\\" in n or ".." in n.split("/")
                   or not (n.startswith("plugins/") or n.startswith(f"content/{content}/"))]
            if bad:
                report.error("the pack has files outside plugins/ and content/<content id>/: " + ", ".join(bad[:5]))
            if f"content/{content}/content.json" not in names:
                report.error(f"the pack has no content/{content}/content.json")
            if inner not in names:
                report.error(f"the pack has no {inner}")
                return manifest, None
            # Outside the release's folder: the release workflow uploads all of it.
            out = Path(tempfile.mkdtemp(prefix="check_mod-")) / "plugins"
            atexit.register(shutil.rmtree, out.parent, True)
            out.mkdir()
            (out / str(manifest["file"])).write_bytes(zf.read(inner))
            return manifest, out / str(manifest["file"])
    except zipfile.BadZipFile:
        report.error(f"{main} isn't a zip")
        return manifest, None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", default="echo-mod.json")
    ap.add_argument("--dll", help="the built DLL")
    ap.add_argument("--smoke", help="the smoke host's output (a JSON line)")
    ap.add_argument("--tag", help="the git tag being released: must be v<version>")
    ap.add_argument("--published", metavar="DIR", help="a release's files as package.py made them (needs --tag)")
    args = ap.parse_args()

    report = Report()
    if args.published:
        if args.tag is None:
            ap.error("--published needs --tag")
        version = json.loads((Path(args.published) / "echo-mod.json").read_text(encoding="utf-8")).get("version", "")
        manifest, dll = check_published(args.published, args.tag, is_prerelease(str(version)), report)
        if dll is not None:
            check_dll(str(dll), manifest, report)
        if args.smoke and manifest:
            check_smoke(args.smoke, manifest, report)
        report.print()
        return 1 if report.errors else 0
    manifest_path = Path(args.manifest)
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        print(f"error: {args.manifest}: {e}")
        return 1
    check_manifest(manifest, report, manifest_path.resolve().parent)
    if args.tag is not None and args.tag != "v" + str(manifest.get("version")):
        report.error(f"tag {args.tag} doesn't match echo-mod.json's version: tag v{manifest.get('version')}")
    if args.dll:
        check_dll(args.dll, manifest, report)
    if args.smoke:
        check_smoke(args.smoke, manifest, report)
    report.print()
    return 1 if report.errors else 0


if __name__ == "__main__":
    sys.exit(main())
