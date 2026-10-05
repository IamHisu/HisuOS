# Setup Wi-Fi và đồng hồ qua BLE

Mở Menu → Bluetooth: tự mở kết nối 5 phút và hiện QR. Quét QR để mở trang BLE, bấm **Kết nối HisuOS**. Khi ghép, QR chuyển sang mã 6 số để nhập vào hộp thoại trên điện thoại. Vol− bật/tắt kết nối, BOOT **Mở 5 phút** gia hạn, Vol+ hoặc giữ BOOT trở lại. Thiết bị tên `HisuOS-XXXX`; QR chỉ chứa URL và tên thiết bị để lọc danh sách, không chứa PIN hoặc credential. BLE không quảng bá sau reboot cho đến khi mở, tự ngắt sau 5 phút. Chưa lưu bond, mỗi kết nối ghép lại. GATT yêu cầu link mã hóa và xác thực bằng passkey; không đọc lại hoặc log mật khẩu Wi-Fi.

## Xuất bản trang cho QR

QR mặc định trỏ tới `https://iamhisu.github.io/HisuOS/ble/`. Địa chỉ này **chỉ hoạt động sau khi xuất bản thành công**, chưa được xuất bản bởi Codex.

1. Trong repository GitHub `IamHisu/HisuOS`, mở **Settings → Pages → Build and deployment → Source → GitHub Actions**.
2. Push `.github/workflows/ble-pages.yml` và `tools/ble-companion/` lên nhánh `main`.
3. Chờ workflow **Publish BLE companion** thành công. Có thể chạy lại từ **Actions → Publish BLE companion → Run workflow**.
4. Mở link trên Chrome Android và kiểm tra trang trước khi thử quét QR trên board.

Workflow chỉ xuất bản `index.html` và `app.js` trong thư mục `/ble/`, không đưa firmware hoặc NVS lên site. Nếu đổi nơi host, chỉnh **idf.py menuconfig → HisuOS UI → HTTPS URL of the BLE companion page**, rồi build/flash lại. QR không bỏ qua bước bấm kết nối và xác nhận ghép nối của trình duyệt.

## Trang điều khiển

Trang `tools/ble-companion/index.html` + `app.js` cho phép kết nối, nhập Wi-Fi, gửi giờ/múi giờ. Nhấn **Kết nối HisuOS**, ghép bằng mã, rồi nhập Wi-Fi. Để trống mật khẩu với mạng mở. Wi-Fi tự bật nếu đang tắt; chỉ lưu mạng sau nhận IP thành công. Giờ được gửi ngay khi kết nối và có nút đồng bộ lại.

Web Bluetooth cần HTTPS hoặc localhost. [Chrome hỗ trợ trên Android/máy tính, không hỗ trợ kết nối website/Bluetooth trên iPhone/iPad](https://support.google.com/chrome/answer/6362090?co=GENIE.Platform%3DAndroid&hl=en-GB). [Tài liệu Chrome](https://developer.chrome.com/docs/capabilities/bluetooth/).

Trên máy tính có Bluetooth, chạy từ project rồi mở `http://localhost:8765` bằng Chrome/Edge:

```powershell
python -m http.server 8765 --bind 127.0.0.1 --directory tools/ble-companion
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

Ghi SSID → password → commit theo thứ tự, Write With Response, MTU ưu tiên 128 cho password dài. Xóa staging khi commit thành công, ngắt hoặc hết cửa sổ. Giờ chấp nhận 2024–2099, offset −720..+840 phút. Múi giờ lưu NVS; reboot/mất nguồn chưa có giờ hợp lệ hiện `--:--`. Khi nhận IP, SNTP đồng bộ từ `pool.ntp.org`; mặc định UTC+7 trước khi nhận múi giờ từ điện thoại. Không có RTC giữ giờ qua mất nguồn.

## Kiểm chứng

`node tests/test_ble_companion.mjs`: đã chạy kiểm tra UUID, giới hạn SSID/password và packet UTC/múi giờ little endian. Firmware đã build; chưa chạy radio trên board.

Cần kiểm tra board: PIN đúng/sai, quyền trước authentication, đóng/gia hạn/tự ngắt 5 phút; Wi-Fi đúng/sai, đang tắt/reconnect, giữ credential cũ khi lỗi; reconnect BLE không tái dùng staging; đồng bộ giờ/múi giờ, reboot và NTP; GIF/BLE/Wi-Fi/ADC cùng chạy, heap/stack/watchdog.
