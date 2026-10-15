# Third-Party Notices

Experimental Zing MP3 playback uses `espressif/esp_audio_codec` 2.6.2, provided by
Espressif Systems under the Espressif Modified MIT License. The complete license
is in `Espressif-Audio-Codec-LICENSE.txt`; upstream component:
https://components.espressif.com/components/espressif/esp_audio_codec/versions/2.6.2

The Zing MP3 HTTP adapter is a HisuOS implementation of the public web-client
request format researched from https://github.com/nvhung9/mp3-api. It is unofficial,
does not bypass account/paid-track restrictions, and can break when the source changes.

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

Bộ resize GIF trong trang Setting được chuyển từ `FriendOS/main/web-src/gif-resize.js`, commit `71654b47462d4723e1dd4f1b6e2108a4d54cbe85`, theo yêu cầu của chủ repository:

- Nguồn: https://github.com/IamHisu/FriendOS/tree/71654b47462d4723e1dd4f1b6e2108a4d54cbe85/main/web-src
- Dùng `gifuct-js` 2.1.2 để parse/giải mã frame và `gifenc` 1.0.3 để lượng tử màu/mã hóa lại. Hai package dùng giấy phép MIT; thông tin phiên bản, license và integrity được giữ trong `components/hisu_media/web-src/package-lock.json`.
- Bundle được nhúng trực tiếp vào firmware, không tải mã hoặc tài nguyên từ Internet khi dùng hotspot.
