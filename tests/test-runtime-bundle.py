"""Regression checks against configured real release inputs; never modifies them."""
import importlib.util
import json
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("bundle", ROOT / "tools/pack-runtime-assets.py")
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class ReleaseInputs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads((ROOT / "build/runtime-assets-inputs.json").read_text())

    def test_complete_catalog(self):
        files = bundle.collect(self.config)
        self.assertIn("cod4_facial/xmodelparts/head_mp_usmc_tactical_mich_lod01", files)
        self.assertEqual(sum(x.startswith("cod4_facial/xmodelparts/") for x in files), 42)
        self.assertEqual(sum(x.startswith("cod4_facial/xanim/") for x in files), 244)
        self.assertFalse(any(x.endswith(".h1sky") for x in files))
        self.assertIn("catalogs/skies.json", files)
        self.assertEqual(len(json.loads(files["catalogs/skies.json"][0].read_text())["assets"]), 45)
        self.assertIn("cod4_snipers/viewmodel_remington_putaway", files)
        self.assertIn("cdata/ui_scripts/theater/weapon_swapper.lua", files)
        self.assertIn("tools/ffmpeg.exe", files)

    def test_tester_missing_head_is_build_failure(self):
        read = Path.read_bytes
        def missing(path):
            if path.name == "head_mp_usmc_tactical_mich_lod01":
                raise FileNotFoundError("test: missing source facial rest asset")
            return read(path)
        with patch.object(Path, "read_bytes", missing):
            with self.assertRaisesRegex(FileNotFoundError, "facial rest"):
                bundle.collect(self.config)

    def test_stale_sniper_source_is_build_failure(self):
        read = Path.read_bytes
        def corrupt(path):
            data = read(path)
            return data + b"corrupt" if path.name == "viewmodel_remington_ads_up" else data
        with patch.object(Path, "read_bytes", corrupt):
            with self.assertRaisesRegex(ValueError, "provenance mismatch"):
                bundle.collect(self.config)

    def test_paths_cannot_escape_cache(self):
        for name in ("../file", "/file", "C:/file", "a//b", "a/../b", "a\\b", "a/./b", "a:b"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                bundle.safe_name(name)

    def test_build_does_not_need_extracted_skies(self):
        config = {k: v for k, v in self.config.items() if k not in ("skies", "sky_manifest")}
        self.assertIn("catalogs/skies.json", bundle.collect(config))


if __name__ == "__main__":
    unittest.main()
