# Hoi thoai theo phien

Firmware su dung MultiNet cho cau goi, Groq Whisper cho STT va Gemini cho tra loi/TTS. Chua tich hop MimiClaw.

## Hanh vi

- Goi "Hi Su" de mo phien khi co Wi-Fi va Groq API key. Gemini API key can cho hoi dap va tao giong noi; lenh den pin van chay rieng.
- Khi mo phien, man hinh bao dang nghe. VAD xac nhan ba frame co giong noi, kem 200ms pre-roll de giu dau cau. Moi cau toi da 12 giay; 1,1 giay im lang ket thuc cau.
- Giu toi da 6 cap hoi/dap hoan tat trong RAM. Phien moi khong dung lai ngu canh phien cu; khong luu lich su xuong flash.
- Tra loi xong tu nghe tiep. Doi 300ms tranh duoi tieng loa, roi bat dau timeout 15 giay. Timeout khong chay trong khi thu cau, cho API hoac phat loa.
- Nhan BOOT ngan hoac goi "Hi Su" khi dang xu ly/phat loa: huy luot cu, giu ngu canh cac luot hoan tat va nghe cau moi. Luot dang thu co the huy bang BOOT.
- Giu BOOT 800ms: dong phien. Noi "thoi nhe", "tam biet" hoac "ket thuc hoi thoai" (co/khong dau theo implementation) la lenh ket thuc, khong can hoi LLM.
- Ngoai phien, cac nut van theo menu hien tai. Trong phien, BOOT uu tien dieu khien hoi thoai.
- Loi API hien tren man hinh; cua so nghe tiep van co timeout. Can goi lai sau khi phien dong.

## Streaming va huy luot

Text su dung `streamGenerateContent?alt=sse`. Moi chunk cap nhat phu de; bo qua thinking. Chi TTS khi da nhan text hoan tat. TTS su dung `POST /v1beta/interactions` voi `stream: true`, `store: false`, `audio/l16`, 24kHz mono; phat tung audio delta thay vi doi toan bo am thanh.

STT van gui WAV sau khi noi xong. Chua co pipeline STT streaming, TTS song song voi text generation, Gemini Live, AEC hoac noi chen tu do. Vi vay khong cam ket do tre tuong duong Xiaozhi.

Moi luot co generation rieng. Huy luot vo hieu hoa generation, danh thuc HTTP bang socket shutdown va ngan phat audio/ghi ngu canh/cuoi luot cu. HTTP worker so huu client va giai phong tai nguyen; UI khong cleanup client cua worker. Khi socket chua ket noi (DNS/TLS), worker phai doi lenh ket noi tra ve/timeout truoc khi xu ly yeu cau moi. Loa kiem tra generation giua cac block PCM; huy se bo buffer DMA.

SSE co buffer gioi han (16KiB/event text, 256KiB/event audio). Su kien bi cat, audio sai dinh dang, phan hoi vuot 255 byte hoac khong hoan tat deu bao loi. Chunk audio lon hon gioi han can duoc danh gia lai theo heap thuc te.

API tham khao: [Text generation](https://ai.google.dev/api/generate-content), [TTS streaming](https://ai.google.dev/gemini-api/docs/speech-generation), [Interactions event names](https://ai.google.dev/gemini-api/docs/interactions-breaking-changes-may-2026).

## Kiem thu

Chay `tools/test-voice.ps1` de kiem tra timeout, huy/ket qua cu, ngu canh phien moi, cau ket thuc, SSE phan manh/CRLF/multiline/tran buffer va JSON Gemini.

Can thu tren board:

1. Goi "Hi Su", hoi mot cau, sau do hoi tiep co tham chieu cau truoc ma khong goi lai.
2. Khong noi 15 giay sau khi loa dung; xac nhan ve man hinh cho va can goi lai.
3. Ngat bang BOOT khi STT, khi dang cho Gemini, khi TTS dang tao va khi loa dang phat; hoi cau moi, xac nhan khong co audio/phu de cu quay lai.
4. Goi "Hi Su" khi loa phat o nhieu muc am luong. Do nhan dung va tu ngat nham do nghe chinh minh; chua co AEC nen day la gioi han can do thuc te.
5. Giu BOOT, noi "thoi nhe", mat Wi-Fi, API loi va tat loa; xac nhan van thoat/nghe lai dung.
6. Do heap noi/PSRAM, stack va chay nhieu luot lien tiep. Log `Transcription`, `First reply text`, `First speech audio` cho biet do tre tu khi worker nhan cau; chua tinh thoi gian nguoi dung noi va cho VAD.

Build/host tests khong thay the kiem thu tren board va API that.
