# HisuOS development

- ESP-IDF 5.5.5, ESP32-S3, default board `zhengchen_154`.
- Read `docs/ARCHITECTURE.md` and `docs/MIGRATION.md` before changing ownership.
- Keep `main` as the entry point; assemble services in `hisu_app`.
- Declare direct component dependencies in each CMakeLists.txt. No sibling-project paths.
- Public headers belong in `include`; private implementation belongs in `src`.
- LVGL calls must run on the UI task, including callbacks that modify display state.
- Bind service callbacks before starting their worker tasks.
- Preserve imported license notices. Never put credentials into source or defaults.
- Run `idf.py build` after firmware changes. Flash only when the user requests it.
- Run `npm test` from `web/src` after changing web behavior.
