# HisuOS

Setting → Thiết bị có bốn key Gemini riêng: Chat chính, một Chat dự phòng,
Reader và Translate. Chat chỉ tự chuyển dự phòng khi có lỗi xác thực hoặc hết
hạn mức được nhận diện, không mượn key Reader/Translate. Key Chat đã lưu vẫn
được giữ. Chi tiết: [Phân luồng key Gemini](docs/GEMINI_KEYS.md).

Firmware ESP32-S3 cho ZHENGCHEN 1.54 TFT WiFi, xây dựng lại theo [System Design](docs/SYSTEM_DESIGN.md).

Nguồn và đánh giá tái sử dụng: [Tài liệu tham khảo](docs/REFERENCES.md), gồm XiaoClaw (voice Xiaozhi + agent MimiClaw), điều kiện tương thích và các giới hạn chưa kiểm chứng.

## Hiện Trạng

Bước đầu là UI LVGL 9.4.0: GIF mèo 240×240 ở màn hình chờ, menu Hisu, editor cấu hình và API phụ đề tiếng Việt.

Màn hình chờ có icon Wi-Fi theo mức sóng và pin nằm ngang chia 5 vạch ở góc trên bên phải, không hiện số phần trăm. Khi GPIO9 báo nguồn ngoài, pin hiện tia sét thay các vạch; đây không phải tín hiệu xác nhận sạc đầy. Pin còn 20% trở xuống chuyển đỏ khi không cắm sạc. Tắt Wi-Fi sẽ ẩn icon; bật nhưng chưa kết nối hiển thị icon mờ. Danh sách mạng dùng cùng icon, với các vòng ngoài mờ theo tín hiệu. Cụm trạng thái cập nhật mỗi giây mà không khởi động lại GIF.

Màn hình chờ chỉ dùng ảnh/GIF tải lên từ Setting, không nhúng hình nền mặc định. Chưa có ảnh hoặc ảnh lỗi thì dùng nền đen/trắng theo theme, vẫn giữ đồng hồ và biểu tượng trạng thái. GIF đã tải lên lặp khi idle và tạm dừng khi backlight tắt. Menu và phụ đề thay thế GIF. LVGL dùng heap ESP-IDF (có PSRAM) để chứa framebuffer GIF thay cho pool 96 KiB. Lựa chọn hình nền mặc định từ firmware cũ được bỏ qua, không xóa ảnh người dùng.

Mục Theme chuyển Sáng/Tối bằng Vol+/Vol− và preview ngay trên editor. BOOT lưu vào NVS; giữ BOOT hủy. Mặc định là Tối. Theme áp dụng cho menu, phụ đề và nền trống khi không có ảnh hợp lệ; không đổi màu ảnh đã tải lên.

- Độ sáng: PWM thật, preview khi chỉnh, lưu NVS khi xác nhận.
- Hội thoại: gọi “Hi Su” mở Gemini Live qua WSS; mic gửi PCM 16kHz trong lúc thu, loa phát PCM 24kHz từng phần. Chỉ cần Gemini API key. Board lọc mở lượt bằng VAD + năng lượng, kết thúc sau 700ms im lặng; nghe tiếp tối đa 60 giây sau loa. BOOT/ngắt bằng câu gọi dừng audio cũ và nghe lại trong cùng phiên, giữ ngữ cảnh; giữ BOOT hoặc nói tạm biệt đóng phiên. Chưa có chống vọng/ngắt lời tự do như điện thoại; cần kiểm chứng API/độ trễ/tiếng nền trên board. Xem [Hội thoại](docs/VOICE_CONVERSATION.md).
- Timeout: tắt backlight khi idle, không deep sleep.
- Wi-Fi: quét/chọn mạng, nhập mật khẩu bằng ba nút, RSSI/IP, bật/tắt radio; lưu mạng kết nối thành công vào NVS và tự quét lại khi mất kết nối.
- Pin: phần trăm ước tính từ ADC GPIO8 theo profile Muse ZHENGCHEN; hiển thị raw ADC và tín hiệu nguồn ngoài GPIO9. Cần kiểm chứng calibration/polarity trên board thật.
- Trang Pin có thời gian còn dùng được ước tính từ tốc độ tụt pin đo được: chờ ổn định 1 phút, cần ít nhất 10 phút và tụt 5 điểm phần trăm, nếu chưa đủ thì hiện Đang đo. Thống kê màn hình sáng, dùng pin và sleep tính trong lúc không có nguồn ngoài, theo chu kỳ từ lần rút sạc gần nhất; giữ qua reboot bằng checkpoint NVS mỗi phút và khi đổi nguồn. Rút sạc bắt đầu chu kỳ mới; cắm sạc tạm dừng bộ đếm. Reboot giữ thống kê nhưng đo lại tốc độ tụt pin. Sleep ở đây là backlight tắt, chưa phải deep sleep của ESP32. Ước tính còn lại phụ thuộc bảng ADC và mức sử dụng, chưa được kiểm chứng trên board.
- Bluetooth tạm ngưng: không có mục Bluetooth/icon BLE, không khởi động BLE host. Mã nguồn và trang BLE cũ giữ lại để tham khảo, không dùng cho setup bản này.
- Lịch: lịch tháng dương, ngày âm Việt Nam UTC+7 và tháng nhuận (2024–2099). Vol− ngày trước, Vol+ ngày sau; giữ 1 giây lặp mỗi 180ms, tự chuyển tháng/năm. BOOT về hôm nay, giữ BOOT trở lại menu. Cần giờ đồng bộ qua Wi-Fi hoặc trang Setting; sau đó xem offline được khi thiết bị vẫn có giờ.
- Đèn pin: giữ BOOT 1,5 giây ở màn hình chờ để mở màn trắng, backlight 100%; Vol− bật/tắt, BOOT chuyển Sáng → Chớp → SOS → Sáng, Vol+ hoặc giữ BOOT về màn hình chờ và khôi phục độ sáng đã lưu. Chớp 500ms sáng/500ms tắt; SOS `... --- ...` với đơn vị 200ms, lặp lại. Không ghi lại setting độ sáng, không tự sleep khi đang ở Đèn pin.
- Phụ đề: API queue nối output audio transcription từ Gemini Live.
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
.\tools\test-network.ps1
.\tools\test-voice.ps1
.\tools\test-memory.ps1
.\tools\test-alert.ps1
```

Helper tự tìm headers/libraries của MSVC x64 và Windows SDK đã cài, chạy bộ UI/Wi-Fi/pin và test lịch 2024–2099. Không cần mở Developer PowerShell.

Helper chỉ build, không flash. `sdkconfig.defaults` bật octal PSRAM và flash 16MB theo profile N16R8. Partition dành hai app slots 4MB cho OTA tương lai; storage bắt đầu bằng hai slot ảnh/GIF, tiếp theo là hai slot trí nhớ 8 KiB. Chưa mount filesystem.

## Báo Pin Offline

Tạo sẵn bốn câu Leda báo pin và một câu nhắc lịch trên máy tính bằng `python tools/prepare-battery-audio.py`, dùng biến môi trường `GEMINI_DEV_API_KEY` hoặc `GEMINI_API_KEY`. Sau đó chạy `idf.py reconfigure build` và `idf.py flash` để nạp kèm vùng âm thanh riêng, không đụng ảnh nền hay trí nhớ. Setting chỉ hiển thị số câu sẵn sàng, không còn nút chuẩn bị. Board phát cảnh báo hoàn toàn offline ở mốc 15% và 5%, mỗi mốc một lần trong chu kỳ sạc; hai câu mỗi mức để đỡ lặp. Tôn trọng tắt loa/âm lượng 0, không chen ngang hội thoại/nhạc và không bật loa ở mức pin ≤2%. Chi tiết, giới hạn và cách thử: [Offline Battery Audio](docs/OFFLINE_BATTERY_AUDIO.md).

## Lịch và Nhắc Hẹn

Đặt, xem, sửa và hủy lịch một lần qua AI; tối đa 32 mục. Menu → Lịch đánh dấu ngày có hẹn; chọn ngày rồi bấm BOOT để xem, lên/xuống để cuộn, BACK để trở lại. Khi rảnh, có mạng thì thử đọc đúng nội dung bằng Leda; nếu lỗi hoặc offline thì dùng câu Leda sẵn và hiện nội dung trên màn hình. Thay đổi lưu flash khi kết thúc phiên, không tắt nguồn ngay sau khi đặt. Board cần còn bật và giờ đã đồng bộ; chưa có lặp lịch, báo thức tự bật nguồn hay báo lại. Chi tiết và cách thử: [Schedules](docs/SCHEDULES.md).

## Trí Nhớ

Gemini tự chọn lọc tối đa 24 thông tin ngắn về sở thích, thói quen và việc đang làm; không cần nói đúng câu “nhớ giúp mình”. Ghi nhớ lâu dài được lưu khi phiên trò chuyện/nhạc kết thúc, không ghi flash ngay trong lúc nói. Tắt nguồn trước khi lưu xong có thể mất thay đổi mới nhất. Ngữ cảnh trò chuyện gần đây được giữ có giới hạn giữa các phiên trong RAM, không còn sau reboot.

Khi đủ 24 mục, Gemini xem danh sách và ưu tiên gộp/cập nhật cùng chủ đề hoặc thay mục lỗi thời, ít hữu ích hơn. Không mặc định xóa mục cũ nhất; nếu mục mới kém hữu ích hơn các mục đã có thì không lưu. Firmware kiểm tra mục được chọn trước khi thay nguyên tử, không xóa trước rồi thêm sau.

Web Setting → Thiết bị → Trí nhớ cho xem/xóa từng mục, xóa toàn bộ, tắt tự động ghi nhớ và xóa ngữ cảnh gần đây. Bộ lọc nội dung nhạy cảm dựa vào chỉ dẫn cho AI, không phải bảo đảm tuyệt đối; không cung cấp bí mật vì flash chưa mã hóa. Chi tiết và cách thử: [Memory](docs/MEMORY.md).

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

MultiNet cần flash cả partition table và model bằng `idf.py -p COM6 flash` (không dùng `app-flash`). Model tại `0xD00000`. Setting → Thiết bị lưu Gemini API key để dùng Live; Groq key cũ không dùng trong hội thoại này. Model `gemini-3.8-live` cần quyền/quota phù hợp. Đây là nhận câu gọi bằng MultiNet, chưa có WakeNet riêng hoặc AEC.

## Setup Wi-Fi

Mật khẩu mới nhập sai không chặn mật khẩu đã lưu trước đó. Mật khẩu đã lưu chỉ
bị tạm chặn sau 3 lỗi xác thực liên tiếp và được thử lại sau 10 phút; timeout bắt
tay đơn lẻ chưa được xem là sai mật khẩu. BACK hủy lần kết nối đang chạy; không
xóa mạng đã lưu hay ngắt một kết nối đã thành công. Không thấy AP thì kết thúc
lần thử ngay. Log sau quét có mức stack thấp nhất để kiểm tra trên board.

Mở menu → Wi-Fi. Vol−/Vol+ di chuyển danh sách, BOOT chọn mạng hoặc Quét lại/Bật-tắt Wi-Fi/Trở lại. Giữ BOOT để về menu. Danh sách có icon mức sóng; SSID dài được rút gọn để vừa màn hình. Khi tắt Wi-Fi, chỉ còn Bật Wi-Fi và Trở lại.

Bàn phím 8×5: nhấn Vol− sang phải, Vol+ xuống hàng; giữ 1 giây bắt đầu lặp cùng hướng mỗi 180ms và vòng lại ở mép. Nhả sau giữ không phát thêm nhấn ngắn. BOOT chọn ký tự. `Aa` đổi chữ thường/chữ hoa/ký hiệu; `<` xóa, `OK` kết nối, `X` hủy. Phím `SP` trong bảng ký hiệu là dấu cách. Mật khẩu chỉ hiển thị dấu `*`, được xóa khỏi buffer UI khi gửi/hủy; firmware không log credential.

Hỗ trợ mạng 2.4GHz mở và WPA2/WPA3 Personal, mật khẩu ASCII 8–63 ký tự hoặc PSK hex 64 ký tự. Menu chưa có nhập SSID ẩn; mục Thêm mạng trên web có thể gửi SSID trực tiếp. Chưa hỗ trợ Wi-Fi Enterprise hoặc tự đăng nhập captive portal. Credential dùng NVS thường của prototype, chưa bật encryption. Lưu tối đa 8 mạng sau khi nhận IP thành công. Khi khởi động, thử mạng đã lưu thấy được theo mức sóng rồi thử các SSID đã lưu không thấy khi quét, gồm mạng ẩn. Khi đầy, lưu mạng mới thay mạng lâu nhất. Bản nâng cấp giữ mạng đã lưu theo định dạng cũ. Mạng đã lưu hiện tên màu xanh; BOOT dùng lại mật khẩu, lỗi xác thực rồi chọn lại sẽ mở bàn phím để sửa, kể cả sau khi quét lại. Chưa kết nối thì không hiện tên mạng cũ ở dòng trạng thái.

Nhận IP chỉ xác nhận đã nối Wi-Fi. Hisu kiểm tra Internet ngay sau đó và mỗi 30 giây bằng hai điểm kiểm tra độc lập: HTTPS `connectivitycheck.gstatic.com/generate_204` và HTTP `www.msftconnecttest.com/connecttest.txt` (dự phòng, đối chiếu nội dung). Mỗi yêu cầu có timeout 4 giây và không theo chuyển hướng. Trang đăng nhập/chuyển hướng được báo là mạng yêu cầu đăng nhập; lỗi truy cập được báo là Wi-Fi chưa có Internet. Đây là phát hiện qua phản hồi mạng, không phải trình duyệt đăng nhập. Icon Wi-Fi ở màn hình chờ, menu và mạng hiện tại có dấu `!` vàng khi cần đăng nhập hoặc chưa có Internet; dấu tự mất khi kiểm tra thành công. Mạng tự chọn có thể chuyển sang mạng đã lưu khác sau hai lần kiểm tra lỗi liên tiếp, chờ 120 giây trước khi thử lại mạng cũ. Nếu không có mạng khác hoặc mạng đang được người dùng chọn thủ công, giữ kết nối Wi-Fi hiện tại. AI báo lỗi mạng rõ ràng khi chưa có Internet.

Hướng dẫn kiểm thử: [UI Bring-up](docs/UI_BRINGUP.md). Nguồn font và license: [NOTICE](licenses/NOTICE.md).
