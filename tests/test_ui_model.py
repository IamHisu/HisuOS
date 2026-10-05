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
dll.hisu_ui_valid_utf8.argtypes = [ctypes.c_char_p, ctypes.c_uint]
dll.hisu_ui_valid_utf8.restype = ctypes.c_int
dll.hisu_wifi_password_is_valid.argtypes = [ctypes.c_char_p]
dll.hisu_wifi_password_is_valid.restype = ctypes.c_int
dll.hisu_power_percent_from_adc.argtypes = [ctypes.c_int]
dll.hisu_power_percent_from_adc.restype = ctypes.c_int
dll.hisu_power_remaining_seconds.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint64]
dll.hisu_power_remaining_seconds.restype = ctypes.c_int64


class UiModelTests(unittest.TestCase):
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
        self.assertEqual(m.selected, 11)
        self.key(m, 3)
        self.assertEqual(m.selected, 0)
        self.key(m, 4)
        self.assertEqual(m.page, 0)

    def test_every_menu_destination(self):
        for item in range(12):
            m = Model(1, item, 75, 0)
            self.key(m, 1)
            expected = 2 if item < 5 else 5 if item == 5 else 7 if item == 8 else 8 if item == 9 else 0 if item in (10, 11) else 3
            self.assertEqual(m.page, expected, item)

    def test_edit_and_cancel(self):
        m = Model(2, 0, 75, 0)
        self.key(m, 2)
        self.assertEqual(m.value, 85)
        self.key(m, 3)
        self.assertEqual(m.value, 75)
        self.key(m, 4)
        self.assertEqual(m.page, 1)

    def test_menu_scrolls_one_row_and_retains_position(self):
        m = Model(1, 0, 0, 0, 0)
        for selected in range(1, 12):
            self.key(m, 3)
            self.assertEqual(m.selected, selected)
            self.assertEqual(m.first_visible, max(0, selected - 4))
        self.key(m, 3)
        self.assertEqual((m.selected, m.first_visible), (0, 0))
        self.key(m, 2)
        self.assertEqual((m.selected, m.first_visible), (11, 7))
        for selected in range(10, -1, -1):
            self.key(m, 2)
            self.assertEqual(m.first_visible, min(7, selected))

    def test_info_back_is_physical_left_button(self):
        for item in (6, 7):
            m = Model(1, item, 0, 0, 2)
            self.key(m, 1)
            self.assertEqual(m.page, 3)
            self.key(m, 1)
            self.assertEqual(m.page, 3)
            self.key(m, 2)
            self.assertEqual(m.page, 3)
            self.key(m, 3)
            self.assertEqual((m.page, m.selected, m.first_visible), (1, item, 2))

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
