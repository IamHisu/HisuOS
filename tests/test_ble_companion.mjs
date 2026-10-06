import assert from 'node:assert/strict';
import {encodeWifi, encodeTime, characteristic,decodeNetworks} from '../docs/ble/app.js';
assert.equal(characteristic(4), 'e3a10004-7d4b-4ee0-b417-6d9f67485501');
assert.equal(encodeWifi('Home', 'abcdefgh')[1].length, 8);
assert.equal(encodeWifi('Open', '')[1].length, 0);
assert.equal(encodeWifi('Home', 'f'.repeat(64))[1].length, 64);
for (const [ssid, pass] of [['', 'abcdefgh'], ['a'.repeat(33), 'abcdefgh'], ['a\0b', 'abcdefgh'], ['Home', 'short'], ['Home', 'g'.repeat(64)], ['Home', 'mật khẩu']]) {
  assert.throws(() => encodeWifi(ssid, pass));
}
const date = {getTime: () => 1791196200000, getTimezoneOffset: () => -420};
const value = new DataView(encodeTime(date).buffer);
assert.equal(value.getBigUint64(0, true), 1791196200n);
assert.equal(value.getInt16(8, true), 420);
assert.throws(() => encodeTime(new Date('invalid')));
const packet=Uint8Array.of(1,1,1,0,0,0,0,4,72,111,109,101,186,7);
assert.deepEqual(decodeNetworks(new DataView(packet.buffer)).aps,[{ssid:'Home',rssi:-70,secure:true,saved:true,supported:true}]);
assert.throws(()=>decodeNetworks(new DataView(packet.buffer,0,packet.length-1)));
assert.throws(()=>decodeNetworks(new DataView(Uint8Array.of(1,1,13,0,0,0,0).buffer)));
assert.equal(decodeNetworks(new DataView(Uint8Array.of(1,3,0,0,0,0,0).buffer)).scanning,true);
console.log('BLE companion payload tests passed.');
