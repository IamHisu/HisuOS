# Tài Liệu Tham Khảo

**Cập nhật:** 2026-10-06

**Phạm vi:** Đánh giá nguồn để phát triển HisuOS; không phải danh sách tính năng đã tích hợp.

Kiến trúc, ownership và roadmap của HisuOS theo [System Design](SYSTEM_DESIGN.md). Trạng thái firmware và cách build theo [README](../README.md). Trước khi lấy code, ghi commit/version, kiểm tra license của từng phần và giữ attribution cần thiết.

## XiaoClaw

- Repository: [beancookie/xiaoclaw](https://github.com/beancookie/xiaoclaw).
- Commit ghi nhận khi tham khảo: [`55550bd461a5f760c256f59f32cbe2200a87e2c9`](https://github.com/beancookie/xiaoclaw/tree/55550bd461a5f760c256f59f32cbe2200a87e2c9).
- Đã đọc README, API bridge, đường HTTP trong LLM proxy, dependency manifest, partition XiaoClaw và power manager Zhengchen. Chưa audit toàn repo, build hoặc chạy firmware XiaoClaw trên board.
- Repo ghép voice I/O của Xiaozhi với agent của MimiClaw. Đây là nguồn tham khảo tích hợp, không thay thế ESP-IDF hoặc các service HisuOS.

### Phần Có Thể Tham Khảo

| Phần nguồn | Hướng áp dụng trong HisuOS | Giai đoạn |
|---|---|---|
| [Bridge queue/callback](https://github.com/beancookie/xiaoclaw/blob/55550bd461a5f760c256f59f32cbe2200a87e2c9/main/bridge/bridge.h) | Text từ STT tới agent; câu trả lời tới phụ đề/TTS. Application sở hữu điều phối, có cancellation và queue bounded | Text AI / Voice |
| [Agent runner, context và hooks](https://github.com/beancookie/xiaoclaw/tree/55550bd461a5f760c256f59f32cbe2200a87e2c9/main/mimi/agent) | Tham khảo vòng lặp có giới hạn; kiểm tra schema, lỗi tool và budget trước khi tái sử dụng | Tools |
| [LLM proxy](https://github.com/beancookie/xiaoclaw/blob/55550bd461a5f760c256f59f32cbe2200a87e2c9/main/mimi/llm/llm_proxy.c) | Tham khảo request/response; giữ provider interface HisuOS và Gemini là provider đầu tiên | Text AI |
| [Memory và sessions](https://github.com/beancookie/xiaoclaw/tree/55550bd461a5f760c256f59f32cbe2200a87e2c9/main/mimi/memory) | Tham khảo lưu hội thoại và gom context; cần đo tài nguyên và giới hạn dữ liệu | Sau MVP |
| [Tools](https://github.com/beancookie/xiaoclaw/tree/55550bd461a5f760c256f59f32cbe2200a87e2c9/main/mimi/tools) | Tham khảo registry, MCP và scheduler; không mặc định cho agent quyền ghi file hoặc chạy script không giới hạn | Device tools trước; MCP/Lua/scheduler sau MVP |

### Giới Hạn Và Điều Kiện

- **Không đồng nghĩa LLM offline:** Đường đã đọc trong `llm_proxy.c` gọi API HTTP với credential. Agent điều phối trên ESP32 không chứng minh model được suy luận trên ESP32. Không quảng bá HisuOS có LLM offline dựa vào mô tả repo này.
- **ESP-IDF:** [Manifest](https://github.com/beancookie/xiaoclaw/blob/55550bd461a5f760c256f59f32cbe2200a87e2c9/main/idf_component.yml) khai báo `>=5.5.2`; 5.5.5 đáp ứng điều kiện khai báo, nhưng chưa chứng minh toàn bộ dependency/API build được trên HisuOS.
- **Flash:** [Partition XiaoClaw](https://github.com/beancookie/xiaoclaw/blob/55550bd461a5f760c256f59f32cbe2200a87e2c9/partitions/partitions_xiaoclaw.csv) dành cho 32MB, vượt board N16R8 của HisuOS. Không copy nguyên layout; chọn assets/storage sau khi đo app size, heap và PSRAM, bảo toàn hai OTA slots.
- **Pin:** [Power manager Zhengchen](https://github.com/beancookie/xiaoclaw/blob/55550bd461a5f760c256f59f32cbe2200a87e2c9/main/boards/zhengchen-1.54tft-wifi/power_manager.h) vẫn cộng offset ADC `80`, bảng từ `2030` tới `2606`, tương đương raw ADC `2526` tại 100%. Không phải bản sửa sai lệch pin trên board Hisu; cần xác minh điện áp, mạch chia áp và calibration thực tế.
- **BLE:** Chưa xác định được implementation ghép Web Bluetooth riêng trong phần đã xem có thể sửa lỗi Windows của HisuOS. Không lấy sự tồn tại của hỗ trợ board làm bằng chứng pairing tương thích. Chẩn đoán hiện tại theo [BLE Setup](BLE_SETUP.md).
- **License:** Repo hiển thị MIT; phải kiểm tra license/NOTICE của phần kế thừa và dependency trước khi đưa code vào HisuOS. Ghi nhận nguồn tham khảo chưa có nghĩa đã import code hoặc hoàn thành kiểm tra license.

### Quyết Định Cho HisuOS

Giữ nền HisuOS hiện tại và dùng XiaoClaw như mẫu tích hợp chọn lọc. Luồng mục tiêu: `Audio/STT -> Application -> Agent/LLM -> Application -> Subtitles/TTS/Audio`. UI và board drivers không phụ thuộc trực tiếp vào agent hoặc provider. Không bổ sung Lua, auto-skills, toàn bộ scheduler hoặc đổi partition chỉ để giống project nguồn.

## Nguồn Nền Tảng

- [ESP-IDF 5.5.5 cho ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/): nền tảng firmware.
- [ADC calibration ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/peripherals/adc_calibration.html): đổi ADC sang điện áp đã hiệu chỉnh; vẫn cần tỉ lệ chia áp và profile pin đúng.
- [Xiaozhi](https://github.com/78/xiaozhi-esp32): voice pipeline và board reference.
- [MimiClaw](https://github.com/memovai/mimiclaw): agent, tools, memory và scheduling reference.
- Muse: nguồn setup/UI/board đang tham khảo trong workspace `muse-gadget-sdk`; attribution của code đã lấy theo [NOTICE](../licenses/NOTICE.md).

## Quan Sát Trên HisuOS Chưa Giải Quyết

- Người dùng báo ADC `2378` khi đèn sạc báo đầy, `2369` rồi `2366` sau rút sạc; lần khác ADC `2415` khi đèn chưa báo đầy. Đây là mẫu quan sát, không phải bộ calibration, chưa chọn mốc 100% mới.
- Log BLE có `Pairing action=3`, inject PIN `rc=0`, sau khoảng 4,25 giây `Security result: status=7` và disconnect `531`, trước khi người dùng nhập PIN. Đối chiếu header NimBLE của IDF 5.5.5: `7` là `BLE_HS_ENOTCONN`, `531 = 0x200 + 0x13` là HCI remote user termination. Không phải bằng chứng nhập sai mã; chưa xác định nguyên nhân máy tính ngắt kết nối.
- Việc giữ màn hình PIN khi thất bại và thêm log không kéo dài timeout SMP, không xác nhận đã sửa nguyên nhân pairing. Test mock GATT và build thành công không thay thế kiểm thử radio trên board.
