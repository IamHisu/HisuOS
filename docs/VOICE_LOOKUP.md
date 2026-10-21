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

## Ban Tin Ngan

`lookup_info({topic:"news"})` khong nhan URL, dia diem hay tham so tim kiem
tu model. Ba truy van RSS Google News co dinh uu tien mot tin moi nhom:
cong nghe/robot tai Viet Nam, AI/mo hinh moi, kinh te/tai chinh quoc te.
Ket qua toi da ba tin, co tieu de, nguon, lien ket va gio dang. Pham vi tim
kiem khong phai xac nhan dia ly su kien; model phai doc y nghia tieu de.

Day la DIEM TIN tu tieu de, khong phai tom tat toan van bai bao. Google RSS
thuong chi co tieu de va danh sach bai lien quan trong HTML description;
khong dua HTML do cho model hay suy doan noi dung bai goc. `headlines_only`
va `summary_available=false` duoc tra ro rang. Moi tin noi ngan mot cau,
gop cac tin cung su kien; hoi sau ma thieu noi dung thi noi ro gioi han.
Khong doc ten nguon neu khong duoc hoi, nhung van giu nguon de tra loi that.

Chi nhan ngay dang hop le trong 24 gio qua, khong qua 5 phut trong tuong lai.
Uu tien tin tu 00:00 UTC+7; neu phai lay tin hom qua thi `scope=last_24_hours`,
khong goi tat ca la tin hom nay. Khong co tin phu hop thi bao chua lay duoc;
thieu mot nhom thi tra `partial` va it hon ba tin, khong chen tin cho du so.
Tieu de trung nhau sau khi bo hau to ten bao duoc loai; cung su kien nhung
khac tieu de van can AI nhan dien, khong cam ket dedup ngu nghia bang firmware.

RSS toi da 256 KiB/feed trong PSRAM, yxml state cung trong PSRAM, toi da 12
cap XML va stack ten 512 byte. Tu choi DTD/entity declarations, unknown entity,
byte NUL, sai XML, bi cat cut; chi lay cac truong truc tiep cua item. Khong
theo lien ket bai bao hay redirect, khong co SSRF hay them API key. Noi dung
tra ve la du lieu khong dang tin, khong phai lenh hay quyen goi cong cu.
Ba request noi tiep, muc tieu 7 giay/request, deadline chung 24 giay; DNS/TLS
van co gioi han huy cua SDK. Cache ban tin 15 phut, ban thieu nhom 2 phut,
bo cache khi sang ngay moi. Chi tai khi duoc hoi, khong chay cap nhat nen.

Da test parser native voi ca ba RSS that ngay 2026-10-21 (khoang 19-54 KiB/feed).
Chua do dinh RAM, stack TLS va do tre tren board. Google feed copyright gioi
han personal/non-commercial feed-reader use; phai xem lai dieu kien truoc
khi phan phoi san pham/thuong mai. Khong luu hay phan phoi snapshot bai bao
trong source; mau tai thu chi nam trong build/.

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

- Mot job toi da, khong tao task thuong truc: `info_lookup` 6144 byte stack
  trong PSRAM qua `xTaskCreateWithCaps`, uu tien 3. Worker khong ghi WebSocket.
  Worker cong bo ket qua roi suspend; consumer doi suspended va xoa bang
  `vTaskDeleteWithCaps` truoc khi tra ket qua. Hai consumer (Live/nhac) duoc
  serialize de khong xoa hai lan. Khong self-delete tao task don dep tam.
- Guard khoi tao giu toi thieu 10 KiB RAM noi bo, block lon nhat 2 KiB;
  HTTP van kiem tra toi thieu 6 KiB/block 2 KiB truoc moi request. Khong chi
  ha guard cho stack 6 KiB noi bo. Log `LOOKUP: Rejected` ghi ly do va heap
  de phan biet sai tham so, thieu RAM va loi tao worker.
- Deadline 12 giay cho vang/thoi tiet (tin tuc 24 giay), HTTP timeout 1 giay, polling async.
  DNS/TLS cua SDK van co the giu CPU lau hon mot vong poll; khong cam ket
  thoi gian huy cung theo mili giay. Khong tu thu lai HTTP.
- Kiem tra RAM truoc khi tao task va truoc moi HTTP; TLS xac thuc chung chi,
  host/path co dinh, khong theo redirect. Thieu RAM thi bao khong tra duoc.
- Body JSON toi da 16 KiB o PSRAM (RSS 256 KiB), JSON sau toi da 10 cap; chan body cat cut,
  overflow, byte NUL, sai don vi, null/gia tri ngoai mien, thieu gio du bao.
- Cache RAM mot ket qua moi nhat moi chu de: vang 5 phut, thoi tiet 10 phut.
  Tin tuc 15 phut (partial 2 phut).
  Thay tham so thi tra lai. Gioi han yeu cau moi cach nhau 2 giay; cache
  khong bi chan boi gioi han nay. Khong luu vi tri/ket qua vao flash.
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
Build kiem tra frame <= 2048 byte. Chua do high-water mark cua TLS/stack,
RAM va do muot tren board khi WSS + HTTPS cung chay. Log `LOOKUP` ghi HTTP,
do dai va stack con lai, khong log khoa hay dia diem rieng tu.
