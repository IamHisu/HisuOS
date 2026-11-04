# Tin công nghệ nhiều nguồn, không dùng Google Search

Luồng: GenK / QbitAI / IT之家 / TechCrunch → chuẩn hóa và lọc → Gemini 3.5 Flash-Lite dịch,
tóm tắt → tool response → Gemini Live đọc tiếng Việt. Không dùng RSS VnExpress,
không gọi Google Search Grounding trong đường `topic=news`, không đổi billing.
Slang vẫn là đường riêng hiện có, chưa được chuyển sang nguồn mới.

## Nguồn đã kiểm tra

- GenK: `https://genk.vn/ajax-home/page-1.chn` trả HTML danh sách bài. Đây là
  endpoint tải thêm của website, không phải API JSON có hợp đồng ổn định.
  Parser chỉ lấy tiêu đề, sapo, URL GenK và thời gian đăng, không tải toàn bài.
- QbitAI / 量子位: WordPress REST API tại `www.qbitai.com/wp-json/wp/v2/posts`,
  chỉ yêu cầu id, date_gmt, link, title, excerpt; chuyên tin AI/agent/robot/chip.
- IT之家 / IT Home: `https://api.ithome.com/json/newslist/news`, lấy newslist,
  bỏ bài có aid/quảng cáo, lọc AI/robot/chip/phần cứng và giới hạn host bài gốc.
- TechCrunch: WordPress REST API `https://techcrunch.com/wp-json/wp/v2/posts`,
  `per_page=6`, `_fields=id,date_gmt,link,title,excerpt`; mẫu 6 tin chỉ 2756 byte,
  parser C lọc được 2 bài phù hợp. Ngày dùng UTC. Có con trỏ trang như QbitAI.
- Đã bỏ Tinh tế khỏi schema, danh sách nguồn và code tải/parser: trên board
  chỉ tải được khoảng 237 KB/1,8 MB trong 7 giây, chưa tới dữ liệu bài viết.
  Không còn lượt nền hoặc lượt fallback tới Tinh tế.

Các endpoint được xác minh HTTP 200 ngày 04/11/2026; chúng không bảo đảm sẽ giữ
schema mãi. Khi cấu trúc đổi hoặc nguồn lỗi, bỏ nguồn đó, không bịa dữ liệu.

## Thời gian và không lặp tin

- Chỉ nhận bài trong 7 × 24 giờ trước lúc tra cứu; bỏ ngày lỗi/quá tương lai.
  QbitAI dùng date_gmt UTC, IT之家 dùng giờ Trung Quốc UTC+8, GenK UTC+7.
- Lọc trùng URL và tiêu đề đã chuẩn hóa. Model nhận thêm 24 tiêu đề đã nghe
  gần nhất để tránh kể lại cùng sự kiện từ nguồn khác; lọc ngữ nghĩa là bước
  mô hình thực hiện, không phải bảo đảm tuyệt đối với mọi cách diễn đạt.
- Lịch sử tối đa 128 bài trong PSRAM, tồn tại qua các phiên Live. Dấu URL và
  tiêu đề băm cùng ngày đăng được lưu riêng vào blob NVS `news_history`, tối
  đa 1552 byte, không chiếm 24 ô trí nhớ cá nhân và không lưu nội dung bài báo.
  Có version/checksum và kiểm tra độ dài/count. Sau reboot khôi phục các dấu
  này để không đọc lại cùng URL/tiêu đề; tiêu đề đầy đủ dùng so ngữ nghĩa chỉ
  giữ trong RAM nên lọc cùng sự kiện nhưng khác tiêu đề sau reboot còn hạn chế.
  Không ghi NVS/flash từ task có stack PSRAM.
- Chỉ commit lịch sử khi lượt trả lời hiện tại hoàn tất. Hủy/ngắt lượt hoặc
  lỗi trước khi phát xong không đánh dấu toàn bộ bản tin là đã nghe. Tín hiệu
  hoàn tất lượt không chứng minh từng câu đã được model đọc nguyên vẹn.
- `lookup_info({topic:"news", repeat:true})` chỉ dùng khi người dùng yêu cầu
  đọc lại; vẫn áp dụng giới hạn 7 ngày. Mặc định repeat=false.
- Kho tin RAM tối đa 64 mục, hiệu lực 10 phút; mỗi lần chỉ đưa tối đa 12 ứng
  viên chưa đọc cho Gemini chọn 0–3 sự kiện. Còn ứng viên thì không tải nguồn
  lại, chỉ gọi Gemini tóm tắt. Mỗi lượt bình thường chỉ tải một nguồn nếu cần.
- Thiếu tin hoặc nguồn lỗi mới thử thêm nguồn khác (tối đa 2 request nguồn
  trong một lượt), xoay vòng IT之家 → GenK → QbitAI → TechCrunch. GenK/QbitAI/TechCrunch giữ con trỏ
  trang để lần sau tra tiếp, tối đa 8 trang/nguồn trong vòng đời kho. Không
  reset về vài bài đầu mỗi câu hỏi. Bài đã đọc bị loại khỏi kho để chừa chỗ mới.
- Nguồn lỗi được cooldown 5 phút, không đợi timeout lại ở từng câu hỏi. Trang
  lỗi không được coi là đã quét xong. Chưa tra xong thì báo chưa lấy thêm được,
  không suy ra đã hết tin công nghệ trong cả tuần.
- Nếu Gemini chọn rỗng trong một batch, tự thử một batch khác và ưu tiên nguồn
  tiếp theo, tối đa hai lần tóm tắt và hai GET nguồn trong toàn lượt. Các mục
  bị bỏ qua chỉ bị loại khỏi lần thử lại này, không ghi là đã nghe. Nếu vẫn
  không chọn được, báo `news_scan_incomplete`, không suy ra đã hết tin.
- `source` nhận `auto` (mặc định), `genk`, `ithome`, `qbitai`, `techcrunch`.
  Khi người dùng chỉ định nguồn, lọc kho và tải đúng nguồn đó; không trả bài
  của nguồn khác. Ví dụ `lookup_info({topic:"news",source:"techcrunch"})`.
- Kết quả có remaining_candidates và chỉ dẫn bắt buộc gọi lại lookup_info
  khi người dùng hỏi tin khác/tiếp theo. Đây là chỉ dẫn cho model; cần xác
  minh việc chọn tool trên Live thật, không coi test prompt là bằng chứng runtime.
- Hết mục phù hợp thì nói rõ, không lấy kiến thức cũ của model để lấp chỗ trống.

NVS đọc ở đầu `live_send` (stack nội bộ); ghi khi phiên đã đóng và loa/nhạc
đã rảnh, cùng lịch housekeeping 5 giây. Cần thoát chat và đợi vài giây trước
khi tắt nguồn để chắc lượt mới đã lưu. Mất điện trước flush có thể mất dấu mới.
Chỉ ghi khi lịch sử đổi, lỗi ghi giữ bản RAM và thử lại; không tự erase NVS hay
xóa trí nhớ cá nhân. Snapshot loại bài quá 7 ngày, mục cũ nhất bị thay khi đầy;
đọc lại theo yêu cầu không chiếm thêm mục trùng.

## Giới hạn và an toàn dữ liệu

Source GET danh sách có deadline 5 giây (QbitAI 2,5 giây), body tối đa 128 KiB,
JSON tối đa 12 tầng, kho chuẩn hóa tối đa 64 bài, lô tóm tắt tối đa 12 bài.
DNS/TLS có thể khiến thời gian thực tế vượt một vòng poll. Chỉ URL cố định được firmware tải; không
theo redirect hoặc tự fetch URL trong output mô hình. Nguồn lỗi có thể được
bỏ qua nếu nguồn khác còn dữ liệu hợp lệ. Khi các nguồn lỗi thì báo chưa lấy được.

Một POST `gemini-3.5-flash-lite:generateContent`, không có tools, JSON output,
tối đa 2048 output tokens. Model chỉ được chọn 0–3 index vào tập đầu vào và
viết bản tóm tắt tiếng Việt. Firmware lấy URL/nguồn/ngày từ dữ liệu gốc, không
nhận URL mới do model đặt ra; chặn index sai/trùng, output cắt cụt hoặc sai
schema. Kiểm tra cấu trúc không xác minh độc lập mọi mệnh đề mô hình sinh ra.
Tin đồn và tuyên bố của hãng phải giữ cách quy nguồn, không gọi là đã xác thực.

Điểm tin chỉ dùng tiêu đề/đoạn mô tả. Khi hỏi nội dung một tin, mở bài gốc theo
luồng dưới đây. Bản gần nhất cùng nguồn hiện ở Setting → Nguồn tra cứu.
Tôn trọng điều kiện tái sử dụng; diễn đạt lại để nghe cá nhân, không phát hành
lại toàn văn hay giả rằng đã kiểm chứng độc lập tuyên bố trong bài.

## Nghe nội dung và hỏi tiếp một bài

- Mỗi mục `sources` có `article_id` 8 ký tự hex từ URL. Model sao chép ID đã
  được tool trả về khi gọi `lookup_info({topic:"news",mode:"detail",article_id:"..."})`.
  `brief` kèm ID để tóm tắt ngắn; `query` có thể chứa câu hỏi về nội dung bài.
  Không nhận URL do model đặt; ID phải khớp duy nhất một bài đã giới thiệu.
- Giữ riêng tối đa 12 bài gần đây trong PSRAM, cùng metadata và nội dung nếu
  đã tải; không bị bộ lọc “đã nghe” xóa mất. Hỏi sâu không cần `repeat=true`
  và không làm các bài cũ quay lại danh sách điểm tin. Vẫn giới hạn 7 ngày.
  Thiếu ID thì chỉ chọn khi query khớp duy nhất tiêu đề đã giới thiệu; không
  rõ thì hỏi chọn bài, không tra nguồn khác để giả làm nội dung bài cũ.
- IT Home: `https://api.ithome.com/json/newscontent/{newsid}`, xác minh id và
  success rồi lấy `detail`. QbitAI/TechCrunch: `/wp-json/wp/v2/posts/{id}` với
  `_fields=id,link,title,content,date_gmt`; kiểm tra id, link và protected.
  GenK: chỉ tải URL GenK `.chn` đã lấy từ danh sách, tách div `ContentDetail`.
  Không theo redirect, không vượt paywall/đăng nhập, không fallback Google Search.
- Chỉ khi người dùng hỏi nội dung mới có GET bài gốc. Deadline mỗi GET 8 giây,
  body tối đa 256 KiB trong PSRAM. Bỏ HTML/script/style, giải mã entity và giữ
  tối đa 12000 byte UTF-8 nội dung. Nếu cắt ngắn, kết quả có `article_truncated`;
  Gemini phải nói rõ giới hạn, không tự bịa phần thiếu.
- Cache nội dung 10 phút, các câu hỏi tiếp chỉ cần một POST tóm tắt. Khi phải
  tải thì một GET bài + một POST. `article_loaded=true`, `snippets_only=false`
  phân biệt bài gốc với điểm tin. Body không được sao chép vào tool response
  hoặc lưu NVS; model Live nhận bản tóm tắt và nguồn. Cache mất khi reboot.
- Test dữ liệu thật: IT Home 4360 → 2631 byte text; QbitAI 23016 → 9582;
  GenK 114496 → 6249; TechCrunch 3778 → 2531. Đây là kết quả trên máy phát triển;
  tốc độ/heap và cách Live chọn ID, diễn đạt nội dung còn phải thử trên board.

## Kiểm chứng

`tools/test-alert.ps1` có test normalizer JSON/HTML, timezone, ranh giới 7 ngày,
URL allowlist, lọc trùng, commit sau hoàn tất, generation fence, repeat, index
sai/trùng và schema mô hình; giữ regression thời tiết/vàng/slang/HTTP/OOM.
`tools/test-voice.ps1` kiểm tra flow Live và các đường âm thanh hiện có.

Dữ liệu công khai thật của ba nguồn đã qua parser C: 11 bài hợp lệ. Request
do chính code C tạo đã được thử bằng key môi trường: Gemini trả HTTP 200,
finishReason=STOP; parser C xác minh được bản tóm tắt 3 nguồn, 530 byte tiếng
Việt, scope last_7_days. Key không được in hoặc lưu trong request/response.
Kết quả này chưa xác minh DNS/TLS, heap và thời gian phát trên board thật.
