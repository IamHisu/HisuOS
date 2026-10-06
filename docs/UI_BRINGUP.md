# UI Bring-up

## Backlight GPIO20 Theo Muse (2026-10-06)

- GPIO20 vừa là backlight vừa là USB D+. Tắt USB Serial/JTAG và console phụ USB trong defaults lẫn cấu hình build đang dùng; giữ log UART0. Download mode ROM vẫn dùng được để recovery.
- Khi sáng bằng 0 hoặc reboot phần mềm, tháo LEDC khỏi GPIO20, kéo thấp bằng output và hold như Muse. Boot giữ hold đến khi truyền frame đầu xong; nối lại PWM rồi mới nhả hold.
- Về màn chính khi revision nền thay đổi sau khi lưu thành công, kể cả khi đang ở menu hoặc màn đã ngủ; không đóng phiên Setting.
- Chưa kiểm chứng power-cycle trên LCD thật. Nếu còn lóe trong giai đoạn ROM trước firmware, cần đo GPIO20 và xem pull-down phần cứng/bootloader.

## Thoát Setting Và Reboot (2026-10-06)

- Vol+ / giữ BOOT rời màn Setting mà không đóng phiên. Vol− chọn mục Dừng riêng; BOOT xác nhận mới đóng hotspot/web. Các điều kiện đóng từ web, mất AP và lease giữ nguyên.
- Shutdown handler của `esp_restart()` dừng PWM ở mức thấp và giữ GPIO backlight. Boot cấu hình chân thấp trước khi nhả hold; vẫn chỉ bật đèn sau khi frame đầu tiên đã được truyền xong.
- Reset vật lý, mất nguồn và watchdog không chạy shutdown handler; cần kiểm chứng trên LCD thật. Nếu lóe trước app_main, cần pull-down phần cứng hoặc xử lý bootloader.

## Overlay Icon Trong Suốt (2026-10-06)

- Bỏ nền đặc phía sau cụm Wi-Fi/pin và đồng hồ trên màn chờ. Container dùng LV_OPA_TRANSP; giữ nét icon và các vạch pin đặc.
- Áp dụng cả ảnh tự tải và GIF, không đổi vị trí, màu theme, phụ đề hoặc boot splash. Test source: `node tests/test_idle_overlay.mjs`; cần kiểm tra nền nhiều màu trên LCD thật.

## List Wi-Fi Và Vòng Đời Setting (2026-10-06)

- Thay combobox bằng dòng mạng ổn định, icon Lucide mức sóng, nhãn Đã lưu nhỏ màu riêng. Không dựng lại DOM mỗi lần poll.
- Chạm mạng đã lưu gửi saved join ngay, mạng mới có bảo mật mở dialog; mạng mở kết nối ngay. Hỗ trợ SSID ẩn và nhập lại mật khẩu sau lỗi.
- Bỏ deadline 5 phút. Dừng/X đóng ngay; ngắt AP >10 giây đóng. Pagehide keepalive báo leave có token/Origin/mã trang; grace 8 giây chống reload, lease 90 giây dự phòng browser chết.
- BOOT ở QR/thông tin bật/tắt hotspot Setting. Không đóng theo visibilitychange; browser bị đóng băng >90 giây vẫn có thể làm lease mất.
- Các mục 5 phút/BOOT reset bên dưới là lịch sử. Cần kiểm chứng AP chuyển kênh, captive browser/pagehide/background trên điện thoại thật.

## Setting HTTP Trong Hotspot (2026-10-06)

- Thay triển khai HTTPS bên dưới bằng một HTTP server cổng 80; control port 32768. Portal chỉ quản lý DNS và đăng ký redirect trên server chung.
- Giữ WPA2 ngẫu nhiên, cửa sổ vật lý 5 phút, token RAM, Host/Origin và đối chiếu client SoftAP. HTTP không có mã hóa/xác thực TLS; không mở ra LAN/Internet.
- Khi điện thoại nối hotspot, tự hiện QR URL HTTP; ngắt kết nối trả về QR Wi-Fi. BOOT ở màn QR đặt lại bộ đếm 5 phút, không đổi mật khẩu/token. Trang thông tin thay fingerprint bằng SSID, mật khẩu và IP.
- Captive GET/HEAD chuyển về trang HTTP; HEAD root kết thúc không vòng lặp. POST lạ trả 404 và đóng socket, không chuyển body tới API.
- Không bảo đảm điện thoại tự mở trang. Cần flash và thử thực tế; các ghi chú HTTPS/fingerprint bên dưới là lịch sử.
- Sửa kiểm tra socket: IDF HTTP dùng AF_INET6 khi `CONFIG_LWIP_IPV6=y`, nên không thể đọc bằng buffer `sockaddr_in` và chỉ nhận AF_INET. Dùng `sockaddr_storage`, nhận IPv4 hoặc IPv4-mapped IPv6, giữ đối chiếu IP AP/subnet/client thật. Đây là lỗi có thể gây chặn nhầm thành Setting closed.
- Phân biệt phản hồi phiên hết hạn với client không hợp lệ, thêm log lý do và no-store. Log parser 400 riêng không đủ xác định nguồn request hoặc nguyên nhân đóng phiên.

## Setting Không Cần Wi-Fi Nhà

- Log lỗi `error in creating ctrl socket (112)` là `EADDRINUSE`: captive HTTP bị trùng cổng UDP control 32769 với mặc định của HTTPS IDF 5.5.5.
- Gán rõ hai cổng control HTTP 32768 / HTTPS 32769 trong `hisu_setup_ports.h`, kèm static assertion và test source regression.
- Đây không phải điều kiện phải nối Wi-Fi nhà: hotspot trong log đã lên IP 192.168.4.1 trước khi lỗi server. Flow vẫn là Setting → hotspot QR → cấu hình Wi-Fi nhà trên web.
- UI hiện mã lỗi startup và log không chứa credential, giúp phân biệt lỗi radio/server. Cần thử trên board với không mạng đã lưu và station tắt.

## Không Chớp Nền Trước Splash

- Giữ GPIO đèn nền tắt ngay đầu application, trước settings/network/media.
- LCD init ở trạng thái display-off. UI vẽ toàn bộ splash bằng `lv_refr_now` trước khi bật panel và đèn nền.
- Lệnh display-on qua SPI của IDF chờ các DMA pixel đã xếp hàng hoàn tất, kể cả chunk cuối; không dùng delay cố định để đoán.
- Nếu frame đầu hoặc bật đèn nền lỗi, giữ màn tối và báo lỗi startup. Thời gian splash 3 giây bắt đầu sau khi bật màn.
- Host test kiểm tra thứ tự source và asset GIF; cần thử reset mềm và ngắt/cấp nguồn trên board để xác minh hết chớp.

## Setting QR — 2026-10-06

- Hotspot dùng mật khẩu ngẫu nhiên 16 ký tự mỗi phiên; bỏ file mật khẩu cố định. SSID ổn định theo MAC AP.
- QR Wi-Fi là màn chính, BOOT đổi sang QR URL. Captive portal DNS/HTTP hỗ trợ mở HTTPS tự động; không bảo đảm trên mọi máy.
- Không PIN riêng hoặc BOOT duyệt ảnh; web tự lấy token, gửi ảnh tự lưu an toàn hai slot. Giữ fingerprint, kiểm tra Host/Origin/AP và cửa sổ 5 phút.
- Người có QR hotspot có quyền chỉnh Setting trong phiên. X/hết hạn/logout đóng DNS/HTTP/HTTPS/AP. Chưa flash hoặc thử captive portal trên điện thoại thật.

Các mục bên dưới ghi lịch sử bring-up; mô tả mã riêng/BOOT duyệt ảnh/Bluetooth cũ không còn là flow hiện tại. Checklist hiện hành: [Setting](WALLPAPER_SETUP.md).

## Boot GIF — 2026-10-06

- Sửa nguyên nhân GIF không tải: AnimatedGIF trong LVGL giới hạn MAX_WIDTH=480 nhưng bản gốc rộng 498 px. Thu phóng widget xảy ra sau decode nên không giải quyết được giới hạn.
- Giữ bản gốc `boot_windows_xp_source.gif`; Sharp tạo asset 240x240, giữ đủ 18 frame và delay gốc, letterbox đen. Chỉ nhúng bản chuẩn hoá vào firmware, không scale lúc chạy.
- Splash 3 giây không có clock, icon Wi-Fi/pin, header hoặc footer. Nếu GIF lỗi, giữ nền đen đủ thời gian thay vì render idle sớm.
- `tests/test_boot_gif.mjs` kiểm tra kích thước theo giới hạn decoder, timing, đủ frame không trống, frame khác nhau và letterbox RGB565 đen. Đây là kiểm tra tài nguyên trên host, chưa phải test LCD/decoder runtime trên board.
- Build IDF 5.5.5 qua, binary `0x14e1f0`, app slot còn 67% trống. Chưa flash; cần nạp bản mới và reset/power-cycle để kiểm tra.

## Setting Hotspot — 2026-10-06

- Menu 11 mục; Setting thay Hình nền, bỏ Bluetooth và icon BLE; application không start BLE host.
- Network worker quản lý AP WPA2 có mật khẩu ngẫu nhiên và AP+STA. Trang HTTPS chỉ dùng địa chỉ AP, có tab Wi-Fi/Hình nền/Thiết bị; không cần router/Internet/BLE.
- Settings/Wi-Fi/time API có token, Host/Origin/subnet/socket destination checks, body/nesting/schema limits. Gửi ảnh vẫn cần BOOT duyệt đúng preview ID.
- UI model 19/19, calendar và media model qua. Playwright API mô phỏng trên ba viewport qua: tabs/keyboard, scan/saved join, SSID injection safety, brightness/time và gửi ảnh/expiry. Không phải kiểm thử TLS/radio backend thật.
- Chưa flash, commit hoặc push. Cần thử AP khi station tắt, join/scan đổi kênh, mật khẩu/fingerprint trên điện thoại, đóng/hết hạn và flash persistence trên board.
- Hướng dẫn hiện hành: [Setting](WALLPAPER_SETUP.md). Các mục BLE/upload cùng LAN bên dưới là nhật ký các phiên bản trước.

## Hình Nền Nội Bộ — 2026-10-06

- Thêm `hisu_media`: HTTPS local, mã dùng một lần, cửa sổ 5 phút, token ngắn hạn, giới hạn attempts/body/socket và BOOT xác nhận đúng ID ảnh đã xem.
- Ảnh tĩnh RGB565 240×240 override GIF khi idle; menu/phụ đề giữ nguyên. UI dùng pixel copy riêng; service sở hữu network/storage, hai slots CRC-checked và đọc lại flash trước publish.
- Build ESP-IDF 5.5.5 thành công: `HisuOS.bin` `0x179710` bytes, app slot `0x400000`, còn 63% trống.
- UI model 18/18, lịch, media CRC/header/PIN lockout/stale approval và JS encoder/crop qua. Hai test BLE giả lập cũ vẫn qua.
- Playwright desktop/mobile/small-mobile: 1280×800, 390×844, 320×720; ảnh PNG thật, canvas pixels/màu RGB565 đúng, không overflow/overlap, mã sai, chờ xác nhận, logout/hết hạn. API mô phỏng, không phải TLS server thật.
- Chưa flash hoặc kiểm tra TLS, certificate UX, flash power-loss, memory/stack/latency trên board. Chưa commit/push. Hướng dẫn và security limitations theo [Wallpaper Setup](WALLPAPER_SETUP.md).

## Sửa Lỗi Sau Pull — 2026-10-06

- Sửa chọn đầu tháng âm bằng khoảng ngày sóc thực tế, tránh ngày âm bằng 0 tại 07/05/2054 và 09/04/2062.
- Thêm regression tests cho hai ngày đó; test lịch kiểm tra invariant và chuyển đổi ngày trong toàn bộ 2024–2099 (27.759 ngày), cùng các ngày tham chiếu có sẵn. Đây không phải đối chiếu mọi ngày với một nguồn lịch độc lập.
- Sửa fixture menu để mục được chọn nằm trong năm dòng hiển thị; bộ UI/Wi-Fi/pin 17/17 qua.
- Helper `test-ui.ps1` cung cấp MSVC include paths, tự tìm Windows SDK và chạy cả test lịch; chạy được từ PowerShell thường, không chỉnh môi trường toàn máy.
- Hai test web BLE (payload và mock GATT flow) qua; chưa xác minh radio/pairing thật.
- `build.ps1 -RefreshConfig` là tùy chọn rõ ràng để lưu config cũ vào `.cache/sdkconfig-backups/` và tạo lại từ defaults sau pull. Không reset config trong build thường và không erase dữ liệu trên board.
- Build ESP-IDF 5.5.5 thành công sau refresh và khi chạy lại `build.ps1` thường. Binary `build/HisuOS.bin`: `0x15a9a0` bytes; app slot `0x400000`, còn 66% trống.
- Chưa flash hoặc kiểm thử trên board; chưa commit/push các bản sửa này.

## Kết Quả Kiểm Tra UI Ban Đầu — 2026-10-05

- Build ESP-IDF 5.5.5, ESP32-S3, LVGL 9.4.0: thành công.
- Binary `build/HisuOS.bin`: `0x84c00` bytes; app slot `0x400000`, còn 87% trống.
- Host model C tests: 10/10 qua, chạy bằng MSVC DLL và Python ctypes.
- Chưa flash; chưa kiểm tra rendering, PWM, nút hoặc NVS persistence trên board thật.
- Chưa thực hiện screenshot/visual simulation; host tests chỉ kiểm tra logic và UTF-8.
- Chưa commit/push các thay đổi UI.

## Phạm Vi

Bản thêm Wi-Fi/pin trước đây đã build và link thành công trên ESP-IDF 5.5.5, tạo `HisuOS.bin` kích thước `0x116d50`, còn 73% app slot trống. Đây là số liệu lịch sử, không phải binary hiện tại. Bàn phím kiểm tra đủ 95 ký tự ASCII in được, mask và nhãn phím vừa 240×240. Đây chưa phải kết quả kết nối/ADC trên board; kết quả host tests hiện tại ở phần Sửa Lỗi Sau Pull.

Firmware dùng ESP-IDF 5.5.5 và LVGL 9.4.0. Đã thêm service Wi-Fi và battery ADC theo profile Muse ZHENGCHEN; chưa kiểm chứng chúng trên board thật. Đã tích hợp BLE setup Wi-Fi và gửi giờ, chưa kiểm chứng ghép nối trên board. Chưa tích hợp microphone, speaker hoặc AI. Menu chỉ lưu volume/speaker preferences, không phát âm thanh.

Board driver không include LVGL; UI sở hữu display buffer, render và input loop. LCD DMA callback chỉ báo `lv_display_flush_ready`, không thay đổi LVGL objects. Render buffer internal DMA 40 dòng, RGB565. SPI mode 3/80MHz, RGB/inversion theo cấu hình FriendOS trước; cần kiểm tra lại trên board cho bản driver mới.

Brightness có live hardware preview; Theme có live UI preview sáng/tối. BOOT lưu, giữ BOOT hủy; NVS chỉ commit lúc xác nhận, không mỗi lần chỉnh. NVS lỗi không bị erase tự động.

## Host Tests

```powershell
.\tools\test-ui.ps1
```

Helper dùng MSVC x64 đã cài trên máy để build model C thành DLL Windows không phụ thuộc CRT, rồi chạy Python EIM qua ctypes. Test lịch được build thành executable với static CRT và Windows SDK UCRT. Không cài package hoặc tool mới. Các test kiểm tra menu, editor bounds, timeout presets, preview, UTF-8, Wi-Fi password, power model và lịch. Không thay thế visual/hardware tests.

## Build

```powershell
.\tools\build.ps1
```

Không flash trong build helper. Flash cần được thực hiện riêng sau khi xác nhận đúng port và board; không erase toàn flash/NVS để thử UI.

## Kiểm Thử Trên Board

- Khi chưa có ảnh custom, boot hiển thị GIF mèo 240×240, lặp đủ tám frame; vùng trong suốt theo nền trắng/đen của theme đã lưu. Khi có ảnh custom, kiểm tra boot dùng ảnh đã lưu và khôi phục nền mặc định.
- BOOT mở Hisu, up/down chạy đủ 13 mục và wrap đúng.
- Trong menu chính, giữ Vol+ khoảng 1,5 giây để về màn hình chờ; nhả nút không di chuyển thêm. Nhấn nhanh vẫn di chuyển một mục; mở lại menu giữ vị trí. Kiểm tra thao tác giữ nút ở lịch và bàn phím Wi-Fi không thay đổi.
- Theme: up/down chuyển Sáng/Tối, chữ và nền đổi ngay; giữ BOOT hủy, BOOT lưu. Reboot kiểm tra theme và nền GIF giữ đúng; mở/đóng menu nhiều lần không có bóng frame cũ.
- Không có chữ bị cắt/đè trong header, row, value hoặc footer 240x240.
- Font UI là Hisu Semibold 16px (Source Code Pro Semibold), đủ dấu tiếng Việt. Kiểm tra nét chữ ở cả theme sáng/tối.
- Từ mục đầu đi xuống: sau năm dòng, cửa sổ danh sách dịch từng dòng; đi lên và quay lại từ editor/info giữ vị trí. Chỉ wrap khi đi qua đầu/cuối danh sách.
- Footer khớp thứ tự vật lý Vol− / BOOT / Vol+: menu v / Chọn / ^; editor − / Lưu / +; trang Pin: Vol− trở lại; Bluetooth: BOOT đổi Tắt/1/5/10/30 phút, Vol− xác nhận mở/gia hạn hoặc tắt, Vol+ trở lại. Chấm tròn BLE biểu thị trạng thái mở. Wi-Fi có danh sách riêng, giữ BOOT về menu.
- Brightness 10–100%, preview đúng, giữ BOOT hủy và trả về giá trị đã lưu.
- BOOT xác nhận brightness/volume/speaker/timeout; reboot giữ giá trị.
- Timeout chỉ tắt backlight khi idle, không khi editor/preview hoặc active subtitle.
- Nút đầu tiên khi backlight tắt chỉ wake, nút tiếp theo mới thao tác.
- Preview phụ đề có đủ dấu tiếng Việt, text dài đọc được, thoát về menu.
- Phụ đề từ API wrap theo chiều rộng, up/down cuộn dọc nếu dài; không chạy chữ ngang.
- Bluetooth dùng service GATT riêng với mã ghép nối 6 số; kiểm tra theo docs/BLE_SETUP.md. Wi-Fi/pin có snapshot thật; lỗi khởi tạo phải hiển thị không khả dụng thay vì số đo giả.
- Giữ BOOT không phát thêm click khi nhả; debounce không nhảy hai mục.
- Theo dõi LCD transfer errors, heap, stack và watchdog qua nhiều vòng điều hướng.

## Kiểm Thử Wi-Fi Và Pin

- Quét mạng khi chưa có credential; khi có mạng đã lưu thì nhóm đã lưu đứng trước nhóm chưa lưu, mỗi nhóm RSSI giảm dần. Mesh không lặp SSID, danh sách có Quét lại/Bật-tắt Wi-Fi/Trở lại. Scan không làm treo nút.
- Mạng mở: BOOT kết nối trực tiếp. WPA2/WPA3 Personal: bàn phím 8×5, nhấn Vol− sang phải/Vol+ xuống, giữ 1 giây lặp cùng hướng mỗi 180ms, BOOT thêm ký tự/chọn chức năng. Kiểm tra nhả sau giữ không đi thêm cột, quấn mép bàn phím, giữ nhiều giây lặp liên tục và vòng lại và điều khiển ở menu khác không đổi. Thử chữ hoa, số, ký hiệu và SP (space); chỉ thấy mật khẩu dạng sao, không xuất hiện trong log.
- Nhập sai mật khẩu rồi nhập đúng; lỗi auth dừng retry. Mất AP tạm thời: reconnect có backoff, tối đa sáu lần; lỗi/timeouts không crash UI. Tắt Wi-Fi phải dừng radio và retry, dù có scan/join chờ.
- Kết nối rồi tắt modem: bấm Quét lại trong lúc reconnect, phải quét được mạng khác và đưa con trỏ về mạng mạnh nhất. Quét tạm hủy lần kết nối đang chạy; retry tiếp sau 10 giây. Thử quét lặp, quét khi vẫn có kết nối và bật lại modem; không mất credential đã lưu. Kiểm tra icon Wi-Fi ở cả hai theme: các vòng có nét 1 px, tách nhau và tách chấm giữa. Các trường hợp này cần chạy trên board thật.
- Sau nhận IP: hiển thị IP/RSSI; reboot tự nối cùng mạng nếu radio bật. Chọn mạng mới nhưng kết nối lỗi không ghi đè mạng đã lưu. Kiểm tra trạng thái radio giữ qua reboot.
- Pin: đối chiếu raw ADC GPIO8 với board, mức ~0–100% và bảng reference; kiểm tra GPIO9 khi cắm/rút nguồn và pin đầy. Đây là ước tính từ ADC, không phải đo điện áp mV hoặc xác nhận đã hiệu chuẩn.
- Thống kê Pin: rút sạc bắt đầu chu kỳ, màn hình sáng 60 giây rồi tắt 60 giây phải cộng riêng MH sáng/Sleep và khoảng 120 giây Dùng pin. Cắm sạc dừng cộng, rút lại bắt đầu chu kỳ mới. Sau checkpoint 1 phút rồi reboot khi vẫn dùng pin, thống kê phải tiếp tục; mất nguồn đột ngột có thể mất tối đa khoảng 1 phút chưa lưu. Không cộng thời gian máy đã tắt nguồn.
- Thời gian còn lại: chưa đủ 10 phút đo hoặc chưa tụt 5 điểm phần trăm phải hiện Đang đo; có nguồn ngoài hiện Cắm sạc. Khởi động khi pin thấp vẫn tính từ mức đo thực tế, không giả định bắt đầu ở 100%. Reboot giữ thống kê nhưng thu thập slope mới; ADC tăng trên baseline hơn 3 điểm phải đo lại. Đối chiếu ước tính với chu kỳ xả thật có cả màn hình sáng và tắt; chưa chạy kiểm chứng trên board.
- Lưu nhiều Wi-Fi: nâng cấp từ một credential cũ, nối thêm mạng B rồi reboot; cả A/B phải còn và hiện xanh trong danh sách. Chọn A dùng mật khẩu cũ, nhập sai mạng C không được thay credential nào. Đổi mật khẩu A: lỗi auth rồi chọn lại để sửa, chỉ cập nhật sau nhận IP. Lưu đủ 8 mạng rồi mạng thứ 9 chỉ loại mạng lâu nhất; chọn lại một mạng đã lưu đưa nó về đầu mà không tạo bản sao. Chưa chạy các ca NVS này trên board.
- Tắt màn hình: chọn mục mới phải về idle, backlight về 0 và GIF pause. Vol−, BOOT hoặc Vol+ đầu tiên chỉ đánh thức; Đóng menu chỉ về idle, không tắt ngay. Thời gian chờ vẫn giữ các preset tự tắt cũ.
- Chạy lâu khi GIF, Wi-Fi và ADC cùng hoạt động; theo dõi heap/stack/watchdog, đổi theme và menu nhiều lần. Chưa tuyên bố đã qua hardware/soak tests.

Host suite bổ sung password bounds/hex PSK và bảng pin (biên, điểm hiệu chuẩn, tính đơn điệu). Chạy bằng `tools/test-ui.ps1`; helper đã được sửa để chạy với MSVC/Windows SDK trên máy, kết quả hiện tại ở phần Sửa Lỗi Sau Pull.

## API Phụ Đề

Worker gọi `hisu_ui_post_status(status, text)`, không gọi LVGL. Text UTF-8 tối đa 255 bytes, không cắt giữa codepoint; input không hợp lệ trả `ESP_ERR_INVALID_ARG`. Queue tám event hữu hạn, đầy trả `ESP_ERR_TIMEOUT`; caller phải xử lý lỗi/backpressure.

UI nhận full text snapshot, không nhận raw token delta. Controller tương lai tích lũy token với budget riêng rồi gửi snapshot. Menu che tạm phụ đề, không xóa snapshot. Trạng thái idle hiển thị GIF theo theme.

Session cancellation và chống stale events thuộc conversation controller tương lai; UI hiện không tự nhận diện session. Phụ đề preview là dữ liệu kiểm tra, không phải phản hồi AI thật.

## Cập nhật điều khiển Setting và đèn pin

- BOOT ở QR/thông tin bật/tắt hotspot; Vol− đổi trang, Vol+ hoặc giữ BOOT về menu. BOOT ở xác nhận khôi phục vẫn khôi phục nền.
- Đèn pin bỏ khỏi menu; giữ BOOT 1,5 giây ở màn chờ để mở, Vol+ trở về màn chờ.
- Bootloader kéo GPIO20 xuống thấp, chờ 20 ms cho đèn nền xả rồi mới giữ LCD reset ở GPIO45; trạng thái được áp lại sau khởi tạo bootloader. App chỉ nhả reset khi driver LCD sẵn sàng và chỉ bật đèn sau frame đầu. GPIO20 dùng chung USB D+, nên ROM có thể tạo một chớp rất ngắn trước khi bootloader chạy; loại bỏ hoàn toàn nhịp này cần pull-down phần cứng hoặc tắt USB bằng eFuse (không thực hiện vì eFuse không đảo ngược). PWM không nối lại ở mỗi lần chỉnh sáng.
