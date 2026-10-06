# UI Bring-up

## Kết Quả Kiểm Tra UI Ban Đầu — 2026-10-05

- Build ESP-IDF 5.5.5, ESP32-S3, LVGL 9.4.0: thành công.
- Binary `build/HisuOS.bin`: `0x84c00` bytes; app slot `0x400000`, còn 87% trống.
- Host model C tests: 10/10 qua, chạy bằng MSVC DLL và Python ctypes.
- Chưa flash; chưa kiểm tra rendering, PWM, nút hoặc NVS persistence trên board thật.
- Chưa thực hiện screenshot/visual simulation; host tests chỉ kiểm tra logic và UTF-8.
- Chưa commit/push các thay đổi UI.

## Phạm Vi

Bản thêm Wi-Fi/pin đã build và link thành công trên ESP-IDF 5.5.5, tạo `HisuOS.bin` kích thước `0x116d50`, còn 73% app slot trống. Bàn phím kiểm tra đủ 95 ký tự ASCII in được, mask và nhãn phím vừa 240×240. Đây chưa phải kết quả kết nối/ADC trên board; host tests mới chưa chạy do thiếu MSVC.

Firmware dùng ESP-IDF 5.5.5 và LVGL 9.4.0. Đã thêm service Wi-Fi và battery ADC theo profile Muse ZHENGCHEN; chưa kiểm chứng chúng trên board thật. Đã tích hợp BLE setup Wi-Fi và gửi giờ, chưa kiểm chứng ghép nối trên board. Chưa tích hợp microphone, speaker hoặc AI. Menu chỉ lưu volume/speaker preferences, không phát âm thanh.

Board driver không include LVGL; UI sở hữu display buffer, render và input loop. LCD DMA callback chỉ báo `lv_display_flush_ready`, không thay đổi LVGL objects. Render buffer internal DMA 40 dòng, RGB565. SPI mode 3/80MHz, RGB/inversion theo cấu hình FriendOS trước; cần kiểm tra lại trên board cho bản driver mới.

Brightness có live hardware preview; Theme có live UI preview sáng/tối. BOOT lưu, giữ BOOT hủy; NVS chỉ commit lúc xác nhận, không mỗi lần chỉnh. NVS lỗi không bị erase tự động.

## Host Tests

```powershell
.\tools\test-ui.ps1
```

Helper dùng MSVC x64 đã cài trên máy để build model C thành DLL Windows không phụ thuộc CRT, rồi chạy Python EIM qua ctypes. Không cài package hoặc tool mới. Các test kiểm tra menu, editor bounds, timeout presets, preview và UTF-8 payload. Không thay thế visual/hardware tests.

## Build

```powershell
.\tools\build.ps1
```

Không flash trong build helper. Flash cần được thực hiện riêng sau khi xác nhận đúng port và board; không erase toàn flash/NVS để thử UI.

## Kiểm Thử Trên Board

- Boot hiển thị GIF mèo 240×240, lặp đủ tám frame; vùng trong suốt theo nền trắng/đen của theme đã lưu.
- BOOT mở Hisu, up/down chạy đủ mười mục và wrap đúng.
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

Host suite bổ sung password bounds/hex PSK và bảng pin (biên, điểm hiệu chuẩn, tính đơn điệu). Chạy bằng `tools/test-ui.ps1`; môi trường Codex hiện không có MSVC ở đường dẫn helper, nên các case mới chưa được chạy tại đây.

## API Phụ Đề

Worker gọi `hisu_ui_post_status(status, text)`, không gọi LVGL. Text UTF-8 tối đa 255 bytes, không cắt giữa codepoint; input không hợp lệ trả `ESP_ERR_INVALID_ARG`. Queue tám event hữu hạn, đầy trả `ESP_ERR_TIMEOUT`; caller phải xử lý lỗi/backpressure.

UI nhận full text snapshot, không nhận raw token delta. Controller tương lai tích lũy token với budget riêng rồi gửi snapshot. Menu che tạm phụ đề, không xóa snapshot. Trạng thái idle hiển thị GIF theo theme.

Session cancellation và chống stale events thuộc conversation controller tương lai; UI hiện không tự nhận diện session. Phụ đề preview là dữ liệu kiểm tra, không phải phản hồi AI thật.
