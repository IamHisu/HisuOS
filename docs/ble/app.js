export const service = 'e3a10000-7d4b-4ee0-b417-6d9f67485501';
export const characteristic = n => `e3a1000${n}-7d4b-4ee0-b417-6d9f67485501`;
export function decodeNetworks(value) {
  const bytes = new Uint8Array(value.buffer,value.byteOffset,value.byteLength);
  if (bytes.length < 7 || bytes[0] !== 1 || bytes[2] > 12) throw new Error('Danh sách Wi-Fi không hợp lệ.');
  const out = {enabled:!!(bytes[1]&1),scanning:!!(bytes[1]&2),error:value.getUint32(3,true),aps:[]};
  let pos=7;
  for (let i=0;i<bytes[2];i++) {
    const length=bytes[pos++];
    if (!length || length>32 || pos+length+2>bytes.length) throw new Error('Danh sách Wi-Fi bị thiếu dữ liệu.');
    const ssid=new TextDecoder().decode(bytes.subarray(pos,pos+length)); pos+=length;
    const rssi=value.getInt8(pos++), flags=bytes[pos++];
    out.aps.push({ssid,rssi,secure:!!(flags&1),saved:!!(flags&2),supported:!!(flags&4)});
  }
  if (pos!==bytes.length) throw new Error('Danh sách Wi-Fi dư dữ liệu.');
  return out;
}
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
  let selected=null, lastNetworks='';
  function controls() {
    el('connect').disabled = pending || ready || !navigator.bluetooth || !isSecureContext;
    el('send').disabled = pending || !ready;
    el('time').disabled = pending || !ready;
    el('disconnect').disabled = !device?.gatt.connected;
    el('scan').disabled = pending || !ready;
  }
  function disconnected() {
    clearInterval(polling); ready = false; fields = [];
    selected=null; lastNetworks=''; el('wifi').hidden=true; el('wifi-list').replaceChildren();
    el('state').textContent = 'Chưa kết nối'; controls();
    message('Đã ngắt Bluetooth. Trên HisuOS, BOOT đổi thời gian mở và Vol− xác nhận để kết nối lại.');
  }
  async function syncTime() {
    while (pollBusy) await new Promise(resolve => setTimeout(resolve, 50));
    await fields[4].writeValueWithResponse(encodeTime());
  }
  function showNetworks(network) {
    const signature=JSON.stringify(network);
    if (signature===lastNetworks) return;
    lastNetworks=signature;
    el('wifi-list').replaceChildren();
    el('scan-status').textContent=network.scanning ? 'Đang quét…' : !network.enabled ? 'Wi-Fi đang tắt. Bấm Quét để bật và tìm mạng.' : network.error ? 'Quét chưa thành công. Bấm Quét lại.' : !network.aps.length ? 'Chưa tìm thấy mạng. Bấm Quét lại.' : 'Chọn Wi-Fi để kết nối';
    if (network.scanning || !network.enabled) return;
    for (const ap of network.aps) {
      const row=document.createElement('button'); row.type='button'; row.className='network'+(ap.saved ? ' saved' : '');
      const signal=ap.rssi>=-55 ? 4 : ap.rssi>=-65 ? 3 : ap.rssi>=-75 ? 2 : 1;
      row.textContent=`${ap.ssid}  ${'▂▄▆█'.slice(0,signal)}${ap.saved ? ' · Đã lưu' : ''}${!ap.secure ? ' · Mở' : ''}`;
      row.disabled=!ap.supported;
      row.onclick=()=>{
        if (pending || !ready) return;
        selected=ap; el('ssid').value=ap.ssid; el('password').value='';
        el('password-field').hidden=!ap.secure; el('wifi').hidden=false;
        if (ap.secure) el('password').focus();
      };
      el('wifi-list').append(row);
    }
  }
  async function refresh() {
    if (pending || pollBusy || !ready) return;
    pollBusy = true;
    try {
      const value = await fields[5].readValue();
      const status = JSON.parse(new TextDecoder().decode(value));
      showNetworks(decodeNetworks(await fields[7].readValue()));
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
      for (const i of [1,2,3,4,5,7]) fields[i] = await setup.getCharacteristic(characteristic(i));
      await syncTime(); ready = true;
      el('state').textContent = device.name || 'HisuOS'; message('Đã đồng bộ giờ. Chọn mạng Wi-Fi bên dưới.');
      try { await fields[7].writeValueWithResponse(Uint8Array.of(1)); }
      catch { message('Đã ghép nối. Wi-Fi đang bận; có thể bấm Quét lại sau.'); }
      polling = setInterval(refresh, 2000);
    } catch (error) {
      if (device?.gatt.connected) device.gatt.disconnect();
      message(error.name === 'NotFoundError' ? 'Chưa chọn thiết bị.' : 'Chưa ghép nối được. Kiểm tra Bluetooth và mã trên HisuOS, rồi thử lại.');
    } finally { pending = false; controls(); }
    await refresh();
  };
  el('disconnect').onclick = () => device?.gatt.disconnect();
  el('scan').onclick = async () => {
    if (pending || !ready) return;
    pending=true; controls();
    while (pollBusy) await new Promise(resolve=>setTimeout(resolve,50));
    try { await fields[7].writeValueWithResponse(Uint8Array.of(1)); }
    catch { message('Chưa quét được Wi-Fi. Hãy thử lại.'); }
    finally { pending=false; controls(); }
    await refresh();
  };
  el('time').onclick = async () => {
    pending = true; controls();
    try { await syncTime(); message('Đã đồng bộ giờ và múi giờ.'); }
    catch { message('Chưa gửi được giờ. Hãy kết nối lại.'); }
    finally { pending = false; controls(); }
  };
  el('wifi').onsubmit = async event => {
    event.preventDefault();
    if (!ready || pending || !selected) return;
    let payload;
    try {
      if (selected.secure && !el('password').value) throw new Error('Nhập mật khẩu của Wi-Fi đã chọn.');
      payload = encodeWifi(selected.ssid, selected.secure ? el('password').value : '');
    }
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
