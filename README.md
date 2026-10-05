# HisuOS

Firmware ESP32-S3 cho ZHENGCHEN 1.54 TFT WiFi, xây dựng lại theo [System Design](docs/SYSTEM_DESIGN.md).

## Hiện Trạng

Bước đầu là UI LVGL 9.4.0: màn hình chờ đen, menu Hisu, editor cấu hình và preview phụ đề tiếng Việt.

- Độ sáng: PWM thật, preview khi chỉnh, lưu NVS khi xác nhận.
- Âm lượng/bật loa: chỉ lưu cấu hình; chưa nối audio service.
- Timeout: tắt backlight khi idle, không deep sleep.
- Wi-Fi/BLE/pin: màn hình không khả dụng, chưa có service.
- Phụ đề: API queue cho worker và preview kiểm tra, chưa nối AI/STT/TTS.

## Build

Yêu cầu EIM đã cài ESP-IDF **5.5.5**. Chạy từ thư mục HisuOS:

```powershell
.\tools\build.ps1
```

Helper chỉ build, không flash. `sdkconfig.defaults` bật octal PSRAM và flash 16MB theo profile N16R8. Partition dành hai app slots 4MB cho OTA tương lai; vùng storage chỉ được reserve, chưa mount filesystem.

## Điều Khiển

- Nhấn BOOT: mở menu, chọn mục, hoặc lưu giá trị.
- Volume up/down: di chuyển; trong editor thì tăng/giảm.
- Giữ BOOT 800ms: quay lại; trong editor là hủy; từ menu là về màn hình đen.
- Mục Kiểm tra phụ đề: duyệt ba mẫu bằng BOOT/down, up quay lại mẫu trước.
- Khi hiển thị phụ đề thật: up/down cuộn nội dung dài theo chiều dọc.
- Khi backlight đã tắt: nút đầu tiên chỉ đánh thức, không kích hoạt hành động.

Trong bước UI này BOOT chưa dành cho push-to-talk. Mapping sẽ được điều chỉnh khi tích hợp voice.

Hướng dẫn kiểm thử: [UI Bring-up](docs/UI_BRINGUP.md). Nguồn font và license: [NOTICE](licenses/NOTICE.md).
