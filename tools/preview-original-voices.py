"""Audition original Gemini TTS voices on the PC, without effects or firmware changes."""
import argparse
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import time
import wave

ROOT=Path(__file__).resolve().parents[1]
OUTPUT=ROOT / ".cache/voice-preview-originals"
spec=importlib.util.spec_from_file_location("battery_tts",ROOT / "tools/prepare-battery-audio.py")
tts=importlib.util.module_from_spec(spec)
spec.loader.exec_module(tts)
VOICES=("Zephyr","Puck","Charon","Kore","Fenrir","Leda","Orus","Aoede","Callirrhoe",
        "Autonoe","Enceladus","Iapetus","Umbriel","Algieba","Despina","Erinome","Algenib",
        "Rasalgethi","Laomedeia","Achernar","Alnilam","Schedar","Gacrux","Pulcherrima",
        "Achird","Zubenelgenubi","Vindemiatrix","Sadachbia","Sadaltager","Sulafat")
TEXT="Ừ, mình nghe nè. Bạn muốn kể chuyện hay nghe nhạc?"
STYLE="Use your natural voice with a clear, conversational Vietnamese delivery. Do not imitate a robot or another voice. "


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--voices",nargs="+",choices=VOICES,default=["Zephyr","Aoede","Kore","Achernar","Laomedeia"])
    parser.add_argument("--reuse-only",action="store_true",help="No API requests; validate existing samples only")
    args=parser.parse_args()
    OUTPUT.mkdir(parents=True,exist_ok=True)
    last_request=None
    for voice in dict.fromkeys(args.voices):
        cache=OUTPUT / f"{voice}.pcm"
        metadata=OUTPUT / f"{voice}.json"
        expected={"voice":voice,"model":tts.MODEL,"text":TEXT,"style":STYLE,"effects":False}
        if cache.exists() and metadata.exists() and json.loads(metadata.read_text(encoding="utf-8"))==expected:
            pcm=tts.validate_pcm(cache.read_bytes())
        else:
            if args.reuse_only:
                raise ValueError(f"Missing cached {voice}; no API call was made")
            key=os.environ.get("GEMINI_DEV_API_KEY") or os.environ.get("GEMINI_API_KEY")
            if not key:
                raise ValueError("Set GEMINI_DEV_API_KEY; never pass keys as arguments")
            if last_request is not None:
                time.sleep(max(0,22-(time.monotonic()-last_request)))
            last_request=time.monotonic()
            print(f"Generating original {voice}...",flush=True)
            pcm=tts.generate(TEXT,key,voice=voice,style=STYLE)
            tts.atomic_write(cache,pcm)
            tts.atomic_write(metadata,json.dumps(expected,ensure_ascii=False,indent=2).encode())
        output=io.BytesIO()
        with wave.open(output,"wb") as audio:
            audio.setparams((1,2,24000,0,"NONE","not compressed"))
            audio.writeframes(pcm)
        path=OUTPUT / f"{voice}.wav"
        tts.atomic_write(path,output.getvalue())
        with wave.open(str(path),"rb") as check:
            if check.getparams()[:3]!=(1,2,24000) or check.readframes(check.getnframes())!=pcm:
                raise ValueError("Audition WAV verification failed")
        print(f"{voice}: {len(pcm)/48000:.2f} seconds; PCM unchanged",flush=True)
    print("Auditions ready. No EQ, modulation, pitch/tempo changes or firmware activation.")


if __name__=="__main__":
    try:
        main()
    except (ValueError,RuntimeError,OSError) as error:
        print(f"Audition stopped: {error}" if isinstance(error,(ValueError,RuntimeError))
              else "Audition stopped: local file/connection error",file=sys.stderr)
        sys.exit(1)
