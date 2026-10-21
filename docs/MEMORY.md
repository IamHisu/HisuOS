# Hisu Memory

This is an independent HisuOS implementation, not the MimiClaw agent loop. Gemini
Live selects facts through bounded device tools. An optional post-session Gemini
text extraction now supplements these tools; no filesystem, vector database,
agent loop or additional background task is added.

## Two Lifetimes

- Long-term: at most 24 user facts, each at most 256 UTF-8 bytes. Automatic fact
  selection is initially enabled, as requested, and can be disabled in web
  Setting > Device. The model is instructed to select stable stated preferences,
  names, nicknames, basic relationships, habits and projects, not every question
  or inferred personal details.
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

Automatic memory is standing permission, not a requirement to say "remember"
or confirm each fact. Live may save at most three clear, stable user-stated facts
per turn. Both Live and post-session extraction include names, nicknames, basic
relationships to the user and preferences. Third-party scope is names/nicknames
and the stated relationship only, not contact details or other private data.
Ambiguous spelling, identity, relationship or whether an old fact is being replaced
must be clarified in Live before writing; background extraction skips ambiguity.
A nickname is separate from a real name. Clear changes to the same person and
attribute/relationship update its existing id; unchanged facts do not need a write.
New people/additional preferences do not imply discarding older ones. Old summaries
are fenced against concurrent corrections. These are semantic model instructions,
not a local keyword parser or a guarantee every fact will be extracted correctly.
The existing automatic-memory-off switch is still respected; no user data is
prepopulated by the firmware or its example tests.

`remember(fact, replace_id?)` creates a fact or replaces the same fact by its
existing id. Exact duplicates are deduplicated. Invalid UTF-8, controls, oversized
facts and writes while automatic memory is disabled are rejected. At capacity,
a plain add returns `memory_full` and a structured next action instead of a
generic allocation failure. Gemini uses `list_memory()` to inspect all 24 facts,
prefers merging/correcting the same topic, and can automatically replace a less
useful or obsolete fact by calling `remember(fact, evict_id, expected_fact)`.
`replace_id` and `evict_id` are mutually exclusive.

Merge-first policy: before evicting at capacity, Live must inspect ALL facts via
`list_memory`, enrich an existing same-person/topic fact with `replace_id`, or
use `remember(fact, merge_ids, memory_revision)` to combine 2-3 related slots
losslessly. Use the exact `revision` from the full `list_memory` result; any
intervening memory change rejects the merge. Compact ids/revision keep long,
escaped source facts from overflowing the existing tool input slot. Merge mode
is exclusive with replace/evict fields; firmware rejects duplicates/missing ids
and stale revisions, then compacts the cache atomically under its mutex.
The merged fact has a fresh id, freeing 1-2 slots without a delete-then-add gap.
It stays within 256 UTF-8 bytes and is persisted by the existing idle A/B flush.
Only when no lossless merge fits may the model select a lower-value eviction;
less useful additions can be skipped instead. Semantic relevance and preservation
of useful details are model responsibilities, not provable by firmware string checks.
Additional nicknames preserve the person's existing names. Background extraction
can enrich the same fact, but still cannot delete other slots or evict at capacity.

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

## Post-Session Extraction

Closing a session queues extraction only if actual Chat user transcription exists.
A separate 2048-byte PSRAM buffer contains user text only, not assistant replies,
Reader stories or interpreter output. Microphone-turn boundaries delimit excerpts;
streamed chunks of one utterance remain contiguous. This is bounded volatile data,
not a complete transcript, and is never saved to flash.

The existing live_send worker makes one bounded text request when the device is
idle, Internet is ONLINE, the Setting hotspot is inactive, and battery reporting
is valid with either external power or at least 20% battery. Power is checked
throughout; unplugging below 20%, a new wake/turn, a due reminder, music or output
cancels/defer the work. Cancellation is cooperative: an individual SDK DNS/TLS/HTTP
step may take up to its configured timeout (1 second); no zero-latency guarantee.
Offline wake feedback can play during cancellation. Flash maintenance is finished
before this HTTP call, and accepted facts are only marked dirty in RAM for the
normal gated idle flush later.

Default text model: `gemini-3.5-flash-lite`, configurable under Hi Su voice /
`HISU_MEMORY_SUMMARY_MODEL`. It uses ONLY the primary Chat key, no automatic key,
model or paid-tier fallback. It does not enable billing. API quota/charges depend
on the configured Google project; availability/free access is not guaranteed.
Requests have no tools, maxOutputTokens=1024, JSON Schema output, an 8192-byte
response ceiling, depth limits before recursive parsing, and a 10-second overall
deadline. TLS is released before proposal parsing. Logs contain only result code,
HTTP status, accepted count, cancellation, request count and stack watermark.

There are at most 12 API attempts per boot and at least 10 minutes between normal
attempts. HTTP errors impose a one-hour cooldown; a failed completed job is dropped,
not retried automatically. Cancellation retains pending data for a later idle
opportunity after 30 seconds, still within the attempt cap. Preconditions/missing
key do not rotate credentials or consume an API attempt. A reboot clears pending
data and these in-RAM counters; this is not a persistent daily quota limiter.

The model proposes at most three stable facts, prioritizing user corrections,
names, nicknames, basic relationships, preferred address, preferences, habits and
projects. Known facts and user text are
explicitly untrusted data. Each proposal needs a verbatim 8-256-byte evidence span
from user text, valid fact UTF-8 <=256 bytes, and only the supported fields. Existing
facts can be corrected only with the same id and exact expected old text. Validation
is atomic: one invalid proposal rejects the batch. Exact duplicates are skipped;
semantic deduplication and sensitive-data exclusion remain model instructions, not
a deterministic privacy guarantee. Facts can be reviewed/deleted in Setting > Device.

At 24 facts, background extraction skips new additions; it does NOT evict unrelated
facts. Corrections still work. The existing Live remember/evict_id workflow remains
available for semantic eviction during a conversation. No prompt/tool self-editing.

Turning automatic memory off prevents collection and transmission. Forget, clear,
clear recent, disabling/re-enabling memory and eviction clear queued source data
and invalidate in-flight results. Late chunks of that user turn are suppressed
until a new microphone turn. Every new input turn, source change or memory edit
also invalidates stale snapshots before commit. Deletion cannot retract requests
already sent to Google or erase an already-open Live remote context.

Sources: [Google model documentation](https://ai.google.dev/gemini-api/docs/models/gemini-3.5-flash-lite),
[structured JSON output](https://ai.google.dev/gemini-api/docs/generate-content/structured-output).

## Storage and Flash Ownership

### Optional Private Provisioning

If `.private/hisu_private_memory.json` exists at CMake configure time, it is
embedded as a local provisioning package. `.private/` is excluded from Git;
the built firmware, ELF and generated assembly STILL contain its plaintext data.
Do not distribute those artifacts. Removing the JSON requires reconfigure and
rebuild to remove it from future firmware. Deleting a fact cannot erase data
embedded in an already-flashed application; use a new build without the package
when that is required. Flash encryption is not enabled by this feature.

The JSON has only `version` (positive monotonic integer) and `facts` (1-24 valid
strings of at most 256 UTF-8 bytes). On startup, before voice/UI tasks begin,
an atomic RAM merge preserves existing facts, deduplicates exact matches and
refuses insufficient capacity without evicting anything or partially importing.
Automatic-memory-off is respected; failures log an error code, never fact text.
The normal A/B flush commits the complete merged data and its provisioning
version together. Interrupted saves recover either the prior record or the full
new record, never a committed marker for an incomplete import.

Header `reserved[0]` now holds the last provisioned version, protected by the
existing header CRC. Record/payload sizes, offsets and version 1 remain unchanged;
old records use zero. A seed of the same or older version is never reapplied,
including after forget/clear and reboot. Later explicit provisioning requires a
higher version. It adds exact nonduplicates only, not semantic replacements;
normal AI tools manage corrections. Removing the file does not delete saved facts.
Older firmware can read the records but does not preserve this marker when it
writes; remove private provisioning before a downgrade/upgrade cycle to prevent
unintended reprovisioning. Erasing the whole storage partition also removes the
marker. Keep private records out of logs, screenshots and shared binaries.

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
excerpt and 2 KiB user-only extraction buffer. Extraction also temporarily stages
one roughly 6.25 KiB fact table for atomic validation. JSON/request/response and
connection setup buffers are bounded transient allocations;
facts serialize one at a time to limit internal-heap peaks. No new task stack is
reserved. The component retains the 2048-byte stack-frame compiler guard.

## Verification

`tools/test-memory.ps1` compiles the real store with mocked partition/RTOS APIs.
It tests bounds/UTF-8, duplicates, corrections, disabled mode, reboot recall,
torn payload/header writes, power loss before retirement, removal of old text,
atomic full-capacity eviction, stale target rejection and fresh-id behavior,
changes during a save and cleanup after allocation failure. It also tests extracted
fact evidence, all-or-nothing proposals, full capacity, corrections, disabled mode,
stale snapshots, deletion and suppression of late source fragments. `tools/test-alert.ps1`
also runs the real summary HTTP client with mocked transport: battery 20%/charging,
idle/online/setup gates, cancellation, quota cooldown, bounded response/timeout,
TLS cleanup and per-boot request cap. No real key or network call is used. Voice host tests
cover the three tools and dynamic setup escaping. Browser tests cover memory
controls, confirmation, safe text rendering and overflow at 1280/390/320 pixels.

Flash timing, audible continuity, actual Gemini fact selection and recall still
require a board test. Try stating a preference, end the session, call again and
ask about it; then wait for idle save, reboot and repeat. Also delete it and
verify after reopening/rebooting. Check that existing wallpaper and Wi-Fi remain.
