# Chat Hi Su Qua USB

Cửa sổ riêng trên máy tính, không phải Debug Console. Chỉ dùng USB để chuyển tin
nhắn; Gemini Live, mic, loa, công cụ và trí nhớ vẫn chạy trên board. Không gửi API
key cho công cụ trên máy tính và không gọi Gemini từ máy tính.

## Mở Và Sử Dụng

1. Đóng `idf.py monitor` hoặc ESP-IDF Monitor để nhả cổng COM. Không chạy hai
   chương trình cùng giữ cổng của board.
2. Trong VS Code mở thư mục HisuOS, chọn **Terminal → Run Task → HisuOS: Chat Hi Su**.
   Hoặc chạy `./tools/chat.ps1` trong terminal PowerShell tại HisuOS.
3. Chọn USB/COM rồi bấm **Kết nối**. Launcher chọn sẵn `idf.port` trong cài đặt
   VS Code nếu cổng đó có mặt; cũng có thể chạy `./tools/chat.ps1 -Port COM8`.
4. Nhấn đúp BOOT hoặc gọi Hi Su như bình thường. Khi board lắng nghe, nhập câu rồi
   bấm **Gửi** hoặc Enter. Có thể tự đặt âm lượng board bằng 0 khi cần im lặng.
5. Nhấn BOOT trong chat để kết thúc phiên; khi kể chuyện/nhạc thì về Chat. Ngắt USB
   trên cửa sổ chỉ đóng cổng máy tính, không tự kết thúc cuộc trò chuyện trên board.

Khi chỉ muốn nhập chữ ở nơi ồn, bỏ chọn **Nhận giọng nói** và đợi board xác nhận.
Mặc định vẫn bật mic như trước; âm lượng/loa không bị thay đổi. BOOT vẫn mở/đóng
phiên như bình thường. Khi tắt nhận giọng nói, không nhận wake word hoặc gửi mic
cho Gemini; I2S/AFE vẫn chạy để đọc USB ổn định, đây không phải ngắt nguồn mic.
Lượt mic đang thu bị hủy, còn lời đáp hoặc truyện đang phát không bị ngắt.
Sau khi đóng cửa sổ/mất USB khoảng 5 giây, board tự khôi phục nhận giọng nói.
Tùy chọn này không ghi flash và cần firmware mới hỗ trợ `HISU_MIC`; firmware cũ
sẽ từ chối gói điều khiển, không gửi nó thành câu hỏi cho AI.

Không cần flash lại nếu board đã có bản hỗ trợ `HISU_INPUT`, `HISU_REPLY` và nhận
chữ qua UART. Cửa sổ mới chỉ là giao diện cho giao thức hiện có. Khi chỉ mở cửa
sổ hoặc chạy kiểm thử, công cụ không tự mở cổng, reset hay phát âm thanh trên board.
Khi kết nối, công cụ đặt DTR/RTS về false trước khi mở và không chạy chuỗi reset
ESP-IDF; driver USB của từng máy vẫn có thể gây xung điều khiển khi mở cổng.
Sau khi mở cổng, gửi Ctrl+U và newline để firmware bỏ toàn bộ dòng gõ dở từ lần
kết nối trước. Không gửi newline đơn có thể vô tình thực thi một lệnh bị cắt.
Đây chỉ là đồng bộ đầu vào, không gọi Gemini hay thay đổi cài đặt trên board.

## Tin Nhắn Và Log

- Tối đa 255 byte UTF-8 mỗi tin, tính theo byte chứ không phải số chữ. Giữ nguyên
  cùng giới hạn của firmware; không gửi nhiều dòng hoặc ký tự điều khiển.
- Chỉ thêm câu của người dùng vào hội thoại khi board xác nhận `HISU_INPUT`.
  Khi board bận/từ chối hoặc chưa xác nhận sau 5 giây, giữ câu trong ô nhập.
  Không tự gửi lại, kể cả sau mất USB hay mở lại cổng, để tránh lặp tác vụ.
- Ghép các mảnh `HISU_REPLY` theo epoch/generation; giữ khoảng trắng và dấu tiếng
  Việt. Transcript mic `HISU_HEARD` cũng được ghép theo lượt và hiện là **Mic nghe được**,
  nhưng không được tính là xác nhận tin gõ. Log VAD, Wi-Fi, TLS không chen vào
  hội thoại. Transcript là điều Gemini nhận dạng, có thể sai khi có tiếng nền.
- Dòng `Hi Su ...` chạy dấu chấm trong khi board xử lý/phát lời đáp, dựa trên
  `HISU_STATE`. Hết lượt, chờ nghe, mất kết nối hoặc lỗi thì tự ẩn, không chèn
  dấu chấm vào nội dung hội thoại. Firmware cũ chưa có trạng thái thì chỉ hiện
  từ lúc nhận tin đến mảnh trả lời đầu tiên. Có giới hạn 60 giây không cập nhật
  để tránh dấu chấm chạy mãi khi board treo/mất trạng thái.
- Toàn bộ dữ liệu nhận từ board được ghi UTF-8 vào `.cache/chat-logs/`. **Mở log**
  mở file của kết nối hiện tại/gần nhất. Mỗi kết nối có file riêng, xoay vòng
  5 MiB/file và giữ tối đa ba file cho kết nối đó; các kết nối trước vẫn giữ lại.
  Log có thể chứa câu hỏi và câu trả lời riêng tư. Không nhập key/mật khẩu vào chat.
- Hội thoại trên cửa sổ giới hạn khoảng 100.000 ký tự, không lưu bền riêng ngoài
  file log. Đóng cửa sổ dừng luồng đọc và đóng cổng; không giữ tin để gửi lần sau.

## Môi Trường Và Kiểm Thử

`chat.ps1` tìm Python ESP-IDF v5.5.5 qua EIM hoặc môi trường đang kích hoạt; cần
`tkinter` và `pyserial` (đã có trong Python ESP-IDF trên máy hiện tại). Không cài
thêm gói qua mạng. Có thể chọn Python khác bằng tham số `-Python <đường dẫn>`.
Lỗi khởi động cửa sổ được ghi vào `launcher-*-error.log` trong cùng thư mục cache.

Chạy `./tools/chat.ps1 -Test` để kiểm tra bộ tách log, UTF-8, giới hạn tin, mô phỏng
cổng bận/mất USB/ghi thiếu, log xoay vòng và widget Tk ẩn. Kiểm thử không mở COM
thật hoặc kích hoạt mic/loa. Vẫn cần kiểm tra USB và nhấn nút thực tế trên board.
