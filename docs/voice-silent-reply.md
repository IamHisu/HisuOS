# Silent Replies and Expression Tools

A device report showed a successful set_expression call after the wake phrase,
but no `First audio`; the user saw the replying screen switch to listening without
hearing a greeting. The report does not contain raw server messages, so it does
not distinguish a model-generated silent turn from output lost at a premature
completion boundary. Both paths now have explicit handling.

Tool calls are counted for the current epoch/generation. A completion received
while tools are pending does not finish the voice response. Successful tool
responses retire the count immediately before sending. New PCM clears a deferred
boundary. A completion received while a tool is outstanding is never replayed as
final completion when its result is sent: it predates the model's continuation.
The response stays open for a fresh tool call, audio or completion after the
result. Retiring before sending also lets a fast fresh response complete while
the blocking send is returning, without losing its boundary.

`interactionStatus: IN_PROGRESS` is not treated as final completion. This follows
the [Live protocol status definition](https://ai.google.dev/api/live): more output
may follow while processing continues. This does not change the configured model
to Extended Thinking.

A final response containing no PCM does not emit playback DONE. Instead, it queues
one spoken-continuation request on the transport worker. The model is asked to
finish the previous answer naturally in Vietnamese, without repeating actions or
calling tools. Tool requests during this recovery are rejected locally as well.
If audio arrives before the continuation is sent, that audio wins and the queued
request is skipped. Duplicate boundaries cannot spend the retry twice. Another
silent completion after recovery logs the cause and closes the conversation
quietly to the idle screen, rather than showing an error subtitle or falsely
returning to listening. Existing response deadlines bound a stalled recovery.
Setup readiness also has no placeholder subtitle; captions are reserved for
the actual response. Other connection and device errors retain their messages.

Interruption before a queued continuation must not count an already-completed
server turn as obsolete. Open/close/new-input/interruption reset the per-reply
state. Semantic errors from stale generations cannot close a newer turn.
End-conversation, successful music handoff and confirmed reboot still close the
session explicitly; they do not force an extra spoken continuation.

The prompt also asks Gemini to speak for a name-only greeting without calling
set_expression; automatic speaking eyes already smile. Other replies may still
use model-selected expressions. Greetings remain model-generated, not canned.

Tests compile the actual transport source with a mocked server/RTOS. They cover
tool completion before/after acknowledgment, early/late PCM, intermediate status,
duplicate boundaries, stale recovery, no repeated tool actions, exhausted retry,
queue/allocation/write failure and prompt preservation. They do not establish
real Gemini behavior or audible output on hardware.
Regression cases include search -> pre-result audio/completion -> result -> play,
and a fresh response arriving inside the result-send call. The device log showed
successful gzip search and speaker startup but no accepted play request; the log
alone does not establish the exact server-message ordering behind that stall.
