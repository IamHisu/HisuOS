"""PC-only Leda audition. Never changes firmware, flash arguments or offline slots."""
import argparse
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import time
import wave

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".cache/voice-preview-deps"))
import numpy as np
from scipy import signal

spec = importlib.util.spec_from_file_location("battery_tts", ROOT / "tools/prepare-battery-audio.py")
tts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tts)
RATE = 24000
OUTPUT = ROOT / ".cache/voice-preview-leda"
SCRIPTS = [
    "Ừ, mình nghe nè. Bạn muốn kể chuyện hay nghe nhạc?",
    "Mình hơi hết năng lượng sống rồi đó. Cắm sạc lẹ nha.",
]
STYLE = ("Sound youthful, friendly and lightly playful, with natural Vietnamese intonation. "
         "Keep every Vietnamese tone and consonant clear. No exaggerated squeaking, stutter or foreign accent. ")


class PreviewEffect:
    """Causal filtering with persistent state/oscillator across PCM chunks."""
    def __init__(self, robotic=False, stronger_eq=False, strong_eq=False):
        highpass = 300 if strong_eq else 180 if stronger_eq else 160
        lowpass = 4800 if strong_eq else 5900 if stronger_eq else 6200
        self.sos = np.vstack([signal.butter(2,highpass,btype="highpass",fs=RATE,output="sos"),
                              signal.butter(2,lowpass,btype="lowpass",fs=RATE,output="sos")])
        self.presence_mix = 1.5 if strong_eq else 0.35 if stronger_eq else 0.20
        self.peak = signal.tf2sos(*signal.iirpeak(1900,1.1 if strong_eq else 0.9,fs=RATE))
        self.state = np.zeros((len(self.sos),2))
        self.peak_state = np.zeros((len(self.peak),2))
        self.position = 0
        self.robotic = robotic

    def process(self, samples):
        if len(samples)==0:
            return np.empty(0,dtype=np.float64)
        out,self.state = signal.sosfilt(self.sos,samples,zi=self.state)
        presence,self.peak_state = signal.sosfilt(self.peak,out,zi=self.peak_state)
        out = out + self.presence_mix * presence
        if self.robotic:
            phase = 2*np.pi*72*(np.arange(len(out))+self.position)/RATE
            out *= 0.70 + 0.30*np.cos(phase)
        self.position += len(out)
        return out


def condition(samples):
    out=np.asarray(samples,dtype=np.float64).copy()
    out-=np.mean(out)
    # Match RMS for comparison; preserve at least 2 dB of peak headroom.
    rms=float(np.sqrt(np.mean(out*out)))
    peak=float(np.max(np.abs(out)))
    if not np.isfinite(out).all() or rms<1e-6:
        raise ValueError("Silent or non-finite audition")
    out *= min(0.10/rms,0.79/peak)
    fade=min(120,len(out)//2)
    out[:fade] *= np.linspace(0,1,fade)
    out[-fade:] *= np.linspace(1,0,fade)
    return out


def write_wav(path, samples):
    if np.max(np.abs(samples))>=1 or not np.isfinite(samples).all():
        raise ValueError("Unsafe audio level")
    data=np.rint(samples*32767).astype("<i2").tobytes()
    buffer=io.BytesIO()
    with wave.open(buffer,"wb") as audio:
        audio.setparams((1,2,RATE,0,"NONE","not compressed"))
        audio.writeframes(data)
    tts.atomic_write(path,buffer.getvalue())


def self_test():
    rng=np.random.default_rng(17)
    samples=rng.normal(0,0.1,24000)
    for robotic,stronger_eq,strong_eq in ((False,False,False),(True,False,False),(False,True,False),(False,False,True)):
        whole=PreviewEffect(robotic,stronger_eq,strong_eq).process(samples)
        effect=PreviewEffect(robotic,stronger_eq,strong_eq)
        chunks=np.concatenate([effect.process(samples[i:i+256]) for i in range(0,len(samples),256)])
        np.testing.assert_allclose(whole,chunks,atol=1e-12,rtol=1e-12)
        assert len(chunks)==len(samples) and np.isfinite(chunks).all()
        assert np.max(np.abs(condition(chunks)))<0.8
        np.testing.assert_array_equal(PreviewEffect(robotic,stronger_eq,strong_eq).process(np.zeros(256)),np.zeros(256))
    print("Preview DSP tests passed: chunk continuity, silence, finite output, length and headroom.")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reuse-only",action="store_true",help="Use cached Leda speech only; no API requests")
    parser.add_argument("--test",action="store_true",help="Run DSP tests only; no API requests")
    args=parser.parse_args()
    self_test()
    if args.test:
        return
    OUTPUT.mkdir(parents=True,exist_ok=True)
    previews={name:[] for name in ("leda-original","leda-eq","leda-eq-plus","leda-eq-strong","leda-robot-light")}
    report={"voice":"Leda","model":tts.MODEL,"rate":RATE,"prototype_only":True,
            "firmware_changed":False,"pitch_shift":False,"scripts":SCRIPTS,
            "eq":{"highpass_hz":160,"lowpass_hz":6200,"presence_hz":1900,"presence_mix":0.2},
            "eq_plus":{"highpass_hz":180,"lowpass_hz":5900,"presence_hz":1900,"presence_mix":0.35},
            "eq_strong":{"highpass_hz":300,"lowpass_hz":4800,"presence_hz":1900,"presence_q":1.1,"presence_mix":1.5},
            "robot":{"modulation_hz":72,"depth":0.3},"samples":[]}
    last_request=None
    for index,text in enumerate(SCRIPTS):
        cache=OUTPUT / f"source-{index+1}.pcm"
        metadata=OUTPUT / f"source-{index+1}.json"
        expected={"voice":"Leda","model":tts.MODEL,"text":text,"style":STYLE}
        if cache.exists() and metadata.exists() and json.loads(metadata.read_text(encoding="utf-8"))==expected:
            pcm=tts.validate_pcm(cache.read_bytes())
        else:
            if args.reuse_only:
                raise ValueError("Missing matching Leda source; no API request was made")
            key=os.environ.get("GEMINI_DEV_API_KEY") or os.environ.get("GEMINI_API_KEY")
            if not key:
                raise ValueError("Set GEMINI_DEV_API_KEY; do not pass keys in arguments")
            if last_request is not None:
                time.sleep(max(0,22-(time.monotonic()-last_request)))
            last_request=time.monotonic()
            print(f"Generating Leda audition {index+1}/{len(SCRIPTS)}...",flush=True)
            pcm=tts.generate(text,key,voice="Leda",style=STYLE)
            tts.atomic_write(cache,pcm)
            tts.atomic_write(metadata,json.dumps(expected,ensure_ascii=False).encode())
        dry=np.frombuffer(pcm,dtype="<i2").astype(np.float64)/32768
        variants={"leda-original":condition(dry)}
        for name,robotic,stronger_eq,strong_eq in (("leda-eq",False,False,False),("leda-eq-plus",False,True,False),
                                                  ("leda-eq-strong",False,False,True),("leda-robot-light",True,False,False)):
            effect=PreviewEffect(robotic,stronger_eq,strong_eq)
            wet=np.concatenate([effect.process(dry[i:i+256]) for i in range(0,len(dry),256)])
            variants[name]=condition(wet)
        for name,samples in variants.items():
            write_wav(OUTPUT / f"{name}-{index+1}.wav",samples)
            previews[name].extend([samples,np.zeros(int(RATE*0.65))])
            report["samples"].append({"file":f"{name}-{index+1}.wav","seconds":len(samples)/RATE,
                                      "peak_dbfs":float(20*np.log10(np.max(np.abs(samples))))})
    for name,clips in previews.items():
        write_wav(OUTPUT / f"{name}.wav",np.concatenate(clips[:-1]))
    tts.atomic_write(OUTPUT / "preview.json",json.dumps(report,ensure_ascii=False,indent=2).encode())
    print("Leda audition ready. Firmware and existing Puck recordings are unchanged.")
    print("This is a PC DSP prototype, not Espressif library output or measured board performance.")


if __name__=="__main__":
    try:
        main()
    except (ValueError,RuntimeError,OSError) as error:
        print(f"Audition failed: {error}" if isinstance(error,(ValueError,RuntimeError))
              else "Audition failed: local file/connection error",file=sys.stderr)
        sys.exit(1)
