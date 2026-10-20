# Tra Thoi Tiet Va Gia Vang

Chat co them `lookup_info` (khong cap quyen trong Reader/Interpreter).
Day la hai chuc nang trong mot khai bao tool, khong doi luong Gemini Live.

## Cach Dung

- "Chieu nay o Can Tho co mua khong?"
- "Ngay mai o Da Lat nhiet do the nao?"
- "Gia vang SJC hom nay bao nhieu?"
- "Gia nhan SJC va vang the gioi thi sao?"

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

- Mot job toi da, khong tao task thuong truc: `info_lookup` 6144 byte stack,
  uu tien 3. Ket qua di ve `live_send`; worker khong ghi WebSocket.
- Deadline chung 12 giay, moi buoc HTTP timeout 1 giay, polling async.
  DNS/TLS cua SDK van co the giu CPU lau hon mot vong poll; khong cam ket
  thoi gian huy cung theo mili giay. Khong tu thu lai HTTP.
- Kiem tra RAM truoc khi tao task va truoc moi HTTP; TLS xac thuc chung chi,
  host/path co dinh, khong theo redirect. Thieu RAM thi bao khong tra duoc.
- Body toi da 16 KiB o PSRAM, JSON sau toi da 10 cap; chan body cat cut,
  overflow, byte NUL, sai don vi, null/gia tri ngoai mien, thieu gio du bao.
- Cache RAM mot ket qua moi nhat moi chu de: vang 5 phut, thoi tiet 10 phut.
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
