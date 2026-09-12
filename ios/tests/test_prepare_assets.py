import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    "prepare_assets", Path(__file__).resolve().parents[1] / "prepare_assets.py")
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


class PrepareAssetsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "web"
        self.output = Path(self.temp.name) / "native"
        self.source.mkdir()
        self.files = []
        for name in ("TIBSUN.MIX", "SIDECD01.MIX", "MAPS01.MIX"):
            data = ("synthetic " + name).encode()
            path = name + ".hashed"
            (self.source / path).write_bytes(data)
            self.files.append({"name": name, "path": path, "size": len(data),
                               "sha256": hashlib.sha256(data).hexdigest()})

    def manifest(self):
        raw = json.dumps({"files": self.files}).encode()
        (self.source / "manifest.json").write_bytes(raw)
        (self.source / "assets.json").write_text(json.dumps({
            "format": "opents-web-assets-pointer-v1", "manifest": "manifest.json",
            "sha256": hashlib.sha256(raw).hexdigest()}))

    def test_native_names_and_omitted_media(self):
        self.files.extend([{"name": None}, {"name": "INTRO.MP4"}])
        self.manifest()
        assets.prepare(self.source, self.output)
        self.assertEqual(sorted(path.name for path in self.output.iterdir()),
                         ["MAPS01.MIX", "SIDECD01.MIX", "TIBSUN.MIX"])
        self.assertEqual((self.output / "TIBSUN.MIX").read_bytes(), b"synthetic TIBSUN.MIX")

    def test_corrupt_archive_writes_nothing(self):
        self.manifest()
        (self.source / self.files[-1]["path"]).write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "checksum"):
            assets.prepare(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_corrupt_manifest(self):
        self.manifest()
        (self.source / "manifest.json").write_text("{}")
        with self.assertRaisesRegex(ValueError, "manifest checksum"):
            assets.prepare(self.source, self.output)

    def test_missing_campaign_archive(self):
        self.files.pop()
        self.manifest()
        with self.assertRaisesRegex(ValueError, "MAPS01.MIX"):
            assets.prepare(self.source, self.output)

    def test_archive_path_cannot_escape_source(self):
        self.files[0]["path"] = "../outside.MIX"
        self.manifest()
        with self.assertRaisesRegex(ValueError, "escapes asset tree"):
            assets.prepare(self.source, self.output)


if __name__ == "__main__":
    unittest.main()
