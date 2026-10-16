# Leda Voice Audition (Not Activated)

Historical audition notes: the selected production voice is now original Kore,
without effects. Old audition audio and Puck caches were deleted at the user's
request. The scripts below can recreate previews, but are not firmware effects.

## Other Original Voices

`tools/preview-original-voices.py` auditions Zephyr, Aoede, Kore, Achernar and
Laomedeia by default; `--voices` selects other prebuilt names. All voices read
the same short Vietnamese text with the same neutral delivery instruction.
Sources, metadata and WAV files live under `.cache/voice-preview-originals/`.
WAV payloads are verified byte-for-byte against the generated PCM: no EQ,
normalization, pitch shift, modulation or time stretch is applied. Perceived
volume can naturally differ between voices. `--reuse-only` validates the cached
samples without API calls. A quota/API error stops the batch without retry or
changing models. Existing firmware and offline recordings remain unchanged.

Hi Su is the assistant's name. Leda/Puck are voice selections, not character names.
The user requires an audition before changing the installed voice or offline
recordings. No firmware voice selection, cached Puck clip or flash image is
changed by this prototype. Generated previews are outside the audio-flash path.

## Reproduce

`tools/preview-leda-voice.py` uses NumPy and SciPy (installed locally under
`.cache/voice-preview-deps/`). Use the bundled desktop Python runtime, not the
ESP-IDF Python environment unless those packages are available there. The
Gemini key is read only from `GEMINI_DEV_API_KEY`/`GEMINI_API_KEY`, sent in an
HTTPS header and never persisted. The existing Gemini 2.5 Flash Preview TTS
client is reused with an explicit Leda override. Kore is now its default.

Two short Vietnamese scripts cover conversation and a low-battery warning.
Sources are cached with exact model/voice/text/style metadata; no automatic
retries or paid-model fallback. `--reuse-only` processes cached sources with
no network requests; `--test` runs only deterministic DSP checks.

Outputs under `.cache/voice-preview-leda/`:

- `leda-original.wav`: Leda source, with level matching and edge fades only.
- `leda-eq.wav`: high-pass 160 Hz, low-pass 6200 Hz, a 1900 Hz presence band mixed
  at 0.20. These are causal second-order filters, not a zero-phase offline EQ.
- `leda-eq-plus.wav`: a modest stronger-EQ revision requested after the first
  audition: high-pass 180 Hz, low-pass 5900 Hz, presence mix 0.35. No modulation
  or pitch shift. The original EQ audition remains available for comparison.
- `leda-eq-strong.wav`: a deliberately pronounced EQ audition: high-pass 300 Hz,
  low-pass 4800 Hz, 1900 Hz presence mix 1.5 and Q 1.1. This narrows the spectrum
  and emphasizes the midrange; output levels are still matched with headroom.
  No modulation, pitch change or firmware activation.
- `leda-robot-light.wav`: the same EQ plus 72 Hz amplitude modulation at 30%
  depth, preserving a dry component to retain Vietnamese intelligibility.
- Individual `*-1.wav`/`*-2.wav` files and `preview.json` for parameters/metrics.

All outputs are 24 kHz mono PCM16, duration preserved. No pitch shifting,
resampling, time stretching or inserted syllable gaps. They are level-matched
subject to peak headroom, not perceptually loudness-normalized. Streaming tests
compare 256-sample chunks against whole-file output and check silence, finite
samples, length and headroom. Listening approval is still required: tests do
not prove intelligibility, perceived cuteness or similarity to Cozmo.

## Espressif Reference and Later Integration

Espressif's [ESP-Audio-Effects overview](https://docs.espressif.com/projects/esp-adf/en/latest/basic-components/effects/esp-audio-effects.html)
describes standalone/chained modules usable from ESP-IDF. Its
[EQ module](https://github.com/espressif/esp-adf-libs/blob/master/esp_audio_effects/docs/README_EQ.md)
supports high/low-pass, shelves and peak filters. Its
[Sonic module](https://github.com/espressif/esp-adf-libs/blob/master/esp_audio_effects/docs/README_SONIC.md)
provides tempo/pitch processing. EQ alone changes the spectrum, not fundamental
pitch. The modulation in this audition is an additional prototype effect, not
an Espressif EQ feature. This is SciPy output, not output from Espressif binaries.
No runtime CPU/RAM/latency claim is made based on the PC audition.

After approval, evaluate a pinned ESP-Audio-Effects component, not a wholesale
ESP-ADF migration. Process spoken 24 kHz mono PCM before stereo/volume mixing,
reuse one stateful chain and preserve state across chunks; reset only at proper
utterance/interruption boundaries. Apply the same profile to Gemini speech,
reminder TTS and dry offline Leda clips. Leave music unchanged. Do not bake the
effect into stored clips and apply it again at runtime. Update voice fingerprints
when switching the offline cache so Puck recordings cannot silently pass as Leda.
Re-generate offline clips only after approval; the old cached sources remain
available for rollback. Actual Live delivery and the small board speaker may
sound different from TTS played on a PC, so final acceptance needs board tests.
