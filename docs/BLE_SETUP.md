# Setup Wi-Fi và đồng hồ qua BLE

**Bluetooth tạm ngưng:** firmware hiện tại không khởi động BLE host, không có
mục Bluetooth hoặc icon BLE. Dùng hotspot và trang [Setting](WALLPAPER_SETUP.md).
Hướng dẫn bên dưới dành cho phiên bản cũ, giữ lại để tham khảo.

Mở Menu → Bluetooth: tự mở theo lựa chọn hiện tại và hiện QR (mặc định 5 phút). BOOT đổi **Tắt / 1 / 5 / 10 / 30 phút**, Vol− xác nhận mở/gia hạn hoặc tắt, Vol+ hoặc giữ BOOT trở lại. Lựa chọn giữ trong phiên chạy, reboot về 5 phút. Quét QR để mở trang BLE, bấm **Kết nối HisuOS**. Khi ghép, QR chuyển sang mã 6 số để nhập vào hộp thoại trên điện thoại. Thiết bị tên `HisuOS-XXXX`; QR chỉ chứa URL và tên thiết bị để lọc danh sách, không chứa PIN hoặc credential. BLE không quảng bá sau reboot cho đến khi mở, tự ngắt sau thời gian đã xác nhận. Chưa lưu bond, mỗi kết nối ghép lại. GATT yêu cầu link mã hóa và xác thực bằng passkey; không đọc lại hoặc log mật khẩu Wi-Fi. Web Bluetooth không cung cấp tên điện thoại, nên board giữ trạng thái ghép nối chung, không có ô nhập tên.

## Xuất bản trang cho QR

QR mặc định trỏ tới `https://iamhisu.github.io/HisuOS/ble/`. Địa chỉ này **chỉ hoạt động sau khi xuất bản thành công**, chưa được xuất bản bởi Codex.

1. Trong repository GitHub `IamHisu/HisuOS`, mở **Settings → Pages → Build and deployment → Source → Deploy from a branch**, chọn **main → /docs → Save**.
2. Push `docs/ble/`, `docs/index.html`, `docs/.nojekyll` và workflow đã sửa lên nhánh `main`. Chỉ đổi setting mà chưa push các file này thì `/ble/` vẫn 404.
3. Chờ workflow tự động của GitHub **pages build and deployment** thành công.
4. Mở link trên Chrome Android và kiểm tra trang trước khi thử quét QR trên board.

Nguồn `/docs` xuất bản trang BLE cùng các tài liệu trong `docs`, không đưa firmware hoặc NVS lên site. Workflow riêng **Publish BLE companion** chỉ còn chạy thủ công nếu chọn Source **GitHub Actions**, để tránh hai cách deploy tự động cùng chạy. Nếu đổi nơi host, chỉnh **idf.py menuconfig → HisuOS UI → HTTPS URL of the BLE companion page**, rồi build/flash lại. QR không bỏ qua bước bấm kết nối và xác nhận ghép nối của trình duyệt.

## Trang điều khiển

Trang `docs/ble/index.html` + `app.js` cho phép kết nối, chọn Wi-Fi, gửi giờ/múi giờ. Nhấn **Kết nối HisuOS**, ghép bằng mã; trang tự bật radio và quét danh sách Wi-Fi. Mạng đã lưu đứng trước theo mức sóng, rồi tới mạng chưa lưu. Bấm chọn mạng mới hiện form; mạng mở ẩn ô mật khẩu, mạng có khóa yêu cầu nhập mật khẩu. Có nút Quét lại; mạng Enterprise không cho chọn. Chỉ lưu mạng sau nhận IP thành công. Giờ được gửi ngay khi kết nối và có nút đồng bộ lại. Firmware và trang web phải cùng được cập nhật để có characteristic danh sách mới.

Web Bluetooth cần HTTPS hoặc localhost. [Chrome hỗ trợ trên Android/máy tính, không hỗ trợ kết nối website/Bluetooth trên iPhone/iPad](https://support.google.com/chrome/answer/6362090?co=GENIE.Platform%3DAndroid&hl=en-GB). [Tài liệu Chrome](https://developer.chrome.com/docs/capabilities/bluetooth/).

Trên máy tính có Bluetooth, chạy từ project rồi mở `http://localhost:8765` bằng Chrome/Edge:

```powershell
python -m http.server 8765 --bind 127.0.0.1 --directory docs/ble
```

Trên Android, dùng host HTTPS cho hai file hoặc chuyển tiếp cổng localhost qua USB. Địa chỉ HTTP máy tính trong LAN không đáp ứng secure context. Chưa xuất bản trang lên Internet. Client BLE hỗ trợ GATT cũng dùng được giao thức dưới đây; app ESP BLE Provisioning không tương thích giao thức riêng này.

## GATT

Service: `e3a10000-7d4b-4ee0-b417-6d9f67485501`. Các characteristic cùng hậu tố `-7d4b-4ee0-b417-6d9f67485501`.

| Tiền tố UUID | Quyền | Dữ liệu |
|---|---|---|
| `e3a10001` | Write authenticated | SSID UTF-8 1–32 byte, không NUL; xóa mật khẩu staging cũ. |
| `e3a10002` | Write authenticated | Password ASCII 8–63 byte, PSK hex 64 byte hoặc rỗng. |
| `e3a10003` | Write authenticated | Byte `01`: kết nối bằng credential staging. |
| `e3a10004` | Write authenticated | 10 byte: UTC Unix giây uint64 LE + offset phút phía đông UTC int16 LE. |
| `e3a10005` | Read authenticated | JSON `wifi` (connected/joining/disconnected), `error` (esp_err_t), `time` (bool). |
| `e3a10007` | Read/write authenticated | Ghi byte `01` để bật Wi-Fi và quét. Đọc packet danh sách, tối đa 427 byte, hỗ trợ ATT Read Blob. |

Packet danh sách: version `01`, flags (bit 0 radio bật, bit 1 đang quét), số mạng, mã lỗi uint32 little endian; mỗi mạng gồm độ dài SSID 1 byte + SSID UTF-8 + RSSI int8 + flags (bit 0 có khóa, bit 1 đã lưu, bit 2 hỗ trợ). Snapshot cố định cho từng lần đọc dài; không gửi mật khẩu đã lưu.

Ghi SSID → password → commit theo thứ tự, Write With Response, MTU ưu tiên 128 cho password dài. Xóa staging khi commit thành công, ngắt hoặc hết cửa sổ. Giờ chấp nhận 2024–2099, offset −720..+840 phút. Múi giờ lưu NVS; reboot/mất nguồn chưa có giờ hợp lệ hiện `--:--`. Khi nhận IP, SNTP đồng bộ từ `pool.ntp.org`; mặc định UTC+7 trước khi nhận múi giờ từ điện thoại. Không có RTC giữ giờ qua mất nguồn.

## Kiểm chứng

Mã PIN được giữ trên trang Bluetooth khi ghép thất bại, kèm mã lỗi BLE. Nó ẩn sau khi xác thực thành công, đóng/hết cửa sổ setup hoặc bắt đầu lần kết nối mới. PIN cũ trên màn hình thất bại không dùng cho lần ghép mới; nhập mã mới khi web kết nối lại. Thay đổi này không kéo dài timeout SMP của stack và không xác nhận đã sửa nguyên nhân lỗi ghép.

Nếu Windows báo `Connection failed` dù đã nhập đúng mã, dùng firmware có log `HisuBLE` và chạy `./tools/build.ps1 -Action monitor` sau khi flash. Thử ghép từ web một lần, lấy các dòng `Connected`, `Pairing action`, `Passkey supplied to stack`, `Security result` và `Disconnected`. Log không chứa PIN hoặc mật khẩu Wi-Fi. Mã lỗi xác thực được giữ qua lần quảng bá lại, đến khi mở lại cửa sổ hoặc kết nối mới. Console của web ghi lỗi gốc để phân biệt lỗi GATT với lỗi ghép đôi; không kết luận nguyên nhân chỉ từ hộp thoại Windows.

`node tests/test_ble_companion.mjs`: đã chạy kiểm tra UUID, giới hạn SSID/password và packet UTC/múi giờ little endian. Firmware đã build; chưa chạy radio trên board.

Cần kiểm tra board: PIN đúng/sai, quyền trước authentication, đóng/gia hạn/tự ngắt 5 phút; Wi-Fi đúng/sai, đang tắt/reconnect, giữ credential cũ khi lỗi; reconnect BLE không tái dùng staging; đồng bộ giờ/múi giờ, reboot và NTP; GIF/BLE/Wi-Fi/ADC cùng chạy, heap/stack/watchdog.
