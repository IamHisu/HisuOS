# Setting Qua QR Hotspot

**Cập nhật:** 2026-10-06

Setting dùng hotspot WPA2 tạm thời, mật khẩu ngẫu nhiên 16 ký tự mỗi phiên.
Không dùng mật khẩu cố định cũ, mã truy cập riêng hoặc BOOT duyệt ảnh.
Bluetooth đã gỡ khỏi firmware và code. Không cần router, Internet hay GitHub Pages.
Không cần từng lưu hoặc kết nối Wi-Fi nhà để mở Setting; mục Wi-Fi trên board chỉ là lựa chọn nhập trực tiếp.

## Flow Sử Dụng

1. Mở menu **Setting** trên Hisu. Board mở hotspot `Hisu-Setting-xxxxxx` và hiện QR kết nối Wi-Fi.
2. Quét QR bằng camera điện thoại, đồng ý kết nối và giữ mạng dù không có Internet.
3. Captive portal trả DNS về IP AP và chuyển yêu cầu kiểm tra mạng sang trang HTTP Setting.
   Hệ điều hành có thể tự mở trang; không bảo đảm trên mọi máy, nhất là khi dùng Private DNS/VPN.
4. Khi điện thoại nối hotspot, board tự đổi sang QR **Trang Setting**.
   Nếu không tự mở, quét QR để vào `http://192.168.4.1/`. Khi điện thoại ngắt, board trở lại QR Wi-Fi.
   BOOT ở màn QR hoặc thông tin bật/tắt hotspot Setting; không còn bộ đếm thời gian.
5. HTTP không có cảnh báo chứng chỉ tự ký, nhưng trình duyệt vẫn có thể ghi Không bảo mật.
6. Trang tự lấy token qua HTTP, không hỏi mã kết nối. Các tab là **Wi-Fi**, **Hình nền**, **Thiết bị**.
7. Gửi ảnh hợp lệ sẽ tự lưu và áp dụng; không cần xác nhận BOOT. Thoát Setting để xem hình nền.
8. Vol+ / X hoặc giữ BOOT chỉ quay về menu, Setting vẫn chạy. Vol− chuyển qua lại QR → thông tin → QR; BOOT bật/tắt hotspot và web. Bấm **Đã kết nối** trên web đóng toàn bộ phiên. Không giới hạn tổng thời gian 5 phút.
9. Lưu ảnh nền mới thành công sẽ tự đưa HISU về màn hình chính và đánh thức màn hình. Phiên Setting vẫn chạy; lưu lỗi không chuyển màn hình.
   Ngắt hotspot sẽ đóng sau 10 giây nếu không nối lại. Đóng trang gửi `/leave`, chờ 8 giây để tải lại trang không mất phiên.
   Nếu trình duyệt bị tắt đột ngột không gửi được tín hiệu, mất request xác thực trong 90 giây sẽ đóng phiên dự phòng.
   Muốn mở lại, thao tác trên board và quét QR mới.

Vol− / biểu tượng danh sách chuyển qua lại thông tin hotspot/địa chỉ và QR.
Ở thông tin hotspot, BOOT bật/tắt Setting rồi quay về QR. Giữ BOOT quay về menu, không đóng Setting.
Không còn mục khôi phục nền, API/lệnh xóa ảnh hay ảnh nền mặc định nhúng sẵn.
Chỉ dùng ảnh/GIF tải lên; chưa có ảnh hoặc ảnh lỗi thì nền đen/trắng theo theme.
Nút **Căn lại ảnh** trên web chỉ đặt lại thu phóng/vị trí bản xem trước, không đổi ảnh đã lưu trên board.

## Wi-Fi Và Thiết Bị

- Tab Wi-Fi bật/tắt **station Wi-Fi**, không tắt hotspot đang cấu hình.
- Danh sách mạng giống điện thoại, icon Lucide Wi-Fi biểu thị mức sóng thay số dBm.
  Mạng đã lưu có tên xanh và nhãn nhỏ màu riêng; chạm kết nối bằng mật khẩu NVS, không mở dialog.
  Mạng mới có bảo mật mở dialog mật khẩu; mạng mở kết nối ngay. Thêm mạng dùng cho SSID ẩn.
  Nếu mạng đã lưu báo lỗi, chọn lại để nhập mật khẩu mới. Chỉ cập nhật dòng thay đổi, không dựng lại danh sách mỗi lần poll.
- Mạng mới chỉ lưu sau khi kết nối thành công. Không trả credential trong API trạng thái.
- Tab Thiết bị lưu qua settings service; đồng bộ giờ lấy UTC điện thoại.
  Âm lượng/loa hiện lưu cấu hình, chưa tích hợp audio service.
- AP+STA chia sẻ radio; quét/join và chuyển kênh có thể làm điện thoại ngắt ngắn.
  Chưa có NAT/bridge. Không mở service qua địa chỉ station hay ra Internet.

## Bảo Mật Và Đánh Đổi

- Mật khẩu WPA2 là 8 byte ngẫu nhiên biểu diễn thành 16 ký tự hex, 64 bit entropy;
  lấy ngẫu nhiên khi radio hoạt động. SSID ổn định theo MAC, không phụ thuộc mật khẩu.
- Tối đa một điện thoại kết nối AP. Chỉ thao tác vật lý mở phiên; không còn thời hạn cố định.
  Trước khi điện thoại/web kết nối lần đầu, hotspot tiếp tục chờ cho tới khi đóng trên Hisu.
  Sau khi dùng web, request xác thực giữ lease liveness 90 giây; không được hồi sinh phiên đã đóng.
- **Người có QR/mật khẩu hotspot có quyền chỉnh mọi Setting trong phiên.**
  Không còn PIN hoặc phê duyệt ảnh làm lớp bảo vệ thứ hai. Không chia sẻ ảnh chụp QR và đóng Setting khi xong.
- Token 256 bit chỉ ở RAM board/trình duyệt; không cookie, localStorage, URL hoặc QR.
  Tải lại trang có thể lấy lại token của phiên hiện tại qua HTTP. Đóng/hết hạn thu hồi ngay token.
- Đóng tab gửi POST keepalive `/leave` có token, Origin và mã trang hiện tại, không token trong URL.
  Login mới trong 8 giây hủy đóng; tín hiệu unload đến muộn từ trang cũ không đóng trang mới.
  Không đóng khi chỉ đổi tab, mở hộp chọn ảnh hoặc chuyển app; nếu trình duyệt treo/đóng băng
  quá 90 giây, board không phân biệt được với đóng web và sẽ đóng để tránh bỏ quên hotspot.
  Nên dùng một trang Setting. Nút Đã kết nối luôn đóng toàn bộ phiên.
- HTTP kiểm tra Host chính xác IP AP (hoặc cổng 80), POST có Origin chính xác,
  socket đích đúng IP AP và nguồn thuộc subnet AP, đối chiếu thêm IP của client đang thực sự nối SoftAP.
  Không chỉ tin địa chỉ subnet. Không CORS hoặc tin proxy.
  CSP same-origin, no-store/no-referrer, không iframe.
- Socket dùng buffer đủ lớn cho cả IPv4/IPv6. Chỉ nhận IPv4 thuần hoặc IPv4-mapped IPv6
  (`::ffff:a.b.c.d`), chuẩn hóa về IPv4 rồi kiểm tra IP AP/client. IPv6 thuần vẫn bị chặn.
- Một server HTTP cổng 80 phục vụ Setting/API và captive redirect; cổng UDP control nội bộ 32768.
  Route API chính xác đăng ký trước wildcard. GET/HEAD lạ chuyển về địa chỉ AP;
  POST lạ như `/mmtls/...` trả 404 và đóng socket, không chuyển tiếp body tới API.
  Không chặn/intercept HTTPS tên miền bên ngoài. Không còn server HTTPS cổng 8443.
- DNS UDP cổng 53 bind IP AP, lọc subnet và client đang nối SoftAP; parser chỉ nhận một câu hỏi IN không nén,
  giới hạn packet, label, tên và buffer. A trả IP AP, AAAA trả rỗng.
- HTTP giới hạn hai client socket; timeout nhận/gửi và upload.
  Người trong vùng phát sóng vẫn có thể gây DoS; chưa có audit bảo mật.
- Nếu Wi-Fi nhà cũng dùng subnet 192.168.4.x cần thử xung đột/routing thực tế; chưa tự đổi subnet AP.
- **HTTP không có TLS hoặc xác thực danh tính server.** WPA2 chỉ bảo vệ đường truyền Wi-Fi;
  token không thay thế TLS. Mật khẩu Wi-Fi nhà, ảnh và token không có mã hóa tầng HTTP.
  Không chống được AP giả/MITM chỉ bằng Host/Origin/token. Chỉ dùng hotspot cục bộ và đóng khi xong,
  không công khai QR, không mở port ra Internet. Chưa audit bảo mật.
- Chưa bật secure boot, flash/NVS encryption hoặc bảo vệ truy cập vật lý.

## Ảnh Và Storage

- PNG/JPEG/WebP tối đa 8 MiB, mỗi chiều tối đa 4096 px, tổng tối đa 12 megapixel.
  Browser giải mã ảnh tĩnh, crop/letterbox và ghép transparency lên màu nền.
- GIF nguồn tối đa 8 MiB, 1,5 megapixel và 180 frame. Bộ xử lý tham khảo từ FriendOS ghép đúng frame trong suốt/disposal, thu cạnh dài xuống 240 px khi cần và mã hóa lại thành GIF động tối đa 1 MiB; xem trước hiển thị frame đầu. Firmware nhận canvas tối đa 240×240, kiểm tra header, palette/block và kích thước frame trước khi lưu.
- Payload RGB565 little-endian cố định 240×240×2 = 115200 byte.
  Board không parse tên file, đường dẫn, PNG/JPEG tùy ý.
- Nhận đủ ảnh trong phiên hợp lệ mới xếp vào hàng lưu tự động. Ảnh thiếu, upload quá 30 giây,
  phiên bị đóng/hết hạn trước khi nhận đủ hoặc trước khi worker nhận hàng sẽ bị bỏ.
- Worker sở hữu buffer khi bắt đầu ghi; đóng phiên lúc đang ghi không giải phóng buffer
  hoặc cắt ngang commit đã được chấp nhận.
- Hai slot 122880 byte ở đầu partition `storage` subtype `0x40`, tổng 240 KiB.
  Ghi slot không active, đọc lại CRC pixels, ghi header cuối cùng, kiểm tra header rồi mới publish.
  Boot chọn record hợp lệ mới nhất và fallback record cũ nếu bản mới hỏng.
- Thiếu RAM/ghi lỗi giữ nền cũ; không format storage hoặc erase NVS.
- Firmware chỉ ghi ảnh/GIF tải lên, không tạo tombstone mới. Vẫn đọc tombstone từ firmware cũ
  để không làm ảnh từng bị xóa xuất hiện lại khi nâng cấp; không xóa/format ảnh hiện tại.
- Pixel buffer nằm trong PSRAM; UI giữ bản sao riêng. HTTP/flash không chạy trong UI task.
  Vẫn cần đo latency/cache stall trên board.

## API Nội Bộ

| Endpoint | Điều kiện | Nội dung |
|---|---|---|
| HTTP GET/HEAD đường dẫn lạ | Phiên mở, client thật/socket AP | Chuyển về HTTP IP AP, không token |
| HTTP GET `/`, `/app.js`, `/style.css` | Phiên mở, Host/socket AP | Trang nhúng, không CDN |
| POST `/session` | Phiên mở, Origin/Host/socket AP | Body rỗng; token và mã trang; hủy leave cũ, không PIN |
| POST `/image` | Token + Origin | Octet-stream đúng 115200 byte hoặc image/gif ≤1 MiB, tối đa 240×240; tự lưu |
| GET `/status` | Token | Pending/saving/revision/error, không credential; cập nhật liveness |
| POST `/logout` | Token + Origin | Thu hồi và đóng phiên/AP |
| POST `/leave` | Token + Origin + X-Hisu-Page | Body rỗng; đóng sau 8 giây nếu không tải lại |
| GET/POST `/settings` | Token; POST có Origin | Whitelist setting, kiểu và khoảng giá trị |
| GET `/wifi` | Token | Trạng thái và scan, không mật khẩu |
| POST `/wifi/scan`, `/wifi/join`, `/wifi/power` | Token + Origin | Queue đến network worker |
| POST `/time` | Token + Origin | UTC từ điện thoại |

## Kiểm Thử

```powershell
.\tools\test-ui.ps1
node tests/test_media_web.mjs
node tests/test_setup_config.mjs
node tests/test_media_browser.mjs
.\tools\build.ps1
```

Browser test dùng Playwright, có thể đặt `HISU_BROWSER_PATH` trỏ Chrome đã cài.
Mock API kiểm tra tự kết nối không PIN, Wi-Fi/settings/time, upload tự lưu, hết hạn/logout và retry.
Không thay thế kiểm chứng captive portal/AP/flash trên board.

Đã chạy: UI/Wi-Fi/pin 20/20 (có kiểm tra 256 mẫu biểu diễn entropy thành mật khẩu),
lịch, CRC/header/tombstone, RGB565/crop và DNS với 10000 packet fuzz.
Browser mock qua 1280×800, 390×844 và 320×720, đã kiểm tra screenshots không đè chữ.
Test source cấu hình kiểm tra một HTTP server, thứ tự route, QR HTTP và không có điều kiện station-connected khi mở Setting.

**Checklist board sau flash:**

- Wi-Fi QR đọc được ở màn 240×240, mật khẩu đổi sau đóng/mở phiên; SSID giữ nguyên.
- Android/iOS tự mở captive portal hoặc QR URL tự hiện sau kết nối; ngắt điện thoại trả về QR Wi-Fi.
- BOOT ở QR/thông tin bật/tắt Setting; dùng web quá 5 phút không tự đóng.
- AP mở khi station tắt; quét/join/tắt station không tắt AP; không quảng bá BLE.
- Bấm Đã kết nối hoặc BOOT ở QR/thông tin đóng DNS/HTTP/AP; token cũ không dùng lại.
- Ngắt hotspot >10 giây, đóng web >8 giây hoặc mất liveness >90 giây đều đóng; reload nhanh giữ phiên.
- Host/Origin sai, không token, truy cập station/LAN, body thiếu/dài và ngắt upload đều bị chặn.
- Upload tự lưu, màu RGB565 đúng, reboot giữ nền; cắt nguồn lúc commit không làm mất bản cũ hợp lệ.
- Phụ đề/menu/theme/độ sáng và boot GIF không bị ảnh hưởng. Boot GIF không có icon phủ.
- Heap/PSRAM/stack/watchdog và xung đột subnet khi AP+STA cùng hoạt động.

Build/test trên máy không flash hoặc erase dữ liệu board. Captive portal và flash commit
cần thử trên phần cứng thật trước khi coi bring-up hoàn tất.
