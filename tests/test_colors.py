# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
import json
import colorsys
from contextlib import closing
from pathlib import Path
import sqlite3
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("color_sync", Path(__file__).resolve().parents[1] / "tools/color_sync.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

COLORS = dict(zip([
    "background", "on_background", "surface_container_highest", "on_surface_variant",
    "outline", "primary", "on_primary", "secondary", "tertiary", "error",
], ["#141218", "#e6e0e9", "#36343b", "#cac4d0", "#938f99", "#d0bcff",
    "#381e72", "#ccc2dc", "#efb8c8", "#ffb4ab"]))


class ColorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.home = Path(self.temporary.name).resolve()
        self.sync = module.ColorSync(self.home, self.home / ".config", self.home / ".local/state")

    def tearDown(self):
        self.temporary.cleanup()

    def put(self, name, text):
        path = self.home / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def test_only_color_keys_change(self):
        kitty = self.put(".config/kitty/kitty.conf", "font_size 10\nbackground_opacity 0.80\nshell /usr/bin/fish\nbackground #1e1e2e\ncolor1 #f38ba8\n")
        kwin = self.put(".config/kwinrc", "[Round-Corners]\nSize=12\nOutlineThickness=2\nOutlineColor=192,97,255\nInactiveOutlineColor=88,64,112\n\n[Script-krohnkite]\nscreenDefaultLayout=:BTree\nscreenGapTop=20\n")
        changes = self.sync.plan(COLORS)
        self.assertIn("font_size 10\nbackground_opacity 0.80\nshell /usr/bin/fish\n", changes[kitty])
        self.assertIn("color1 #f38ba8\n", changes[kitty])
        self.assertIn("background #141218", changes[kitty])
        self.assertIn("Size=12\nOutlineThickness=2\nOutlineColor=208,188,255", changes[kwin])
        self.assertIn("[Script-krohnkite]\nscreenDefaultLayout=:BTree\nscreenGapTop=20\n", changes[kwin])

    def test_btop_keeps_original_theme_and_transparency(self):
        config = self.put(".config/btop/btop.conf", 'color_theme = "Cristal-Morado"\ntheme_background = false\nupdate_ms = 3000\n')
        original = self.put(".config/btop/themes/Cristal-Morado.theme", 'theme[main_bg]=""\ntheme[main_fg]="#cdd6f4"\n')
        before = original.read_text()
        changes = self.sync.plan(COLORS)
        self.assertNotIn(original, changes)
        self.assertIn('theme_background = false\nupdate_ms = 3000\n', changes[config])
        self.assertIn('theme[main_bg]=""', changes[self.home / ".config/btop/themes/Waywallen.theme"])
        self.assertEqual(original.read_text(), before)

    def test_css_is_idempotent_and_keeps_opacity(self):
        css = "/* my css */\nbody { background: rgba(30, 30, 46, 0.88) !important; }\n"
        once = module.css_colors(css, COLORS)
        self.assertEqual(module.css_colors(once, COLORS), once)
        self.assertEqual(once.count(module.BEGIN), 1)
        self.assertIn(css.strip(), once)
        self.assertIn("rgba(20, 18, 24, 0.88)", once)

    def test_backup_and_restore_exact_originals(self):
        path = self.put(".config/kitty/kitty.conf", "font_size 10\nbackground #1e1e2e\n")
        before = path.read_bytes()
        self.sync.apply(COLORS)
        manifest = self.sync.state / "backup.json"
        self.assertEqual(manifest.stat().st_mode & 0o777, 0o600)
        changed = COLORS | {"primary": "#aabbcc"}
        self.sync.apply(changed)
        self.sync.apply(restore=True)
        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(json.loads(manifest.read_text())["files"], {})

    def test_generated_theme_is_removed_on_restore(self):
        self.put(".config/btop/btop.conf", 'color_theme = "Cristal-Morado"\n')
        self.put(".config/btop/themes/Cristal-Morado.theme", 'theme[main_bg]=""\n')
        generated = self.home / ".config/btop/themes/Waywallen.theme"
        self.sync.apply(COLORS)
        self.assertTrue(generated.is_file())
        self.sync.apply(restore=True)
        self.assertFalse(generated.exists())

    def test_restore_preserves_new_unrelated_user_edits(self):
        path = self.put(".config/kitty/kitty.conf", "font_size 10\n")
        self.sync.apply(COLORS)
        edited = path.read_text() + "# NEW USER WORK\n"
        path.write_text(edited)
        self.sync.apply(restore=True)
        self.assertEqual(path.read_text(), "font_size 10\n# NEW USER WORK\n")

    def test_restore_preserves_user_edits_between_syncs(self):
        path = self.put(".config/kitty/kitty.conf", "font_size 10\nbackground #1e1e2e\n")
        self.sync.apply(COLORS)
        path.write_text(path.read_text().replace("font_size 10", "font_size 12"))
        self.sync.apply(COLORS | {"primary": "#aabbcc"})
        self.sync.apply(restore=True)
        self.assertEqual(path.read_text(), "font_size 12\nbackground #1e1e2e\n")

    def test_restore_refuses_new_managed_color_edits(self):
        path = self.put(".config/kitty/kitty.conf", "background #1e1e2e\n")
        self.sync.apply(COLORS)
        edited = path.read_text().replace("background #141218", "background #abcdef")
        path.write_text(edited)
        with self.assertRaises(ValueError):
            self.sync.apply(restore=True)
        self.assertEqual(path.read_text(), edited)

    def test_restore_preserves_dynamic_kwin_state(self):
        path = self.put(".config/kwinrc", "[Round-Corners]\nOutlineColor=192,97,255\n\n[Tiling][monitor]\ntiles=OLD\n")
        self.sync.apply(COLORS)
        path.write_text(path.read_text().replace("tiles=OLD", "tiles=NEW"))
        self.sync.apply(restore=True)
        self.assertIn("tiles=NEW", path.read_text())
        self.assertIn("OutlineColor=192,97,255", path.read_text())

    def test_restore_preserves_other_css_edits(self):
        path = self.put(str(self.sync.vesktop.relative_to(self.home)), "body { background: rgba(30, 30, 46, 0.88); }\n")
        self.sync.apply(COLORS)
        path.write_text(path.read_text() + "/* NEW CSS */\n")
        self.sync.apply(restore=True)
        self.assertIn("/* NEW CSS */", path.read_text())
        self.assertNotIn(module.BEGIN, path.read_text())

    def test_existing_unmanaged_theme_is_not_overwritten(self):
        self.put(".config/btop/btop.conf", 'color_theme = "Cristal-Morado"\n')
        self.put(".config/btop/themes/Cristal-Morado.theme", 'theme[main_bg]=""\n')
        existing = self.put(".config/btop/themes/Waywallen.theme", "USER WORK\n")
        with self.assertRaises(ValueError):
            self.sync.plan(COLORS)
        self.assertEqual(existing.read_text(), "USER WORK\n")

    def test_no_missing_app_configs_are_created(self):
        self.assertEqual(self.sync.plan(COLORS), {})
        self.assertFalse((self.home / ".config").exists())
        self.assertFalse(self.sync.state.exists())

    def test_symlinks_are_not_followed(self):
        outside = self.put("outside.conf", "USER WORK\n")
        link = self.home / ".config/kitty/kitty.conf"
        link.parent.mkdir(parents=True)
        link.symlink_to(outside)
        with self.assertRaises(ValueError):
            self.sync.plan(COLORS)
        self.assertEqual(outside.read_text(), "USER WORK\n")

    def test_unmanaged_state_paths_are_rejected(self):
        outside = self.put("outside.conf", "USER WORK\n")
        with self.assertRaises(ValueError):
            self.sync.read(outside)

    def test_failed_write_rolls_back_other_files(self):
        kitty = self.put(".config/kitty/kitty.conf", "font_size 10\n")
        kwin = self.put(".config/kwinrc", "[Round-Corners]\nOutlineColor=192,97,255\n")
        originals = {path: path.read_bytes() for path in [kitty, kwin]}
        writer = self.sync.atomic_write

        def fail_once(path, text, mode=0o600):
            if path == kwin:
                raise OSError("simulated failed write")
            writer(path, text, mode)

        with patch.object(self.sync, "atomic_write", side_effect=fail_once):
            with self.assertRaises(OSError):
                self.sync.apply(COLORS)
        for path, before in originals.items():
            self.assertEqual(path.read_bytes(), before)

    def test_damaged_css_markers_abort(self):
        with self.assertRaises(ValueError):
            module.css_colors("body {}\n" + module.BEGIN, COLORS)

    def test_concurrent_edits_are_not_discarded(self):
        path = self.put(".config/kitty/kitty.conf", "font_size 10\n")
        planner = self.sync.plan

        def edit_after_planning(colors):
            result = planner(colors)
            path.write_text("font_size 12\n# concurrent user edit\n")
            return result

        with patch.object(self.sync, "plan", side_effect=edit_after_planning):
            with self.assertRaises(ValueError):
                self.sync.apply(COLORS)
        self.assertEqual(path.read_text(), "font_size 12\n# concurrent user edit\n")

    def theme_fixtures(self):
        self.put(".config/kdeglobals", "[General]\nfont=KEEP FONT\n\n[Icons]\nTheme=KEEP ICONS\n")
        self.put(".config/plasmarc", "[Theme]\nname=PurPurNight-Plasma\n")
        self.put(".config/Kvantum/kvantum.kvconfig", "[General]\ntheme=Sweet-Blur\n")
        self.put(".local/share/plasma/desktoptheme/PurPurNight-Plasma/metadata.json", json.dumps({"KPlugin": {"Id": "PurPurNight-Plasma", "Name": "Original", "License": "GPL"}}))
        self.put(".local/share/plasma/desktoptheme/PurPurNight-Plasma/colors", "[Colors:Window]\nBackgroundNormal=22,22,47\n")
        self.put(".local/share/plasma/desktoptheme/PurPurNight-Plasma/dialogs/background.svg", '<svg><rect id="background" x="1" fill="#16162f" opacity="0.88"/><path fill="#7367b3"/></svg>')
        self.put(".local/share/plasma/desktoptheme/PurPurNight-Plasma/LICENSE", "KEEP ORIGINAL LICENSE\n")
        self.put(".config/Kvantum/Sweet-Blur/Sweet-Blur.svg", '<svg><rect fill="#c50ed2" opacity="0.82"/><path fill="#252a3f78"/></svg>')
        self.put(".config/Kvantum/Sweet-Blur/Sweet-Blur.kvconfig", "[%General]\nreduce_window_opacity=18\ntranslucent_windows=true\n\n[GeneralColors]\nhighlight.color=#c50ed2\nalt.base.color=#252a3f78\n\n[ItemView]\nframe.top=2\ntext.normal.color=#aaaaac\n")

    def test_native_palette_preserves_fonts_icons_and_other_options(self):
        self.theme_fixtures()
        path = self.home / ".config/kdeglobals"
        self.sync.apply(COLORS)
        parsed = module.ini(path.read_text())
        self.assertEqual(parsed["General"]["font"], "KEEP FONT")
        self.assertEqual(parsed["Icons"]["Theme"], "KEEP ICONS")
        self.assertEqual(parsed["Colors:Selection"]["BackgroundNormal"], "208,188,255")
        self.assertEqual(parsed["General"]["AccentColorFromWallpaper"], "false")
        path.write_text(path.read_text().replace("KEEP FONT", "NEW USER FONT"))
        self.sync.apply(restore=True)
        restored = module.ini(path.read_text())
        self.assertEqual(restored["General"]["font"], "NEW USER FONT")
        self.assertEqual(restored["Icons"]["Theme"], "KEEP ICONS")

    def test_theme_copies_keep_geometry_alpha_originals_and_license(self):
        self.theme_fixtures()
        sources = {path: path.read_bytes() for root in [self.home / ".local/share/plasma/desktoptheme/PurPurNight-Plasma", self.home / ".config/Kvantum/Sweet-Blur"] for path in root.rglob("*") if path.is_file()}
        changes = self.sync.apply(COLORS)
        generated_svg = self.home / ".local/share/plasma/desktoptheme/WaywallenSelector-Plasma-A/dialogs/background.svg"
        content = generated_svg.read_text()
        self.assertIn('id="background" x="1"', content)
        self.assertIn('opacity="0.88"', content)
        self.assertNotIn("#7367b3", content)
        kvantum = self.home / ".config/Kvantum/WaywallenSelector-Kvantum-A/WaywallenSelector-Kvantum-A.kvconfig"
        self.assertIn("reduce_window_opacity=18\ntranslucent_windows=true", kvantum.read_text())
        self.assertIn("frame.top=2", kvantum.read_text())
        self.assertEqual(module.ini(kvantum.read_text())["GeneralColors"]["alt.base.color"][-2:], "78")
        active = module.ini((self.home / ".config/Kvantum/kvantum.kvconfig").read_text())["General"]
        self.assertEqual(dict(active), {"theme": "WaywallenSelector-Kvantum-A"})
        for path, before in sources.items():
            self.assertEqual(path.read_bytes(), before)
        self.assertEqual((generated_svg.parents[1] / "LICENSE").read_text(), "KEEP ORIGINAL LICENSE\n")
        self.assertTrue(changes)

    def test_theme_palette_is_idempotent_and_alternates_cache_keys(self):
        self.theme_fixtures()
        self.sync.apply(COLORS)
        self.assertEqual(self.sync.apply(COLORS), {})
        self.sync.apply(COLORS | {"primary": "#ff4488"})
        self.assertIn("name=WaywallenSelector-Plasma-B", (self.home / ".config/plasmarc").read_text())
        self.assertEqual(dict(module.ini((self.home / ".config/Kvantum/kvantum.kvconfig").read_text())["General"]), {"theme": "WaywallenSelector-Kvantum-B"})
        self.sync.apply(restore=True)
        self.assertIn("name=PurPurNight-Plasma", (self.home / ".config/plasmarc").read_text())
        self.assertIn("theme=Sweet-Blur", (self.home / ".config/Kvantum/kvantum.kvconfig").read_text())
        self.assertEqual(dict(module.ini((self.home / ".config/Kvantum/kvantum.kvconfig").read_text())["General"]), {"theme": "Sweet-Blur"})
        self.assertFalse(any(root.exists() for root in self.sync.generated_roots))
        self.sync.apply(COLORS)  # A restored installation can be enabled again.

    def test_generated_theme_user_edits_are_not_lost(self):
        self.theme_fixtures()
        self.sync.apply(COLORS)
        path = self.home / ".local/share/plasma/desktoptheme/WaywallenSelector-Plasma-A/dialogs/background.svg"
        path.write_text(path.read_text() + "<!-- USER EDIT -->")
        with self.assertRaises(ValueError):
            self.sync.apply(COLORS)
        self.assertIn("<!-- USER EDIT -->", path.read_text())

    def test_cava_colors_only(self):
        path = self.put(".config/cava/config", "[input]\nmethod=pipewire\n\n[color]\ngradient=1\ngradient_color_1='#8f00ff'\n")
        self.sync.apply(COLORS)
        self.assertIn("method=pipewire", path.read_text())
        self.assertIn("gradient_color_1='#d0bcff'", path.read_text())
        self.sync.apply(restore=True)
        self.assertNotIn("gradient_color_8", path.read_text())

    def test_gray_and_small_vivid_accent(self):
        from PIL import Image, ImageDraw
        path = self.home / "wallpaper.png"
        image = Image.new("RGB", (100, 100), "#888888")
        image.save(path)
        self.assertIsNone(module.image_accent(path))
        draw = ImageDraw.Draw(image)
        draw.rectangle((30, 30, 40, 80), fill="#a020d0")
        image.save(path)
        color = module.image_accent(path)
        hue = colorsys.rgb_to_hsv(*(v / 255 for v in module.rgb(color)))[0]
        self.assertTrue(0.72 < hue < 0.88, color)

    def test_material_warm_neutrals_are_not_used_as_window_backgrounds(self):
        from PIL import Image
        path = self.home / "wallpaper.png"
        Image.new("RGB", (10, 10), "#9911cc").save(path)
        data = {"colors": {name: {"dark": {"color": value}} for name, value in COLORS.items()}}
        with patch.object(module.shutil, "which", return_value="/fake/matugen"), patch.object(module.subprocess, "run", return_value=SimpleNamespace(returncode=0, stdout=json.dumps(data))) as run:
            colors = module.palette_from_image(path)
        self.assertEqual(colors["background"], "#111216")
        self.assertGreaterEqual(module.contrast(colors["primary"], colors["background"]), 4.5)
        hue = colorsys.rgb_to_hsv(*(v / 255 for v in module.rgb(colors["primary"])))[0]
        self.assertTrue(0.72 < hue < 0.88)
        self.assertIn("--dry-run", run.call_args.args[0])
        self.assertIn("color", run.call_args.args[0])

    def test_scene_color_override_wins_but_gray_override_does_not(self):
        database = self.home / ".var/app/org.waywallen.waywallen/data/waywallen/waywallen-v2.db"
        database.parent.mkdir(parents=True)
        with closing(sqlite3.connect(database)) as db:
            db.execute("CREATE TABLE item (id TEXT, user_property_overrides TEXT)")
            db.executemany("INSERT INTO item VALUES (?,?)", [
                ("purple", json.dumps({"waywallen.scheme_color": "0.7 0.2 0.8"})),
                ("gray", json.dumps({"waywallen.scheme_color": "0.12 0.12 0.12"})),
            ])
            db.commit()
        with patch.object(module.Path, "home", return_value=self.home):
            self.assertEqual(module.wallpaper_override("purple"), "#b233cc")
            self.assertIsNone(module.wallpaper_override("gray"))


if __name__ == "__main__":
    unittest.main()
