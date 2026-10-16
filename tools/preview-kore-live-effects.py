"""PC-only audition: cached Kore Live, strong EQ, 1.25x tempo and octave-up pitch."""
import hashlib
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import wave

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".cache/voice-preview-deps"))
import numpy as np
from pedalboard import time_stretch

spec = importlib.util.spec_from_file_location("preview", ROOT / "tools/preview-leda-voice.py")
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)
RATE = 24000
SPEED = 1.25
SEMITONES = 12


def transform(samples, semitones=SEMITONES):
    output = time_stretch(np.asarray(samples, dtype=np.float32), RATE,
        stretch_factor=SPEED, pitch_shift_in_semitones=semitones,
        preserve_formants=True, high_quality=True)
    if output.ndim == 2 and output.shape[0] == 1:
        output = output[0]
    if output.ndim != 1 or not np.isfinite(output).all():
        raise ValueError("Invalid mono stretch output")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--electronic", action="store_true", help="Normal pitch with light electronic modulation")
    args = parser.parse_args()
    semitones = 0 if args.electronic else SEMITONES
    tone = 0.1 * np.sin(2 * np.pi * 220 * np.arange(RATE * 2) / RATE)
    shifted = transform(tone, semitones)
    center = shifted[RATE//4:-RATE//4]
    frequencies = np.fft.rfftfreq(len(center), 1/RATE)
    measured = float(frequencies[np.argmax(abs(np.fft.rfft(center * np.hanning(len(center)))) )])
    target = 220 * 2**(semitones/12)
    if abs(measured - target) > 5 or abs(len(shifted) - len(tone)/SPEED) > RATE*0.02:
        raise ValueError("Pitch/tempo calibration failed")
    source = ROOT / ".cache/voice-preview-live/Kore.wav"
    original = source.read_bytes()
    with wave.open(str(source), "rb") as audio:
        if audio.getparams()[:3] != (1, 2, RATE):
            raise ValueError("Expected original 24 kHz mono Kore Live")
        dry = np.frombuffer(audio.readframes(audio.getnframes()), dtype="<i2").astype(np.float64)/32768
    raised = transform(dry, semitones)
    effect = preview.PreviewEffect(strong_eq=True)
    wet = effect.process(raised)
    if args.electronic:
        # A small dry/wet amplitude modulation adds texture without shifting pitch.
        phase = 2*np.pi*72*np.arange(len(wet))/RATE
        wet *= 0.88 + 0.12*np.cos(phase)
    wet = preview.condition(wet)
    if not np.isfinite(wet).all() or abs(len(wet) - len(dry)/SPEED) > RATE*0.02:
        raise ValueError("Invalid effect output")
    path = source.with_name("Kore-eq-strong-speed125-electronic-light.wav" if args.electronic else
        "Kore-eq-strong-speed125-pitch200.wav")
    preview.write_wav(path, wet)
    with wave.open(str(path), "rb") as audio:
        if audio.getparams()[:3] != (1, 2, RATE) or audio.getnframes() != len(wet):
            raise ValueError("Output WAV verification failed")
    if source.read_bytes() != original:
        raise ValueError("Original Live audio changed")
    report = {"source_sha256": hashlib.sha256(original).hexdigest(), "source_api": "Gemini Live",
        "tempo": SPEED, "pitch_semitones": semitones, "pitch_factor": 2**(semitones/12),
        "modulation_hz": 72 if args.electronic else 0, "modulation_depth": 0.12 if args.electronic else 0,
        "preserve_formants": True, "eq": {"highpass_hz": 300, "lowpass_hz": 4800,
        "presence_hz": 1900, "presence_q": 1.1, "presence_mix": 1.5},
        "source_seconds": len(dry)/RATE, "output_seconds": len(wet)/RATE,
        "peak_dbfs": float(20*np.log10(np.max(abs(wet)))), "calibration_hz": measured,
        "firmware_changed": False, "offline_clips_changed": False}
    preview.tts.atomic_write(path.with_suffix(".json"), json.dumps(report, indent=2).encode())
    print(f"Ready: {report['source_seconds']:.2f}s -> {report['output_seconds']:.2f}s; "
          f"pitch calibration={measured:.1f}Hz; peak={report['peak_dbfs']:.1f}dBFS. Original preserved; no API calls.")


if __name__ == "__main__":
    main()
