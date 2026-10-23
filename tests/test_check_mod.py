#!/usr/bin/env python3
"""Tests of tools/check_mod.py and tools/package.py: a release as the release workflow makes it
passes the published check, mods and packs alike, and what the catalogue refuses is refused here.

    python3 tests/test_check_mod.py        # needs build/mingw/<file> (tools/ci-build.sh builds it)
"""
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import check_mod  # noqa: E402

MANIFEST = json.loads((ROOT / "echo-mod.json").read_text(encoding="utf-8"))
DLL = ROOT / "build" / "mingw" / MANIFEST["file"]


def package(repo, out):
    subprocess.run([sys.executable, str(ROOT / "tools" / "package.py"), "--dll", str(DLL),
                    "--manifest", str(repo / "echo-mod.json"), "--out", str(out)],
                   check=True, capture_output=True, text=True)


def checked(work, tag, prerelease=False):
    report = check_mod.Report()
    manifest, dll = check_mod.check_published(work, tag, prerelease, report)
    if dll is not None:
        check_mod.check_dll(str(dll), manifest, report)
    return report


def repo_copy(tmp, **changes):
    repo = Path(tmp) / "repo"
    repo.mkdir()
    # A plain mod unless the test asks for more (a mod repo's own echo-mod.json may be a pack).
    m = {**{k: v for k, v in MANIFEST.items() if k != "pack"}, "kind": "mod", "id": "test-mod", "author": "Tester",
         **changes}
    (repo / "echo-mod.json").write_text(json.dumps(m), encoding="utf-8")
    if isinstance(m.get("settings"), str):
        shutil.copy(ROOT / m["settings"], repo / m["settings"])
    return repo


@unittest.skipUnless(DLL.is_file(), f"no {DLL} (build first: tools/ci-build.sh)")
class Published(unittest.TestCase):
    def test_a_mod_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = repo_copy(tmp)
            package(repo, Path(tmp) / "dist")
            report = checked(Path(tmp) / "dist", f"v{MANIFEST['version']}")
            self.assertEqual(report.errors, [])

    def test_a_pack_passes_and_keeps_its_plugin(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = repo_copy(tmp, kind="pack", pack={"content": "test-content"})
            (repo / "content" / "test-content").mkdir(parents=True)
            (repo / "content" / "test-content" / "content.json").write_text("{}", encoding="utf-8")
            package(repo, Path(tmp) / "dist")
            published = json.loads((Path(tmp) / "dist" / "echo-mod.json").read_text(encoding="utf-8"))
            self.assertEqual(published["file"], MANIFEST["file"])
            self.assertTrue(published["pack"]["file"].endswith(".zip"))
            report = checked(Path(tmp) / "dist", f"v{MANIFEST['version']}")
            # Built and checked as a pack; only not published yet.
            self.assertEqual(report.errors, [check_mod.PACKS_LATER])
            # Only files in dist/: the release workflow uploads every entry (gh release create dist/*).
            self.assertEqual([p.name for p in (Path(tmp) / "dist").iterdir() if not p.is_file()], [])

    def test_a_pack_with_a_path_out_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = repo_copy(tmp, kind="pack", pack={"content": "test-content"})
            (repo / "content" / "test-content").mkdir(parents=True)
            (repo / "content" / "test-content" / "content.json").write_text("{}", encoding="utf-8")
            dist = Path(tmp) / "dist"
            package(repo, dist)
            m = json.loads((dist / "echo-mod.json").read_text(encoding="utf-8"))
            zf_path = dist / m["pack"]["file"]
            with zipfile.ZipFile(zf_path, "a") as zf:
                zf.writestr("content/test-content/../../BugSplat64.dll", b"x")
            m["pack"]["sha256"] = check_mod.sha256(zf_path)
            (dist / "echo-mod.json").write_text(json.dumps(m), encoding="utf-8")
            (dist / "SHA256SUMS").write_text("".join(f"{check_mod.sha256(p)}  {p.name}\n"
                                                     for p in sorted(dist.iterdir()) if p.is_file()
                                                     and p.name != "SHA256SUMS"), encoding="utf-8")
            report = checked(dist, f"v{MANIFEST['version']}")
            self.assertTrue(any("outside plugins/" in e for e in report.errors), report.errors)

    def test_tag_flag_and_sums(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = repo_copy(tmp, version="1.0.0-beta.1")
            dist = Path(tmp) / "dist"
            package(repo, dist)
            self.assertEqual(checked(dist, "v1.0.0-beta.1", prerelease=True).errors, [])
            self.assertTrue(any("prerelease flag" in e for e in checked(dist, "v1.0.0-beta.1").errors))
            self.assertTrue(any("doesn't match echo-mod.json's version" in e
                                for e in checked(dist, "v1.0.0", prerelease=True).errors))
            (dist / MANIFEST["file"]).write_bytes((dist / MANIFEST["file"]).read_bytes() + b"\0")
            errors = checked(dist, "v1.0.0-beta.1", prerelease=True).errors
            self.assertTrue(any("SHA256SUMS" in e for e in errors), errors)


class Manifest(unittest.TestCase):
    def errors(self, **changes):
        report = check_mod.Report()
        check_mod.check_manifest({**MANIFEST, "id": "test-mod", **changes}, report, ROOT)
        return report.errors

    def test_the_template_passes(self):
        self.assertEqual(self.errors(), [])

    def test_reserved_families(self):
        self.assertTrue(self.errors(id="nevr-tools"))
        self.assertTrue(self.errors(id="echoxr-extra"))
        self.assertTrue(self.errors(file="NvrMyMod.dll"))
        self.assertTrue(self.errors(file="EchoXRThing.dll"))
        self.assertEqual(self.errors(file="CoolMod.dll"), [])

    def test_server_mods_not_yet(self):
        self.assertTrue(self.errors(target="server"))

    def test_packs_come_later(self):
        self.assertIn(check_mod.PACKS_LATER, self.errors(kind="pack", pack={"content": "test-content"}))
        self.assertEqual(self.errors(target="both"), [])


if __name__ == "__main__":
    unittest.main()
