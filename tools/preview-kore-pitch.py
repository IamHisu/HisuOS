"""PC-only Kore pitch/EQ audition, using cached audio and no Gemini requests."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import wave

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT / ".cache/voice-preview-deps"))
import numpy as np
from pedalboard import PitchShift
from scipy import signal

spec=importlib.util.spec_from_file_location("voice_preview",ROOT / "tools/preview-leda-voice.py")
preview=importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)
RATE=24000
SEMITONES=1.5


def shift(samples):
    padding=RATE//4
    guarded=np.pad(np.asarray(samples,dtype=np.float32),(padding,padding))
    shifted=PitchShift(semitones=SEMITONES)(guarded,RATE)
    if shifted.shape!=guarded.shape or not np.isfinite(shifted).all():
        raise ValueError("Invalid pitch-shifter output")
    return shifted[padding:padding+len(samples)].astype(np.float64)


def envelope(samples):
    frames=len(samples)//240
    return np.sqrt(np.mean(samples[:frames*240].reshape(frames,240)**2,axis=1))


def main():
    # Confirm pitch movement independently of the speech and EQ.
    tone=0.1*np.sin(2*np.pi*220*np.arange(RATE*2)/RATE)
    test=shift(tone)[RATE//2:-RATE//2]
    frequencies=np.fft.rfftfreq(len(test),1/RATE)
    measured=float(frequencies[np.argmax(np.abs(np.fft.rfft(test*np.hanning(len(test)))))])
    target=220*2**(SEMITONES/12)
    if abs(measured-target)/target>0.03:
        raise ValueError("Pitch calibration outside tolerance")
    source=ROOT / ".cache/voice-preview-originals/Kore.wav"
    original=source.read_bytes()
    with wave.open(str(source),"rb") as audio:
        if audio.getparams()[:3]!=(1,2,RATE):
            raise ValueError("Expected original 24 kHz mono PCM16 Kore")
        dry=np.frombuffer(audio.readframes(audio.getnframes()),dtype="<i2").astype(np.float64)/32768
    raised=shift(dry)
    effect=preview.PreviewEffect(stronger_eq=True)
    eq=np.concatenate([effect.process(raised[i:i+256]) for i in range(0,len(raised),256)])
    before,after=envelope(dry),envelope(raised)
    correlation=signal.correlate(after-after.mean(),before-before.mean(),mode="full")
    lag=int(signal.correlation_lags(len(after),len(before))[np.argmax(correlation)])
    if abs(lag)>8:
        raise ValueError("Speech envelope shifted more than 80 ms")
    output=ROOT / ".cache/voice-preview-kore"
    output.mkdir(parents=True,exist_ok=True)
    for name,samples in (("kore-reference",dry),("kore-pitch-light",raised),("kore-pitch-eq",eq)):
        conditioned=preview.condition(samples)
        if len(conditioned)!=len(dry) or np.max(np.abs(conditioned))>=0.8:
            raise ValueError("Duration or headroom check failed")
        path=output / f"{name}.wav"
        preview.write_wav(path,conditioned)
        with wave.open(str(path),"rb") as audio:
            if audio.getparams()[:3]!=(1,2,RATE) or audio.getnframes()!=len(dry):
                raise ValueError("WAV verification failed")
        print(f"{name}: {len(conditioned)/RATE:.3f} seconds",flush=True)
    if source.read_bytes()!=original:
        raise ValueError("Original Kore recording changed")
    report={"voice":"Kore","source_sha256":hashlib.sha256(original).hexdigest(),
            "pitch_semitones":SEMITONES,"tempo_changed":False,"rate":RATE,
            "eq":{"highpass_hz":180,"lowpass_hz":5900,"presence_hz":1900,"presence_mix":0.35},
            "modulation":False,"seconds":len(dry)/RATE,"envelope_lag_ms":lag*10,
            "calibration_hz":{"expected":target,"measured":measured},
            "firmware_changed":False,"offline_recordings_changed":False,
            "processor":"Pedalboard PitchShift (Rubber Band), then SciPy causal EQ; PC audition only"}
    preview.tts.atomic_write(output / "preview.json",json.dumps(report,indent=2).encode())
    print("Pitch/EQ, sample length, envelope timing, headroom and source-preservation checks passed. No API calls.")


if __name__=="__main__":
    main()
