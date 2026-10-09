# HisuOS

Firmware ESP32-S3 cho ZHENGCHEN 1.54 TFT WiFi, xây dựng lại theo [System Design](docs/SYSTEM_DESIGN.md).

Nguồn và đánh giá tái sử dụng: [Tài liệu tham khảo](docs/REFERENCES.md), gồm XiaoClaw (voice Xiaozhi + agent MimiClaw), điều kiện tương thích và các giới hạn chưa kiểm chứng.

## Hiện Trạng

Bước đầu là UI LVGL 9.4.0: GIF mèo 240×240 ở màn hình chờ, menu Hisu, editor cấu hình và API phụ đề tiếng Việt.

Màn hình chờ có icon Wi-Fi theo mức sóng và pin nằm ngang chia 5 vạch ở góc trên bên phải, không hiện số phần trăm. Khi GPIO9 báo nguồn ngoài, pin hiện tia sét thay các vạch; đây không phải tín hiệu xác nhận sạc đầy. Pin còn 20% trở xuống chuyển đỏ khi không cắm sạc. Tắt Wi-Fi sẽ ẩn icon; bật nhưng chưa kết nối hiển thị icon mờ. Danh sách mạng dùng cùng icon, với các vòng ngoài mờ theo tín hiệu. Cụm trạng thái cập nhật mỗi giây mà không khởi động lại GIF.

GIF nguồn trong suốt là `components/hisu_ui/assets/idle_cat.gif`; firmware nhúng hai bản `idle_cat_dark.gif` và `idle_cat_light.gif`, ghép vùng trong suốt lên màu nền đen/trắng tương ứng. GIF lặp khi idle và tạm dừng khi backlight tắt. Menu và phụ đề thay thế GIF. LVGL dùng heap ESP-IDF (có PSRAM) để chứa framebuffer GIF thay cho pool 96 KiB.

Mục Theme chuyển Sáng/Tối bằng Vol+/Vol− và preview ngay trên editor. BOOT lưu vào NVS; giữ BOOT hủy. Mặc định là Tối. Theme áp dụng cho menu, phụ đề và nền GIF.

- Độ sáng: PWM thật, preview khi chỉnh, lưu NVS khi xác nhận.
- Hội thoại: gọi "Hi Su" một lần để mở phiên; sau mỗi câu trả lời tự nghe câu tiếp theo. Giữ tối đa 6 cặp hỏi/đáp trong RAM, xóa ngữ cảnh khi mở phiên mới. Chờ 15 giây sau khi loa nói xong (cộng 300ms tránh tiếng vọng) mà không có câu mới thì đóng phiên. Nói "thôi nhé"/"tạm biệt" hoặc giữ BOOT 800ms để kết thúc; nhấn BOOT hoặc gọi "Hi Su" khi đang xử lý/phát loa để ngắt và nghe câu mới. Không phát kết quả của lượt đã hủy. Board ghi mỗi câu tối đa 12 giây, kết thúc sau 1,1 giây im lặng, rồi gửi WAV tới Groq Whisper. Gemini trả chữ từng phần qua SSE; TTS dùng Interactions API, giải mã/phát PCM 24kHz từng phần. Nhận dạng giọng nói vẫn theo từng câu; chưa phải voice realtime hoặc nói chen tự do. Cần kiểm chứng API, độ trễ và ngắt bằng câu gọi khi loa phát trên board thật. Xem [Hội thoại](docs/VOICE_CONVERSATION.md).
- Timeout: tắt backlight khi idle, không deep sleep.
- Wi-Fi: quét/chọn mạng, nhập mật khẩu bằng ba nút, RSSI/IP, bật/tắt radio; lưu mạng kết nối thành công vào NVS và tự quét lại khi mất kết nối.
- Pin: phần trăm ước tính từ ADC GPIO8 theo profile Muse ZHENGCHEN; hiển thị raw ADC và tín hiệu nguồn ngoài GPIO9. Cần kiểm chứng calibration/polarity trên board thật.
- Trang Pin có thời gian còn dùng được ước tính từ tốc độ tụt pin đo được: chờ ổn định 1 phút, cần ít nhất 10 phút và tụt 5 điểm phần trăm, nếu chưa đủ thì hiện Đang đo. Thống kê màn hình sáng, dùng pin và sleep tính trong lúc không có nguồn ngoài, theo chu kỳ từ lần rút sạc gần nhất; giữ qua reboot bằng checkpoint NVS mỗi phút và khi đổi nguồn. Rút sạc bắt đầu chu kỳ mới; cắm sạc tạm dừng bộ đếm. Reboot giữ thống kê nhưng đo lại tốc độ tụt pin. Sleep ở đây là backlight tắt, chưa phải deep sleep của ESP32. Ước tính còn lại phụ thuộc bảng ADC và mức sử dụng, chưa được kiểm chứng trên board.
- Bluetooth tạm ngưng: không có mục Bluetooth/icon BLE, không khởi động BLE host. Mã nguồn và trang BLE cũ giữ lại để tham khảo, không dùng cho setup bản này.
- Lịch: lịch tháng dương, ngày âm Việt Nam UTC+7 và tháng nhuận (2024–2099). Vol− ngày trước, Vol+ ngày sau; giữ 1 giây lặp mỗi 180ms, tự chuyển tháng/năm. BOOT về hôm nay, giữ BOOT trở lại menu. Cần giờ đồng bộ qua Wi-Fi hoặc trang Setting; sau đó xem offline được khi thiết bị vẫn có giờ.
- Đèn pin: giữ BOOT 1,5 giây ở màn hình chờ để mở màn trắng, backlight 100%; Vol− bật/tắt, BOOT chuyển Sáng → Chớp → SOS → Sáng, Vol+ hoặc giữ BOOT về màn hình chờ và khôi phục độ sáng đã lưu. Chớp 500ms sáng/500ms tắt; SOS `... --- ...` với đơn vị 200ms, lặp lại. Không ghi lại setting độ sáng, không tự sleep khi đang ở Đèn pin.
- Phụ đề: API queue cho worker, chưa nối AI/STT/TTS.
- Setting: QR hotspot WPA2 với mật khẩu ngẫu nhiên 16 ký tự mỗi phiên, SSID ổn định theo MAC. QR địa chỉ HTTP tự hiện khi điện thoại kết nối. Web dùng list Wi-Fi/icon sóng; chạm mạng đã lưu kết nối ngay, mạng mới mở dialog mật khẩu. Không giới hạn 5 phút: thoát màn HISU vẫn chạy; mục Dừng Setting riêng hoặc Dừng trên web đóng ngay. Ngắt hotspot 10 giây hoặc đóng web 8 giây sẽ đóng phiên; mất liveness 90 giây đóng dự phòng. Không cần router/BLE/Internet/PIN/BOOT duyệt ảnh. HTTP không có bảo vệ TLS. Hướng dẫn: [Setting](docs/WALLPAPER_SETUP.md).

## Build

Yêu cầu EIM đã cài ESP-IDF **5.5.5**. Chạy từ thư mục HisuOS:

```powershell
.\tools\build.ps1
```

Nếu vừa pull thay đổi `sdkconfig.defaults` nhưng máy còn `sdkconfig` cũ, refresh cấu hình một lần:

```powershell
.\tools\build.ps1 -RefreshConfig
```

Lệnh này lưu config cũ vào `.cache/sdkconfig-backups/`, rồi tạo lại config từ defaults. Các tùy chỉnh riêng cần được rà lại; build thường không tự reset cấu hình. Không erase NVS hoặc flash board.

Chạy host tests từ PowerShell thường:

```powershell
.\tools\test-ui.ps1
.\tools\test-voice.ps1
```

Helper tự tìm headers/libraries của MSVC x64 và Windows SDK đã cài, chạy bộ UI/Wi-Fi/pin và test lịch 2024–2099. Không cần mở Developer PowerShell.

Helper chỉ build, không flash. `sdkconfig.defaults` bật octal PSRAM và flash 16MB theo profile N16R8. Partition dành hai app slots 4MB cho OTA tương lai; media dùng 240 KiB đầu vùng storage cho hai records hình nền, phần còn lại reserve. Chưa mount filesystem.

## Điều Khiển

- Nhấn BOOT: mở menu, chọn mục, hoặc lưu giá trị.
- Volume up/down: di chuyển; trong editor số thì tăng/giảm. Âm lượng và độ sáng chỉnh mỗi lần 10%. Thời gian trên 60 giây hiển thị bằng phút.
- Editor Loa/Theme: Vol− đổi trạng thái (chấm rỗng/đặc), BOOT lưu, Vol+ trở lại không lưu. Chấm đặc biểu thị Loa bật hoặc Theme sáng.
- Thời gian chờ đặt thời gian tự tắt backlight. Tắt màn hình tắt ngay và về idle; nút tiếp theo chỉ đánh thức. Đóng menu trở về màn hình chờ.
- Menu cuộn từng dòng, giữ vị trí khi quay lại. Font UI 16px Semibold hỗ trợ tiếng Việt.
- Trong menu chính, giữ Vol+ khoảng 1,5 giây để đóng menu; nhả không phát thêm nhấn ngắn.
- Footer theo thứ tự nút Vol− / BOOT / Vol+. Trang Pin dùng Vol− để trở lại. Setting dùng ba gạch mở tuỳ chọn; BOOT ở QR/thông tin bật/tắt hotspot Setting; X quay về menu. Bấm Đã kết nối trên web ngắt kết nối và đóng phiên. QR tự đổi theo kết nối điện thoại. Gửi ảnh tự lưu; browser tự thu GIF động về tối đa 240×240 và 1 MiB trước khi gửi.
- Giữ BOOT 800ms: quay lại; trong editor là hủy; từ menu là về màn hình đen.
- Khi hiển thị phụ đề thật: up/down cuộn nội dung dài theo chiều dọc.
- Khi backlight đã tắt: nút đầu tiên chỉ đánh thức, không kích hoạt hành động.

Trong phiên hội thoại, nhấn BOOT ngắt lượt hiện tại và chuyển sang nghe; giữ BOOT 800ms đóng phiên. Ngoài phiên, BOOT giữ hành vi menu/đèn pin hiện tại. Đây chưa phải push-to-talk.

MultiNet cần flash cả partition table và model: `idf.py -p COM6 flash` (không dùng `app-flash`). Model nằm ở cuối flash tại `0xD00000`; hai slot ảnh vẫn giữ offset cũ ở đầu partition `storage`. Nói “Hi Su” sẽ đánh thức màn hình; nếu đã có Wi-Fi và Groq API key, board mở phiên để nghe các câu tiếp theo. Vào Setting hotspot → Thiết bị để lưu/xóa Groq và Gemini API key; trang web chỉ cho biết đã cấu hình hay chưa, không trả giá trị key. Gemini nhận phản hồi dạng streaming qua HTTPS, chưa truyền microphone liên tục như Xiaozhi. Đây là nhận dạng câu gọi bằng MultiNet, chưa phải model WakeNet được huấn luyện riêng.

## Setup Wi-Fi

Mở menu → Wi-Fi. Vol−/Vol+ di chuyển danh sách, BOOT chọn mạng hoặc Quét lại/Bật-tắt Wi-Fi/Trở lại. Giữ BOOT để về menu. Danh sách có icon mức sóng; SSID dài được rút gọn để vừa màn hình. Khi tắt Wi-Fi, chỉ còn Bật Wi-Fi và Trở lại.

Bàn phím 8×5: nhấn Vol− sang phải, Vol+ xuống hàng; giữ 1 giây bắt đầu lặp cùng hướng mỗi 180ms và vòng lại ở mép. Nhả sau giữ không phát thêm nhấn ngắn. BOOT chọn ký tự. `Aa` đổi chữ thường/chữ hoa/ký hiệu; `<` xóa, `OK` kết nối, `X` hủy. Phím `SP` trong bảng ký hiệu là dấu cách. Mật khẩu chỉ hiển thị dấu `*`, được xóa khỏi buffer UI khi gửi/hủy; firmware không log credential.

Hỗ trợ mạng 2.4GHz mở và WPA2/WPA3 Personal, mật khẩu ASCII 8–63 ký tự hoặc PSK hex 64 ký tự. Menu chưa có nhập SSID ẩn; trang BLE có thể gửi SSID trực tiếp. Chưa hỗ trợ Wi-Fi Enterprise hoặc captive portal. Credential dùng NVS thường của prototype, chưa bật encryption. Lưu tối đa 8 mạng sau khi nhận IP thành công; mạng dùng gần nhất đứng đầu và được thử kết nối khi khởi động. Khi đầy, lưu mạng mới thay mạng lâu nhất. Bản nâng cấp giữ mạng đã lưu theo định dạng cũ. Mạng đã lưu hiện tên màu xanh; BOOT dùng lại mật khẩu, lỗi xác thực rồi chọn lại sẽ mở bàn phím để sửa. Chưa kết nối thì không hiện tên mạng cũ ở dòng trạng thái.

Hướng dẫn kiểm thử: [UI Bring-up](docs/UI_BRINGUP.md). Nguồn font và license: [NOTICE](licenses/NOTICE.md).
