# Hisu Memory

This is an independent HisuOS implementation, not the MimiClaw agent loop. Gemini
Live selects facts through bounded device tools; no filesystem, second model,
vector database or background task is added.

## Two Lifetimes

- Long-term: at most 24 user facts, each at most 256 UTF-8 bytes. Automatic fact
  selection is initially enabled, as requested, and can be disabled in web
  Setting > Device. The model is instructed to select stable stated preferences,
  names, habits and projects, not every question or inferred personal details.
- Recent conversation: a rolling 2048-byte text excerpt in PSRAM, retained across
  voice sessions while powered. Input transcription and delivered subtitle
  segments feed it. It is incomplete, can include an interrupted response, is
  cleared at reboot, and is never written to flash.

The model receives up to approximately 2 KiB of fact text/metadata (newest first)
plus the recent excerpt on each connection. `list_memory` exposes all facts when
the relevant one did not fit. Dynamic data is cJSON-escaped inside the existing
setup template; its large schema is not parsed into a second object tree in
internal heap. Instructions identify history as untrusted data, not executable
instructions or consent. This prompt boundary is not a guarantee against model
mistakes or prompt injection.

## Tools and Management

`remember(fact, replace_id?)` creates a fact or replaces the same fact by its
existing id. Exact duplicates are deduplicated. Invalid UTF-8, controls, oversized
facts and writes while automatic memory is disabled are rejected. At capacity,
a plain add returns `memory_full` and a structured next action instead of a
generic allocation failure. Gemini uses `list_memory()` to inspect all 24 facts,
prefers merging/correcting the same topic, and can automatically replace a less
useful or obsolete fact by calling `remember(fact, evict_id, expected_fact)`.
`replace_id` and `evict_id` are mutually exclusive.

Eviction is accepted only at full capacity, with automatic writes enabled, a
valid new fact, an existing target id and an exact match of its old text. A
changed/deleted target, duplicate new fact or spare capacity rejects eviction
without changing the cache. Validation and replacement share one mutex
transaction; the new fact gets a fresh monotonic id and moves to the newest end.
Stale deletion tools referring to the evicted id cannot delete its replacement.
Flash format, record size, A/B persistence and idle-save rules are unchanged.
Do not implement this as `forget` followed by `remember`.

Selection is Gemini's semantic judgment, not a firmware FIFO/keyword ranking.
The prompt prioritizes names, stable preferences, active projects and things the
user emphasized; id/age is only a tie-breaker. If every existing fact is more
useful, Gemini should skip the new one. These priorities are model instructions,
not immutable per-record protection flags or a guarantee of good ranking.
`forget(id)` still means an explicit user deletion request. Model relevance,
avoiding reintroducing evicted history and excluding sensitive content are model
policies, not a deterministic privacy classifier. Do not tell Hisu secrets.

Web Setting adds authenticated GET/POST `/memory`, reusing the existing numeric
Host, Origin, hotspot-peer and session-token checks. It can list/delete facts,
clear all facts, toggle future automatic writes, or clear the recent excerpt.
It cannot add arbitrary files or paths. The browser renders facts with
`textContent`, never HTML. Disabling automatic writes does not erase existing
facts or disable conversational context; use the deletion controls for that.

Deleting a fact also clears the local recent excerpt. The model's already-open
remote context cannot be retroactively erased: close and reopen a voice session
to use a fresh context. Local deletion is not a request to delete data already
sent to the API provider.

## Storage and Flash Ownership

The existing `storage` partition is unchanged. Shared layout constants reserve
the two existing wallpaper slots first (2 x 0x101000 bytes), then two 8192-byte
memory slots at partition-relative offsets 0x202000 and 0x204000. Slot size or
offset changes require migration. Startup checks partition bounds. There is no
partition-table change, filesystem mount or automatic NVS erase.

Each record has a versioned 64-byte CRC-protected header and a CRC-protected
fixed-size data payload. A save erases the inactive slot, writes payload, writes
header last, and reads back/validates the new record before retiring the previous
slot. An empty record is the durable deletion marker. Boot chooses the newest
valid sequence; if an older/partially erased slot remains, idle maintenance
erases it. An entirely erased retired slot is not erased again on each reboot.
Both invalid records recover to empty memory; corrupted facts are not injected.

Tools and HTTP handlers only mutate a bounded PSRAM cache and revision counter.
The existing `live_send` worker flushes after WebSocket teardown, only when there
is no active voice session, music producer/playback or in-flight voice output.
New Gemini connection setup also runs on that worker, so it cannot overlap the
flash operation. A new wake may wait for a save to finish. The memory mutex is
never held during erase/write/readback; revisions changed during a save remain
dirty for another flush. Failed saves retry no more often than every five
seconds. No periodic writes occur when nothing changed.

Accepted changes are immediately available between sessions but are not durable
until the idle flush completes. Cutting power before that can lose the latest
changes, including a deletion; the UI shows pending state and has a refresh
control. Old records are erased after successful commits, but this is not a
forensic secure-erasure guarantee. Flash remains unencrypted.

Persistent data and snapshot buffers use about 12.5 KiB PSRAM, plus a 2 KiB recent
excerpt. JSON buffers and connection setup are bounded transient PSRAM buffers;
facts serialize one at a time to limit internal-heap peaks. No new task stack is
reserved. The component retains the 2048-byte stack-frame compiler guard.

## Verification

`tools/test-memory.ps1` compiles the real store with mocked partition/RTOS APIs.
It tests bounds/UTF-8, duplicates, corrections, disabled mode, reboot recall,
torn payload/header writes, power loss before retirement, removal of old text,
atomic full-capacity eviction, stale target rejection and fresh-id behavior,
changes during a save and cleanup after allocation failure. Voice host tests
cover the three tools and dynamic setup escaping. Browser tests cover memory
controls, confirmation, safe text rendering and overflow at 1280/390/320 pixels.

Flash timing, audible continuity, actual Gemini fact selection and recall still
require a board test. Try stating a preference, end the session, call again and
ask about it; then wait for idle save, reboot and repeat. Also delete it and
verify after reopening/rebooting. Check that existing wallpaper and Wi-Fi remain.
