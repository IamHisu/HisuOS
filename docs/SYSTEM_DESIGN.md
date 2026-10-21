# HisuOS — System Design Specification

**Phiên bản tài liệu:** 0.6

**Cập nhật BLE:** 2026-10-21; đã gỡ Bluetooth, NimBLE và trang ghép nối; setup dùng QR hotspot/captive portal. Giữ roadmap AI/voice.

**Trạng thái:** Thiết kế cho bản xây dựng lại; chưa phải mô tả firmware đã triển khai  
**Hardware đầu tiên:** ZHENGCHEN 1.54 TFT WiFi — ESP32-S3 N16R8  
**Framework:** ESP-IDF 5.5.5  
**Ngôn ngữ:** C/C++  
**UI:** LVGL  
**AI provider đầu tiên:** Gemini

## 1. Mục tiêu

HisuOS là firmware trợ lý AI có thể mở rộng, với nền tảng thiết bị hoạt động độc lập với AI và cloud.

Ưu tiên bản đầu:

- Board hoạt động ổn định: màn hình, nút, microphone, speaker, pin và Wi-Fi.
- Setup trên thiết bị và qua hotspot HTTP: Wi-Fi, âm lượng, bật/tắt loa, độ sáng và thời gian tắt màn hình. Không có BLE trong firmware hiện tại.
- Màn hình chờ đen, không có avatar hoặc animation tích hợp sẵn.
- Hội thoại có phụ đề tiếng Việt, sau đó bổ sung voice.
- Code chia theo ownership, không trộn nhiều project thành một firmware.

Nguyên tắc: **Borrow ideas. Reuse good implementations. Own the architecture.**

Không giả định có màn hình cảm ứng. Điều khiển dựa trên các nút thực tế của board, được xác minh khi bring-up.

## 2. Phạm vi

### 2.1 MVP

- Hardware bring-up và kiểm tra từng peripheral.
- Setup Wi-Fi/hotspot, lưu cấu hình, reconnect và phục hồi khi mất mạng.
- Menu Hisu, màn hình chờ đen, phụ đề và thông báo lỗi ngắn.
- Gemini text streaming, context ngắn hạn, hủy phiên và timeout.
- Voice theo lượt: nhấn nút nói, STT, LLM, TTS, speaker.
- Device tools: đặt âm lượng, đặt độ sáng, đọc trạng thái pin.

### 2.2 Sau MVP

- Wake word, VAD nâng cao, barge-in và voice realtime.
- Gemini Live hoặc provider khác.
- Memory dài hạn, MCP, scheduler và reminder.
- Upload ảnh/GIF do người dùng cung cấp.
- OTA, tối ưu điện năng và hỗ trợ thêm board.

Partition cho OTA và dữ liệu cần được dự trù từ đầu. Upload media không được lấn át setup, lỗi hoặc phụ đề khi hội thoại.

## 3. Kiến trúc

```text
Application / Conversation Controller
    ├── UI Controller
    ├── Agent / Session / Context
    │     ├── LLM Provider
    │     └── Tool Registry
    │           ├── Local Device Tools → Device Services
    │           └── Remote Tools → MCP Client [sau MVP]
    └── Voice Controller
          ├── STT Provider
          ├── TTS Provider
          └── Audio Service

Device Services
    ├── Settings / Storage
    ├── Network / Provisioning
    ├── Audio
    ├── Display / Input
    └── Power
          ↓
Board Drivers / Board Profile
          ↓
ESP-IDF / ESP32-S3
```

Dependency phải tạo thành đồ thị không có vòng. Application lắp ráp các module và điều phối hành vi; driver không biết Agent, Gemini hoặc UI đang làm gì.

Event từ tầng dưới được đưa qua callback hoặc queue có contract rõ ràng. Điều này không cho phép tầng dưới include hoặc gọi trực tiếp module tầng trên.

Không tạo HAL cho mọi API ESP-IDF. Chỉ tạo interface ở những ranh giới cần thay implementation: board, AI provider, voice provider và tool transport.

## 4. Cấu trúc project

```text
HisuOS/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── main/                         # app_main, gọi bootstrap
├── boards/
│   └── zhengchen_154/            # Pin map, capabilities, hardware notes
├── components/
│   ├── hisu_app/                 # Bootstrap, application state, orchestration
│   ├── hisu_board/               # Driver implementation cho board hiện tại
│   ├── hisu_settings/            # Device config, validation, persistence
│   ├── hisu_network/             # Wi-Fi lifecycle và provisioning
│   ├── hisu_media/               # HTTP hotspot, xác thực và lưu hình nền
│   ├── hisu_audio/               # Capture/playback, PCM queues, volume
│   ├── hisu_power/               # Battery, backlight/power policy
│   ├── hisu_ui/                  # LVGL, menu, subtitles, status
│   ├── hisu_ai/                  # Provider contracts và Gemini adapter
│   ├── hisu_voice/               # STT/TTS orchestration
│   └── hisu_agent/               # Session, context, bounded tool loop
├── tools/                        # Build và development helpers
├── tests/                        # Host tests và hardware test procedures
├── docs/
│   └── SYSTEM_DESIGN.md
└── licenses/                     # Third-party licenses và notices
```

Đây là cấu trúc đích, không phải yêu cầu tạo toàn bộ folder ngay. Chỉ tạo component khi có implementation hoặc contract thực sự cần dùng. MCP, scheduler và media bổ sung ở giai đoạn tương ứng.

Mỗi component có `CMakeLists.txt`, public headers trong `include/`, implementation trong `src/`, dependency khai báo tường minh. Header nội bộ không đưa vào public API.

## 5. Ownership và resource lifecycle

| Resource / capability | Owner | Trách nhiệm |
|---|---|---|
| GPIO, I2S, SPI, ADC, LEDC | Board drivers | Init/deinit, wiring, DMA, truy cập peripheral |
| Capture và playback | Audio service | Buffer, stream lifecycle, volume, mute |
| Wi-Fi radio và kết nối | Network service | Scan, join, reconnect, bật/tắt |
| Local media upload / image records | Media service | AP-only HTTP window, authorization, bounded payload, durable storage |
| Device settings | Settings service | Validation, cache, persistence |
| LVGL objects | UI task | Tạo/sửa/hủy object, render menu và phụ đề |
| Conversation lifecycle | Application controller | Start, cancel, completion, error |
| AI request | Provider adapter | Transport, parsing, normalized events |
| Context và tool loop | Agent | History, budget, tool execution policy |

Không có hai module cùng quản lý một I2S channel, Wi-Fi state hoặc LVGL tree. Service không tự tạo lại peripheral đã được board driver khởi tạo.

Bootstrap đăng ký callback và tạo queue trước khi worker bắt đầu phát event. Worker readiness và init failure phải được xử lý rõ ràng.

## 6. Board và hardware contracts

Pin mapping chỉ nằm trong board profile. Không sao chép pin ví dụ từ tài liệu hoặc project khác mà chưa đối chiếu wiring thực tế.

Hardware bring-up phải xác nhận:

- Controller màn hình, resolution, orientation, color order và backlight polarity.
- Microphone sample format, I2S channel và mức tín hiệu.
- Speaker PCM format, amplifier enable và âm lượng an toàn.
- Nút và hành vi debounce/hold.
- Battery ADC scaling, calibration và tín hiệu USB/charging nếu board hỗ trợ.

Audio API cần quy định sample rate, channel count, sample type, số frame, timeout và cách báo partial read/write. Không dùng một `Read(buffer, size)` mơ hồ cho mọi định dạng.

Nếu dùng interface C++, phải có virtual destructor khi object có thể bị hủy qua base pointer. Không bắt buộc dùng class nếu API C nhỏ đã đủ rõ.

Display driver cung cấp panel flush, geometry và backlight; không render menu hoặc phụ đề. LVGL thuộc UI layer, không thuộc HAL.

## 7. Settings và provisioning

Device settings tối thiểu:

- Wi-Fi credentials và trạng thái radio.
- Volume, speaker enabled, brightness, screen-off timeout.
- AI provider, model và credential theo cơ chế lưu trữ riêng.

Settings service là nơi duy nhất validate và persist device values. UI, web và tools sử dụng cùng API; không có ba bản validation khác nhau.

Network service serialize scan/join/reconnect. UI và web gửi command, nhận progress/result; không block UI task khi scan hoặc kết nối.

Không log, echo hoặc đưa password/API key vào status response. Quên Wi-Fi cần xác nhận khi có tác động mất kết nối.

Menu vẫn dùng được khi offline hoặc không có API key.

## 8. UI và phụ đề

UI tối thiểu: boot, idle, listening, thinking, speaking, settings và error.

- Idle là màn hình đen; không avatar hoặc animation mặc định.
- Menu có Wi-Fi, Setting, âm lượng, bật/tắt loa, độ sáng, timeout và pin; không có Bluetooth trong bản hiện tại.
- Mapping click/hold/navigation phải tránh xung đột giữa mở menu và push-to-talk; chốt sau khi xác minh nút.
- Phụ đề tiếng Việt có font phù hợp, wrapping và giới hạn độ dài.
- Có thể hiển thị transcript người dùng và câu trả lời AI; phân biệt partial với final.
- Token delta không đồng nghĩa với một từ hoặc một câu hoàn chỉnh.
- Phụ đề streaming chữ chưa được coi là đồng bộ với audio TTS. Nếu cần đồng bộ, phải có segment/timestamp contract.
- Setup hoặc màn hình lỗi có ưu tiên hiển thị rõ ràng; việc mở menu không được tự làm mất trạng thái hội thoại.

Chỉ UI task được gọi LVGL. Các module khác gửi immutable event hoặc snapshot. Không gọi LVGL từ callback Wi-Fi, audio hoặc AI.

Screen-off timeout ban đầu chỉ tắt backlight; không gọi đó là deep sleep. Deep sleep cần thiết kế riêng cho wake source, network và audio lifecycle.

## 9. AI provider contracts

Provider đầu tiên là Gemini, nhưng Agent không phụ thuộc Gemini-specific JSON hoặc endpoint.

Contract tối thiểu cần có:

- Request: messages, tool definitions, model options và request/session ID.
- Events: text delta, completed tool call, usage nếu có, completion và error.
- Lifecycle: start, cancel, timeout, cleanup.
- Quy định ownership và thời gian sống của request/event data.

Provider adapter chuyển response về dạng chung. Agent không parse trực tiếp raw provider JSON. Tool arguments chỉ được execute sau khi nhận đủ dữ liệu và validate schema.

Chỉ triển khai Gemini trước. Không tạo các provider rỗng để chứng minh tính mở rộng. Provider mới cần vượt qua cùng contract tests.

Gemini Live là một realtime session transport riêng, không ép vào interface text chat. Nếu triển khai sau, nó vẫn dùng chung device services và application lifecycle.

Model, availability, quota và giá phải kiểm tra tại thời điểm triển khai; không xem free tier là cam kết vận hành lâu dài.

## 10. Voice pipeline

MVP dùng hội thoại theo lượt, ưu tiên push-to-talk:

```text
Button → Start capture → Microphone → Audio Service
       → Stop capture → STT → User transcript
       → Agent → LLM → Response text / Device tools
       → TTS → Audio Service → Speaker
```

Phụ đề nhận transcript và response events độc lập với playback. Setup luôn hoạt động dù STT/TTS không khả dụng.

STT và TTS provider phải được chọn và kiểm thử riêng; việc đã tích hợp Gemini text không có nghĩa voice đã hoàn thành.

MVP ưu tiên half-duplex để tránh thu lại tiếng loa. Wake word, AEC, continuous listening và barge-in là bước sau, không tự tuyên bố hỗ trợ nếu chưa test trên board.

## 11. Application state và concurrency

```text
IDLE → LISTENING → TRANSCRIBING → THINKING → SPEAKING → IDLE
                   └──────────── Error / Cancel ─────────→ IDLE
```

State machine cấp ứng dụng là nơi quyết định phiên hội thoại. Network và audio vẫn có state machine nội bộ cho resource lifecycle. Settings là UI mode, không buộc phải trở thành voice state.

Mọi session có ID; event từ session cũ bị bỏ qua sau cancel hoặc restart. Hủy phải dừng request/playback tương ứng và giải phóng buffer. Không chờ vô hạn.

Không tạo task cho mọi class. Các execution context chính dự kiến là application/agent worker, audio capture/playback và UI; ESP-IDF/network có task riêng theo stack sử dụng. Chốt task count, priority, stack và core affinity bằng đo đạc.

Queue phải hữu hạn và có overflow policy: backpressure, coalesce hoặc reject. Không âm thầm bỏ completion/error events. Callback driver không thực hiện AI request hoặc tác vụ chặn dài.

## 12. Agent và tools

```text
Input → Build bounded context → Call LLM
      → Tool calls? → Validate → Execute → Append results → Call LLM
      → Final response → Update session history
```

Agent loop có giới hạn số vòng tool, tổng thời gian, context size và response size. Tool failure được trả về có cấu trúc; không lặp vô hạn để sửa lỗi.

Device tools MVP:

- `set_volume(percent)`
- `set_brightness(percent)`
- `get_battery()`
- `get_device_status()`

Tools gọi service API, không GPIO trực tiếp. Thao tác phá hủy dữ liệu, đổi credential hoặc có tác động bên ngoài cần policy/xác nhận riêng. Không tự động retry tool có side effect.

## 13. Memory và storage

- Short-term context: giữ trong RAM, giới hạn message/token/byte budget.
- Device configuration: settings service, NVS.
- Long-term preferences/facts: sau MVP, có consent, xem/xóa được.
- Conversation archive/media: filesystem nếu cần, không dùng NVS cho payload lớn.

Không ghi flash mỗi token. Dùng batching, schema version, migration và recovery cho dữ liệu bị ghi dở. Không cần vector database cho MVP.

NVS thông thường không đồng nghĩa với secure storage. Production cần lựa chọn rõ flash/NVS encryption, provisioning và key handling; không bật secure boot hoặc thay eFuse tùy tiện trên board phát triển.

### 13.1 Hình nền nội bộ

Triển khai ảnh tĩnh local trước cloud media: service `hisu_media` phục vụ HTTP chỉ qua hotspot WPA2 mở vật lý, token theo phiên, không mã truy cập riêng hoặc BOOT duyệt ảnh. Không giới hạn tổng thời gian; đóng thủ công hoặc khi mất client/web theo grace/liveness. Browser chuyển PNG/JPEG/WebP về RGB565 240×240; board nhận đủ payload hợp lệ rồi tự lưu. Hai records luân phiên trong 240 KiB đầu `storage`, CRC và commit header sau cùng, chưa cần filesystem. HTTP không có TLS; token không thay thế mã hóa/xác thực server. Không mở service ra Internet, không ảnh hưởng ownership UI/network/settings. Contract và giới hạn kiểm chứng theo [Setting](WALLPAPER_SETUP.md).

### 13.2 Setting qua hotspot

Mục Hình nền đổi thành **Setting**. Network worker sở hữu vòng đời WPA2 SoftAP,
mật khẩu ngẫu nhiên 16 ký tự mỗi phiên và AP+STA mode; HTTP handler chỉ gửi queue command.
`hisu_media` tái sử dụng phiên bảo mật cho trang nhúng và `hisu_setup_api` tách
API settings/Wi-Fi/time khỏi storage ảnh. Tab Wi-Fi quét/join/bật-tắt station;
Hình nền preview trên trình duyệt rồi tự lưu khi gửi; Thiết bị dùng chung settings service và gửi UTC
từ điện thoại. Settings được validate/persist qua API chung, không gọi LVGL/GPIO
từ HTTP. Âm lượng/loa chưa nối audio service.

Trang chỉ phục vụ IP AP, không IP station. Host/Origin/token/subnet/socket đích
được kiểm tra; JSON bị giới hạn body/nesting/schema. Hotspot sống độc lập với
kết nối Wi-Fi nhà, đóng khi phiên kết thúc; không NAT/bridge.
QR Wi-Fi chứa SSID và mật khẩu; QR URL HTTP tự hiện khi điện thoại kết nối, ngắt kết nối trả về QR Wi-Fi.
BOOT ở QR/thông tin bật/tắt hotspot Setting. Web dùng danh sách ổn định/icon sóng, mạng đã lưu kết nối ngay; mạng mới mở dialog mật khẩu.
Không có deadline 5 phút. Ngắt hotspot có grace 10 giây; pagehide có grace 8 giây và mã trang chống unload cũ;
request xác thực giữ lease 90 giây để đóng dự phòng khi không nhận được pagehide. Tải lại trang nhanh giữ phiên.
`hisu_setup_portal` dùng DNS AP và redirect trên cùng HTTP server Setting cổng 80;
không bảo đảm tự mở trên mọi điện thoại. POST lạ trả 404, không chuyển body tới API.
Người có QR hotspot có quyền chỉnh Setting trong phiên; token tự cấp qua HTTP,
vẫn cần kiểm tra Origin/Host/socket AP; web không được mở lại phiên đã đóng.
Socket dual-stack chuẩn hóa IPv4-mapped IPv6 về IPv4 trước các kiểm tra AP/client; không cho phép IPv6 thuần.
BLE source, companion web, cấu hình QR, test và workflow đã gỡ. `CONFIG_BT_ENABLED`
phải tắt; build chặn sdkconfig cũ còn bật BT. Không khởi tạo hay giải phóng controller
bằng API BT trong app vì bản này không link Bluetooth host/controller.
HTTP không chống AP giả/MITM bằng TLS. Chưa xác minh AP/channel switching/flash trên board thực tế.

## 14. MCP và scheduler

Local tool calling không cần MCP. Sau MVP, tách hai vai trò:

- MCP client: Agent gọi tool từ external server.
- MCP server: bên ngoài truy cập capability được HisuOS cho phép.

Không expose toàn bộ device tools mặc định. Cần authentication/authorization, timeout, giới hạn payload và xử lý reconnect. Kiểm tra vai trò client/server của dependency trước khi chọn.

Scheduler sau MVP hỗ trợ one-shot/periodic/reminder, có timezone, time sync và policy khi clock chưa hợp lệ hoặc task bị lỡ. Dùng monotonic clock cho deadline; wall clock cho lịch người dùng.

Không mặc định reminder gọi LLM mỗi lần. Không replay side effect chỉ vì reboot hoặc reconnect.

## 15. Resource budget và reliability

N16R8 không có nghĩa toàn bộ flash dành cho app hoặc toàn bộ PSRAM dùng được cho DMA.

Trước khi mở rộng cần đo và ghi lại:

- App size, partition capacity và OTA slot capacity.
- Internal heap, largest free block, PSRAM và stack high-water marks.
- Audio buffer duration, overflow/underrun và UI latency.
- Peak memory khi TLS, JSON parsing, audio và display cùng hoạt động.

Ưu tiên buffer bounded, reuse buffer và parse streaming. DMA/internal memory theo yêu cầu driver. Không chốt buffer/stack size tùy ý khi chưa có đo đạc.

Retry có giới hạn, backoff và phân loại lỗi. Không retry lỗi authentication/configuration như lỗi mạng. Lỗi recoverable trả về application; không dùng fatal abort cho timeout AI hoặc mất Wi-Fi.

Logging có tag theo module, không in credential hoặc toàn bộ hội thoại mặc định. Diagnostic mode phải tắt được và không làm nghẽn audio/UI.

## 16. OTA và bảo mật

Partition layout dự trù bootloader, NVS, OTA metadata, hai app slots và dữ liệu cần thiết, phù hợp flash thực tế. Kích thước phải được xác nhận từ build output trước khi freeze.

OTA sau MVP cần HTTPS validation, version policy, image validation, rollback và post-update health check. Không coi tải được binary là OTA hoàn chỉnh.

TLS phải validate certificate và hostname. Development credential không commit vào repo; có đường xóa/reset credential. Nếu dùng backend cấp token tạm thời cho Live API, backend trở thành dependency vận hành và phải được ghi rõ.

## 17. Chính sách tái sử dụng

| Nguồn | Vai trò dự kiến |
|---|---|
| ESP-IDF | Nền tảng và peripheral/network APIs |
| ESP-IoT-Solution | Selected components hoặc reference, kiểm tra compatibility |
| Muse | Reference/selected implementation cho setup, menu, BLE và board |
| Xiaozhi | Reference cho voice pipeline, buffering và voice lifecycle |
| MimiClaw | Reference cho agent loop, tools, memory và scheduling |
| XiaoClaw | Reference cho tích hợp voice Xiaozhi với agent MimiClaw qua queue/callback; chọn từng module, không thay nền HisuOS |
| Gemini | AI provider đầu tiên |

Không phụ thuộc bắt buộc backend của Muse hoặc Xiaozhi. Không copy toàn bộ resource manager từ project khác vào cạnh service HisuOS.

Mỗi phần tái sử dụng phải ghi nguồn, commit/version, license, modification và dependency. Giữ copyright/license/NOTICE theo yêu cầu. Chỉ chọn component sau khi xác minh tương thích ESP-IDF 5.5.5; tài liệu `latest` không phải bằng chứng tương thích.

Đánh giá XiaoClaw và các giới hạn về LLM qua API, flash, pin và BLE nằm trong [Tài liệu tham khảo](REFERENCES.md). Học cách tách voice/agent qua message bus; application HisuOS vẫn sở hữu điều phối và vòng đời phiên. Không thêm một resource manager hoặc tầng bridge tổng quát chỉ vì project nguồn có nó. Ưu tiên text AI, voice theo lượt và bounded tools; Lua, tự tạo skills, memory dài hạn và scheduler không trở thành yêu cầu MVP.

## 18. Roadmap và tiêu chí hoàn thành

| Giai đoạn | Deliverable | Điều kiện qua |
|---|---|---|
| 1. Foundation | Board drivers, build, partition draft | Build 5.5.5; từng peripheral được test thật |
| 2. Setup | Menu, Wi-Fi/hotspot, settings | Offline setup; reconnect; persistence qua reboot |
| 3. Text AI | Gemini, streaming subtitles, cancel | Tiếng Việt; timeout/lỗi; không block UI |
| 4. Voice | Push-to-talk, STT/TTS, playback | Nghe/nói thực tế; hủy; buffer bounded |
| 5. Tools | Bounded agent loop, device tools | Schema validation; tool failure; không loop vô hạn |
| 6. MVP stability | Long-run và fault tests | Không crash/leak tăng dần; tài liệu test và known limits |
| Sau MVP | Memory/MCP/scheduler/media/OTA | Triển khai từng capability, không mở đồng thời tất cả |

MVP hoàn thành khi giai đoạn 1–6 đạt điều kiện, không yêu cầu MCP, scheduler hoặc OTA đã hoạt động. Board test phải phân biệt với host test/mock test.

## 19. Verification strategy

- Build firmware với ESP-IDF 5.5.5; kiểm tra app/partition size.
- Unit/host tests: settings validation, context bounds, tool schema, stream parser và session cancellation.
- Provider fixtures: response chia chunk, Unicode, tool call partial, lỗi và response quá lớn.
- Hardware tests: display, buttons, mic, speaker, battery, pairing, reconnect và reboot persistence.
- Fault tests: mất mạng trong request, ngắt hotspot khi setup, cancel khi playback và queue đầy.
- Soak tests: nhiều lượt hội thoại và setup; theo dõi heap, stack, underrun và watchdog.

Không tự flash, erase NVS hoặc đổi eFuse trong build helper. Các thao tác ảnh hưởng board/dữ liệu cần được yêu cầu riêng.

## 20. Những quyết định cần chốt khi triển khai

- Wiring chính xác, battery calibration và chức năng các nút.
- LVGL version, font tiếng Việt, display buffer và integration tương thích IDF.
- STT/TTS provider, model và tiếng Việt thực tế.
- Partition sizes, storage format và credential security cho prototype/production.
- Timeout/buffer/context/tool budgets dựa trên measurements.

Những mục chưa chốt không được biến thành lời khẳng định đã hỗ trợ.

## 21. Nguồn tham khảo

- [ESP-IDF v5.5.5 documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/)
- [ESP-IoT-Solution MCP](https://docs.espressif.com/projects/esp-iot-solution/en/latest/ai/mcp.html)
- [Gemini Live API capabilities](https://ai.google.dev/gemini-api/docs/live-api/capabilities)
- [MimiClaw repository](https://github.com/memovai/mimiclaw)
- [Xiaozhi repository](https://github.com/78/xiaozhi-esp32)
- [XiaoClaw repository](https://github.com/beancookie/xiaoclaw)
- [Đánh giá nguồn và hướng tái sử dụng](REFERENCES.md)

Nguồn bên ngoài là tài liệu tham khảo, không quyết định ownership hoặc kiến trúc HisuOS.
