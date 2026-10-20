#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define HISU_MEMORY_ERR_FULL ((esp_err_t)0x7a01)

esp_err_t hisu_memory_init(void);
esp_err_t hisu_memory_remember(const char *fact, uint32_t replace_id, uint32_t *id);
/* Only at capacity; compare the selected old fact before atomic replacement.
 * The new fact receives a fresh id, so stale tools cannot delete it by old id. */
esp_err_t hisu_memory_evict_and_remember(const char *fact, uint32_t evict_id,
                                      const char *expected_fact, uint32_t *id);
esp_err_t hisu_memory_forget(uint32_t id);
esp_err_t hisu_memory_clear(void);
esp_err_t hisu_memory_set_enabled(bool enabled);
/* Allocated JSON/context belongs to the caller (free). No personal text in logs. */
char *hisu_memory_json(void);
char *hisu_memory_context(void);
void hisu_memory_recent(bool user, const char *text);
void hisu_memory_clear_recent(void);
bool hisu_memory_pending(void);
/* Caller must serialize with voice/music startup; flash is never written by tools. */
esp_err_t hisu_memory_flush(void);
/* Volatile, user-only post-session extraction; never writes flash directly. */
void hisu_memory_summary_request(void);
void hisu_memory_summary_begin_turn(void);
char *hisu_memory_summary_snapshot(uint32_t *ticket);
bool hisu_memory_summary_current(uint32_t ticket);
void hisu_memory_summary_done(uint32_t ticket);
/* Strict JSON proposals, max three. No deletion or unrelated eviction. */
esp_err_t hisu_memory_summary_apply(uint32_t ticket,const char *proposals,unsigned *stored);
