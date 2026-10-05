# UI Bring-up

## Kết Quả Kiểm Tra — 2026-10-05

- Build ESP-IDF 5.5.5, ESP32-S3, LVGL 9.4.0: thành công.
- Binary `build/HisuOS.bin`: `0x84c00` bytes; app slot `0x400000`, còn 87% trống.
- Host model C tests: 10/10 qua, chạy bằng MSVC DLL và Python ctypes.
- Chưa flash; chưa kiểm tra rendering, PWM, nút hoặc NVS persistence trên board thật.
- Chưa thực hiện screenshot/visual simulation; host tests chỉ kiểm tra logic và UTF-8.
- Chưa commit/push các thay đổi UI.

## Phạm Vi

Firmware UI-first dùng ESP-IDF 5.5.5 và LVGL 9.4.0. Chưa tích hợp Wi-Fi, BLE, battery ADC, microphone, speaker hoặc AI. Menu chỉ lưu volume/speaker preferences, không phát âm thanh.

Board driver không include LVGL; UI sở hữu display buffer, render và input loop. LCD DMA callback chỉ báo `lv_display_flush_ready`, không thay đổi LVGL objects. Render buffer internal DMA 40 dòng, RGB565. SPI mode 3/80MHz, RGB/inversion theo cấu hình FriendOS trước; cần kiểm tra lại trên board cho bản driver mới.

Chỉ brightness có live hardware preview. BOOT lưu, giữ BOOT hủy; NVS chỉ commit lúc xác nhận, không mỗi lần chỉnh. NVS lỗi không bị erase tự động.

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

- Boot là màn hình đen, không avatar hoặc animation.
- BOOT mở Hisu, up/down chạy đủ chín mục và wrap đúng.
- Không có chữ bị cắt/đè trong header, row, value hoặc footer 240x240.
- Brightness 10–100%, preview đúng, giữ BOOT hủy và trả về giá trị đã lưu.
- BOOT xác nhận brightness/volume/speaker/timeout; reboot giữ giá trị.
- Timeout chỉ tắt backlight khi idle, không khi editor/preview hoặc active subtitle.
- Nút đầu tiên khi backlight tắt chỉ wake, nút tiếp theo mới thao tác.
- Preview phụ đề có đủ dấu tiếng Việt, text dài đọc được, thoát về menu.
- Phụ đề từ API wrap theo chiều rộng, up/down cuộn dọc nếu dài; không chạy chữ ngang.
- Wi-Fi/Bluetooth/pin không hiển thị số đo hoặc kết nối giả.
- Giữ BOOT không phát thêm click khi nhả; debounce không nhảy hai mục.
- Theo dõi LCD transfer errors, heap, stack và watchdog qua nhiều vòng điều hướng.

## API Phụ Đề

Worker gọi `hisu_ui_post_status(status, text)`, không gọi LVGL. Text UTF-8 tối đa 255 bytes, không cắt giữa codepoint; input không hợp lệ trả `ESP_ERR_INVALID_ARG`. Queue tám event hữu hạn, đầy trả `ESP_ERR_TIMEOUT`; caller phải xử lý lỗi/backpressure.

UI nhận full text snapshot, không nhận raw token delta. Controller tương lai tích lũy token với budget riêng rồi gửi snapshot. Menu che tạm phụ đề, không xóa snapshot. Trạng thái idle hiển thị màn hình đen.

Session cancellation và chống stale events thuộc conversation controller tương lai; UI hiện không tự nhận diện session. Phụ đề preview là dữ liệu kiểm tra, không phải phản hồi AI thật.
