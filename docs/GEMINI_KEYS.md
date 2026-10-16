# Gemini Key Routing

Setting > Thiet bi has four independent password fields:

| Slot | Purpose | Status/save endpoint |
| --- | --- | --- |
| Chat chinh | Ordinary Gemini Live conversation | `/gemini-key` |
| Chat du phong | One fallback for Chat only | `/gemini-backup-key` |
| Reader | Bedtime narration and short reminder TTS | `/gemini-reader-key` |
| Translate | Explicit interpreter mode | `/gemini-translate-key` |

Existing `gemini_key` in NVS remains the Chat primary. No migration, credential
deletion, project ID, flash-layout change or audio regeneration is required.
Reader and Translate never borrow Chat keys or act as additional Chat backups.
Initial voice requests to enter these modes still go through Chat. Existing
offline Erinome clips need no key or Internet.

## Chat Fallback

Chat starts with the primary. A confirmed authentication or quota failure can
switch to the single configured backup: HTTP 401/429, API UNAUTHENTICATED or
RESOURCE_EXHAUSTED (including corresponding numeric codes), or API_KEY_INVALID.
WebSocket close 1008/1011 is eligible only with a recognized authentication/quota
reason prefix; an unknown close is not assumed to be a key failure. Generic 400,
403, 5xx, socket, Wi-Fi and TLS errors do not select another key.

The old connection is destroyed before the replacement opens. Old microphone
frames, audio and tool calls are invalidated. Hi Su asks the user to repeat any
unfinished request; commands are not replayed automatically, and side-effect
tools remain blocked until a new real microphone capture. This is not seamless
conversation resumption. A failing backup ends the attempt, with no rotation.

For five minutes after a primary failure, new Chat sessions start with the backup.
After that, the next new session tries the primary again; an active backup session
is not interrupted. Reboot or a saved key change clears the cooldown. With only
a backup configured, Chat starts there directly.

## Role Changes

Successful `start_sleep_story` changes to Reader while preserving the authorized
bedtime deadline and passing the topic as untrusted text data. A deliberate wake
cancels the timer and reconnects Chat. Successful `start_interpreter` changes to
Translate and preserves the selected language pair; stopping it reconnects Chat.
These changes briefly reconnect, invalidate old output and do not silently borrow
credentials when a role key is missing or fails. Reader/Translate have no backup.

## Security And Verification

Each endpoint requires the existing Setting session guard. GET exposes only
`configured`; POST accepts one `key` field, with an empty string deleting that
slot. The page clears password inputs after saving and on logout. Logs identify
the slot numerically, never the key or raw server close reason. NVS is still
unencrypted, and the local Setting transport remains HTTP.

Keys do not enable billing or guarantee free-tier availability. Keys in the same
Google project share project quota. No automatic project creation or pool exists.

Host tests cover independent NVS persistence, legacy compatibility, per-field web
save/delete, bounded auth/quota fallback, stale epochs, missing role keys, mode
handoffs, and blocking replayed actions. HTTP/TLS/Gemini/audio are mocked; these
checks do not establish real API access, quota availability or device latency.
