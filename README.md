# HisuOS

Firmware ESP32-S3 cho ZHENGCHEN 1.54 TFT WiFi, xây dựng lại theo [System Design](docs/SYSTEM_DESIGN.md).

## Hiện Trạng

Bước đầu là UI LVGL 9.4.0: GIF mèo 240×240 ở màn hình chờ, menu Hisu, editor cấu hình và API phụ đề tiếng Việt.

Màn hình chờ có icon Wi-Fi theo mức sóng và pin nằm ngang chia 5 vạch ở góc trên bên phải, không hiện số phần trăm. Khi GPIO9 báo nguồn ngoài, pin hiện tia sét thay các vạch; đây không phải tín hiệu xác nhận sạc đầy. Pin còn 20% trở xuống chuyển đỏ khi không cắm sạc. Tắt Wi-Fi sẽ ẩn icon; bật nhưng chưa kết nối hiển thị icon mờ. Danh sách mạng dùng cùng icon, với các vòng ngoài mờ theo tín hiệu. Cụm trạng thái cập nhật mỗi giây mà không khởi động lại GIF.

GIF nguồn trong suốt là `components/hisu_ui/assets/idle_cat.gif`; firmware nhúng hai bản `idle_cat_dark.gif` và `idle_cat_light.gif`, ghép vùng trong suốt lên màu nền đen/trắng tương ứng. GIF lặp khi idle và tạm dừng khi backlight tắt. Menu và phụ đề thay thế GIF. LVGL dùng heap ESP-IDF (có PSRAM) để chứa framebuffer GIF thay cho pool 96 KiB.

Mục Theme chuyển Sáng/Tối bằng Vol+/Vol− và preview ngay trên editor. BOOT lưu vào NVS; giữ BOOT hủy. Mặc định là Tối. Theme áp dụng cho menu, phụ đề và nền GIF.

- Độ sáng: PWM thật, preview khi chỉnh, lưu NVS khi xác nhận.
- Âm lượng/bật loa: chỉ lưu cấu hình; chưa nối audio service.
- Timeout: tắt backlight khi idle, không deep sleep.
- Wi-Fi: quét/chọn mạng, nhập mật khẩu bằng ba nút, RSSI/IP, bật/tắt radio; lưu mạng kết nối thành công vào NVS và tự reconnect có giới hạn.
- Pin: phần trăm ước tính từ ADC GPIO8 theo profile Muse ZHENGCHEN; hiển thị raw ADC và tín hiệu nguồn ngoài GPIO9. Cần kiểm chứng calibration/polarity trên board thật.
- Trang Pin có thời gian còn dùng được ước tính từ tốc độ tụt pin đo được: chờ ổn định 1 phút, cần ít nhất 10 phút và tụt 5 điểm phần trăm, nếu chưa đủ thì hiện Đang đo. Thống kê màn hình sáng, dùng pin và sleep tính trong lúc không có nguồn ngoài, theo chu kỳ từ lần rút sạc gần nhất; giữ qua reboot bằng checkpoint NVS mỗi phút và khi đổi nguồn. Rút sạc bắt đầu chu kỳ mới; cắm sạc tạm dừng bộ đếm. Reboot giữ thống kê nhưng đo lại tốc độ tụt pin. Sleep ở đây là backlight tắt, chưa phải deep sleep của ESP32. Ước tính còn lại phụ thuộc bảng ADC và mức sử dụng, chưa được kiểm chứng trên board.
- BLE: vào trang Bluetooth tự mở kết nối 5 phút và hiện QR mở trang điều khiển HTTPS; ghép bằng mã 6 số trên màn hình. Trang ở `docs/ble/`, xuất bản GitHub Pages bằng main → /docs; hướng dẫn: `docs/BLE_SETUP.md`.
- Lịch: lịch tháng dương, ngày âm Việt Nam UTC+7 và tháng nhuận (2024–2099). Vol− ngày trước, Vol+ ngày sau; giữ 1 giây lặp mỗi 180ms, tự chuyển tháng/năm. BOOT về hôm nay, giữ BOOT trở lại menu. Ngày chọn tô xanh, hôm nay có viền. Cần giờ đã đồng bộ qua Wi-Fi hoặc BLE; sau đó xem lịch offline được khi thiết bị vẫn có giờ.
- Đèn pin: vào là màn trắng, backlight 100%; Vol− bật/tắt, BOOT chuyển Sáng → Chớp → SOS → Sáng, Vol+ hoặc giữ BOOT về menu và khôi phục độ sáng đã lưu. Chớp 500ms sáng/500ms tắt; SOS `... --- ...` với đơn vị 200ms, lặp lại. Không ghi lại setting độ sáng, không tự sleep khi đang ở Đèn pin.
- Phụ đề: API queue cho worker, chưa nối AI/STT/TTS.

## Build

Yêu cầu EIM đã cài ESP-IDF **5.5.5**. Chạy từ thư mục HisuOS:

```powershell
.\tools\build.ps1
```

Helper chỉ build, không flash. `sdkconfig.defaults` bật octal PSRAM và flash 16MB theo profile N16R8. Partition dành hai app slots 4MB cho OTA tương lai; vùng storage chỉ được reserve, chưa mount filesystem.

## Điều Khiển

- Nhấn BOOT: mở menu, chọn mục, hoặc lưu giá trị.
- Volume up/down: di chuyển; trong editor số thì tăng/giảm. Âm lượng và độ sáng chỉnh mỗi lần 10%. Thời gian trên 60 giây hiển thị bằng phút.
- Editor Loa/Theme: Vol− đổi trạng thái (chấm rỗng/đặc), BOOT lưu, Vol+ trở lại không lưu. Chấm đặc biểu thị Loa bật hoặc Theme sáng.
- Thời gian chờ đặt thời gian tự tắt backlight. Tắt màn hình tắt ngay và về idle; nút tiếp theo chỉ đánh thức. Đóng menu trở về màn hình chờ.
- Menu cuộn từng dòng, giữ vị trí khi quay lại. Font UI 16px Semibold hỗ trợ tiếng Việt.
- Footer theo thứ tự nút Vol− / BOOT / Vol+. Trang Pin dùng Vol− để trở lại; trang Bluetooth dùng Vol− bật/tắt, BOOT mở/gia hạn kết nối, Vol+ trở lại.
- Giữ BOOT 800ms: quay lại; trong editor là hủy; từ menu là về màn hình đen.
- Khi hiển thị phụ đề thật: up/down cuộn nội dung dài theo chiều dọc.
- Khi backlight đã tắt: nút đầu tiên chỉ đánh thức, không kích hoạt hành động.

Trong bước UI này BOOT chưa dành cho push-to-talk. Mapping sẽ được điều chỉnh khi tích hợp voice.

## Setup Wi-Fi

Mở menu → Wi-Fi. Vol−/Vol+ di chuyển danh sách, BOOT chọn mạng hoặc Quét lại/Bật-tắt Wi-Fi/Trở lại. Giữ BOOT để về menu. Danh sách có icon mức sóng; SSID dài được rút gọn để vừa màn hình. Khi tắt Wi-Fi, chỉ còn Bật Wi-Fi và Trở lại.

Bàn phím 8×5: nhấn Vol− sang phải, Vol+ xuống hàng; giữ 1 giây bắt đầu lặp cùng hướng mỗi 180ms và vòng lại ở mép. Nhả sau giữ không phát thêm nhấn ngắn. BOOT chọn ký tự. `Aa` đổi chữ thường/chữ hoa/ký hiệu; `<` xóa, `OK` kết nối, `X` hủy. Phím `SP` trong bảng ký hiệu là dấu cách. Mật khẩu chỉ hiển thị dấu `*`, được xóa khỏi buffer UI khi gửi/hủy; firmware không log credential.

Hỗ trợ mạng 2.4GHz mở và WPA2/WPA3 Personal, mật khẩu ASCII 8–63 ký tự hoặc PSK hex 64 ký tự. Menu chưa có nhập SSID ẩn; trang BLE có thể gửi SSID trực tiếp. Chưa hỗ trợ Wi-Fi Enterprise hoặc captive portal. Credential dùng NVS thường của prototype, chưa bật encryption. Lưu tối đa 8 mạng sau khi nhận IP thành công; mạng dùng gần nhất đứng đầu và được thử kết nối khi khởi động. Khi đầy, lưu mạng mới thay mạng lâu nhất. Bản nâng cấp giữ mạng đã lưu theo định dạng cũ. Mạng đã lưu hiện tên màu xanh; BOOT dùng lại mật khẩu, lỗi xác thực rồi chọn lại sẽ mở bàn phím để sửa. Chưa kết nối thì không hiện tên mạng cũ ở dòng trạng thái.

Hướng dẫn kiểm thử: [UI Bring-up](docs/UI_BRINGUP.md). Nguồn font và license: [NOTICE](licenses/NOTICE.md).
