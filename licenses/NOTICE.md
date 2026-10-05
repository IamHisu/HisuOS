# Third-Party Notices

Pin và luồng bàn phím Wi-Fi tham khảo Muse Gadget SDK, nhánh `feature/zhengchen-board`, commit `a69273db94d94590e72198688c4f897f78c9e57b`:

- Nguồn: https://github.com/IamHisu/muse-gadget-sdk/tree/a69273db94d94590e72198688c4f897f78c9e57b
- `esp32/components/muse/boards/board_zhengchen_154.c`: GPIO8/ADC1_CH7, GPIO9 active-high, bảng ADC/phần trăm và offset +80; port sang board driver và power service HisuOS.
- `esp32/components/muse/muse_menu.c`: ý tưởng bàn phím 8×5, Vol− sang cột, Vol+ sang hàng, BOOT chọn; viết lại cho UI tiếng Việt, thêm đầy đủ ký tự ASCII và lưu cấu hình khi kết nối thành công.
- Copyright (c) Meta Platforms, Inc. and affiliates. Apache License 2.0, bản đầy đủ trong `Muse-Apache-2.0.txt`.
- Không mang backend, token hoặc giao thức BLE Muse vào HisuOS. Mạng dùng API ESP-IDF 5.5.5 trực tiếp.

UI hiện dùng `font_vi_semibold_16.c`, raster hóa từ Source Code Pro Semibold 16px (Adobe, 2010–2020), đặt symbol riêng `hisu_font_vi_semibold_16`. License SIL OFL 1.1 trong `SourceCodePro-LICENSE.txt`. Generator: `tools/generate-ui-font.py` (Pillow, nhận TTF nguồn làm tham số). Bitmap 4bpp không nén, hỗ trợ ASCII và dấu tiếng Việt. Font cũ bên dưới được giữ làm nguồn tham khảo.

`components/hisu_ui/fonts/font_vietnamese_16.c` là font LVGL generated được tái sử dụng nguyên trạng từ `FriendOS/main/ui/font_vietnamese_16.c` trong workspace. Font được tạo từ DejaVuSans.ttf; symbol `friend_font_vietnamese_16` được giữ nguyên để không chỉnh payload generated.

License font được lưu tại `DejaVuSans-LICENSE.txt`. Không sao chép driver/UI implementation của Muse vào bước này.

LVGL 9.4.0 được tải bằng ESP-IDF Component Manager, cùng license do upstream cung cấp trong managed component.

`hisu_calendar.c` viết lại bằng C các phương trình thiên văn và quy tắc lịch âm Việt Nam do Hồ Ngọc Đức công bố tại https://www.xemamlich.uhm.vn/calrules.html (18/08/2008). Dùng kinh tuyến UTC+7; không sao chép bảng lịch hoặc thư viện JavaScript. Thuật toán thiên văn giản lược, giới hạn giao diện 2024–2099.
