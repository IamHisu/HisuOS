import base64
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import urllib.error
from unittest.mock import patch
import wave
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("battery_audio", ROOT / "tools/prepare-battery-audio.py")
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)
PCM = struct.pack("<h", 500) * 12000


def response(data=PCM, mime="audio/L16;codec=pcm;rate=24000"):
    return {"candidates": [{"finishReason": "STOP", "content": {"parts": [
        {"inlineData": {"mimeType": mime, "data": base64.b64encode(data).decode()}}]}}]}


class HostAudioTests(unittest.TestCase):
    def test_shared_phrases_and_voice(self):
        self.assertEqual(len(host.phrases()), 5)
        config = host.request_body(host.phrases()[0])["generationConfig"]
        self.assertEqual(config["speechConfig"]["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"], "Kore")
        preview = host.request_body("Hi Su",voice="Leda",style="Clear Vietnamese. ")
        self.assertEqual(preview["generationConfig"]["speechConfig"]["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"], "Leda")
        self.assertEqual(host.request_body("Hi Su")["generationConfig"]["speechConfig"]["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"], "Kore")

    def test_decode_and_bounds(self):
        self.assertEqual(host.decode_audio(response()), PCM)
        for data in [b"", b"\0" * 24000, PCM[:-1], PCM * 17]:
            with self.assertRaises(ValueError):
                host.decode_audio(response(data))
        with self.assertRaises(ValueError):
            host.decode_audio(response(mime="audio/mpeg"))
        blocked = response()
        blocked["candidates"][0]["finishReason"] = "SAFETY"
        with self.assertRaises(ValueError):
            host.decode_audio(blocked)

    def test_wav_format(self):
        output = io.BytesIO()
        with wave.open(output, "wb") as audio:
            audio.setparams((1, 2, 24000, 0, "NONE", "not compressed"))
            audio.writeframes(PCM)
        self.assertEqual(host.decode_audio(response(output.getvalue(), "audio/wav")), PCM)

    def test_http_failure_no_key_or_retry(self):
        payload = json.dumps({"error": {"status": "INVALID_ARGUMENT", "message": "private-key",
            "details": [{"reason": "API_KEY_INVALID"}]}}).encode()
        error = urllib.error.HTTPError(host.URL, 400, "bad", {}, io.BytesIO(payload))
        with patch.object(host.urllib.request, "build_opener") as opener:
            opener.return_value.open.side_effect = error
            with self.assertRaisesRegex(RuntimeError, "API_KEY_INVALID") as caught:
                host.generate("test", "private-key")
            self.assertNotIn("private-key", str(caught.exception))
            self.assertEqual(opener.return_value.open.call_count, 1)

    def test_header_and_firmware_compatibility(self):
        clips = [host.pack_slot(i, phrase, PCM) for i, phrase in enumerate(host.phrases())]
        for index, clip in enumerate(clips):
            header = list(struct.unpack("<16I", clip[:64]))
            checksum = header[7]
            header[7] = 0
            self.assertEqual(zlib.crc32(struct.pack("<16I", *header)), checksum)
            self.assertEqual(header[2], index)
            self.assertEqual(header[6], zlib.crc32(PCM))
            self.assertEqual(clip[4096:4096 + len(PCM)], PCM)
            self.assertEqual(len(clip), 0x60000)
        executable = ROOT / "build/alert-tests/alert-test.exe"
        self.assertTrue(executable.exists(), "Run tools/test-alert.ps1 first")
        with tempfile.TemporaryDirectory(dir=ROOT / "build/alert-tests") as directory:
            image = Path(directory) / "test.bin"
            image.write_bytes(b"".join(clips))
            subprocess.run([str(executable), str(image)], check=True, capture_output=True)

    def test_flash_region_registration(self):
        cmake = Path("C:/Espressif/tools/cmake/3.30.2/bin/cmake.exe")
        self.assertTrue(cmake.exists())
        with tempfile.TemporaryDirectory(dir=ROOT / "build/alert-tests") as directory:
            project = Path(directory)
            cache = project / ".cache/battery-audio"
            cache.mkdir(parents=True)
            image = cache / "battery_audio.bin"
            image.write_bytes(b"\xff" * (5 * host.SLOT_BYTES))
            script = project / "test.cmake"
            script.write_text(f'''set(PROJECT_DIR "{project.as_posix()}")
function(partition_table_get_partition_info variable selection field)
    if(field STREQUAL "offset")
        set(${{variable}} 0x820000 PARENT_SCOPE)
    else()
        set(${{variable}} 0x4e0000 PARENT_SCOPE)
    endif()
endfunction()
function(esptool_py_flash_target_image target name offset image)
    if(NOT target STREQUAL "flash" OR NOT name STREQUAL "battery_audio" OR NOT offset STREQUAL "0xa26000")
        message(FATAL_ERROR "Unexpected flash region")
    endif()
endfunction()
include("{(ROOT / 'cmake/battery_audio.cmake').as_posix()}")
''', encoding="ascii")
            subprocess.run([str(cmake), "-P", str(script)], check=True, capture_output=True)
            image.write_bytes(b"bad")
            result = subprocess.run([str(cmake), "-P", str(script)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
