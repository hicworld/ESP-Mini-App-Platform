/* Behaviour test for the Bounce example application.
 *
 * Passing a module name lets the same contract be applied to both language
 * chains: node tools/bounce_test.mjs dist/bounce.wasm (C++) and
 * node tools/bounce_test.mjs dist/rust_bounce.wasm (Rust) must behave
 * identically, which is the point of sharing one ABI.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const target = process.argv[2] ?? 'dist/bounce.wasm';
const label = path.basename(target);
const module = await WebAssembly.compile(fs.readFileSync(path.join(root, target)));

for (const i of WebAssembly.Module.imports(module)) {
  assert.equal(i.module, 'platform');
  assert.ok(['scene', 'millis'].includes(i.name), i.name);
}
assert.ok(!WebAssembly.Module.imports(module).some(i => ['clear', 'rect', 'text'].includes(i.name)),
  'atomic renderer must not import legacy immediate drawing');

let instance, now = 0, frame = [], calls = 0;
const bytes = (p, n) => new Uint8Array(instance.exports.memory.buffer, p, n);
const string = (p, n) => new TextDecoder().decode(bytes(p, n));
const imports = {platform: {
  scene: (p, count, background) => {
    calls++;
    assert.ok(count > 0 && count <= 96, `scene count ${count}`);
    frame = [];
    const view = new DataView(instance.exports.memory.buffer);
    for (let i = 0; i < count; i++) {
      const base = p + i * 64;
      const kind = view.getUint32(base, true), rgb = view.getUint32(base + 4, true);
      const x = view.getInt32(base + 8, true), y = view.getInt32(base + 12, true);
      const w = view.getInt32(base + 16, true), h = view.getInt32(base + 20, true);
      assert.ok(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= 480 && y + h <= 400,
        `command ${kind}: ${x},${y},${w},${h}`);
      if (kind === 1) frame.push({type: 'rect', x, y, w, h, rgb});
      else if (kind === 4) {
        const raw = bytes(base + 32, 32), end = raw.indexOf(0);
        assert.ok(end >= 0, 'scene text must be NUL terminated');
        frame.push({type: 'text', x, y, text: string(base + 32, end), rgb});
      } else assert.fail(`unexpected scene command ${kind}`);
    }
    return background <= 0xffffff ? 0 : -1;
  },
  millis: () => now,
}};
instance = await WebAssembly.instantiate(module, imports);

function call(fn, ...args) {
  calls = 0;
  instance.exports[fn](...args);
  assert.ok(calls <= 1, `expected at most one atomic frame, got ${calls}`);
  return calls;
}
function tick(ms = 40) { now = (now + ms) >>> 0; return call('app_event', 1, now, 0); }
function touch(x, y) { return call('app_event', 2, x, y); }
const block = () => frame.find(d => d.type === 'rect');
const at = () => [block().x, block().y];

assert.equal(call('app_init'), 1, 'initial frame is one atomic scene call');
assert.deepEqual(at(), [228, 188], 'starts near the centre');
assert.equal(frame.filter(d => d.type === 'text').length, 1, 'draws one label');

assert.equal(tick(), 1, 'moving frame is one atomic scene call');
assert.deepEqual(at(), [231, 190], 'moves by its velocity each frame');
tick(10);
assert.deepEqual(at(), [231, 190], 'ignores ticks inside the frame interval');

touch(100, 100);
assert.deepEqual(at(), [88, 88], 'tap recentres the block');

// Right edge: the frame that reaches the edge clamps in place and flips the
// velocity, so the block only starts moving back on the following frame.
touch(470, 200);
assert.equal(block().x, 456);
tick();
assert.equal(block().x, 456, 'clamps at the right edge');
tick();
assert.ok(block().x < 456, 'reverses at the right edge');

// Bottom edge behaves the same way.
touch(200, 390);
assert.equal(block().y, 376);
tick();
assert.equal(block().y, 376, 'clamps at the bottom edge');
tick();
assert.ok(block().y < 376, 'reverses at the bottom edge');

// The 32-bit millisecond counter wraps; elapsed time must stay correct.
now = 0xfffffff0;
call('app_init');
const before = block().x;
tick(64);
assert.notEqual(block().x, before, 'keeps moving across the clock wrap');

console.log(`PASS Bounce (${label}): ABI, initial position, per-frame motion, frame throttle, tap placement, edge reversal and clock wrap`);
