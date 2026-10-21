"""Run the actual freestanding C UI model through a host DLL, not a mock."""
import ctypes
import sys
import unittest
from pathlib import Path


class Model(ctypes.Structure):
    _fields_ = [(name, ctypes.c_int) for name in ("page", "selected", "value", "preview", "first_visible")]


dll = ctypes.CDLL(str(Path(sys.argv.pop(1)).resolve()))
dll.hisu_ui_model_key.argtypes = [ctypes.POINTER(Model), ctypes.c_int]
dll.hisu_ui_model_key.restype = None
dll.hisu_ui_adjust.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
dll.hisu_ui_adjust.restype = ctypes.c_int

dll.hisu_ui_battery_bars.argtypes = [ctypes.c_int, ctypes.c_int]
dll.hisu_ui_battery_bars.restype = ctypes.c_int

dll.hisu_ui_battery_color.argtypes = [ctypes.c_int, ctypes.c_uint]
dll.hisu_ui_battery_color.restype = ctypes.c_uint
dll.hisu_ui_battery_pulse.argtypes = [ctypes.c_uint]
dll.hisu_ui_battery_pulse.restype = ctypes.c_uint
dll.hisu_ui_battery_charge_bar.argtypes = [ctypes.c_int, ctypes.c_int]
dll.hisu_ui_battery_charge_bar.restype = ctypes.c_int
dll.hisu_ui_valid_utf8.argtypes = [ctypes.c_char_p, ctypes.c_uint]
dll.hisu_ui_valid_utf8.restype = ctypes.c_int
dll.hisu_ui_overlay_dark_ink.argtypes = [ctypes.c_char_p] + [ctypes.c_uint] * 3 + [ctypes.c_int] * 5
dll.hisu_ui_overlay_dark_ink.restype = ctypes.c_int
dll.hisu_wifi_password_is_valid.argtypes = [ctypes.c_char_p]
dll.hisu_wifi_password_is_valid.restype = ctypes.c_int
dll.hisu_wifi_can_auto_join.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
dll.hisu_wifi_can_auto_join.restype = ctypes.c_int
dll.hisu_wifi_setup_password.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
dll.hisu_wifi_setup_password.restype = ctypes.c_int
dll.hisu_power_percent_from_adc.argtypes = [ctypes.c_int]
dll.hisu_power_percent_from_adc.restype = ctypes.c_int
dll.hisu_power_remaining_seconds.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint64]
dll.hisu_power_remaining_seconds.restype = ctypes.c_int64


class UiModelTests(unittest.TestCase):
    def test_only_next_unfilled_battery_bar_pulses(self):
        active = dll.hisu_ui_battery_charge_bar
        for percent in range(101):
            expected = min(3, percent // 20) if percent < 100 else -1
            self.assertEqual(active(1, percent), expected)
            self.assertEqual(active(0, percent), -1)
        self.assertEqual(active(1, -2147483648), 0)
        self.assertEqual(active(1, 2147483647), -1)

    def test_only_one_battery_bar_is_red_others_match_frame(self):
        color = dll.hisu_ui_battery_color
        for frame in (0x000000, 0xffffff):
            for bars in range(5):
                self.assertEqual(color(bars, frame), 0xef4444 if bars == 1 else frame)
            for percent in range(101):
                active = dll.hisu_ui_battery_charge_bar(1, percent)
                bars = active + 1 if active >= 0 else 4
                self.assertEqual(color(bars, frame), 0xef4444 if bars == 1 else frame)

    def test_charging_bar_blinks_fully_on_and_off(self):
        pulse = dll.hisu_ui_battery_pulse
        self.assertEqual(pulse(0), 255)
        self.assertEqual(pulse(799), 255)
        self.assertEqual(pulse(800), 0)
        self.assertEqual(pulse(1599), 0)
        self.assertEqual(pulse(1600), 255)
        for ms in range(3201):
            value = pulse(ms)
            self.assertEqual(value, 255 if ms % 1600 < 800 else 0)
            self.assertEqual(value, pulse(ms + 1600))

    def test_battery_icon_merges_top_two_old_levels(self):
        bars = dll.hisu_ui_battery_bars
        for percent in range(101):
            self.assertEqual(bars(1, percent), min(4, (percent + 19) // 20))
            self.assertEqual(bars(0, percent), 0)
        for percent, expected in ((-2147483648, 0), (-1, 0), (0, 0),
                                  (1, 1), (20, 1), (21, 2), (40, 2),
                                  (41, 3), (60, 3), (61, 4), (80, 4),
                                  (81, 4), (100, 4), (2147483647, 4)):
            self.assertEqual(bars(1, percent), expected)

    def test_overlay_rgb565_colors(self):
        choose = dll.hisu_ui_overlay_dark_ink
        for pixel, expected in ((0x0000, 0), (0xffff, 1), (0xf800, 0), (0x07e0, 1), (0x001f, 0)):
            pixels = pixel.to_bytes(2, "little") * 16
            self.assertEqual(choose(pixels, 4, 4, 8, 0, 0, 4, 4, -1), expected)

    def test_overlay_uses_local_region_and_stride(self):
        choose = dll.hisu_ui_overlay_dark_ink
        pixels = (b"\x00\x00" * 4 + b"\xff\xff" * 4 + b"\xff" * 4) * 4
        self.assertEqual(choose(pixels, 8, 4, 20, 0, 0, 4, 4, -1), 0)
        self.assertEqual(choose(pixels, 8, 4, 20, 4, 0, 4, 4, -1), 1)
        self.assertEqual(choose(pixels, 8, 4, 20, -2, -2, 6, 6, -1), 0)
        self.assertEqual(choose(pixels, 8, 4, 20, 6, 2, 6, 6, -1), 1)

    def test_overlay_hysteresis(self):
        choose = dll.hisu_ui_overlay_dark_ink
        def gray(level):
            rgb = ((level * 31 // 255) << 11) | ((level * 63 // 255) << 5) | (level * 31 // 255)
            return rgb.to_bytes(2, "little") * 16
        for level in (115, 125, 135, 145):
            for previous in (0, 1):
                self.assertEqual(choose(gray(level), 4, 4, 8, 0, 0, 4, 4, previous), previous)
        self.assertEqual(choose(gray(180), 4, 4, 8, 0, 0, 4, 4, 0), 1)
        self.assertEqual(choose(gray(80), 4, 4, 8, 0, 0, 4, 4, 1), 0)

    def test_overlay_invalid_region_preserves_color(self):
        choose = dll.hisu_ui_overlay_dark_ink
        pixels = b"\xff\xff" * 16
        for previous in (0, 1):
            for data, width, height, stride, x, y, w, h in (
                (None, 4, 4, 8, 0, 0, 4, 4),
                (pixels, 0, 4, 8, 0, 0, 4, 4),
                (pixels, 4, 4, 7, 0, 0, 4, 4),
                (pixels, 4, 4, 8, 10, 10, 4, 4),
                (pixels, 4, 4, 8, 0, 0, 0, 4),
                (pixels, 513, 4, 1026, 0, 0, 4, 4),
            ):
                self.assertEqual(choose(data, width, height, stride, x, y, w, h, previous), previous)

    def test_random_hotspot_password(self):
        for start in range(256):
            entropy = bytes((start + i) % 256 for i in range(8))
            out = ctypes.create_string_buffer(17)
            self.assertEqual(dll.hisu_wifi_setup_password(entropy, out), 1)
            self.assertEqual(out.value, entropy.hex().encode())
            self.assertEqual(len(out.value), 16)
            self.assertEqual(dll.hisu_wifi_password_is_valid(out.value), 1)
        self.assertEqual(dll.hisu_wifi_setup_password(None, out), 0)
        self.assertEqual(dll.hisu_wifi_setup_password(entropy, None), 0)

    def test_wifi_auto_join_policy(self):
        eligible = dll.hisu_wifi_can_auto_join
        self.assertEqual(eligible(1, 1, 1, b"password123"), 1)
        self.assertEqual(eligible(1, 1, 0, b""), 1)
        for saved, supported, secure, password in (
            (0, 1, 1, b"password123"), (1, 0, 1, b"password123"),
            (1, 1, 0, b"password123"), (1, 1, 1, b""),
            (1, 1, 1, b"short"), (1, 1, 1, None),
        ):
            self.assertEqual(eligible(saved, supported, secure, password), 0)

    def test_remaining_runtime_requires_measured_drain(self):
        estimate = dll.hisu_power_remaining_seconds
        self.assertEqual(estimate(78, 73, 600), 8760)
        self.assertEqual(estimate(20, 15, 600), 1800)
        for start, current, elapsed in ((78, 78, 3600), (78, 80, 3600),
                                        (78, 74, 3600), (78, 73, 599), (101, 73, 600),
                                        (78, -1, 600), (78, 73, 31536001)):
            self.assertEqual(estimate(start, current, elapsed), -1)
        self.assertEqual(estimate(5, 0, 600), 0)

    def test_wifi_password_boundaries(self):
        for password in (b"", b"abcdefgh", b"a" * 63, b"0123456789abcdef" * 4, b"spaces ! allowed"):
            self.assertEqual(dll.hisu_wifi_password_is_valid(password), 1)
        for password in (None, b"short", b"g" * 64, b"a" * 65, b"abc\n12345", "mật khẩu".encode()):
            self.assertEqual(dll.hisu_wifi_password_is_valid(password), 0)

    def test_battery_reference_points(self):
        for raw, percent in ((1950, 0), (2054, 20), (2172, 40), (2290, 60), (2408, 80), (2526, 100)):
            self.assertEqual(dll.hisu_power_percent_from_adc(raw), percent)

    def test_battery_bounds_and_monotonicity(self):
        self.assertEqual(dll.hisu_power_percent_from_adc(-2147483648), 0)
        self.assertEqual(dll.hisu_power_percent_from_adc(2147483647), 100)
        previous = -1
        for raw in range(4096):
            value = dll.hisu_power_percent_from_adc(raw)
            self.assertGreaterEqual(value, previous)
            self.assertLessEqual(value, 100)
            previous = value

    def key(self, model, key):
        dll.hisu_ui_model_key(ctypes.byref(model), key)

    def test_idle_and_menu(self):
        m = Model()
        self.key(m, 2)
        self.assertEqual(m.page, 0)
        self.key(m, 1)
        self.assertEqual(m.page, 1)
        self.key(m, 2)
        self.assertEqual(m.selected, 10)
        self.key(m, 3)
        self.assertEqual(m.selected, 0)
        self.key(m, 4)
        self.assertEqual(m.page, 0)

    def test_menu_close_preserves_selection(self):
        m = Model(1, 8, 75, 0, 4)
        self.key(m, 4)  # Board maps the menu's Vol+ hold to BACK.
        self.assertEqual((m.page, m.selected, m.first_visible), (0, 8, 4))
        self.key(m, 1)
        self.assertEqual((m.page, m.selected, m.first_visible), (1, 8, 4))

    def test_every_menu_destination(self):
        for item in range(11):
            m = Model(1, item, 75, 0)
            self.key(m, 1)
            expected = 2 if item < 5 else 5 if item == 5 else 7 if item == 7 else 9 if item == 8 else 0 if item in (9, 10) else 3
            self.assertEqual(m.page, expected, item)

    def test_edit_and_cancel(self):
        m = Model(2, 0, 75, 0)
        self.key(m, 2)
        self.assertEqual(m.value, 85)
        self.key(m, 3)
        self.assertEqual(m.value, 75)
        self.key(m, 4)
        self.assertEqual(m.page, 1)

    def test_setup_back_preserves_view_without_confirming_stop(self):
        for view in range(2):
            for key in (2, 4):
                m = Model(9, 8, 0, view, 5)
                self.key(m, key)
                self.assertEqual((m.page, m.preview, m.selected), (1, view, 8))

    def test_setup_only_cycles_qr_and_info(self):
        m = Model(9, 8, 0, 0, 5)
        for view in (1, 0, 1, 0):
            self.key(m, 3)
            self.assertEqual((m.page, m.preview), (9, view))
        for view, expected in ((0, 0), (1, 0)):
            m = Model(9, 8, 0, view, 5)
            self.key(m, 1)
            self.assertEqual((m.page, m.preview), (9, expected))

    def test_menu_scrolls_one_row_and_retains_position(self):
        m = Model(1, 0, 0, 0, 0)
        for selected in range(1, 11):
            self.key(m, 3)
            self.assertEqual(m.selected, selected)
            self.assertEqual(m.first_visible, max(0, selected - 4))
        self.key(m, 3)
        self.assertEqual((m.selected, m.first_visible), (0, 0))
        self.key(m, 2)
        self.assertEqual((m.selected, m.first_visible), (10, 6))
        for selected in range(9, -1, -1):
            self.key(m, 2)
            self.assertEqual(m.first_visible, min(6, selected))

    def test_info_back_is_physical_left_button(self):
        for item in (6,):
            first_visible = max(2, item - 4)
            m = Model(1, item, 0, 0, first_visible)
            self.key(m, 1)
            self.assertEqual(m.page, 3)
            self.key(m, 1)
            self.assertEqual(m.page, 3)
            self.key(m, 2)
            self.assertEqual(m.page, 3)
            self.key(m, 3)
            self.assertEqual((m.page, m.selected, m.first_visible), (1, item, first_visible))

    def test_bounds(self):
        for setting, low, high in ((0, 10, 100), (1, 0, 100)):
            self.assertEqual(dll.hisu_ui_adjust(setting, low, -1), low)
            self.assertEqual(dll.hisu_ui_adjust(setting, high, 1), high)
        for setting in (2, 4):
            for value in (0, 1):
                self.assertEqual(dll.hisu_ui_adjust(setting, value, 1), 1)
                self.assertEqual(dll.hisu_ui_adjust(setting, value, -1), 0)
        self.assertEqual(dll.hisu_ui_adjust(0, 75, 0), 75)

    def test_timeout_presets(self):
        values = [0, 30, 60, 120, 300, 600]
        for index, value in enumerate(values):
            self.assertEqual(dll.hisu_ui_adjust(3, value, 1), values[min(index + 1, 5)])
            self.assertEqual(dll.hisu_ui_adjust(3, value, -1), values[max(index - 1, 0)])
        self.assertEqual(dll.hisu_ui_adjust(3, 45, 1), 60)
        self.assertEqual(dll.hisu_ui_adjust(3, 45, -1), 30)

    def test_theme_preview_save_and_cancel(self):
        for setting in (2, 4):
            m = Model(1, setting, 0, 0)
            self.key(m, 1)
            self.assertEqual(m.page, 2)
            self.key(m, 3)  # Vol- toggles, including repeated presses.
            self.assertEqual(m.value, 1)
            self.key(m, 3)
            self.assertEqual(m.value, 0)
            self.key(m, 1)  # BOOT confirms.
            self.assertEqual(m.page, 1)
            self.key(m, 1)
            self.key(m, 3)
            self.key(m, 2)  # Vol+ returns without taking the save path.
            self.assertEqual(m.page, 1)
        self.assertEqual(dll.hisu_ui_adjust(4, 1, -1), 0)

    def test_preview(self):
        m = Model(4, 8, 0, 0)
        self.key(m, 2)
        self.assertEqual(m.preview, 0)
        self.key(m, 1)
        self.assertEqual(m.preview, 1)
        self.key(m, 3)
        self.assertEqual(m.preview, 2)
        self.key(m, 1)
        self.assertEqual(m.page, 1)

    def test_invalid_key(self):
        m = Model(1, 3, 0, 0)
        for key in (0, -1, 5, 100):
            self.key(m, key)
            self.assertEqual((m.page, m.selected), (1, 3))

    def test_vietnamese(self):
        text = "Phụ đề tiếng Việt: độ sáng, âm lượng, đang suy nghĩ.".encode()
        self.assertEqual(dll.hisu_ui_valid_utf8(text, 255), 1)
        self.assertEqual(dll.hisu_ui_valid_utf8(text, len(text) - 1), 0)
        self.assertEqual(dll.hisu_ui_valid_utf8(b"", 255), 1)

    def test_payload_limit(self):
        self.assertEqual(dll.hisu_ui_valid_utf8(b"a" * 255, 255), 1)
        self.assertEqual(dll.hisu_ui_valid_utf8(b"a" * 256, 255), 0)
        self.assertEqual(dll.hisu_ui_valid_utf8(b"a" * 254 + "ệ".encode(), 255), 0)

    def test_invalid_utf8(self):
        for text in (b"\x80", b"\xc0\x80", b"\xe1", b"\xed\xa0\x80", b"\xf4\x90\x80\x80", b"\xff"):
            self.assertEqual(dll.hisu_ui_valid_utf8(text, 255), 0, text)
        self.assertEqual(dll.hisu_ui_valid_utf8(None, 255), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
