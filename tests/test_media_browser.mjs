import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {readFile,mkdir} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const require = createRequire(import.meta.url);
const {chromium} = require('playwright');
const root = new URL('../components/hisu_media/web/',import.meta.url);
const output = new URL('../build/media-browser/',import.meta.url);
await mkdir(output,{recursive:true});
const assets = new Map();
for (const [route,file,type] of [['/','index.html','text/html'],['/app.js','app.js','text/javascript'],
  ['/gif-resize.js','gif-resize.js','text/javascript'],['/style.css','style.css','text/css']])
  assets.set(route,{body:await readFile(new URL(file,root)),type});
let state, uploaded, uploadType, wifi, device, wifiRequests, sessionRequests, leaveRequests = 0;
let gifAssetRequests = 0;
const token = 'a'.repeat(64);
const wifiOnly = process.argv.includes('--wifi-only');
const server = createServer(async(req,res)=>{
  try {
    const asset = assets.get(req.url);
    if (asset) {
      if (req.url === '/gif-resize.js') ++gifAssetRequests;
      res.writeHead(200,{'Content-Type':asset.type});res.end(asset.body);return;
    }
    const chunks=[];
    for await (const chunk of req) chunks.push(chunk);
    const body=Buffer.concat(chunks);
    const send=(status,data)=>{res.writeHead(status,{'Content-Type':'application/json'});res.end(JSON.stringify(data));};
    if (req.url==='/session') {
      ++sessionRequests;assert.equal(req.method,'POST');assert.equal(body.length,0);
      send(state.expired ? 403 : 200,{token,page:sessionRequests});return;
    }
    if (state.expired || req.headers.authorization!==`Bearer ${token}`) {send(403,{});return;}
    if (req.url==='/leave') {assert.equal(req.method,'POST');assert.equal(body.length,0);assert.ok(Number(req.headers['x-hisu-page'])>0);++leaveRequests;send(200,{closing:true});return;}
    if (req.url==='/settings') {
      if (req.method==='POST') {
        const setting=JSON.parse(body); assert.ok(Object.hasOwn(device,setting.key));
        assert.equal(Number.isInteger(setting.value),true); device[setting.key]=setting.value;
        send(200,{saved:true});
      } else send(200,device);
      return;
    }
    if (req.url==='/wifi') {send(200,wifi);return;}
    if (req.method==='GET' && ['/asr-key','/gemini-key'].includes(req.url)) {send(200,{configured:false});return;}
    if (req.url==='/time') {assert.ok(Number.isInteger(JSON.parse(body).utc));send(200,{saved:true});return;}
    if (req.url.startsWith('/wifi/')) {
      const data=body.length ? JSON.parse(body) : null; wifiRequests.push({path:req.url,data});
      if (req.url==='/wifi/power') wifi.enabled=data.enabled;
      if (req.url==='/wifi/join') {wifi.connected=true;wifi.internet='online';wifi.ssid=data.ssid;wifi.ip='192.168.1.55';}
      send(200,{accepted:true});return;
    }
    if (req.url==='/image') {
      uploadType=req.headers['content-type'];
      if (uploadType==='image/gif') assert.equal(body.subarray(0,3).toString(),'GIF');
      else assert.equal(body.length,115200);uploaded=body;state.saving=true;send(200,{saving:true});
      setTimeout(()=>{state.saving=false;++state.revision;},500);return;
    }
    if (req.url==='/status') {send(200,state);return;}
    if (req.url==='/logout') {state.expired=true;send(200,{closed:true});return;}
    send(404,{});
  } catch(error) {res.writeHead(500);res.end(error.message);}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
try {
  browser=await chromium.launch({headless:true,...(process.env.HISU_BROWSER_PATH ? {executablePath:process.env.HISU_BROWSER_PATH} : {})});
  for (const [name,width,height] of [['desktop',1280,800],['mobile',390,844],['small-mobile',320,720]]) {
    state={pending:false,saving:false,revision:1,error:0,seconds:299,expired:false};uploaded=null;sessionRequests=0;gifAssetRequests=0;
    device={brightness:75,volume:55,speaker:1,timeout:0,theme:0,timezone:420};wifiRequests=[];
    wifi={enabled:true,scanning:false,joining:false,connected:false,internet:'unknown',error:0,ssid:'',ip:'',aps:[
      {ssid:'Saved home',rssi:-45,saved:true,secure:true,supported:true},
      {ssid:'<img src=x onerror=alert(1)>',rssi:-60,saved:false,secure:true,supported:true},
      {ssid:'Enterprise',rssi:-70,saved:false,secure:true,supported:false},
    ]};
    const context=await browser.newContext({viewport:{width,height}});
    const page=await context.newPage();
    const errors=[];page.on('pageerror',error=>errors.push(error.message));
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    assert.equal(await page.evaluate(async()=> (await fetch('/settings')).status),403);
    assert.equal(await page.locator('#pin').count(),0);
    await page.locator('#editor').waitFor({state:'visible'});
    assert.equal(gifAssetRequests,0,'Setting must open without loading the GIF processor');
    await page.waitForFunction(()=>document.querySelectorAll('.wifi-row').length===3);
    assert.equal(sessionRequests,1);
    assert.equal(await page.locator('#panel-wifi').isVisible(),true);
    assert.equal(await page.locator('.wifi-row').filter({hasText:'Enterprise'}).isDisabled(),true);
    assert.equal(await page.locator('#panel-wifi img').count(),0);
    assert.equal(await page.locator('#wifi-network').count(),0);
    assert.equal(await page.locator('#wifi-networks').textContent().then(text=>text.includes('dBm')),false);
    await page.evaluate(()=> {window.firstWifiRow=document.querySelector('.wifi-row');});
    await page.waitForTimeout(2300);
    assert.equal(await page.evaluate(()=>window.firstWifiRow===document.querySelector('.wifi-row')),true);
    await page.locator('#wifi-scan').click();
    await page.waitForFunction(()=>document.getElementById('message').textContent.includes('Yêu cầu'));
    assert.ok(wifiRequests.some(request=>request.path==='/wifi/scan'));
    await page.locator('.wifi-row').filter({hasText:'Saved home'}).click();
    await page.waitForFunction(()=>document.getElementById('wifi-status').textContent.includes('192.168.1.55'));
    assert.deepEqual(wifiRequests.find(request=>request.path==='/wifi/join').data,{ssid:'Saved home',password:'',saved:true});
    assert.equal(await page.locator('#wifi-password').inputValue(),'');
    assert.equal(await page.locator('#wifi-dialog').isVisible(),false);
    const savedRow=page.locator('.wifi-row').filter({hasText:'Saved home'});
    for (const [internet,text,warning] of [
      ['checking','Đang kiểm tra Internet',false],
      ['captive','Mạng yêu cầu đăng nhập',true],
      ['offline','Wi-Fi chưa có Internet',true],
      ['online','Có Internet',false],
    ]) {
      wifi.internet=internet;
      await page.waitForFunction(text=>document.getElementById('wifi-status').textContent.includes(text),text);
      assert.equal(await savedRow.locator('.wifi-warning').isVisible(),warning);
      assert.equal(await savedRow.getAttribute('data-connected'),'true');
      assert.equal(await page.locator('.wifi-row').filter({hasText:'Enterprise'}).locator('.wifi-warning').isVisible(),false);
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
      if (warning) {
        assert.equal(await savedRow.evaluate(row=>{
          const badge=row.querySelector('.wifi-warning').getBoundingClientRect();
          const signal=row.querySelector('.wifi-signal-wrap').getBoundingClientRect();
          return badge.left>=signal.left && badge.right<=signal.right && badge.bottom<=signal.bottom;
        }),true);
        await page.screenshot({path:fileURLToPath(new URL(`${name}-wifi-${internet}.png`,output)),fullPage:true});
      }
    }
    // A rescan or another connection must not hide a saved network's auth failure.
    wifi.aps[0].password_rejected=true;
    wifi.connected=false;wifi.error=0;wifi.ssid='Other network';
    await page.locator('#wifi-scan').click();
    await page.waitForFunction(()=>document.querySelector('.wifi-row').dataset.connected === 'false');
    const joinsBeforeCorrection=wifiRequests.filter(request=>request.path==='/wifi/join').length;
    await page.locator('.wifi-row').filter({hasText:'Saved home'}).click();
    await page.locator('#wifi-dialog').waitFor({state:'visible'});
    assert.equal(await page.locator('#wifi-dialog-title').textContent(),'Saved home');
    assert.equal(wifiRequests.filter(request=>request.path==='/wifi/join').length,joinsBeforeCorrection);
    await page.locator('#wifi-cancel').click();
    await page.screenshot({path:fileURLToPath(new URL(`${name}-wifi.png`,output)),fullPage:true});
    await page.locator('.wifi-row').filter({hasText:'<img src=x onerror=alert(1)>'}).click();
    await page.locator('#wifi-dialog').waitFor({state:'visible'});
    assert.equal(await page.locator('#wifi-dialog-title').textContent(),'<img src=x onerror=alert(1)>');
    await page.locator('#wifi-password').fill('password123');
    await page.screenshot({path:fileURLToPath(new URL(`${name}-wifi-dialog.png`,output)),fullPage:true});
    await page.locator('#wifi-connect').click();
    await page.waitForFunction(()=>document.getElementById('wifi-status').textContent.includes('<img'));
    assert.deepEqual(wifiRequests.filter(request=>request.path==='/wifi/join').at(-1).data,{ssid:'<img src=x onerror=alert(1)>',password:'password123'});
    assert.equal(await page.locator('#wifi-password').inputValue(),'');
    assert.equal(await page.locator('#wifi-hidden-option').isVisible(),false);
    await page.locator('#wifi-hidden').click();
    assert.equal(await page.locator('#wifi-hidden-option').isVisible(),true);
    assert.equal(await page.locator('#wifi-is-hidden').isChecked(),false);
    await page.locator('#wifi-ssid').fill('Private hidden');
    await page.locator('#wifi-password').fill('hiddenpass');
    await page.locator('#wifi-is-hidden').check();
    await page.screenshot({path:fileURLToPath(new URL(`${name}-wifi-hidden.png`,output)),fullPage:true});
    await page.locator('#wifi-connect').click();
    await page.waitForFunction(()=>document.getElementById('wifi-status').textContent.includes('Private hidden'));
    assert.deepEqual(wifiRequests.filter(request=>request.path==='/wifi/join').at(-1).data,
      {ssid:'Private hidden',password:'hiddenpass',hidden:true});
    await page.locator('#wifi-hidden').click();
    assert.equal(await page.locator('#wifi-is-hidden').isChecked(),false);
    await page.locator('#wifi-ssid').fill('Normal network');
    await page.locator('#wifi-password').fill('normalpass');
    await page.locator('#wifi-connect').click();
    await page.waitForFunction(()=>document.getElementById('wifi-status').textContent.includes('Normal network'));
    assert.deepEqual(wifiRequests.filter(request=>request.path==='/wifi/join').at(-1).data,
      {ssid:'Normal network',password:'normalpass'});
    if (wifiOnly) {
      await page.locator('#tab-wallpaper').click();
      const tinyGif = {name:'test.gif',mimeType:'image/gif',
        buffer:Buffer.from('R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7','base64')};
      await page.route('**/gif-resize.js',route=>route.abort());
      await page.locator('#file').setInputFiles(tinyGif);
      await page.waitForFunction(()=>document.getElementById('message').textContent.includes('Không tải được bộ xử lý GIF'));
      assert.equal(await page.locator('#editor').isVisible(),true);
      await page.unroute('**/gif-resize.js');
      await page.locator('#file').setInputFiles(tinyGif);
      await page.waitForFunction(()=>document.getElementById('message').textContent.includes('đã thu vừa'));
      assert.equal(gifAssetRequests,1,'A failed GIF download can be retried without reopening Setting');
      await page.screenshot({path:fileURLToPath(new URL(`${name}-gif-retry.png`,output)),fullPage:true});
      assert.deepEqual(errors,[]);
      await context.close();
      continue;
    }
    await page.reload();
    await page.waitForFunction(()=>document.querySelectorAll('.wifi-row').length===3);
    assert.equal(sessionRequests,2);
    assert.ok(leaveRequests>0,'Reload sends authenticated pagehide leave notification');
    await page.locator('#tab-device').click();
    await page.locator('#brightness').fill('42');await page.locator('#brightness').dispatchEvent('change');
    await page.waitForFunction(()=>document.getElementById('message').textContent.includes('Đã lưu thiết lập'));
    assert.equal(device.brightness,42);
    await page.locator('#time-sync').click();
    await page.waitForFunction(()=>document.getElementById('message').textContent.includes('Đã đồng bộ giờ'));
    await page.screenshot({path:fileURLToPath(new URL(`${name}-device.png`,output)),fullPage:true});
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.locator('#tab-device').focus();await page.keyboard.press('ArrowLeft');
    assert.equal(await page.locator('#tab-wallpaper').getAttribute('aria-selected'),'true');
    const png=await page.evaluate(()=>{
      const canvas=document.createElement('canvas');canvas.width=480;canvas.height=240;
      const ctx=canvas.getContext('2d');
      ['#ff0000','#00ff00','#0000ff'].forEach((color,i)=>{ctx.fillStyle=color;ctx.fillRect(i*160,0,160,240);});
      return canvas.toDataURL('image/png').split(',')[1];
    });
    await page.locator('#file').setInputFiles({name:'test.png',mimeType:'image/png',buffer:Buffer.from(png,'base64')});
    await page.waitForFunction(()=>!document.getElementById('send').disabled);
    const center=await page.evaluate(()=>[...document.getElementById('preview').getContext('2d').getImageData(120,120,1,1).data]);
    assert.deepEqual(center,[0,255,0,255]);
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    const overlaps=await page.evaluate(()=>{
      const ids=['preview','file','fit','background','send'];
      const rects=ids.map(id=>document.getElementById(id).getBoundingClientRect());
      return rects.some((a,i)=>rects.slice(i+1).some(b=>a.left<b.right&&a.right>b.left&&a.top<b.bottom&&a.bottom>b.top));
    });
    assert.equal(overlaps,false);
    await page.screenshot({path:fileURLToPath(new URL(`${name}-editor.png`,output)),fullPage:true});
    await page.locator('#fit').selectOption('contain');
    const top=await page.evaluate(()=>[...document.getElementById('preview').getContext('2d').getImageData(120,10,1,1).data]);
    assert.deepEqual(top,[0,0,0,255]);
    await page.locator('#fit').selectOption('cover');
    await page.locator('#send').click();
    await page.waitForFunction(()=>document.getElementById('message').textContent.includes('Đang lưu'));
    assert.ok(uploaded);assert.equal(uploaded.readUInt16LE((120*240+120)*2),0x07e0);
    assert.equal(await page.locator('#send').isDisabled(),true);
    await page.waitForFunction(()=>document.getElementById('message').textContent.includes('đã cập nhật'));
    assert.equal(await page.locator('#send').isDisabled(),false);
    const gif=await readFile(new URL('../components/hisu_ui/assets/boot_windows_xp_source.gif',import.meta.url));
    assert.ok(gif.readUInt16LE(6)>240 || gif.readUInt16LE(8)>240);
    await page.locator('#file').setInputFiles({name:'animated.gif',mimeType:'image/gif',buffer:gif});
    await page.waitForFunction(()=>document.getElementById('message').textContent.includes('đã thu vừa'));
    assert.equal(await page.locator('#zoom').isDisabled(),true);
    await page.locator('#send').click();
    await page.waitForFunction(()=>document.getElementById('message').textContent.includes('đã cập nhật'));
    assert.equal(uploadType,'image/gif');
    assert.ok(uploaded.length<=1024*1024);
    assert.ok(uploaded.readUInt16LE(6)<=240);assert.ok(uploaded.readUInt16LE(8)<=240);
    await page.locator('#file').setInputFiles({name:'broken.gif',mimeType:'image/gif',buffer:Buffer.from('GIF89a')});
    await page.waitForFunction(()=>document.getElementById('message').dataset.error==='true');
    assert.ok((await page.locator('#message').textContent()).length>0);
    assert.equal(await page.locator('#logout').count(),0);
    assert.equal(await page.locator('#preview-hint').count(),0);
    assert.equal(await page.evaluate(()=>localStorage.length),0);
    if (name==='desktop') await page.locator('#connection').click();
    else state.expired=true;
    await page.locator('#login-section').waitFor({state:'visible'});
    assert.equal(await page.locator('#connection').textContent(),'Đã ngắt kết nối');
    await page.locator('#login-button').click();
    await page.waitForFunction(()=>!document.getElementById('login-button').disabled);
    assert.equal(await page.locator('#editor').isVisible(),false);
    assert.deepEqual(errors,[]);
    await context.close();
  }
  state.expired=false;
  const closingContext=await browser.newContext();
  const closingPage=await closingContext.newPage();
  await closingPage.goto(`http://127.0.0.1:${server.address().port}/`);
  await closingPage.locator('#editor').waitFor({state:'visible'});
  const beforeClose=leaveRequests;
  await closingPage.close();
  for (let i=0;i<20 && leaveRequests===beforeClose;++i) await new Promise(resolve=>setTimeout(resolve,100));
  assert.ok(leaveRequests>beforeClose,'Closing an active page sends authenticated leave');
  await closingContext.close();
  console.log(wifiOnly ? 'Wi-Fi browser tests passed: three viewports, connectivity states, warning badge/recovery, saved join, rescan after auth rejection, password correction, unsafe SSID and page close. HTTP API mocked; device radio not tested.' :
    'Setting browser tests passed: three viewports, Wi-Fi/settings, PNG save, animated GIF resize from >240px, reload/close leave, logout/expiry and retry. HTTP API mocked; device captive portal/AP/flash not tested.');
} finally {
  await browser?.close();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
}
