"""Generate short Leda Live spoken feedback, or pack verified cached WAVs offline."""
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import sys
import time
import wave
import zlib
import argparse

ROOT = Path(__file__).resolve().parents[1]
SLOT = 0x3c000

def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack-only", action="store_true")
    args = parser.parse_args()
    battery = load("battery", "prepare-battery-audio.py")
    live = load("live", "preview-live-voices.py")
    source = ROOT / "components/hisu_alert/src/hisu_feedback_phrases.inc"
    phrases = [json.loads(line.removeprefix("HISU_FEEDBACK_PHRASE(")[:-1])
               for line in source.read_text(encoding="utf-8").splitlines()]
    if len(phrases) != 4:
        raise ValueError("Expected four feedback phrases")
    output = ROOT / ".cache/feedback-audio"
    output.mkdir(parents=True, exist_ok=True)
    model = live.board_model()
    slots = []
    last = None
    for index, phrase in enumerate(phrases):
        wav = output / f"leda-{index + 1}.wav"
        meta = wav.with_suffix(".json")
        expected = {"model": model, "voice": "Leda", "api": "Gemini Live", "phrase": phrase,
                    "style": "southern-vietnamese-live-v1"}
        pcm = None
        if wav.exists() and meta.exists() and json.loads(meta.read_text(encoding="utf-8")) == expected:
            with wave.open(str(wav), "rb") as audio:
                if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate(), audio.getcomptype()) == (1, 2, 24000, "NONE"):
                    pcm = battery.validate_pcm(audio.readframes(audio.getnframes()))
        if pcm is None:
            if args.pack_only:
                raise ValueError(f"Missing valid clip {index + 1}")
            key = os.environ.get("GEMINI_DEV_API_KEY") or os.environ.get("GEMINI_API_KEY")
            if not key:
                raise ValueError("Set GEMINI_DEV_API_KEY; never pass credentials as arguments")
            if last is not None:
                time.sleep(max(0, 22 - (time.monotonic() - last)))
            print(f"Generating spoken Leda feedback {index + 1}/4", flush=True)
            last = time.monotonic()
            pcm, transcript = live.generate(key, model, "Leda",
                text="Đọc nguyên văn câu sau, không trả lời hay thêm lời nào: " + phrase,
                style="Read only the supplied Vietnamese device notification, verbatim, naturally, warmly and briefly. No music, beeps or effects. " + battery.SOUTHERN_STYLE,
                max_pcm=SLOT - 4096)
            battery.validate_pcm(pcm)
            buffer = io.BytesIO()
            with wave.open(buffer, "wb") as audio:
                audio.setnchannels(1); audio.setsampwidth(2); audio.setframerate(24000); audio.writeframes(pcm)
            battery.atomic_write(wav, buffer.getvalue())
            battery.atomic_write(meta, json.dumps(expected, ensure_ascii=False).encode("utf-8"))
            battery.atomic_write(wav.with_suffix(".transcript.txt"), transcript.encode("utf-8"))
        if len(pcm) > SLOT - 4096:
            raise ValueError("Feedback too long")
        header = [0x31434648, 1, index, zlib.crc32(b"Leda:vi-southern-v1"), zlib.crc32(phrase.encode()),
                  len(pcm), zlib.crc32(pcm), 0] + [0] * 8
        header[7] = zlib.crc32(struct.pack("<16I", *header))
        slot = bytearray(b"\xff" * SLOT)
        slot[:64] = struct.pack("<16I", *header)
        slot[4096:4096 + len(pcm)] = pcm
        slots.append(slot)
        print(f"Clip {index + 1}: {len(pcm) / 48000:.2f}s", flush=True)
    battery.atomic_write(output / "feedback_audio.bin", b"".join(slots))
    print("Spoken feedback ready; reconfigure/build then normal flash includes the new storage tail.")

if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Feedback preparation failed: {error}" if isinstance(error, (ValueError, RuntimeError))
              else f"Feedback preparation failed: {type(error).__name__}", file=sys.stderr)
        sys.exit(1)
