"""Source/build contract: uploaded media only, with a solid theme fallback."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WallpaperPolicyTests(unittest.TestCase):
    def test_no_bundled_idle_asset(self):
        cmake = (ROOT / "components/hisu_ui/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("idle_cat", cmake)
        self.assertIn('"assets/boot_windows_xp.gif"', cmake)
        self.assertFalse(list((ROOT / "components/hisu_ui/assets").glob("idle_cat*.gif")))

    def test_uploaded_render_and_theme_fallback(self):
        ui = (ROOT / "components/hisu_ui/src/hisu_ui.c").read_text(encoding="utf-8")
        self.assertNotIn("idle_cat", ui)
        self.assertNotIn("HISU_SETTING_WALLPAPER_DEFAULT", ui)
        self.assertIn("case 0x000000: return 0xffffff;", ui)
        self.assertIn("lv_obj_set_style_bg_color(root, lv_color_hex(theme_color(0x000000)), 0);", ui)
        self.assertIn("lv_gif_set_src(s_idle_gif,&s_wallpaper_image);", ui)
        self.assertIn("lv_image_set_src(image,&s_wallpaper_image)", ui)
        failure = ui.split("Uploaded GIF could not be decoded; using theme background", 1)[1].split("} else", 1)[0]
        self.assertIn("lv_obj_delete(s_idle_gif);", failure)
        self.assertIn("s_idle_gif=NULL;", failure)
        self.assertNotIn("lv_gif_set_src", failure)

    def test_removed_setting_and_selection_tool(self):
        settings = (ROOT / "components/hisu_settings/src/hisu_settings.c").read_text(encoding="utf-8")
        prompt = (ROOT / "components/hisu_voice/src/hisu_voice_live_setup.h").read_text(encoding="utf-8")
        self.assertNotIn('"wall_default"', settings)
        self.assertNotIn("set_wallpaper", prompt)
        self.assertIn("get_wallpaper", prompt)


if __name__ == "__main__":
    unittest.main()
