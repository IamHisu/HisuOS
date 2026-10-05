# ZHENGCHEN 1.54 Board Profile

Profile phần cứng đầu tiên của HisuOS: ESP32-S3 N16R8, màn hình ST7789 240x240, không cảm ứng.

Hiện chỉ có `board_config.h`, chưa có driver, build configuration hoặc firmware chạy được. Dung lượng flash/PSRAM trong header là thông tin profile, không tự cấu hình ESP-IDF.

## Pin Map

| Chức năng | GPIO |
|---|---|
| LCD MOSI / SCLK | 41 / 42 |
| LCD CS / DC / RST / backlight | 21 / 40 / 45 / 20 |
| Mic WS / SCK / data | 4 / 5 / 6 |
| Speaker data / BCLK / LRCK | 7 / 15 / 16 |
| BOOT / volume up / volume down | 0 / 10 / 39 |
| Battery ADC | 8 (ADC1 channel 7) |
| Charge status, tham khảo Muse | 9, cần xác minh |

Nguồn đối chiếu trong workspace:

- `FriendOS/main/board/board_config.h`: pin map LCD, mic, loa, nút và battery.
- `muse-gadget-sdk/esp32/components/muse/boards/board_zhengchen_154.c`: pin map tương ứng, nút active-low, profile không cảm ứng và charge status GPIO9.

Không sao chép implementation driver của Muse. Không mang UI, AI, resource manager hoặc calibration pin sang bản mới.

## Cần Xác Minh Khi Bring-up

- SPI mode/clock, inversion, color order và orientation: không chốt chỉ từ pin map.
- I2S sample format, channel selection và sample rate.
- Battery divider/calibration và polarity GPIO9; chưa coi charge status là phép đo USB riêng.
- GPIO20 đang dùng cho backlight: kiểm tra ảnh hưởng nếu sử dụng native USB trên ESP32-S3.
- Init peripheral phải có một owner, không khởi tạo đồng thời từ nhiều service.

Việc kiểm thử trên firmware trước không thay thế kiểm thử driver của bản làm lại.
