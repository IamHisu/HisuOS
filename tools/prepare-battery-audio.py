"""Generate Leda Live clips on the PC; pack only the existing battery audio slots."""
import argparse
import array
import base64
import binascii
import io
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys
import time
import urllib.error
import urllib.request
import wave
import zlib

ROOT = Path(__file__).resolve().parents[1]
SLOT_BYTES = 0x60000
PCM_MAX = 384000
MODEL = "gemini-2.5-flash-preview-tts"
URL = f"https://generativelanguage.googleapis.com/v1beta/models/{MODEL}:generateContent"
SOUTHERN_STYLE = ("Use natural Southern Vietnamese pronunciation, intonation and rhythm throughout the sentence, "
                  "not only for the letter r. Clearly distinguish initial r from gi and d. "
                  "Do not exaggerate the accent or change the supplied words. ")


def phrases():
    source = ROOT / "components/hisu_alert/src/hisu_alert_phrases.inc"
    result = []
    for line in source.read_text(encoding="utf-8").splitlines():
        if not line.startswith("HISU_ALERT_PHRASE(") or not line.endswith(")"):
            raise ValueError("Invalid shared phrase file")
        result.append(json.loads(line[len("HISU_ALERT_PHRASE("):-1]))
    if len(result) != 5:
        raise ValueError("Expected four battery phrases and one reminder phrase")
    return result


def validate_pcm(pcm):
    if not 24000 <= len(pcm) <= PCM_MAX or len(pcm) % 2:
        raise ValueError("Audio must be 0.5-8 seconds of 24 kHz mono PCM16")
    samples = array.array("h", pcm)
    if sys.byteorder != "little":
        samples.byteswap()
    if max(map(abs, samples)) < 32:
        raise ValueError("Silent audio rejected")
    return pcm


def decode_audio(response):
    candidates = response.get("candidates", [])
    if not candidates or candidates[0].get("finishReason") != "STOP":
        raise ValueError("Incomplete or blocked speech response")
    chunks = []
    for part in candidates[0].get("content", {}).get("parts", []):
        blob = part.get("inlineData")
        if not blob:
            continue
        mime = blob.get("mimeType", "").lower()
        raw = base64.b64decode(blob["data"], validate=True)
        if mime in ("audio/wav", "audio/x-wav"):
            with wave.open(io.BytesIO(raw), "rb") as audio:
                if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate(), audio.getcomptype()) != (1, 2, 24000, "NONE"):
                    raise ValueError("Unexpected WAV format")
                expected = audio.getnframes() * 2
                raw = audio.readframes(audio.getnframes())
                if len(raw) != expected:
                    raise ValueError("Truncated WAV")
        elif mime not in ("audio/l16;codec=pcm;rate=24000", "audio/l16;rate=24000", "audio/pcm;rate=24000"):
            raise ValueError("Unexpected audio MIME")
        chunks.append(raw)
    return validate_pcm(b"".join(chunks))


def request_body(phrase, voice="Leda", style=None):
    prompt = ("Read only the following Vietnamese sentence, clearly and naturally, in under eight seconds. "
              + (style or "Use your natural voice without imitating a robot or adding vocal effects. " + SOUTHERN_STYLE) +
              "Do not add words, directions, singing or beeps. Sentence: " + phrase)
    return {"contents": [{"parts": [{"text": prompt}]}], "generationConfig": {
        "responseModalities": ["AUDIO"], "speechConfig": {"voiceConfig": {
            "prebuiltVoiceConfig": {"voiceName": voice}}}}}


def generate(phrase, key, *, voice="Leda", style=None):
    request = urllib.request.Request(URL, data=json.dumps(request_body(phrase,voice,style)).encode(),
                                     headers={"x-goog-api-key": key, "Content-Type": "application/json"})
    # Never forward the credential to a redirect destination or log API bodies.
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            return None
    try:
        with urllib.request.build_opener(NoRedirect).open(request, timeout=90) as response:
            data = response.read(600001)
            if len(data) > 600000:
                raise ValueError("Speech response too large")
            return decode_audio(json.loads(data))
    except urllib.error.HTTPError as error:
        reason = ""
        try:
            detail = json.loads(error.read(16384)).get("error", {})
            codes = [detail.get("status", "")] + [item.get("reason", "") for item in detail.get("details", [])]
            reason = ", ".join(code for code in codes if code and all(c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789" for c in code))
        except (ValueError, TypeError, AttributeError):
            pass
        error.close()
        raise RuntimeError(f"Gemini HTTP {error.code} ({reason}); stopped, no retry or paid model fallback") from None
    except urllib.error.URLError:
        raise RuntimeError("Cannot reach Gemini; check the PC Internet connection") from None


def pack_slot(index, phrase, pcm):
    validate_pcm(pcm)
    header = [0x31505348, 1, index, zlib.crc32(b"Leda"), zlib.crc32(phrase.encode()),
              len(pcm), zlib.crc32(pcm), 0] + [0] * 8
    header[7] = zlib.crc32(struct.pack("<16I", *header))
    slot = bytearray(b"\xff" * SLOT_BYTES)
    slot[:64] = struct.pack("<16I", *header)
    slot[4096:4096 + len(pcm)] = pcm
    return bytes(slot)


def atomic_write(path, data):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack-only", action="store_true", help="Validate and pack cached clips without any API call")
    args = parser.parse_args()
    output = ROOT / ".cache/battery-audio"
    output.mkdir(parents=True, exist_ok=True)
    image = output / "battery_audio.bin"
    scripts = phrases()
    spec = importlib.util.spec_from_file_location("live_preview", ROOT / "tools/preview-live-voices.py")
    live = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(live)
    live_model = live.board_model()
    clips = []
    last_request = None
    for index, phrase in enumerate(scripts):
        wav = output / f"leda-{index + 1}.wav"
        metadata = output / f"leda-{index + 1}.json"
        expected = {"model": live_model, "api": "Gemini Live", "voice": "Leda", "phrase": phrase, "style": "southern-vietnamese-live-v1"}
        pcm = None
        if wav.exists() and metadata.exists():
            if json.loads(metadata.read_text(encoding="utf-8")) == expected:
                with wave.open(str(wav), "rb") as audio:
                    if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate(), audio.getcomptype()) == (1, 2, 24000, "NONE"):
                        pcm = validate_pcm(audio.readframes(audio.getnframes()))
        if pcm is None:
            if args.pack_only:
                raise ValueError(f"Missing valid clip {index + 1}; no image published")
            key = os.environ.get("GEMINI_DEV_API_KEY") or os.environ.get("GEMINI_API_KEY")
            if not key:
                raise ValueError("Set GEMINI_DEV_API_KEY or GEMINI_API_KEY; do not put keys in command arguments")
            if last_request is not None:
                time.sleep(max(0, 22 - (time.monotonic() - last_request)))
            print(f"Generating Leda clip {index + 1}/{len(scripts)}...", flush=True)
            last_request = time.monotonic()
            pcm, transcript = live.generate(key, live_model, "Leda",
                text="Đọc nguyên văn câu sau, không trả lời hay thêm lời nào: " + phrase,
                style="You are reading a prerecorded device notification, not chatting. Read exactly the Vietnamese sentence supplied by the user, verbatim, naturally and clearly in under eight seconds. No commentary, effects or music. " + SOUTHERN_STYLE,
                max_pcm=PCM_MAX)
            pcm = validate_pcm(pcm)
            print("Spoken notification: " + json.dumps(transcript, ensure_ascii=True), flush=True)
            atomic_write(output / f"leda-{index + 1}.transcript.txt", transcript.encode("utf-8"))
            buffer = io.BytesIO()
            with wave.open(buffer, "wb") as audio:
                audio.setnchannels(1)
                audio.setsampwidth(2)
                audio.setframerate(24000)
                audio.writeframes(pcm)
            atomic_write(wav, buffer.getvalue())
            atomic_write(metadata, json.dumps(expected, ensure_ascii=False).encode())
        clips.append(pack_slot(index, phrase, pcm))
        print(f"Clip {index + 1}: {len(pcm) / 48000:.2f} seconds", flush=True)
    atomic_write(image, b"".join(clips))
    print("Five clips ready. Run idf.py reconfigure build before idf.py flash.")
    print("Only the battery slots are added to flash; wallpaper and memory are untouched.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        # Never print raw HTTP responses, request objects or credential values.
        print(f"Battery audio preparation failed: {type(error).__name__}" if not isinstance(error, (ValueError, RuntimeError))
              else f"Battery audio preparation failed: {error}", file=sys.stderr)
        sys.exit(1)
