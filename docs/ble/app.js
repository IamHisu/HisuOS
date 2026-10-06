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
  const note = (id, text, kind='') => { const n = el(id); n.textContent = text; n.className = 'note' + (kind ? ' ' + kind : ''); };
  const wifiNote = (text, kind) => note('wifi-message', text, kind);
  const timeNote = (text, kind) => note('time-message', text, kind);
  let device, fields = [], polling, pending = false, pollBusy = false, ready = false;
  let selected=null, lastNetworks='';
  function controls() {
    document.body.dataset.ready = ready;
    el('connect').disabled = pending || ready || !navigator.bluetooth || !isSecureContext;
    el('send').disabled = pending || !ready;
    el('time').disabled = pending || !ready;
    el('disconnect').disabled = !device?.gatt.connected;
    el('scan').disabled = pending || !ready;
  }
  function disconnected() {
    clearInterval(polling); ready = false; fields = [];
    selected=null; lastNetworks=''; wifiNote(''); timeNote(''); el('wifi').hidden=true; el('wifi-list').replaceChildren();
    el('state').textContent = 'Chưa kết nối'; controls();
    message('Đã ngắt kết nối.');
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
    el('scan-status').textContent=network.scanning ? 'Đang quét…' : !network.enabled ? 'Wi-Fi đang tắt. Bấm quét để bật.' : network.error ? 'Quét thất bại.' : !network.aps.length ? 'Không thấy mạng nào.' : '';
    if (network.scanning || !network.enabled) return;
    for (const ap of network.aps) {
      const row=document.createElement('button'); row.type='button'; row.className='network'+(ap.saved ? ' saved' : '');
      const signal=ap.rssi>=-55 ? 4 : ap.rssi>=-65 ? 3 : ap.rssi>=-75 ? 2 : 1;
      const name=document.createElement('span'); name.className='name'; name.textContent=ap.ssid; row.append(name);
      if (ap.saved) { const tag=document.createElement('span'); tag.className='tag'; tag.textContent='Đã lưu'; row.append(tag); }
      if (ap.secure) { row.insertAdjacentHTML('beforeend','<svg class="lock" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="11" width="16" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/></svg>'); }
      const bars=document.createElement('span'); bars.className='bars'; bars.dataset.level=signal; bars.innerHTML='<i></i><i></i><i></i><i></i>'; row.append(bars);
      row.disabled=!ap.supported;
      row.onclick=()=>{
        if (pending || !ready) return;
        selected=ap; el('ssid').value=ap.ssid; el('password').value='';
        el('password-field').hidden=!ap.secure; el('wifi').hidden=false; el('password').type='password'; el('toggle').textContent='Hiện'; el('send').disabled=false;
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
      if (status.error) wifiNote('Không kết nối được. Kiểm tra mật khẩu.','err');
      else if (status.wifi === 'connected') wifiNote('Đã kết nối Wi-Fi.','ok');
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
      message('Nhập mã 6 số trên màn hình HisuOS.');
      const server = await device.gatt.connect();
      const setup = await server.getPrimaryService(service);
      for (const i of [1,2,3,4,5,7]) fields[i] = await setup.getCharacteristic(characteristic(i));
      await syncTime(); ready = true;
      el('state').textContent = device.name || 'HisuOS'; message('');
      try { await fields[7].writeValueWithResponse(Uint8Array.of(1)); }
      catch { wifiNote('Wi-Fi đang bận. Thử quét lại sau.','err'); }
      polling = setInterval(refresh, 2000);
    } catch (error) {
      console.error('HisuOS BLE connection failed:', error);
      if (device?.gatt.connected) device.gatt.disconnect();
      message(error.name === 'NotFoundError' ? 'Chưa chọn thiết bị.' : 'Không kết nối được. Kiểm tra Bluetooth và mã ghép nối.');
    } finally { pending = false; controls(); }
    await refresh();
  };
  el('disconnect').onclick = () => device?.gatt.disconnect();
  el('scan').onclick = async () => {
    if (pending || !ready) return;
    pending=true; controls();
    while (pollBusy) await new Promise(resolve=>setTimeout(resolve,50));
    try { await fields[7].writeValueWithResponse(Uint8Array.of(1)); }
    catch { wifiNote('Quét thất bại. Thử lại.','err'); }
    finally { pending=false; controls(); }
    await refresh();
  };
  el('time').onclick = async () => {
    pending = true; controls();
    try { await syncTime(); timeNote('Đã đồng bộ giờ.','ok'); }
    catch { timeNote('Gửi giờ thất bại. Kết nối lại.','err'); }
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
    catch (error) { wifiNote(error.message,'err'); return; }
    pending = true; controls();
    // Wait for a status read to finish; browsers serialize GATT operations inconsistently.
    while (pollBusy) await new Promise(resolve => setTimeout(resolve, 50));
    try {
      await fields[1].writeValueWithResponse(payload[0]);
      await fields[2].writeValueWithResponse(payload[1]);
      await fields[3].writeValueWithResponse(Uint8Array.of(1));
      el('password').value = ''; el('wifi').hidden = true; wifiNote('Đang kết nối Wi-Fi…');
    } catch { wifiNote('Gửi Wi-Fi thất bại. Thử lại.','err'); }
    finally { payload[1].fill(0); pending = false; controls(); }
  };
  if (!navigator.bluetooth || !isSecureContext) message('Cần Chrome hoặc Edge qua HTTPS/localhost.');
  el('cancel').onclick = () => { el('wifi').hidden = true; selected = null; el('password').value = ''; };
  el('toggle').onclick = () => { const show = el('password').type === 'password'; el('password').type = show ? 'text' : 'password'; el('toggle').textContent = show ? 'Ẩn' : 'Hiện'; };
  document.addEventListener('keydown', e => { if (e.key === 'Escape' && !el('wifi').hidden) el('cancel').click(); });
  const tick = () => { const d = new Date(); el('clock').firstChild.nodeValue = d.toLocaleTimeString('vi-VN',{hour:'2-digit',minute:'2-digit'}); el('zone').textContent = 'GMT' + (d.getTimezoneOffset() <= 0 ? '+' : '−') + Math.abs(d.getTimezoneOffset() / 60); };
  tick(); setInterval(tick, 10000);
  controls();
}
