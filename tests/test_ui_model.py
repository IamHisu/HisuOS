"""Run the actual freestanding C UI model through a host DLL, not a mock."""
import ctypes
import sys
import unittest
from pathlib import Path


class Model(ctypes.Structure):
    _fields_ = [(name, ctypes.c_int) for name in ("page", "selected", "value", "preview")]


dll = ctypes.CDLL(str(Path(sys.argv.pop(1)).resolve()))
dll.hisu_ui_model_key.argtypes = [ctypes.POINTER(Model), ctypes.c_int]
dll.hisu_ui_model_key.restype = None
dll.hisu_ui_adjust.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
dll.hisu_ui_adjust.restype = ctypes.c_int
dll.hisu_ui_valid_utf8.argtypes = [ctypes.c_char_p, ctypes.c_uint]
dll.hisu_ui_valid_utf8.restype = ctypes.c_int


class UiModelTests(unittest.TestCase):
    def key(self, model, key):
        dll.hisu_ui_model_key(ctypes.byref(model), key)

    def test_idle_and_menu(self):
        m = Model()
        self.key(m, 2)
        self.assertEqual(m.page, 0)
        self.key(m, 1)
        self.assertEqual(m.page, 1)
        self.key(m, 2)
        self.assertEqual(m.selected, 8)
        self.key(m, 3)
        self.assertEqual(m.selected, 0)
        self.key(m, 4)
        self.assertEqual(m.page, 0)

    def test_every_menu_destination(self):
        for item in range(9):
            m = Model(1, item, 75, 0)
            self.key(m, 1)
            expected = 2 if item < 4 else 4 if item == 7 else 0 if item == 8 else 3
            self.assertEqual(m.page, expected, item)

    def test_edit_and_cancel(self):
        m = Model(2, 0, 75, 0)
        self.key(m, 2)
        self.assertEqual(m.value, 80)
        self.key(m, 3)
        self.assertEqual(m.value, 75)
        self.key(m, 4)
        self.assertEqual(m.page, 1)

    def test_bounds(self):
        for setting, low, high in ((0, 10, 100), (1, 0, 100)):
            self.assertEqual(dll.hisu_ui_adjust(setting, low, -1), low)
            self.assertEqual(dll.hisu_ui_adjust(setting, high, 1), high)
        self.assertEqual(dll.hisu_ui_adjust(2, 1, 1), 0)
        self.assertEqual(dll.hisu_ui_adjust(2, 0, -1), 1)
        self.assertEqual(dll.hisu_ui_adjust(0, 75, 0), 75)

    def test_timeout_presets(self):
        values = [0, 30, 60, 120, 300, 600]
        for index, value in enumerate(values):
            self.assertEqual(dll.hisu_ui_adjust(3, value, 1), values[min(index + 1, 5)])
            self.assertEqual(dll.hisu_ui_adjust(3, value, -1), values[max(index - 1, 0)])
        self.assertEqual(dll.hisu_ui_adjust(3, 45, 1), 60)
        self.assertEqual(dll.hisu_ui_adjust(3, 45, -1), 30)

    def test_preview(self):
        m = Model(4, 7, 0, 0)
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
