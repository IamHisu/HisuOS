import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const {instance} = await WebAssembly.instantiate(await readFile(process.argv[2]));
const api = instance.exports;
function feed(text, opcode, fin, offset, payload, capacity=64) {
  const bytes = Buffer.from(text);
  new Uint8Array(api.memory.buffer, api.test_input(), bytes.length).set(bytes);
  return api.test_feed(bytes.length, opcode, fin, offset, payload, capacity);
}
function output() { return Buffer.from(api.memory.buffer, api.test_output(), 64).toString('utf8').split('\0')[0]; }
api.test_reset();
assert.equal(feed('{"setup',1,1,0,19),0);
assert.equal(feed('Complete":{}}',1,1,7,19),1);
assert.equal(output(),'{"setupComplete":{}}');
assert.equal(feed('x'.repeat(63),1,1,0,63),1); // previous message must not count toward capacity
assert.equal(feed('Hi ',1,0,0,3),0);
assert.equal(feed('Su',0,1,0,2),1);
assert.equal(output(),'Hi Su');
api.test_reset();
assert.equal(feed('x',0,1,0,1),-1); // orphan continuation
api.test_reset();
assert.equal(feed('abcd',1,1,0,4,4),-1); // reserve NUL byte
api.test_reset();
assert.equal(feed('ab',1,0,0,4),0);
assert.equal(feed('cd',1,0,1,4),-1); // offset overlap
api.test_reset();
assert.equal(feed('a',1,0,0,1),0);
assert.equal(feed('b',1,1,0,1),-1); // new message before final continuation
api.test_reset();
assert.equal(feed('tiếng Việt',1,1,0,Buffer.byteLength('tiếng Việt')),1);
assert.equal(output(),'tiếng Việt');
assert.equal(feed('{}',2,1,0,2),1); // JSON delivered as binary WebSocket message
assert.equal(api.test_feed(-1,1,1,0,1,64),-1);
console.log('Live WebSocket C framing tests passed (chunking, continuation, UTF-8, overflow, invalid offsets).');
