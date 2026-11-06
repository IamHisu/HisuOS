# PCM jitter buffer và benchmark

## Khảo sát và phạm vi

Điểm xuất phát: HisuOS commit `9395217`. `hisu_voice_live.c` đã có ba task:
WSS callback đóng khung/đưa JSON vào receive queue, `live_decode` parse/base64,
`live_play` là consumer I2S duy nhất (priority 6, cao hơn decode/transport 4).
Không có lệnh I2S trong callback WSS. Tuy nhiên decode trước đây malloc PCM mỗi
inlineData và có thể chờ queue tới 3 giây, kéo dài hơn nếu playback còn tiến triển.
Voice prebuffer là 1200 ms, rebuffer 3/6/12 giây, story tối thiểu 8 giây.

Thay đổi này giữ cấu trúc task, queue marker/phụ đề, music và offline audio.
PCM Gemini 24000 Hz, mono, signed 16-bit little endian đi qua ringbuffer riêng.
Không đổi ESP-GMF, codec hoặc đường mic 16 kHz.

## Hoạt động và bộ nhớ

`components/hisu_voice/src/hisu_audio_jitter.h` là module C độc lập, header-only,
không phụ thuộc ESP-IDF, không cấp phát và không tự khóa. Caller phải đồng bộ mọi
thao tác. Live dùng `s_mux`; mỗi lần copy tối đa 512 byte trong critical section.
Chỉ task decode ghi; chỉ playback đọc hoặc discard. Không gọi I2S khi đang khóa.

Ringbuffer được cấp phát một lần trong PSRAM khi init: 1,536 KiB, giữ nguyên
ngân sách PCM tối đa cũ (khoảng 32,77 giây). Đây là capacity, không phải độ trễ
khởi đầu. Decode scratch cố định là 98,308 byte. Chi phí cố định mới khoảng
1,594 MiB; cần đo PSRAM cùng AFE/UI/WSS trên N16R8. Init thiếu bộ nhớ trả lỗi và
rollback. Trước đây PCM chỉ chiếm bộ nhớ theo backlog thực tế.

Prebuffer và rebuffer mục tiêu 180 ms = 8640 byte. Chỉnh
`HISU_JITTER_PREBUFFER_MS` trong khoảng 160–200 nếu benchmark cho thấy cần thiết.
Playback đợi theo lượng PCM, không chỉ đợi một khoảng thời gian cố định. Short
reply được phát ngay khi producer báo hoàn tất. Giữ hai escape hatch để tránh
kẹt: queue gần đầy hoặc sau 1360 ms chờ ở cấu hình 180 ms thì cho phát phần đã
có. Vì vậy không bảo đảm mọi lần bắt đầu đều có đủ 180 ms.

Queue chứa descriptor PCM cùng marker/phụ đề theo thứ tự cũ; descriptor PCM
không sở hữu con trỏ heap. Khi queue/ring đầy, producer trả lỗi ngay và kết thúc
turn với thông báo quá tải; không chờ loa, không âm thầm bỏ mẫu giữa câu. Nếu
enqueue thất bại, phần ring vừa ghi được rollback. PCM đã dequeue vẫn chiếm ring
cho tới khi đọc/discard, nên capacity tính cả gói đang phát.

Đường copy PCM và cấp PCM cho I2S không malloc/free theo gói. WSS framing,
cJSON, text cues, UI/callback và các đường music/offline hiện có vẫn có cấp phát;
đây không phải cam kết toàn bộ ứng dụng không dùng heap trong hội thoại.

## Cancel và underrun

Cancel/Hi Su/đóng phiên dùng epoch/generation hiện có. Playback bỏ descriptor cũ
và giải phóng phần PCM tương ứng theo FIFO, không reset ring khi producer còn
đang ghi. Vì vậy PCM thế hệ mới không bị xóa nhầm. Khi cancel giữa một gói, phần
chưa đọc bị discard, lần mở I2S sau dùng cơ chế preload silence có sẵn. Kiểm tra
cancel mỗi tối đa 256 mẫu (10,67 ms PCM); `speaker_write` hiện có timeout 100 ms.
Đây chưa phải giới hạn độ trễ ngắt đã đo trên board.

`serverContent.interrupted` của turn hiện tại chặn audio/caption và hoàn tất
playback. Interrupted thuộc obsolete turn chỉ retire fence, không hủy lượt mới.
Đường wake-word/ngắt cục bộ vẫn cần dùng API generation hiện có; chưa thêm AEC
hoặc ngắt lời tự do bằng VAD.

Khi queue rỗng 20 ms trong lúc đã phát và response còn pending, tăng một underrun
cho đợt thiếu dữ liệu, đóng speaker rồi đệm lại. Việc đóng speaker tránh phát
lại DMA cũ trong khoảng thiếu dữ liệu; vẫn có thể nghe khoảng im lặng/click,
chưa có PLC/crossfade. Counter là starvation phần mềm, không phải IRQ underrun
của phần cứng; chưa tính chính xác số mẫu thiếu do DMA còn dữ liệu.

## Counters và đọc log

`Voice buffer ready` ghi target, occupancy hiện tại và thời gian đợi.
`Voice queue starved` giữ thống kê tốc độ RX và max RX gap mỗi turn.
`Jitter:` cuối reply ghi occupancy/peak (ms PCM), underruns, overflows (ring hoặc
descriptor queue đầy),
rx_gaps (khoảng giữa hai PCM chunk lớn hơn 180 ms), max_gap và discarded bytes.
Các counter/peak của ring cộng dồn từ init; lấy hiệu trước/sau mỗi ca thử.
RX gap đo ở lúc decode/enqueue PCM, bao gồm cả chậm do scheduler/parse, không
phải thời gian packet tới tầng Wi-Fi. Baseline gap đầu mỗi generation được bỏ
qua để không tính thời gian người dùng suy nghĩ. Discard cũng bao gồm mute và
audio bị bỏ sau lỗi; không chỉ cancel.

## Chạy kiểm thử

```powershell
.\tools\test-audio-jitter.ps1
.\tools\test-voice.ps1
.\tools\build.ps1
```

Test độc lập chỉ cần MSVC x64 + Windows SDK; không cần IDF/cJSON. Có 20.000
thao tác kiểm tra FIFO/sample order, wrap, overflow, odd bytes, rollback,
discard, ngưỡng prebuffer, short reply và RX gaps. Suite voice có thêm test
allocation-free PCM, partial cancel, server interruption, obsolete fence,
queue-full rollback, ring-full và PCM mới sau discard; giữ các regression
music, lyrics, subtitles, WSS/control và session.

Kiểm tra lại ngày 04/11/2026 sau khi cài SDK mới:

- `tools/test-audio-jitter.ps1`: qua, gồm 20.000 thao tác FIFO.
- `tools/test-voice.ps1`: qua với `IDF_PATH=C:\esp\v6.1\esp-idf`, SDK RC4
  của IDF 6.1 và cJSON 1.7.19~2 do component manager cung cấp. Bản cJSON chép
  tạm cho host tests trước đây đã chuyển sang `.cache/host-test-dependencies/`
  để không cản kiểm tra integrity của component manager.
- `tools/build.ps1`: build đầy đủ thành công trên ESP-IDF 6.1.0, GNU 15.2.0,
  target ESP32-S3. Có bootloader, model partition và `build-idf61/HisuOS.bin`.
  App size `0x2f22c0` byte (khoảng 2,95 MiB), còn `0x10dd40` byte (26%) trong
  phân vùng app 4 MiB. Phần tích hợp jitter buffer không phát sinh lỗi biên dịch.
- Build có các cảnh báo Kconfig/private includes từ SDK và thiếu các bản ghi
  âm Leda offline/wake feedback trong `.cache/`; hai loại âm thanh cục bộ này
  chưa được đóng gói vào bản build. Không tạo audio bằng API hoặc flash thiết bị.

Host tests vẫn mock hardware/TLS/base64/API/FreeRTOS. Build thành công xác minh
biên dịch/liên kết, chưa xác minh ABI/runtime của thư viện speech prebuilt.
Chưa flash board, chưa đo latency, tiếng click, scheduling đa core, heap/PSRAM
hoặc Gemini thật.

## Benchmark trên ESP32-S3 N16R8

1. Cài IDF 6.1.x qua EIM, dùng config N16R8 trong sdkconfig.defaults. Build bản
   nền tại commit trên trong checkout riêng và bản mới với cùng toolchain/config.
   Chạy `tools/build.ps1 -RefreshConfig` khi chuyển SDK/config, sau đó build/size.
   Chỉ flash thiết bị thử khi đã kiểm tra GPIO và partition đúng board.
2. Mỗi bản chạy ít nhất 20 câu ngắn và 20 câu dài 30–60 giây, cùng model/voice,
   AP, RSSI, vị trí, âm lượng và nguồn. Thu serial log cùng ghi âm loa; ghi thời
   gian first RX, First audio, VOICE_START_PLAY, gaps, peak occupancy và counters.
   Tách độ trễ phản hồi AI khỏi độ trễ từ first RX tới playback.
3. Dùng AP/router thử có điều khiển delay/loss (hoặc relay WSS thử đã được cấu
   hình an toàn) để tạo burst gap 50/100/150/250/500 ms. TCP/WSS giữ thứ tự nên
   mất packet có thể thành stall dài do retransmit. So sánh delta underruns,
   thời lượng im lặng nghe được, p50/p95 startup, max gap và overflow. Không suy
   ra chất lượng chỉ từ counter phần mềm; đối chiếu bản ghi âm.
4. Ngắt Hi Su/BOOT ở lúc prebuffer, giữa packet đang phát, khi gần đầy, khi
   turnComplete tới và khi reconnect; lặp ít nhất 50 lần. Xác nhận không phát lại
   tiếng cũ/hiện caption cũ, không crash, không mất tiếng lượt mới. Đo từ thao tác
   ngắt tới mẫu âm cũ cuối cùng, gồm cả DMA/I2S timeout.
5. Kiểm tra short reply dưới 180 ms, mute, đổi volume, story, music/offline alert,
   mất mạng, resume và thiếu PSRAM. Chạy liên tục 30 phút, so free/largest internal,
   DMA và PSRAM từ log heap; theo dõi stack watermark và watchdog.

Biểu ghi mỗi ca: build SHA/IDF, RSSI, câu thử, số lượt, p50/p95 startup,
delta underruns/overflows/rx_gaps, max gap, peak occupancy, giây im lặng,
độ trễ cancel, min heap/largest block và nhận xét bản ghi âm. Chưa có số đo để
điền các mục này. 180 ms không thể bù nguồn gửi chậm hơn realtime kéo dài hoặc
stall nhiều giây; nếu logs cho thấy điều đó, cần xử lý throughput/nguồn gửi
hoặc chọn reserve cao hơn theo yêu cầu sản phẩm.
