import assert from 'node:assert/strict';
const elements = new Map();
for (const id of ['message','connect','disconnect','send','time','state','wifi','ssid','password']) elements.set(id, {textContent:'',value:'',disabled:false});
globalThis.document = {getElementById: id => elements.get(id)};
globalThis.isSecureContext = true;
const writes = []; let poll, disconnected;
const fields = Array.from({length:6}, (_,i) => ({
  writeValueWithResponse: async data => {writes.push([i, [...data]]);},
  readValue: async () => new DataView(new TextEncoder().encode('{"wifi":"connected","error":0,"time":true}').buffer)
}));
const device = {
  addEventListener: (name, callback) => {disconnected=callback;},
  gatt: {connected:false,connect:async()=>{
    device.gatt.connected=true;
    return {getPrimaryService:async()=>({getCharacteristic:async uuid=>fields[Number(uuid.slice(7,8))]})};
  },disconnect:()=>{device.gatt.connected=false;disconnected();}}
};
let requested;
globalThis.location = {hash:'#name=HisuOS-A1B2'};
Object.defineProperty(globalThis,'navigator',{configurable:true,value:{bluetooth:{requestDevice:async options=>{requested=options;return device;}}}});
globalThis.setInterval = fn => {poll=fn;return 1;};
globalThis.clearInterval = () => {poll=null;};
await import('../docs/ble/app.js');
await elements.get('connect').onclick();
assert.equal(requested.filters[0].name,'HisuOS-A1B2');
assert.equal(writes[0][0], 4); // Automatic clock sync precedes Wi-Fi.
assert.equal(elements.get('send').disabled, false);
elements.get('ssid').value='Home';elements.get('password').value='abcdefgh';
await elements.get('wifi').onsubmit({preventDefault(){}});
assert.deepEqual(writes.slice(1).map(x=>x[0]),[1,2,3]);
assert.deepEqual(writes[3][1],[1]);
assert.equal(elements.get('password').value,'');
await poll();assert.equal(elements.get('state').textContent,'Wi-Fi đã kết nối');
const before = writes.length;
elements.get('password').value='short';
await elements.get('wifi').onsubmit({preventDefault(){}});
assert.equal(writes.length,before); // Reject invalid input before any GATT mutation.
elements.get('disconnect').onclick();
assert.equal(elements.get('send').disabled,true);
assert.equal(poll,null);
globalThis.location.hash = '#name=invalid';
await elements.get('connect').onclick();
assert.equal(requested.filters[0].name,undefined);
elements.get('disconnect').onclick();
console.log('BLE companion flow tests passed (mock GATT; no radio).');
