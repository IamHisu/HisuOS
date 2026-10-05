export const service = 'e3a10000-7d4b-4ee0-b417-6d9f67485501';
export const characteristic = n => `e3a1000${n}-7d4b-4ee0-b417-6d9f67485501`;
export function encodeWifi(ssid, password) {
  const name = new TextEncoder().encode(ssid);
  if (!name.length || name.length > 32 || ssid.includes('\0')) throw new Error('Tên Wi-Fi cần từ 1 đến 32 byte.');
  if (password && !(/^[\x20-\x7e]{8,63}$/.test(password) || /^[a-fA-F0-9]{64}$/.test(password)))
    throw new Error('Mật khẩu cần 8–63 ký tự ASCII hoặc 64 ký tự hex.');
  return [name, new TextEncoder().encode(password)];
}
export function encodeTime(date = new Date()) {
  const data = new Uint8Array(10);
  const view = new DataView(data.buffer);
  const seconds = Math.floor(date.getTime() / 1000);
  if (!Number.isFinite(seconds) || seconds < 1704067200 || seconds >= 4102444800) throw new Error('Giờ trên điện thoại chưa đúng.');
  view.setBigUint64(0, BigInt(seconds), true);
  view.setInt16(8, -date.getTimezoneOffset(), true);
  return data;
}
if (typeof document !== 'undefined') {
  const el = id => document.getElementById(id);
  const message = text => { el('message').textContent = text; };
  let device, fields = [], polling, pending = false, pollBusy = false, ready = false;
  function controls() {
    el('connect').disabled = pending || ready || !navigator.bluetooth || !isSecureContext;
    el('send').disabled = pending || !ready;
    el('time').disabled = pending || !ready;
    el('disconnect').disabled = !device?.gatt.connected;
  }
  function disconnected() {
    clearInterval(polling); ready = false; fields = [];
    el('state').textContent = 'Chưa kết nối'; controls();
    message('Đã ngắt Bluetooth. Mở lại kết nối trên HisuOS nếu cửa sổ 5 phút đã hết.');
  }
  async function syncTime() {
    while (pollBusy) await new Promise(resolve => setTimeout(resolve, 50));
    await fields[4].writeValueWithResponse(encodeTime());
  }
  async function refresh() {
    if (pending || pollBusy || !ready) return;
    pollBusy = true;
    try {
      const value = await fields[5].readValue();
      const status = JSON.parse(new TextDecoder().decode(value));
      el('state').textContent = status.wifi === 'connected' ? 'Wi-Fi đã kết nối' : status.wifi === 'joining' ? 'Đang kết nối Wi-Fi' : 'Đã ghép điện thoại';
      if (status.error) message('Wi-Fi chưa kết nối được. Kiểm tra tên mạng và mật khẩu, rồi gửi lại.');
      else if (status.wifi === 'connected') message('Wi-Fi đã kết nối. Thiết bị đã lưu mạng và đồng bộ giờ.');
    } catch { /* A disconnect is handled by the GATT event. */ }
    finally { pollBusy = false; }
  }
  el('connect').onclick = async () => {
    pending = true; controls();
    try {
      const name = typeof location !== 'undefined' ? new URLSearchParams(location.hash.slice(1)).get('name') : null;
      const filter = {services:[service]};
      if (name && /^HisuOS-[0-9A-Fa-f]{4}$/.test(name)) filter.name = name;
      device = await navigator.bluetooth.requestDevice({filters:[filter]});
      device.addEventListener('gattserverdisconnected', disconnected);
      message('Ghép nối: nhập mã 6 số trên màn hình HisuOS nếu được yêu cầu.');
      const server = await device.gatt.connect();
      const setup = await server.getPrimaryService(service);
      for (let i = 1; i <= 5; ++i) fields[i] = await setup.getCharacteristic(characteristic(i));
      await syncTime(); ready = true;
      el('state').textContent = 'Đã ghép điện thoại'; message('Đã đồng bộ giờ. Bạn có thể nhập Wi-Fi bên dưới.');
      polling = setInterval(refresh, 2000);
    } catch (error) {
      if (device?.gatt.connected) device.gatt.disconnect();
      message(error.name === 'NotFoundError' ? 'Chưa chọn thiết bị.' : 'Chưa ghép nối được. Kiểm tra Bluetooth và mã trên HisuOS, rồi thử lại.');
    } finally { pending = false; controls(); }
  };
  el('disconnect').onclick = () => device?.gatt.disconnect();
  el('time').onclick = async () => {
    pending = true; controls();
    try { await syncTime(); message('Đã đồng bộ giờ và múi giờ.'); }
    catch { message('Chưa gửi được giờ. Hãy kết nối lại.'); }
    finally { pending = false; controls(); }
  };
  el('wifi').onsubmit = async event => {
    event.preventDefault();
    if (!ready || pending) return;
    let payload;
    try { payload = encodeWifi(el('ssid').value, el('password').value); }
    catch (error) { message(error.message); return; }
    pending = true; controls();
    // Wait for a status read to finish; browsers serialize GATT operations inconsistently.
    while (pollBusy) await new Promise(resolve => setTimeout(resolve, 50));
    try {
      await fields[1].writeValueWithResponse(payload[0]);
      await fields[2].writeValueWithResponse(payload[1]);
      await fields[3].writeValueWithResponse(Uint8Array.of(1));
      el('password').value = ''; message('Đang kết nối Wi-Fi…');
    } catch { message('Chưa gửi được Wi-Fi. Thử lại khi thiết bị quét mạng xong hoặc kết nối lại Bluetooth.'); }
    finally { payload[1].fill(0); pending = false; controls(); }
  };
  if (!navigator.bluetooth || !isSecureContext) message('Trình duyệt này chưa dùng được Bluetooth. Hãy mở qua HTTPS/localhost bằng Chrome hoặc Edge có hỗ trợ Bluetooth.');
  controls();
}
