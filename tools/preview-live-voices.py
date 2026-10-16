"""Audition Gemini Live voices without changing firmware or applying effects."""
import argparse
import base64
import io
import json
import os
from pathlib import Path
import re
import sys
import time
import wave

import websocket

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / ".cache/voice-preview-live"
URL = "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent"
VOICES = ("Kore", "Leda", "Aoede", "Zephyr", "Puck", "Charon", "Despina", "Erinome", "Autonoe")
TEXT = "Ừ, mình nghe nè. Hôm nay bạn thế nào? Muốn kể chuyện hay nghe nhạc một chút?"
STYLE = "Speak Vietnamese clearly and naturally in the configured voice. No imitation, robot effects, singing or beeps. Read only the sentence requested by the user."
MAX_PCM = 48000 * 20


def board_model():
    source = (ROOT / "components/hisu_voice/src/hisu_voice_live_setup.h").read_text(encoding="utf-8")
    match = re.search(r'model\\":\\"(models/[^"\\]+)', source)
    if not match:
        raise ValueError("Cannot read firmware Live model")
    return match.group(1)


def generate(key, model, voice, *, text=TEXT, style=STYLE, max_pcm=MAX_PCM):
    connection = websocket.create_connection(URL, timeout=15, redirect_limit=0,
        header={"x-goog-api-key": key}, suppress_origin=True)
    try:
        connection.send(json.dumps({"setup": {"model": model,
            "generationConfig": {"responseModalities": ["AUDIO"], "speechConfig": {
                "voiceConfig": {"prebuiltVoiceConfig": {"voiceName": voice}}}},
            "systemInstruction": {"parts": [{"text": style}]},
            "outputAudioTranscription": {}}}))
        deadline = time.monotonic() + 60
        ready = False
        pcm = bytearray()
        transcript = []
        while time.monotonic() < deadline:
            connection.settimeout(min(15, max(0.1, deadline - time.monotonic())))
            raw = connection.recv()
            if not raw:
                raise ValueError("Live connection closed before completion")
            message = json.loads(raw)
            if "error" in message:
                code = message["error"].get("code", 0)
                raise ValueError(f"Live API rejected request (code {code if isinstance(code, int) else 0}); no retry")
            if "setupComplete" in message:
                if ready:
                    raise ValueError("Duplicate Live setup")
                ready = True
                connection.send(json.dumps({"clientContent": {"turns": [{"role": "user",
                    "parts": [{"text": text}]}], "turnComplete": True}}))
            content = message.get("serverContent", {})
            if content.get("interrupted"):
                raise ValueError("Live audition interrupted")
            for part in content.get("modelTurn", {}).get("parts", []):
                inline = part.get("inlineData")
                if inline:
                    if not ready or inline.get("mimeType") != "audio/pcm;rate=24000":
                        raise ValueError("Unexpected Live audio format")
                    chunk = base64.b64decode(inline["data"], validate=True)
                    if len(chunk) % 2 or len(pcm) + len(chunk) > max_pcm:
                        raise ValueError("Invalid or oversized Live PCM")
                    pcm.extend(chunk)
            text = content.get("outputTranscription", {}).get("text")
            if isinstance(text, str):
                transcript.append(text)
            if content.get("turnComplete"):
                if not ready or len(pcm) < 24000:
                    raise ValueError("Missing or incomplete Live audio")
                return bytes(pcm), "".join(transcript)
        raise ValueError("Live audition timed out")
    finally:
        connection.close()


def publish(path, data):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--voices", nargs="+", choices=VOICES, default=list(VOICES[:6]))
    parser.add_argument("--reuse-only", action="store_true")
    args = parser.parse_args()
    model = board_model()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    last_request = None
    for voice in dict.fromkeys(args.voices):
        path = OUTPUT / f"{voice}.wav"
        metadata = OUTPUT / f"{voice}.json"
        expected = {"api": "Gemini Live WebSocket", "model": model, "voice": voice,
            "text": TEXT, "style": STYLE, "effects": False}
        if path.exists() and metadata.exists() and json.loads(metadata.read_text(encoding="utf-8")).get("request") == expected:
            with wave.open(str(path), "rb") as audio:
                if audio.getparams()[:3] != (1, 2, 24000):
                    raise ValueError("Invalid cached Live WAV")
                pcm = audio.readframes(audio.getnframes())
        else:
            if args.reuse_only:
                raise ValueError(f"Missing cached Live {voice}; no request made")
            key = os.environ.get("GEMINI_DEV_API_KEY")
            if not key:
                raise ValueError("Set GEMINI_DEV_API_KEY; never pass keys as arguments")
            if last_request is not None:
                time.sleep(max(0, 22 - (time.monotonic() - last_request)))
            last_request = time.monotonic()
            print(f"Generating Live {voice} ({model})...", flush=True)
            pcm, transcript = generate(key, model, voice)
            output = io.BytesIO()
            with wave.open(output, "wb") as audio:
                audio.setparams((1, 2, 24000, 0, "NONE", "not compressed"))
                audio.writeframes(pcm)
            publish(path, output.getvalue())
            publish(metadata, json.dumps({"request": expected, "transcript": transcript},
                ensure_ascii=False, indent=2).encode("utf-8"))
        if not 24000 <= len(pcm) <= MAX_PCM or len(pcm) % 2:
            raise ValueError("Invalid Live sample length")
        print(f"{voice}: {len(pcm)/48000:.2f} seconds; PCM unchanged", flush=True)
    print("Live auditions ready; no firmware, EQ, pitch or offline-clip changes.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        # Network exceptions can contain response headers/bodies; do not print them.
        safe = str(error) if isinstance(error, ValueError) and not isinstance(error, json.JSONDecodeError) else type(error).__name__
        print(f"Live audition stopped: {safe}", file=sys.stderr)
        sys.exit(1)
