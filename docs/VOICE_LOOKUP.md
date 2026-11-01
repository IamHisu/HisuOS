# Tra Thoi Tiet, Gia Vang Va Tin Tuc

Chat co them `lookup_info` (khong cap quyen trong Reader/Interpreter).
Ba chu de dung chung mot khai bao tool, khong doi luong Gemini Live.
Chat chinh va Chat du phong deu dung duoc; Reader/Interpreter van bi chan.

## Cach Dung

- "Chieu nay o Can Tho co mua khong?"
- "Ngay mai o Da Lat nhiet do the nao?"
- "Gia vang SJC hom nay bao nhieu?"
- "Gia nhan SJC va vang the gioi thi sao?"
- "Hom nay co tin gi moi khong?"

## Tin tức và slang qua Gemini Search

Dùng key Gemini Chat chính đã lưu trên board. Không cần key dịch vụ tin tức
khác. Một request POST tới
`https://generativelanguage.googleapis.com/v1beta/interactions`, model
`gemini-2.5-flash-lite`, tool `google_search`, `store=false` và giới hạn 4096
output token. Model có tìm kiếm web; quyền truy cập và quota phụ thuộc key.
Không tự chuyển model/key, bật thanh toán hay lặp lại request lỗi.
Chọn 2.5 Flash-Lite vì Google hiện công bố Search Grounding trên free tier
(tối đa 500 request/ngày, dùng chung với 2.5 Flash); 3.5 Flash-Lite không có
Search Grounding API ở free tier. Quota thực tế cần xem trong AI Studio.
[Bảng giá](https://ai.google.dev/gemini-api/docs/pricing#gemini-2.5-flash-lite).

- `lookup_info({topic:"news"})`: mặc định `mode:"read"`, bản tin 300–450 từ.
- `mode:"brief"`: điểm tin nhanh 100–150 từ.
- `mode:"detail", query:"chủ đề hoặc tiêu đề vừa nghe"`: 400–550 từ về tin đó.
- `topic:"slang", query:"cụm từ và ngữ cảnh ngắn"`: tra nghĩa, sắc thái, ví dụ
  và trường hợp nên tránh. Không có query thì từ chối; tối đa 256 byte UTF-8.

Tin tổng quát ưu tiên ba nhóm công nghệ/robot Việt Nam, AI và kinh tế quốc tế
trong 24 giờ gần đây. Đây là bản tổng hợp có nguồn, không phải đọc nguyên văn
bài báo. Model phải nêu ngày/nguồn, phân biệt ngày đăng và ngày xảy ra, không
bịa chi tiết. Thời gian mới và nội dung bài vẫn cần kiểm chứng trên board;
firmware xác minh cấu trúc grounding, không xác minh độc lập mọi sự kiện.

Chỉ nhận response `completed` có bước tìm kiếm, nội dung, URL citation và
Search Suggestions. Thiếu grounding hoặc body lỗi/cắt cụt thì báo chưa tra
được, không giả làm tin mới từ kiến thức mô hình. HTTP 403/429 báo key/quota.
Deadline 45 giây; response tối đa 64 KiB, JSON sâu tối đa 16. Nội dung nói tối
đa 12 KiB, tối đa 12 nguồn và 5 gợi ý tìm kiếm (tổng HTML 8 KiB). Không fetch
URL do mô hình trả về, không theo redirect. Kết quả web không được cache để
trả lời yêu cầu tiếp theo. Chỉ giữ bản tra cứu gần nhất trong RAM làm lịch sử
hiển thị, không ghi flash hay dùng làm từ điển slang tự học.

Setting > Thiết bị > Nguồn tra cứu hiển thị nội dung cùng liên kết nguồn và
Search Suggestions gốc; giao diện chặn HTML chủ động/script và URL khác HTTPS.
Nội dung web là dữ liệu không đáng tin, không có quyền đổi cấu hình hay gọi tool.
Không chạy bộ gom slang nền bằng Search Grounding.

Tài liệu đã kiểm tra:
- [Google Search Grounding](https://ai.google.dev/gemini-api/docs/google-search)
- [Interactions API](https://ai.google.dev/api/interactions-api)
- [Điều khoản Grounding](https://ai.google.dev/gemini-api/terms#grounding-with-google-search)
- [GNews](https://docs.gnews.io/): có full content ở gói trả phí, nhưng cần key
  riêng nên không chọn cho cấu hình dùng key Gemini hiện có.

## Thoi Tiet Va Gia Vang

Thoi tiet can dia diem tai Viet Nam. Khong suy ra vi tri tu IP. Neu co nhieu
noi trung ten, tool tra ve toi da ba `choices`; AI hoi lai, sau do gui lai
`place` cu va `location_id` da chon. ID phai khop ket qua geocoding moi,
khong cho model tu gui toa do hay URL. Ten khop chinh xac duy nhat duoc chon
ngay, khong bat nguoi dung xac nhan lai. `day_offset` chi nhan 0, 1, 2.

Open-Meteo cung cap hien tai va 24 moc gio cua ngay duoc hoi (hom nay, mai,
mot). Cau hoi ve buoi chieu phai dung du bao theo gio, khong dung nhiet do
hien tai hay xac suat mua ca ngay thay the. Gio co dinh UTC+7. Dong ho board
phai dong bo; cache duoc bo khi sang ngay moi.

Gia vang lay tu Vang.today: `summary` mac dinh gom SJC 9999, nhan SJC, PNJ
Ha Noi; `all` chi khi nguoi dung muon nghe het. Co the chon tung ma trong
schema. Gia noi dia la VND/luong; XAUUSD la tham chieu USD/troy ounce, khong
co gia ban trong payload da kiem tra. Khong tu gan ten "nhan" cho ma nu
trang. Du lieu cu hon 6 gio duoc danh dau `stale`, qua 48 gio thi tu choi.
Gia chi de tham khao, khong phai tu van dau tu hay bao gia giao dich.

## Tai Nguyen Va Loi

- Mot job toi da, khong tao task thuong truc: `info_lookup` 8192 byte stack
  trong PSRAM qua `xTaskCreateWithCaps`, uu tien 3. Worker khong ghi WebSocket.
  Worker cong bo ket qua roi suspend; consumer doi suspended va xoa bang
  `vTaskDeleteWithCaps` truoc khi tra ket qua. Hai consumer (Live/nhac) duoc
  serialize de khong xoa hai lan. Khong self-delete tao task don dep tam.
- Guard khoi tao giu toi thieu 10 KiB RAM noi bo, block lon nhat 2 KiB;
  HTTP van kiem tra toi thieu 6 KiB/block 2 KiB truoc moi request. Khong chi
  ha guard de ep tac vu chay. Log `LOOKUP: Rejected` ghi ly do va heap
  de phan biet sai tham so, thieu RAM va loi tao worker.
- Deadline 12 giay cho vang/thoi tiet (tin tức/slang 45 giây), HTTP timeout 1 giay, polling async.
  DNS/TLS cua SDK van co the giu CPU lau hon mot vong poll; khong cam ket
  thoi gian huy cung theo mili giay. Khong tu thu lai HTTP.
- Kiem tra RAM truoc khi tao task va truoc moi HTTP; TLS xac thuc chung chi,
  host/path co dinh, khong theo redirect. Thieu RAM thi bao khong tra duoc.
- Body JSON tối đa 16 KiB cho vàng/thời tiết (web 64 KiB), JSON sâu 10 cấp (web 16 cấp); chan body cat cut,
  overflow, byte NUL, sai don vi, null/gia tri ngoai mien, thieu gio du bao.
- Cache RAM mot ket qua moi nhat moi chu de: vang 5 phut, thoi tiet 10 phut.
  Tin tức/slang không cache để trả lời lại.
  Thay tham so thi tra lai. Gioi han yêu cầu mới cách nhau 2 giây (web 10 giây); cache
  khong bi chan boi gioi han nay. Khong luu vi tri/ket qua vao flash.
  Chuoi cache va ket qua worker duoc serialize thang vao PSRAM bang
  `cJSON_PrintPreallocated`, buffer tang 1/2/4/8/16/32 KiB, khong qua 32 KiB.
  Loi cap phat cache moi giu nguyen cache cu va TTL cu, khong gia han du lieu cu.
  Cay parse/ket qua va cache-hit dung ban cJSON rieng prefix `hisu_lookup_`,
  malloc/realloc co dinh PSRAM. Dung lai source managed cJSON 1.7.19, khong sua
  upstream hay doi hook toan cuc. Live chi muon args de doc; cay rieng khong
  di qua module khac. Envelope ket qua cung tao trong lookup/PSRAM, nhung raw
  JSON da serialize an toan thay vi parse lai toan bo du bao trong live_send.
  Call ID van escape bang JSON; khong nhung truc tiep body cua provider.
- Epoch/generation loai ket qua cu khi ngat loi, dong phien, doi key/mode.
  `pending_tools` giu luot mo cho den khi gui ket qua; `turnComplete` den
  som khong ket thuc luot. Bao pin/TTS/summary cho lookup don dep xong
  truoc khi mo them HTTPS luc nghi.
- Tim nhac khong mo them HTTPS khi lookup dang chay. Sau khi dong WSS de
  phat nhac, cho lookup da huy don dep TLS (toi da 12 giay) truoc khi resolve
  nhac; qua han thi bao loi nhac thay vi tranh RAM voi lookup.
- Khong co API key moi va khong them clip audio/phien ban partition.

## Nguon Va Dieu Kien

- [Open-Meteo forecast](https://open-meteo.com/en/docs) va
  [geocoding](https://open-meteo.com/en/docs/geocoding-api).
- [Open-Meteo terms](https://open-meteo.com/en/terms): free endpoint cho
  muc dich phi thuong mai; du lieu CC BY 4.0. Giu attribution Open-Meteo
  khi doc ket qua. Thuong mai can xem lai dieu kien dich vu.
- [Vang.today API](https://www.vang.today/vi/api): nguon tong hop ben thu ba,
  co the doi dinh dang/ha tang. Phan hoi cong khai da kiem tra 2026-10-20:
  `success`, `timestamp`, `prices` la object theo ma; khac vi du `data[]`
  tren trang tai lieu. Khong coi vi du tai lieu la du lieu thuc te.

## Kiem Thu

`tools/test-alert.ps1` chay ca lookup HTTP/parser mock: cache, don vi vang,
du bao 24 gio, dia diem mo ho, timeout, redirect, body khong hop le, huy,
task/malloc loi. `tools/test-voice.ps1` kiem tra ket qua async, turn boundary,
ket qua cu khong tru pending cua luot moi, va chan Reader/Interpreter.
Test lookup con kiem tra capability PSRAM cua chuoi, buffer tang/toi da/OOM,
giu cache cu khi thay the that bai, stack 8 KiB va log canh bao duoi 1536 byte.
Parser rieng va parser thuong cung duoc link/test, khong doi allocator cua nhau;
640 muc OOM parse va OOM envelope phai don sach allocation. Envelope kiem tra
ID co quote/backslash/control, JSON loi bo nho, gioi han va response da serialize.
Build kiem tra frame <= 2048 byte. Chua do high-water mark cua TLS/stack,
RAM va do muot tren board khi WSS + HTTPS cung chay. Log `LOOKUP` ghi HTTP,
do dai va stack con lai, khong log khoa hay dia diem rieng tu.
`RAM phase=before_json_parse/after_json_parse` giup quan sat cay JSON;
`worker_cleaned` do sau khi xoa cay va body. `Complete` ghi `stack_free`
high-water trong ca TLS/parse/serialize/log, `stack_bytes=8192`, `stack_low=1`
khi duoi 1536 byte. `Cache stored`/`Cache hit` khong ghi noi dung du lieu.
`Envelope ready` ghi RAM noi va stack live_send sau khi boc ket qua, khong log
dia diem/noi dung. Test lai tren board de xac nhan khong con sat day SRAM;
host test khong chung minh duoc audio muot hay free SRAM thuc te.


## Tự cập nhật slang từ nguồn mở (2026-10-30)

Đã bật mặc định theo yêu cầu người dùng. Mỗi lần cập nhật thành công cách nhau
ít nhất 24 giờ, kể cả sau reboot nhờ mốc giờ lưu NVS. Điều kiện: đồng hồ hợp lệ,
Internet ONLINE, không mở hotspot Setting, hệ thống rảnh, pin >=30% hoặc đang
sạc. Không thêm task thường trực; chạy trên worker live_send hiện có. Khi wake,
phát nhạc, nhắc việc hoặc mất mạng, HTTP được hủy ở vòng async tiếp theo.

Nguồn: MediaWiki categorymembers của `Vietnamese_internet_slang` trên English
Wiktionary, rồi REST `/api/rest_v1/page/definition/TERM`. Chỉ lấy nghĩa `vi`,
không lấy ví dụ trích dẫn. Tối đa 6 trang mới nhất trong một lần, 12 từ trong bộ
nhớ, mỗi nghĩa 320 byte. Bỏ HTML, giữ term/pageid, ghi nguồn Wiktionary
contributors và CC BY-SA 4.0; trang Setting có link từng mục và giấy phép.
Thời điểm thêm vào category không chứng minh mức phổ biến hiện tại, và nguồn
này không bao phủ mọi trend Gen Z/TikTok. Không quảng cáo là bắt kịp mọi trend.

Tải chỉ từ host Wiktionary cố định qua TLS, không theo redirect. URL term được
percent-encode. Body mỗi lần <=32 KiB PSRAM; deadline chung 30 giây, mỗi HTTP
5 giây; kiểm tra RAM trước HTTP. Lỗi giữ bản cũ, chờ một giờ rồi thử; bị wake
thì chờ 30 giây. Không gọi Gemini hay Search Grounding để gom dữ liệu nền.

Từ điển riêng với trí nhớ cá nhân, tối đa 6144 byte JSON trong NVS. Chỉ thay bản
RAM sau khi commit thành công; không ghi flash từ callback HTTP. Chat/Chat
backup nhận bản mới ở phiên kế tiếp; Reader/Interpreter không nhận. Prompt coi
nội dung từ điển là dữ liệu, không phải chỉ thị hay quyền gọi tool, dùng vừa
phải theo ngữ cảnh. Tắt tự cập nhật ở Setting > Thiết bị vẫn giữ từ đã tải.

Tài liệu nguồn:
- https://en.wiktionary.org/wiki/Category:Vietnamese_internet_slang
- https://www.mediawiki.org/wiki/API:Categorymembers
- https://en.wiktionary.org/wiki/Wiktionary:Copyrights

API đã được thử GET trên máy phát triển. Firmware và UI được kiểm tra riêng;
chu kỳ 24 giờ, mức pin, hủy khi wake và NVS còn cần chạy thử trên board.
