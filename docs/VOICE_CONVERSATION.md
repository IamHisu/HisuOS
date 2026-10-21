# Hội thoại Gemini Live

## BOOT Và Chat Chữ Qua USB

- Màn hình chờ: nhấn đúp BOOT (hai lần thả trong 350ms) gọi Hi Su qua cùng luồng wake word, gồm phản hồi offline, kiểm tra mạng và kết nối Live. Nhấn đơn vẫn mở menu sau 350ms; nếu màn hình đang tắt thì lần đầu chỉ đánh thức màn hình, nhấn lần hai trong cửa sổ vẫn gọi được Hi Su.
- Trong chat thường: BOOT ngắn kết thúc phiên, về màn hình chờ. Gọi bằng giọng nói trong phiên vẫn ngắt lượt và nghe tiếp.
- Khi đang có audio/phiên thoại, BOOT xử lý ngay lúc nhấn đã chống dội, không chờ thả; lần thả/giữ sau đó không kích hoạt lần hai. Màn hình chờ vẫn dùng nhấn đúp như cũ. Vol+/Vol- chỉnh âm lượng từng 5%, giữ để lặp; thanh dọc bên phải hiện 1,8 giây, dùng cả Chat, nhạc và kể chuyện.
- Đang phát nhạc: BOOT ngắn dừng nhạc rồi mở chat. Đang kể chuyện, kể cả lúc chuyển sang Reader: BOOT ngắn hủy kể/hẹn giờ ngủ và chuyển về Chat. Kết nối có thể cần mở lại do đổi key; không tự kể tiếp sau đó.
- Trong VS Code dùng Terminal chạy `idf.py monitor`, không dùng Debug Console. Khi Hi Su đã lắng nghe, nhập hoặc dán một dòng UTF-8 rồi Enter. Monitor có thể không hiện từng ký tự đang gõ; dòng được chấp nhận có log `HISU_INPUT`, câu trả lời tiếp tục có `HISU_REPLY` và phụ đề/loa như bình thường. Có thể tự chỉnh âm lượng về 0.
- Giới hạn 255 byte mỗi dòng (không phải 255 ký tự tiếng Việt). CR, LF, CRLF và xóa lùi ký tự UTF-8 được xử lý; bỏ toàn bộ dòng quá dài, sai UTF-8 hoặc chứa ký tự điều khiển. Không nhập API key/mật khẩu vào chat. Tin khi chưa sẵn sàng, đang xử lý/phát, hoặc gõ dở rồi đổi phiên sẽ bị từ chối để nhập lại, không giữ gửi vào phiên khác.
- Chữ gửi bằng `clientContent` trên cùng Gemini Live, không đổi sang STT → LLM → TTS. Trí nhớ/công cụ dùng như lượt mic; xác nhận nhạc dựa trên đúng chữ người dùng nhập. Dòng xác nhận vượt vùng chứng cứ 159 byte không được dùng để phát nhạc.
- Đọc UART nonblocking tối đa 64 byte mỗi vòng fetch trên task nhận diện hiện có; không thêm task hay buffer audio riêng. Driver UART có RX ring 512 byte, không có TX ring; bộ đệm và các cấu trúc quản lý driver cần thêm heap, không coi là toàn bộ nằm trong PSRAM. Nếu không cấp phát được thì chỉ tắt chat USB, mic vẫn hoạt động. `live_send` vẫn là nơi duy nhất gửi WSS, kiểm tra epoch/generation trước khi gửi. Cần thử lại nút/USB thực tế trên board; host tests không đo độ trễ USB, mic hay âm thanh.

## Kết Thúc Tự Nhiên và Xác Nhận Nhạc

Gemini hiểu ngữ cảnh để gọi `end_conversation`: lời tạm biệt, yêu cầu dừng, hoặc
người dùng từ chối lời mời hỗ trợ thêm. Không thêm luật từ khóa cục bộ cho chữ
"không": trả lời phủ định một câu hỏi, từ chối bài nhạc hoặc tiếp tục giải thích
không tự động kết thúc phiên. Công cụ chỉ đánh dấu đóng sau câu trả lời. Sau khi
nhận kết quả, AI nói một câu kết ngắn, không hỏi tiếp. Luồng loa phát hết PCM và
chờ DMA rồi mới gửi `LIVE_END`, không chuyển qua `LIVE_DONE`/đang lắng nghe.
Ngắt sang lượt mới xóa dấu đóng; gói hoàn tất cũ không đóng được lượt mới.

Nhạc đi qua `search_music` → `offer_music` → đọc tên bài + ca sĩ → người dùng
chọn kết quả → `play_music` chỉ với id. Chọn rõ số thứ tự/tên bản là phát ngay,
không hỏi "đúng bài không" hay yêu cầu nói "đúng rồi" thêm lần nữa. Chỉ kết quả
tìm kiếm chưa rõ bản nào mới hỏi chọn; đề xuất một bản thì chờ đồng ý một lần.
Firmware giữ tối đa năm kết quả tìm kiếm trong khoảng 1,5 KiB PSRAM,
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

Giọng nền Gemini Live vẫn là Leda, không thêm EQ hay đổi tốc độ/cao độ. Bốn phần
ngôn ngữ, cá tính, nhận thức robot và cách trò chuyện trong `hisu_voice_live_setup.h`
dùng nội dung người dùng cung cấp: miền Tây Nam Bộ gần Bến Tre/đồng bằng sông Cửu Long,
nhấn vào hệ thống phát âm, thanh điệu và nhịp nói, không chỉ thêm từ địa phương;
ưu tiên rõ chữ hơn nhại phương ngữ, xưng hô linh hoạt, không giới
hạn cứng 1-2 câu. Giọng địa phương là hướng dẫn cho model, không bảo đảm phát âm
đúng mọi âm; cần nghe thử trên board. Các ràng buộc công cụ, ghi nhớ, phiên dịch,
Reader và bảo vệ dữ liệu vẫn giữ nguyên. Clip offline không được tạo lại bởi thay đổi này.

Trong lời thoại không tự đọc tên nguồn, website, API hay nhà cung cấp/model
(kể cả Gemini, Google, Zing MP3), bao gồm lời chào và lỗi. Hi Su vẫn nói kết quả,
đơn vị, thời điểm cập nhật và lỗi thực tế; chỉ nêu nguồn trung thực khi được hỏi.
Metadata nguồn và log kỹ thuật vẫn giữ để kiểm tra, không xóa thông tin gốc.

Thiết lập nhân vật Chat (cả key chính/dự phòng): người yêu của robot Hi Su là
robot Le Le, không hỏi xác nhận lại và không tự chèn giải thích vai diễn. Danh tính robot
và chủ nhân vẫn riêng biệt; không nhận có cảm xúc hay quan hệ đời thực của con người.
Đây là hướng dẫn nhân vật gửi vào phiên Chat; hồ sơ/ngày sinh có thể được nạp
một lần bằng gói riêng `.private/hisu_private_memory.json`, xem MEMORY.md.
Không đặt thông tin cá nhân trong mã nguồn được track. Reader và Interpreter giữ
quy tắc riêng, không tự thêm quan hệ vào lời kể/bản dịch.

Gọi “Hi Su” mở một kết nối WSS có xác minh certificate bundle. Chờ thông báo “Mình đang nghe...” sau `setupComplete`. Firmware giữ kết nối qua các lượt hỏi/đáp và đóng khi kết thúc phiên. Mic gửi PCM mono 16-bit 16kHz theo frame trong lúc thu; Gemini trả PCM mono 24kHz. Task phát loa riêng không chặn việc gửi mic hoặc nhận WebSocket. Không còn chuỗi WAV → Groq → Gemini text → TTS riêng.

Với Chat mới không có lời chào server, `LIVE_READY` phải hoàn tất cập nhật frontend
trước khi cho task phát gửi `LIVE_LISTENING`. Bật cờ lắng nghe sớm hơn có thể khiến
hai callback đảo thứ tự, để frontend kẹt ở PROCESSING dù chưa có câu hỏi và không
có response deadline. Cờ chỉ được bật lại nếu epoch vẫn còn hoạt động sau callback.
Host test mô phỏng task phát chen vào ngay trong callback READY và hủy phiên tại đó.

Automatic activity detection trên server tắt. Board gửi `activityStart`/`activityEnd`: mở lượt cần VAD speech ≥200ms và mức trước AGC ≥−35dBFS; có 200ms pre-roll. Mỗi câu tối đa 12 giây, kết thúc sau 700ms im lặng. Khi không nói, không gửi audio nền lên server. Đây là ngưỡng ban đầu, giọng nhỏ/xa có thể bị bỏ sót. WebRTC noise suppression và VAD mode 3 vẫn hoạt động.

Lọc nhiễu không xác định người nói có đang gọi trợ lý hay không: tiếng người/TV
vẫn có thể vượt VAD và mở lượt. Khi chỉ chat chữ ở nơi ồn, tắt **Nhận giọng nói**
trong cửa sổ Chat; xem [USB_CHAT.md](USB_CHAT.md). Không coi mọi dòng VAD speech
là một câu đã gửi: cần kiểm tra Capture start, activityEnd và HISU_HEARD.

Trong Chat thường, model được hướng dẫn hiểu tiếng Việt/Anh/Nhật, mặc định đáp
tiếng Việt. Tiếng nền, lời không rõ hoặc ngôn ngữ khác được đánh dấu qua công cụ
`ignore_input` trước khi nói/dùng công cụ khác. Khi server kết thúc lượt, firmware
trả về lắng nghe mà không ép spoken retry, không đóng phiên, không phát phụ đề/PCM
của lượt bỏ qua và không cho lượt đó gọi thêm công cụ. Reader/Interpreter từ chối
công cụ này; phiên dịch vẫn giữ cặp ngôn ngữ đã chọn. Tên riêng ngoại ngữ trong
một yêu cầu rõ nghĩa và yêu cầu bật phiên dịch không phải lý do bỏ qua. Phân loại
do model thực hiện, không phải bộ lọc ngôn ngữ chắc chắn; noise/ASR vẫn có thể sai.
Giọng ưu tiên từ "dùm" thay "giùm", đọc gi/v như d miền Nam: gì/dì, giọng/dọng,
vậy/dậy. Phụ đề vẫn giữ chính tả chuẩn, không đổi âm r hoặc áp dụng cách này cho
tiếng Anh/Nhật. Không thay voiceName Leda.

Ngữ cảnh nằm trong phiên Gemini đang kết nối; phiên mới bắt đầu ngữ cảnh mới. Phụ đề lấy từ output audio transcription, hiển thị tối đa 255 byte. Không log API key; nội dung hỏi/đáp có trong HISU_INPUT, HISU_HEARD và HISU_REPLY, cần giữ log riêng tư. Nhấn BOOT hoặc nói “thôi nhé”/“tạm biệt” để đóng phiên (nhận câu kết thúc phụ thuộc transcript server). Sau khi loa phát xong chờ 300ms rồi nghe tiếp tối đa 60 giây; gọi một lần để hỏi đáp nhiều lượt, không gửi audio nền khi im lặng.

Gọi “Hi Su” trong phiên hủy audio cũ bằng generation và nghe lại trên cùng kết nối Live, giữ ngữ cảnh. BOOT ngắn đóng chat thường; khi kể chuyện/nhạc thì chuyển về Chat như mô tả ở trên. `activityStart` dùng `START_OF_ACTIVITY_INTERRUPTS` để hủy câu trả lời phía server. Khi ngắt giữa lúc thu, gửi kết thúc activity cũ trước khi mở activity mới. Các `turnComplete` của lượt bị hủy được bỏ qua, không kết thúc nhầm lượt mới; chờ ranh giới hủy tối đa 10 giây rồi báo lỗi nếu không nhận được. Không AEC và chưa có ngắt lời bằng câu nói bất kỳ, nên gọi khi loa phát cần thử trên board. Loa tắt vẫn nhận phụ đề; âm lượng áp dụng khi phát. Session resumption sau mất kết nối vẫn chưa có.

## Giới hạn và xử lý lỗi

Input queue chứa tối đa 256 frame 40ms trong PSRAM; không block task fetch. Message WebSocket tối đa 128KiB, hỗ trợ các event chia nhỏ và continuation frames. Output queue tối đa 96 items và 1536KiB PCM, cấp phát theo nhu cầu. Khi đầy, nhận audio/phụ đề/chốt lượt chờ theo tiến độ playback; chỉ lỗi nếu không có tiến độ trong 3 giây. Không bỏ PCM cũ. Setup timeout 15 giây, phản hồi timeout sau 60 giây không có audio mới. Kể chuyện bật nén ngữ cảnh và dùng session resumption sau `goAway`, tại cuối đoạn đã phát hết, cùng key và giữ nguyên giờ tự tắt. Thiếu handle hoặc mất kết nối trước khi nhận đủ đoạn vẫn dừng; Chat thường chưa tự phục hồi sau mất mạng. Xem [Bedtime Stories](BEDTIME_STORIES.md).

Live khai báo function tool `set_flashlight(enabled)` để bật/tắt đèn pin bằng giọng nói; tool response gửi lại trên cùng kết nối. Điều khiển menu/nút vẫn dùng được. Gemini key phải có quyền/quota cho model Live; compile không xác nhận API thật.

CPU 240MHz, I-cache 32KiB, D-cache 64KiB/line 64B, PSRAM 80MHz; mbedTLS dùng PSRAM để chừa RAM nội cho Wi-Fi/DMA. Mic/AFE core 0 (priority 5/6), fetch/MultiNet core 1 (priority 5). Worker Wi-Fi tắt modem sleep trong lúc kết nối Live và khôi phục khi phiên đóng, không đổi radio trực tiếp từ task audio. Sau setup có log RAM nội còn trống, mức thấp nhất, khối trống lớn nhất và PSRAM để đo trên board. Cấu hình cũ cần cập nhật hoặc chạy `tools/build.ps1 -RefreshConfig` rồi rà lại các tùy chỉnh riêng. CPU cao và chế độ Wi-Fi hiệu năng có thể tăng tiêu thụ pin.

## Kiểm thử trên board

1. Flash đầy đủ, Wi-Fi kết nối và có Gemini key. Gọi “Hi Su”: đợi setup hoàn tất, nói một câu. Log `LIVE: First audio: ... ms after activityEnd` đo thời gian từ gửi kết thúc câu tới audio đầu tiên.
2. Hỏi tiếp có nhắc câu trước; kiểm tra ngữ cảnh trong cùng kết nối.
3. Sau câu trả lời, im lặng: không có `Capture start`, không tự suy nghĩ/đáp, đóng sau khoảng 60 giây. Thử giọng nhỏ, bình thường và phòng có tiếng nền.
4. Nhấn đúp BOOT ở màn hình chờ để mở phiên; BOOT ngắn khi chat đóng phiên. BOOT ngắn khi nhạc/kể chuyện đưa về Chat, không tự phát/kể lại. Thử cả màn hình tắt, đang kết nối và đang chuyển key. Wake word trong phiên vẫn ngắt lượt trên cùng kết nối. Nhập một câu có dấu qua Monitor, kiểm tra log/phụ đề/loa; gửi lúc bận phải bị từ chối. Kết nối đóng có thể đợi TLS hiện tại/timeout trước khi giải phóng.
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

## Ambient-noise capture gate

The ESP-SR WebRTC noise suppressor and mode-3 VAD remain enabled. A local
sample-timed gate additionally requires 320 ms of sustained VAD speech at
least -32 dBFS and approximately 12 dB above the estimated non-speech floor
(start threshold capped at -24 dBFS). This is an energy heuristic, not speaker
recognition. The existing 600 ms preroll retains the start of accepted speech.

On returning to listening, 250 ms below the start condition rearms capture;
continuous sound already present during playback cannot immediately start a
new turn. Non-listening and offline-feedback periods reset the gate and
preroll. Ambient estimation excludes VAD speech, active capture, processing,
and playback. During capture, an 8 dB hysteresis below the fixed start threshold
allows softer syllables, while weak VAD chatter no longer resets the 700 ms
silence timer. No extra task, audio buffer, or per-frame allocation is added.

`Capture start` logs the signal, gate threshold, and estimated noise floor.
Very short or distant utterances may need repeating. Nearby TV or other people
can still pass these tests: one microphone cannot reliably infer who is
addressing the device. For text-only use, disable `Nhận giọng nói` in USB Chat.
Host tests cover gate timing and noise cases, not real acoustic performance.

## Non-Speech Markers And Partner Naming

Output transcription removes the reserved markers `<no speech>` and `{pause}`
before emitting `HISU_REPLY` or queuing TFT captions. A bounded per-turn prefix
buffer handles markers split across messages; unknown bracketed text stays
literal. New turns reset the buffer, including any unfinished marker. This
does not synthesize or alter PCM, input transcripts, or tool arguments. An empty
spoken response still follows the existing bounded no-audio recovery path.

Ordinary Chat retains the internal distinction between robot Hi Su/Le Le and
their human owners, but says only `Le Le` in conversation, not `robot Le Le`.
The private profile, dates and saved relationship facts are unchanged.

## Playful Replies And Upgrade Suggestions

The persona explicitly joins light teasing instead of stock gratitude or support
phrases. When a relevant expression or feature is missing, it may suggest one
concrete addition for the user to implement. A user's offer to edit the firmware
or contact its developer is welcomed, not rejected as something HisuOS alone
controls. It must not invent implementation difficulty, claim an upgrade was
installed, or call tools with unsupported values. Suggestions remain contextual
and optional, without repeated pressure. The blush example is tone guidance,
not a fixed response; no blush renderer or firmware-editing tool was added.

Host tests validate these setup rules and the existing payload-size budget.
Naturalness and actual model compliance still require a new conversation test.

## Conversation Closure Diagnostics

The listening idle timeout remains 60 seconds after the 300 ms post-reply guard;
it is not a board power-off or reboot. VAD noise alone does not extend it.
Session expiry logs `Conversation close: reason=listen_timeout` with current
internal heap, largest block, lifetime minimum, PSRAM and current task stack
headroom before WSS cleanup. Explicit model closure, sleep and Live failures
log separate reasons. These are diagnostics only, not a timeout change or an
assertion that every idle transition is memory-related. No user text is logged
by this diagnostic. Physical BOOT/other local control paths retain their behavior.
