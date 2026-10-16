#pragma once

/* Shared boundary: changing wallpaper slots requires a storage migration. */
#define HISU_STORAGE_WALLPAPER_SLOT_BYTES 1052672U
#define HISU_MEMORY_OFFSET (2U * HISU_STORAGE_WALLPAPER_SLOT_BYTES)
#define HISU_MEMORY_SLOT_BYTES 8192U
