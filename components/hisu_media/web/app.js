export function encodeRgb565(rgba) {
  if (rgba.length !== 240 * 240 * 4) throw new Error('Kích thước ảnh không hợp lệ.');
  const bytes = new Uint8Array(240 * 240 * 2);
  for (let i = 0, j = 0; i < rgba.length; i += 4, j += 2) {
    const value = ((rgba[i] >> 3) << 11) | ((rgba[i + 1] >> 2) << 5) | (rgba[i + 2] >> 3);
    bytes[j] = value & 255; bytes[j + 1] = value >> 8;
  }
  return bytes;
}

export function imagePlacement(width, height, fit) {
  if (width < 1 || height < 1 || width > 4096 || height > 4096 || width * height > 12000000)
    throw new Error('Ảnh tối đa 4096 px mỗi chiều và 12 megapixel.');
  const scale = fit === 'contain' ? Math.min(240 / width, 240 / height) : Math.max(240 / width, 240 / height);
  return {x:(240 - width * scale) / 2,y:(240 - height * scale) / 2,width:width * scale,height:height * scale};
}

if (typeof document !== 'undefined') {
  const el = id => document.getElementById(id);
  const canvas = el('preview'), ctx = canvas.getContext('2d', {willReadFrequently:true});
  let token = '', bitmap, gifData, pollTimer, pending = false, uploadBusy = false, pollBusy = false;
  let processing = false, procText = '', procRatio = null, gifUrl = '';
  let baseline = 0, fileGeneration = 0, closed = false;
  let pageId = 0;
  let activeTab = 'wifi', wifiState, wifiBusy = false;
  const SIZE = 240, MAX_ZOOM = 4, pointers = new Map();
  let zoom = 1, panX = 0, panY = 0, frame = 0, pinch = 0;
  const wifiRows = new Map();
  let wifiOrder = '';
  const message = (text, error = false) => { document.querySelectorAll('.msg').forEach(node => { node.textContent = text; node.dataset.error = error; }); };
  const setProc = (text = '', ratio = null) => { processing = !!text; procText = text; procRatio = ratio; controls(); };
  function controls() {
    el('connection').disabled = !token || closed;
    const busy = pending || uploadBusy || processing;
    el('send').disabled = !token || !bitmap || busy;
    el('send').classList.toggle('is-loading', uploadBusy || pending);
    el('send').textContent = uploadBusy ? 'Đang gửi…' : pending ? 'Đang lưu…' : 'Gửi ảnh';
    el('file').disabled = busy;
    el('fit').disabled = !!gifData || busy;
    el('background').disabled = !!gifData || busy;
    const locked = !!gifData || !bitmap || busy;
    el('zoom').disabled = locked;
    el('reset-view').disabled = locked;
    canvas.classList.toggle('editable', !locked);
    canvas.classList.toggle('pickable', !busy);
    canvas.parentElement.dataset.empty = String(!bitmap);
    const label = processing ? procText : uploadBusy ? 'Đang gửi ảnh tới Hisu…' : pending ? 'Đang lưu trên Hisu…' : '';
    const ratio = processing ? procRatio : null;
    el('loading').hidden = !label;
    el('loading-text').textContent = label;
    el('loading-bar').dataset.indeterminate = String(ratio === null);
    el('loading-fill').style.width = ratio === null ? '' : Math.round(ratio * 100) + '%';
    canvas.setAttribute('aria-busy', String(!!label));
  }
  function logout() {
    token = ''; closed = true; clearTimeout(pollTimer);
    el('editor').hidden = true; el('login-section').hidden = false;
    el('connection').textContent = 'Đã ngắt kết nối';
    el('wifi-password').value = ''; el('wifi-ssid').value = '';
    el('asr-key').value = '';
    el('gemini-key').value = '';
    el('wifi-dialog').close();
    controls();
  }
  async function request(path, options = {}) {
    const response = await fetch(path, {...options,cache:'no-store',credentials:'omit',redirect:'error',signal:AbortSignal.timeout(35000),
      headers:{...(token ? {Authorization:`Bearer ${token}`} : {}),...options.headers}});
    if (!response.ok) {
      if (response.status === 401 || response.status === 403) {
        logout(); throw new Error('Phiên đã kết thúc. Mở lại Setting trên Hisu để tạo phiên mới.');
      }
      throw new Error(response.status === 409 ? (path === '/image' ? 'Hisu đang lưu ảnh. Chờ rồi thử lại.' : 'Wi-Fi đang bận. Chờ rồi thử lại.') : 'Không gửi được dữ liệu. Kiểm tra Hisu và Wi-Fi.');
    }
    return response.json();
  }
  function tab(name) {
    activeTab = name; message('');
    for (const key of ['wifi','wallpaper','device']) {
      el(`panel-${key}`).hidden = key !== name;
      el(`tab-${key}`).setAttribute('aria-selected',String(key === name));
      el(`tab-${key}`).tabIndex = key === name ? 0 : -1;
    }
  }
  const tabs = ['wifi','wallpaper','device'];
  for (const name of tabs) {
    el(`tab-${name}`).onclick = () => tab(name);
    el(`tab-${name}`).onkeydown = event => {
      let index = tabs.indexOf(name);
      if (event.key === 'ArrowRight') index = (index + 1) % tabs.length;
      else if (event.key === 'ArrowLeft') index = (index + tabs.length - 1) % tabs.length;
      else if (event.key === 'Home') index = 0;
      else if (event.key === 'End') index = tabs.length - 1;
      else return;
      event.preventDefault(); tab(tabs[index]); el(`tab-${tabs[index]}`).focus();
    };
  }
  function updateWifi(state) {
    wifiState = state;
    const internet = {unknown:'Đang kiểm tra Internet…',checking:'Đang kiểm tra Internet…',
      online:'Có Internet',captive:'Mạng yêu cầu đăng nhập',offline:'Wi-Fi chưa có Internet'}[state.internet] || 'Đang kiểm tra Internet…';
    const warning = state.connected && ['captive','offline'].includes(state.internet);
    el('wifi-enabled').checked = state.enabled;
    el('wifi-status').textContent = state.scanning ? 'Đang quét…' : state.joining ? `Đang kết nối ${state.ssid}…` :
      state.connected ? `${state.ssid} · ${internet} · ${state.ip}` : state.error ? `Kết nối thất bại (${state.error})` : state.enabled ? 'Chưa kết nối' : 'Wi-Fi đã tắt';
    const aps = [...state.aps].sort((a,b) => Number(b.ssid === state.ssid && state.connected) - Number(a.ssid === state.ssid && state.connected) || Number(b.saved) - Number(a.saved) || b.rssi - a.rssi);
    const names = new Set(aps.map(ap => ap.ssid));
    for (const [ssid,row] of wifiRows) if (!names.has(ssid)) { row.remove(); wifiRows.delete(ssid); }
    for (const ap of aps) {
      let row = wifiRows.get(ap.ssid);
      if (!row) {
        row = el('wifi-row-template').content.firstElementChild.cloneNode(true);
        row.querySelector('.wifi-name').textContent = ap.ssid;
        row.onclick = () => chooseNetwork(ap.ssid);
        wifiRows.set(ap.ssid,row);
      }
      const connected = state.connected && state.ssid === ap.ssid;
      row.dataset.saved = String(ap.saved); row.dataset.connected = String(connected);
      row.disabled = wifiBusy || state.scanning || state.joining || !state.enabled || !ap.supported;
      row.querySelector('.wifi-note').textContent = connected ? internet : !ap.supported ? 'Không hỗ trợ' : ap.saved ? 'Đã lưu' : '';
      row.querySelector('.wifi-warning').hidden = !(connected && warning);
      const signal = row.querySelector('.wifi-signal');
      signal.dataset.level = ap.rssi >= -60 ? '3' : ap.rssi >= -75 ? '2' : '1';
      row.title = ap.rssi >= -60 ? 'Sóng mạnh' : ap.rssi >= -75 ? 'Sóng vừa' : 'Sóng yếu';
      row.querySelector('.wifi-lock').toggleAttribute('hidden',!ap.secure);
    }
    const order = JSON.stringify(aps.map(ap => ap.ssid));
    if (order !== wifiOrder) {
      aps.forEach((ap,index) => {
        const row = wifiRows.get(ap.ssid), current = el('wifi-networks').children[index];
        if (current !== row) el('wifi-networks').insertBefore(row,current || null);
      });
      wifiOrder = order;
    }
    el('wifi-empty').hidden = aps.length > 0 || !state.enabled || state.scanning;
    el('wifi-scan').disabled = wifiBusy || state.scanning;
    el('wifi-connect').disabled = wifiBusy || state.scanning || state.joining;
    el('wifi-hidden').disabled = wifiBusy || state.scanning || state.joining || !state.enabled;
  }
  function openWifiDialog(ssid = '') {
    el('wifi-ssid').value = ssid; el('wifi-ssid').readOnly = Boolean(ssid);
    el('wifi-password').value = ''; el('wifi-password').required = Boolean(ssid);
    el('wifi-dialog-title').textContent = ssid || 'Thêm mạng';
    el('wifi-dialog').showModal();
    el(ssid ? 'wifi-password' : 'wifi-ssid').focus();
  }
  function chooseNetwork(ssid) {
    const ap = wifiState?.aps.find(ap => ap.ssid === ssid);
    if (!ap || !ap.supported || wifiBusy || wifiState.joining || wifiState.scanning) return;
    if (wifiState.connected && wifiState.ssid === ssid) return;
    if (ap.saved && (ap.password_rejected || (wifiState.error && wifiState.ssid === ssid))) openWifiDialog(ssid);
    else if (ap.saved) void wifiAction('/wifi/join',{ssid,password:'',saved:true});
    else if (!ap.secure) void wifiAction('/wifi/join',{ssid,password:''});
    else openWifiDialog(ssid);
  }
  el('wifi-hidden').onclick = () => openWifiDialog();
  el('wifi-cancel').onclick = () => el('wifi-dialog').close();
  el('wifi-dialog').addEventListener('close',() => { el('wifi-password').value = ''; });
  async function wifiAction(path, data) {
    if (!token || wifiBusy) return;
    wifiBusy = true;
    if (wifiState) updateWifi(wifiState);
    el('wifi-scan').disabled = el('wifi-connect').disabled = el('wifi-enabled').disabled = true;
    try {
      await request(path,{method:'POST',...(data ? {headers:{'Content-Type':'application/json'},body:JSON.stringify(data)} : {})});
      message('Yêu cầu Wi-Fi đã gửi.');
    } catch (error) { message(error.message,true); }
    finally {
      wifiBusy = false; el('wifi-enabled').disabled = false;
      if (token) try { updateWifi(await request('/wifi')); } catch (error) { message(error.message,true); }
    }
  }
  el('wifi-scan').onclick = () => wifiAction('/wifi/scan');
  el('wifi-enabled').onchange = () => wifiAction('/wifi/power',{enabled:el('wifi-enabled').checked});
  el('wifi-join').onsubmit = async event => {
    event.preventDefault();
    const ssid = el('wifi-ssid').value, password = el('wifi-password').value;
    el('wifi-password').value = '';
    const saved = wifiState?.aps.find(ap => ap.ssid === ssid)?.saved;
    el('wifi-dialog').close();
    await wifiAction('/wifi/join',{ssid,password,...(saved && !password ? {saved:true} : {})});
  };
  async function loadSettings() {
    const state = await request('/settings');
    for (const input of document.querySelectorAll('[data-setting]')) {
      const value = state[input.dataset.setting];
      if (input.type === 'checkbox') input.checked = Boolean(value);
      else {
        if (input.tagName === 'SELECT' && ![...input.options].some(option => Number(option.value) === value))
          input.add(new Option(`${value} giây`,String(value)));
        input.value = value;
      }
      if (el(`${input.id}-value`)) el(`${input.id}-value`).value = `${value}%`;
    }
    for (const name of ['asr','gemini']) {
      const state = await request(`/${name}-key`);
      el(`${name}-key-status`).textContent = state.configured ? 'Đã lưu key trên thiết bị' : 'Chưa có API key';
    }
  }
  for (const name of ['asr','gemini']) {
    el(`${name}-key-form`).onsubmit = async event => {
      event.preventDefault();
      const key = el(`${name}-key`).value.trim();
      if (!key || !token) return;
      el(`${name}-key-save`).disabled = true;
      try {
        await request(`/${name}-key`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({key})});
        el(`${name}-key-status`).textContent = 'Đã lưu key trên thiết bị';
        message('Đã lưu API key.');
      } catch (error) { message(error.message,true); }
      finally { el(`${name}-key`).value = ''; el(`${name}-key-save`).disabled = false; }
    };
    el(`${name}-key-clear`).onclick = async () => {
      if (!token) return;
      try {
        await request(`/${name}-key`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({key:''})});
        el(`${name}-key`).value = ''; el(`${name}-key-status`).textContent = 'Chưa có API key';
        message('Đã xóa API key.');
      } catch (error) { message(error.message,true); }
    };
  }
  el('time-sync').onclick = async () => {
    if (!token) return;
    el('time-sync').disabled = true;
    try {
      await request('/time',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({utc:Math.floor(Date.now()/1000)})});
      message('Đã đồng bộ giờ.');
    } catch (error) { message(error.message,true); }
    finally { el('time-sync').disabled = false; }
  };
  for (const input of document.querySelectorAll('[data-setting]')) {
    input.oninput = () => { if (el(`${input.id}-value`)) el(`${input.id}-value`).value = `${input.value}%`; };
    input.onchange = async () => {
      if (!token || !input.checkValidity()) return;
      input.disabled = true;
      try {
        await request('/settings',{method:'POST',headers:{'Content-Type':'application/json'},
          body:JSON.stringify({key:input.dataset.setting,value:input.type === 'checkbox' ? Number(input.checked) : Number(input.value)})});
        message('Đã lưu thiết lập.');
      } catch (error) { message(error.message,true); if (token) try { await loadSettings(); } catch {} }
      finally { input.disabled = false; }
    };
  }
  // Vị trí ảnh = vị trí căn giữa của imagePlacement + phóng to + độ lệch do người dùng kéo (pan).
  // Ảnh lớn hơn khung thì luôn phủ kín khung; ảnh nhỏ hơn thì nằm gọn trong khung.
  function placement() {
    const base = imagePlacement(bitmap.width, bitmap.height, el('fit').value);
    const width = base.width * zoom, height = base.height * zoom;
    const cx = (SIZE - width) / 2, cy = (SIZE - height) / 2;
    const clamp = (pan, center, size) => Math.min(Math.max(pan, Math.min(0, SIZE - size) - center), Math.max(0, SIZE - size) - center);
    panX = clamp(panX, cx, width); panY = clamp(panY, cy, height);
    return {x:cx + panX,y:cy + panY,width,height};
  }
  function draw() {
    ctx.imageSmoothingQuality = 'high';
    ctx.fillStyle = el('background').value; ctx.fillRect(0,0,SIZE,SIZE);
    if (bitmap && !gifData) {
      const p = placement();
      ctx.drawImage(bitmap,p.x,p.y,p.width,p.height);
    }
    controls();
  }
  // GIF: để chính trình duyệt phát ảnh động (canvas chỉ vẽ được 1 frame), đặt đè lên khung xem trước.
  function showGif() {
    if (gifUrl) URL.revokeObjectURL(gifUrl);
    const img = el('gif-preview');
    gifUrl = gifData ? URL.createObjectURL(new Blob([gifData],{type:'image/gif'})) : '';
    if (gifUrl) { img.style.objectFit = el('fit').value; img.src = gifUrl; } else img.removeAttribute('src');
    img.hidden = !gifUrl;
  }
  function schedule() { if (!frame) frame = requestAnimationFrame(() => { frame = 0; draw(); }); }
  function setZoom(value) {
    const next = Math.min(MAX_ZOOM, Math.max(1, value));
    panX *= next / zoom; panY *= next / zoom; zoom = next;
    el('zoom').value = Math.round(zoom * 100); el('zoom-value').value = `${Math.round(zoom * 100)}%`;
    schedule();
  }
  function resetView() { zoom = 1; panX = panY = 0; setZoom(1); }
  // Thu nhỏ ảnh lớn ngay khi chọn: giữ đủ nét cho mức phóng to tối đa nhưng nhẹ bộ nhớ và kéo mượt hơn.
  async function shrink(source) {
    const scale = Math.min(1, (SIZE * MAX_ZOOM) / Math.min(source.width, source.height), Math.sqrt(4e6 / (source.width * source.height)));
    if (scale >= 1) return source;
    try {
      const small = await createImageBitmap(source,{resizeWidth:Math.max(1,Math.round(source.width * scale)),
        resizeHeight:Math.max(1,Math.round(source.height * scale)),resizeQuality:'high'});
      source.close(); return small;
    } catch { return source; }
  }
  async function poll() {
    if (!token || closed || pollBusy || uploadBusy) return;
    pollBusy = true;
    try {
      const state = await request('/status');
      const wasPending = pending;
      pending = state.pending || state.saving;
      if (state.error) message('Không lưu được ảnh. Nền cũ được giữ lại.',true);
      else if (state.saving || state.pending) message('');
      else if (state.revision !== baseline) { baseline = state.revision; message('Hisu đã cập nhật hình nền.'); }
      else if (wasPending && !pending) message('Ảnh chưa được lưu.',true);
      controls();
      if (activeTab === 'wifi' && !wifiBusy) updateWifi(await request('/wifi'));
    } catch (error) { message(error.message,true); }
    finally {
      pollBusy = false;
      if (token && !closed) pollTimer = setTimeout(poll,2000);
    }
  }
  async function connect() {
    if (el('login-button').disabled) return;
    el('login-button').disabled = true;
    el('connection').textContent = 'Đang kết nối…';
    try {
      const result = await request('/session',{method:'POST'});
      token = result.token; pageId = result.page; closed = false; pending = false;
      el('login-section').hidden = true; el('editor').hidden = false;
      el('connection').textContent = 'Đã kết nối'; message('');
      const state = await request('/status'); baseline = state.revision;
      tab('wifi'); await loadSettings(); updateWifi(await request('/wifi'));
      await poll();
    } catch (error) { logout(); message(error.message,true); }
    finally { el('login-button').disabled = false; controls(); }
  }
  el('login-button').onclick = connect;
  el('file').onchange = async () => {
    const file = el('file').files[0];
    el('file').value = '';
    if (!file) return;
    const generation = ++fileGeneration;
    setProc('Đang đọc ảnh…');
    try {
      const gif = file.type === 'image/gif' || /\.gif$/i.test(file.name);
      if ((!gif && !['image/png','image/jpeg','image/webp'].includes(file.type)) || file.size > 8 * 1024 * 1024)
        throw new Error('Chọn PNG, JPEG, WebP hoặc GIF.');
      let nextGif;
      if (gif) {
        if (!globalThis.FriendGifResize) throw new Error('Không tải được bộ xử lý GIF. Hãy mở lại trang.');
        const resized=await globalThis.FriendGifResize.resizeGif(file,
          (done,total)=>setProc(`Đang xử lý GIF ${done}/${total}`,total ? done / total : null));
        nextGif=new Uint8Array(await resized.arrayBuffer());
      }
      setProc('Đang xử lý ảnh…');
      const decoded = await createImageBitmap(file,{imageOrientation:'from-image'}).catch(() => { throw new Error('Không đọc được ảnh này.'); });
      if (generation !== fileGeneration) { decoded.close(); return; }
      try { imagePlacement(decoded.width,decoded.height,el('fit').value); }
      catch (error) { decoded.close(); throw error; }
      const next = await shrink(decoded);
      if (generation !== fileGeneration) { next.close(); return; }
      const previous = bitmap; bitmap = next; gifData = nextGif; previous?.close(); showGif(); resetView(); draw(); controls();
      message(gif ? `GIF đã thu vừa màn hình (${Math.round(nextGif.length/1024)} KB); xem trước đang phát ảnh động.` : '');
    } catch (error) { message(error.message,true); }
    finally { if (generation === fileGeneration) setProc(); }
  };
  el('fit').onchange = () => { panX = panY = 0; draw(); };
  el('background').oninput = draw;
  el('zoom').oninput = () => setZoom(Number(el('zoom').value) / 100);
  el('reset-view').onclick = () => { resetView(); draw(); };
  const canPick = () => !pending && !uploadBusy && !processing;
  const editable = () => bitmap && !gifData && canPick();
  const openPicker = () => { if (canPick()) el('file').click(); };
  const pinchDistance = () => { const [a,b] = [...pointers.values()]; return Math.hypot(a.x - b.x,a.y - b.y) || 1; };
  let tap = null; // chạm nhẹ không kéo = mở hộp chọn ảnh
  canvas.onpointerdown = event => {
    if (!canPick()) return;
    event.preventDefault(); canvas.setPointerCapture(event.pointerId); canvas.focus({preventScroll:true});
    pointers.set(event.pointerId,{x:event.clientX,y:event.clientY});
    if (pointers.size === 1) tap = {x:event.clientX,y:event.clientY,moved:false};
    else if (tap) tap.moved = true;
    pinch = pointers.size === 2 ? pinchDistance() : 0;
  };
  canvas.onpointermove = event => {
    const last = pointers.get(event.pointerId);
    if (!last) return;
    const now = {x:event.clientX,y:event.clientY};
    pointers.set(event.pointerId,now);
    if (tap && Math.hypot(now.x - tap.x,now.y - tap.y) > 6) tap.moved = true;
    if (!editable()) return;
    if (pointers.size === 1) {
      const ratio = SIZE / canvas.getBoundingClientRect().width;
      panX += (now.x - last.x) * ratio; panY += (now.y - last.y) * ratio;
    } else if (pointers.size === 2) {
      const distance = pinchDistance(); if (pinch) setZoom(zoom * distance / pinch); pinch = distance;
    }
    schedule();
  };
  canvas.onpointerup = event => {
    const wasDown = pointers.delete(event.pointerId);
    pinch = pointers.size === 2 ? pinchDistance() : 0;
    if (wasDown && pointers.size === 0 && tap && !tap.moved) openPicker();
    if (pointers.size === 0) tap = null;
  };
  canvas.onpointercancel = event => {
    pointers.delete(event.pointerId); pinch = pointers.size === 2 ? pinchDistance() : 0;
    if (pointers.size === 0) tap = null;
  };
  canvas.addEventListener('wheel',event => {
    if (!editable()) return;
    event.preventDefault(); setZoom(zoom * Math.exp(-event.deltaY / 500));
  },{passive:false});
  canvas.onkeydown = event => {
    if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); openPicker(); return; }
    if (!editable()) return;
    const step = event.shiftKey ? 20 : 5;
    const move = {ArrowLeft:[-step,0],ArrowRight:[step,0],ArrowUp:[0,-step],ArrowDown:[0,step]}[event.key];
    if (move) { panX += move[0]; panY += move[1]; }
    else if (event.key === '+' || event.key === '=') setZoom(zoom * 1.1);
    else if (event.key === '-') setZoom(zoom / 1.1);
    else if (event.key === '0') resetView();
    else return;
    event.preventDefault(); schedule();
  };
  el('upload').onsubmit = async event => {
    event.preventDefault();
    if (!token || !bitmap || pending || uploadBusy) return;
    uploadBusy = true; clearTimeout(pollTimer); controls();
    try {
      while (pollBusy) await new Promise(resolve => setTimeout(resolve,50));
      const pixels = gifData || encodeRgb565(ctx.getImageData(0,0,240,240).data);
      await request('/image',{method:'POST',headers:{'Content-Type':gifData ? 'image/gif' : 'application/octet-stream'},body:pixels});
      pending = true; message('Đã gửi ảnh, Hisu đang lưu…');
    } catch (error) { message(error.message,true); }
    finally { uploadBusy = false; controls(); await poll(); }
  };
  el('connection').onclick = async () => {
    if (!token || closed) return;
    try { await request('/logout',{method:'POST'}); message('Đã đóng Setting trên Hisu.'); }
    catch { message('Đóng Setting trên Hisu để hủy phiên thiết bị.',true); }
    finally { logout(); }
  };
  window.addEventListener('pagehide', () => {
    if (token && !closed) void fetch('/leave',{method:'POST',keepalive:true,cache:'no-store',credentials:'omit',
      headers:{Authorization:`Bearer ${token}`,'X-Hisu-Page':String(pageId)},body:''}).catch(() => {});
    token = ''; closed = true; clearTimeout(pollTimer); bitmap?.close(); bitmap = undefined; if (gifUrl) URL.revokeObjectURL(gifUrl);
  });
  window.addEventListener('pageshow',event => { if (event.persisted) void connect(); });
  draw();
  void connect();
}
