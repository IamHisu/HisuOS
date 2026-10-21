"""Diagnostic placement contracts; actual heap/stack values require a board."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RamDiagnosticTests(unittest.TestCase):
    def test_ui_separates_lvgl_display_dma_driver_and_idle(self):
        source = (ROOT / "components/hisu_ui/src/hisu_ui.c").read_text(encoding="utf-8")
        task = source.split("static void ui_task(void *arg)", 1)[1]
        phases = ("before_lv_init", "lv_init();", "after_lv_init",
                  "lv_display_create(", "after_display_create",
                  "heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)",
                  "after_draw_buffer", "hisu_board_init(", "after_board_init",
                  "render_boot_splash();", "after_splash_ui",
                  "before_idle_ui", "render();", "after_idle_ui")
        position = 0
        for phase in phases:
            position = task.index(phase, position) + len(phase)
        self.assertIn("internal_used_delta=%d", source)
        self.assertIn("DMA_used_delta=%d", source)
        self.assertIn("PSRAM_used_delta=%d", source)
        self.assertIn("Display draw buffer: bytes=%u memory=internal_DMA", source)

    def test_lookup_keeps_global_allocator_and_guards(self):
        source = (ROOT / "components/hisu_voice/src/hisu_voice_lookup.c").read_text(encoding="utf-8")
        self.assertNotIn("cJSON_InitHooks(", source)
        self.assertNotIn("cJSON_PrintUnformatted(", source)
        self.assertIn("cJSON_PrintPreallocated(", source)
        self.assertIn("MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT", source)
        self.assertIn("<10240", source)
        self.assertIn("<6144", source)
        worker = source.split("static void lookup_worker(", 1)[1].split("static bool lookup_reject(", 1)[0]
        self.assertLess(worker.index("free(j->body)"), worker.index('"worker_cleaned"'))
        self.assertLess(worker.index("Complete:"), worker.index("lookup_done=true"))

    def test_private_json_namespace_matches_dependency(self):
        dependency = (ROOT / "managed_components/espressif__cjson/cJSON/cJSON.h").read_text(encoding="utf-8")
        private = (ROOT / "components/hisu_voice/src/hisu_voice_lookup_json.h").read_text(encoding="utf-8")
        declarations = re.findall(r"^CJSON_PUBLIC[^\n]+", dependency, re.M)
        self.assertEqual(declarations, re.findall(r"^CJSON_PUBLIC[^\n]+", private, re.M))
        for declaration in declarations:
            name = re.search(r"\b(cJSON_\w+)\s*\(", declaration)[1]
            self.assertIn(f"#define {name} hisu_lookup_{name}", private)
        self.assertIn("#define cJSON_Duplicate_rec hisu_lookup_cJSON_Duplicate_rec", private)
        implementation = (ROOT / "components/hisu_voice/src/hisu_voice_lookup_json.c").read_text(encoding="utf-8")
        self.assertIn('#include "cJSON.c"', implementation)
        self.assertNotIn("InitHooks(", implementation)
        for module in ("hisu_voice_lookup.c", "hisu_voice_lookup_data.c", "hisu_voice_news.c"):
            source = (ROOT / "components/hisu_voice/src" / module).read_text(encoding="utf-8")
            self.assertIn('#include "hisu_voice_lookup_json.h"', source)
        live = (ROOT / "components/hisu_voice/src/hisu_voice_live.c").read_text(encoding="utf-8")
        self.assertNotIn('#include "hisu_voice_lookup_json.h"', live)
        sender = live.split("static bool send_lookup_result(", 1)[1].split("static char *setup_substitute(", 1)[0]
        self.assertIn("voice_lookup_encode_result(job)", sender)
        self.assertNotIn("cJSON_Parse", sender)


if __name__ == "__main__":
    unittest.main()
