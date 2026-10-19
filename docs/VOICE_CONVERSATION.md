# Hội thoại Gemini Live

## Kết Thúc Tự Nhiên và Xác Nhận Nhạc

Gemini hiểu ngữ cảnh để gọi `end_conversation`: lời tạm biệt, yêu cầu dừng, hoặc
người dùng từ chối lời mời hỗ trợ thêm. Không thêm luật từ khóa cục bộ cho chữ
"không": trả lời phủ định một câu hỏi, từ chối bài nhạc hoặc tiếp tục giải thích
không tự động kết thúc phiên. Công cụ chỉ đánh dấu đóng sau câu trả lời. Sau khi
nhận kết quả, AI nói một câu kết ngắn, không hỏi tiếp. Luồng loa phát hết PCM và
chờ DMA rồi mới gửi `LIVE_END`, không chuyển qua `LIVE_DONE`/đang lắng nghe.
Ngắt sang lượt mới xóa dấu đóng; gói hoàn tất cũ không đóng được lượt mới.

Nhạc đi qua `search_music` → `offer_music` → câu hỏi tên bài + ca sĩ → lượt xác
nhận mới của người dùng → `play_music`. Kể cả yêu cầu ban đầu nêu đúng bài vẫn
phải chốt. Firmware giữ tối đa năm kết quả tìm kiếm trong khoảng 1,5 KiB PSRAM,
chỉ cho đề xuất id thuộc kết quả đó, và chặn phát cùng lượt đề xuất. Lượt đề xuất
phải hoàn tất phản hồi có audio; chỉ lượt người dùng ngay tiếp theo, có bản nhận
dạng không bị tràn, được phép xác nhận đúng id trong 60 giây. Đổi bài cần đề xuất
và xác nhận lại. Tìm lại, dừng nhạc, hết hạn hoặc mở phiên mới làm mất hiệu lực
xác nhận cũ. Không tự phát một bài khác nếu lỗi.

AI chịu trách nhiệm hiểu câu xác nhận/từ chối theo ngữ cảnh, không yêu cầu một
câu mật khẩu cố định. Chốt firmware ngăn tự xác nhận/cùng lượt/id sai, không phải
bộ phân loại ngữ nghĩa thứ hai và không chứng minh người dùng đã nghe được loa.
Kiểm thử mô phỏng bao gồm câu kết phát xong mới đóng, ngắt lượt, gói cũ, phát
chưa chốt, chưa hoàn tất đề xuất, id khác, quá hạn, thiếu/tràn nhận dạng và phiên
mới. Cần thử thêm hội thoại thật trên board để kiểm tra Gemini hiểu đúng ý.

Hisu dùng MultiNet cho câu gọi và Gemini Live (`gemini-3.8-live`) cho audio trực tiếp. Chỉ cần Gemini API key trong Setting. Groq key được giữ để tương thích cấu hình cũ, không dùng trong hội thoại Live.

## Luồng audio

Gọi “Hi Su” mở một kết nối WSS có xác minh certificate bundle. Chờ thông báo “Mình đang nghe...” sau `setupComplete`. Firmware giữ kết nối qua các lượt hỏi/đáp và đóng khi kết thúc phiên. Mic gửi PCM mono 16-bit 16kHz theo frame trong lúc thu; Gemini trả PCM mono 24kHz. Task phát loa riêng không chặn việc gửi mic hoặc nhận WebSocket. Không còn chuỗi WAV → Groq → Gemini text → TTS riêng.

Automatic activity detection trên server tắt. Board gửi `activityStart`/`activityEnd`: mở lượt cần VAD speech ≥200ms và mức trước AGC ≥−35dBFS; có 200ms pre-roll. Mỗi câu tối đa 12 giây, kết thúc sau 700ms im lặng. Khi không nói, không gửi audio nền lên server. Đây là ngưỡng ban đầu, giọng nhỏ/xa có thể bị bỏ sót. WebRTC noise suppression và VAD mode 3 vẫn hoạt động.

Ngữ cảnh nằm trong phiên Gemini đang kết nối; phiên mới bắt đầu ngữ cảnh mới. Phụ đề lấy từ output audio transcription, hiển thị tối đa 255 byte. Không log API key hoặc nội dung câu nói. Giữ BOOT 800ms hoặc nói “thôi nhé”/“tạm biệt” để đóng phiên (nhận câu kết thúc phụ thuộc transcript server). Sau khi loa phát xong chờ 300ms rồi nghe tiếp tối đa 60 giây; gọi một lần để hỏi đáp nhiều lượt, không gửi audio nền khi im lặng.

BOOT ngắn hoặc gọi “Hi Su” trong phiên hủy audio cũ bằng generation và nghe lại trên cùng kết nối Live, giữ ngữ cảnh. `activityStart` dùng `START_OF_ACTIVITY_INTERRUPTS` để hủy câu trả lời phía server. Khi ngắt giữa lúc thu, gửi kết thúc activity cũ trước khi mở activity mới. Các `turnComplete` của lượt bị hủy được bỏ qua, không kết thúc nhầm lượt mới; chờ ranh giới hủy tối đa 10 giây rồi báo lỗi nếu không nhận được. Không AEC và chưa có ngắt lời bằng câu nói bất kỳ, nên gọi khi loa phát cần thử trên board. Loa tắt vẫn nhận phụ đề; âm lượng áp dụng khi phát. Session resumption sau mất kết nối vẫn chưa có.

## Giới hạn và xử lý lỗi

Input queue chứa tối đa 256 frame 40ms trong PSRAM; không block task fetch. Message WebSocket tối đa 128KiB, hỗ trợ các event chia nhỏ và continuation frames. Output queue tối đa 96 items và 1536KiB PCM, cấp phát theo nhu cầu. Khi đầy, nhận audio/phụ đề/chốt lượt chờ theo tiến độ playback; chỉ lỗi nếu không có tiến độ trong 3 giây. Không bỏ PCM cũ. Setup timeout 15 giây, phản hồi timeout sau 60 giây không có audio mới. Kể chuyện bật nén ngữ cảnh và dùng session resumption sau `goAway`, tại cuối đoạn đã phát hết, cùng key và giữ nguyên giờ tự tắt. Thiếu handle hoặc mất kết nối trước khi nhận đủ đoạn vẫn dừng; Chat thường chưa tự phục hồi sau mất mạng. Xem [Bedtime Stories](BEDTIME_STORIES.md).

Live khai báo function tool `set_flashlight(enabled)` để bật/tắt đèn pin bằng giọng nói; tool response gửi lại trên cùng kết nối. Điều khiển menu/nút vẫn dùng được. Gemini key phải có quyền/quota cho model Live; compile không xác nhận API thật.

CPU 240MHz, I-cache 32KiB, D-cache 64KiB/line 64B, PSRAM 80MHz; mbedTLS dùng PSRAM để chừa RAM nội cho Wi-Fi/DMA. Mic/AFE core 0 (priority 5/6), fetch/MultiNet core 1 (priority 5). Worker Wi-Fi tắt modem sleep trong lúc kết nối Live và khôi phục khi phiên đóng, không đổi radio trực tiếp từ task audio. Sau setup có log RAM nội còn trống, mức thấp nhất, khối trống lớn nhất và PSRAM để đo trên board. Cấu hình cũ cần cập nhật hoặc chạy `tools/build.ps1 -RefreshConfig` rồi rà lại các tùy chỉnh riêng. CPU cao và chế độ Wi-Fi hiệu năng có thể tăng tiêu thụ pin.

## Kiểm thử trên board

1. Flash đầy đủ, Wi-Fi kết nối và có Gemini key. Gọi “Hi Su”: đợi setup hoàn tất, nói một câu. Log `LIVE: First audio: ... ms after activityEnd` đo thời gian từ gửi kết thúc câu tới audio đầu tiên.
2. Hỏi tiếp có nhắc câu trước; kiểm tra ngữ cảnh trong cùng kết nối.
3. Sau câu trả lời, im lặng: không có `Capture start`, không tự suy nghĩ/đáp, đóng sau khoảng 60 giây. Thử giọng nhỏ, bình thường và phòng có tiếng nền.
4. BOOT ngắn khi đang chờ/phát/thu: audio cũ dừng, nghe lại trong cùng kết nối. Hỏi tiếp về câu trước để kiểm tra ngữ cảnh, ngắt nhanh hai lần để kiểm tra ranh giới lượt. Giữ BOOT đóng ngay trạng thái phiên; kết nối có thể đợi thao tác TLS hiện tại/timeout trước khi giải phóng.
5. Mất Wi-Fi, key sai/quota hết, API lỗi, loa tắt: hiện lỗi/phụ đề đúng, không treo/reboot. Chạy 20 lượt, theo dõi heap và queue warnings.
6. Kiểm tra UI/Wi-Fi/GIF và cảnh báo AFE; đo độ trễ thực tế. Chưa cam kết nhanh bằng Xiaozhi hoặc mức thời gian cố định.

Nguồn: [Gemini Live protocol](https://ai.google.dev/api/live), [Live audio formats](https://ai.google.dev/gemini-api/docs/live-api), [model](https://ai.google.dev/gemini-api/docs/models/gemini-3.8-live).
Host tests: tools/test-live.ps1 -Clang <host LLVM clang> biên dịch parser C thật sang WASM rồi chạy bằng Node. Cần host LLVM có wasm32 + wasm-ld; compiler Espressif chỉ hỗ trợ chip nên không chạy bộ này. Test bao gồm event chia nhỏ, continuation, UTF-8, tràn và offset sai. Không thay thế kiểm thử WSS/API/audio trên board.

`tools/test-voice.ps1` chạy test quản lý lượt và mã Live transport C thật với SDK/queue/audio/API giả lập: giữ epoch khi ngắt, setup đang kết nối, bỏ phản hồi cũ, lỗi queue không gọi callback đồng bộ khi caller giữ khóa, và ngắt trong lúc đang xử lý phản hồi cuối. Không xác nhận model/quota của Gemini key hoặc chất lượng audio trên thiết bị.

WebSocket cần CONFIG_ESP_WS_CLIENT_SEPARATE_TX_LOCK=y để task nhận không giữ chung khóa với sender mic. Log Slow WSS send (>100ms) và pending cho biết mạng/khóa đang chặn gửi; queue tràn vẫn đóng phiên để tránh gửi audio thiếu.

## Reliability Checks Without Sound

Opt-in, session-only two-way translation is described in
[Interpreter Mode](INTERPRETER_MODE.md), including its control boundary and font limits.

Timed bedtime narration and its limitations are documented in
[Bedtime Stories](BEDTIME_STORIES.md). This uses Gemini Live, not a separate
STT/LLM/TTS pipeline or the short online reminder TTS endpoint.

The play-music gate waits up to one second for a late input transcription only
when a spoken offer, exact track, next user turn, epoch and offer deadline already
match. After waiting it rechecks all gates, including overflow and cancellation.
This does not infer approval from keywords; Gemini still interprets the user's
answer. Missing transcription is rejected after the bounded wait, not bypassed.
Host tests exercise late/missing transcription and closing a session while waiting.
No microphone, speaker, API request or board flash is needed for these tests.
