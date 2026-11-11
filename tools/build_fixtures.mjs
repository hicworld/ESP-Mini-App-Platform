import wabtFactory from 'wabt';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const wabt=await wabtFactory();
const empty='(func (export "app_event") (param i32 i32 i32))';
const init='(func (export "app_init"))';
const cases=[
  ['audio_no_permission',`(module (import "platform" "audio" (func $a (param i32 i32) (result i32))) (func (export "app_init") (drop (call $a (i32.const 3) (i32.const -1)))) ${empty})`,true,0],
  ['audio_quota',`(module (import "platform" "audio" (func $a (param i32 i32) (result i32))) (func (export "app_init") ${Array(5).fill('(drop (call $a (i32.const 3) (i32.const -1)))').join(' ')}) ${empty})`,true,0],
  ['audio_invalid',`(module (import "platform" "audio" (func $a (param i32 i32) (result i32))) (func (export "app_init") (if (i32.ne (call $a (i32.const 3) (i32.const 100)) (i32.const -2)) (then unreachable))) ${empty})`,false,0],
  ['ok',`(module ${init} ${empty})`,false,0],
  ['loop_init',`(module (func (export "app_init") (loop $l (br $l))) ${empty})`,true,0],
  ['loop_event',`(module ${init} (func (export "app_event") (param i32 i32 i32) (loop $l (br $l))))`,true,1],
  ['bad_pointer',`(module (import "platform" "text" (func $text (param i32 i32 i32 i32 i32))) (memory 1 4) (func (export "app_init") (call $text (i32.const 0) (i32.const 0) (i32.const 65535) (i32.const 100) (i32.const 0))) ${empty})`,true,0],
  ['init_result',`(module (func (export "app_init") (result i32) (i32.const 1)) ${empty})`,true,0],
  ['event_type',`(module ${init} (func (export "app_event") (param i64 i32 i32)))`,true,0],
  ['start_section',`(module (func $start (loop $l (br $l))) (start $start) ${init} ${empty})`,true,0],
  ['constructor',`(module (func (export "__wasm_call_ctors") (loop $l (br $l))) ${init} ${empty})`,true,0],
  ['memory_limit',`(module (memory 5 5) ${init} ${empty})`,true,0],
  ['imported_memory',`(module (import "env" "memory" (memory 5 5)) ${init} ${empty})`,true,0],
  ['memory_growth',`(module (memory (export "memory") 1 8) (func (export "app_init") (if (i32.ne (memory.grow (i32.const 3)) (i32.const 1)) (then unreachable)) (if (i32.ne (memory.grow (i32.const 1)) (i32.const -1)) (then unreachable))) ${empty})`,false,0],
  ['storage',`(module
    (import "platform" "kv_set" (func $kv_set (param i32 i32 i32 i32) (result i32)))
    (import "platform" "kv_get" (func $kv_get (param i32 i32 i32 i32) (result i32)))
    (memory 1 4) (data (i32.const 0) "kvx")
    (func (export "app_init")
      (if (i32.ne (call $kv_set (i32.const 0) (i32.const 1) (i32.const 1) (i32.const 1)) (i32.const 1)) (then unreachable))
      (if (i32.ne (call $kv_get (i32.const 0) (i32.const 1) (i32.const 2) (i32.const 1)) (i32.const 1)) (then unreachable))
      (if (i32.ne (i32.load8_u (i32.const 2)) (i32.const 118)) (then unreachable)))
    ${empty})`,false,0],
  ['storage_no_permission',`(module (import "platform" "kv_set" (func $kv_set (param i32 i32 i32 i32) (result i32))) (memory 1 4) (data (i32.const 0) "kvx") (func (export "app_init") (drop (call $kv_set (i32.const 0) (i32.const 1) (i32.const 1) (i32.const 1)))) ${empty})`,true,0],
  ['storage_reserved',`(module (import "platform" "kv_set" (func $set (param i32 i32 i32 i32) (result i32))) (memory 1 4) (data (i32.const 0) "__id") (func (export "app_init") (drop (call $set (i32.const 0) (i32.const 4) (i32.const 10) (i32.const 1)))) ${empty})`,true,0],
  ['storage_bad_pointer',`(module (import "platform" "kv_set" (func $set (param i32 i32 i32 i32) (result i32))) (memory 1 4) (data (i32.const 0) "key") (func (export "app_init") (drop (call $set (i32.const 0) (i32.const 3) (i32.const 65535) (i32.const 128)))) ${empty})`,true,0],
  ['storage_write_limit',`(module
    (import "platform" "kv_set" (func $set (param i32 i32 i32 i32) (result i32)))
    (memory 1 4) (data (i32.const 0) "rate12345")
    (func (export "app_init")
      (if (i32.ne (call $set (i32.const 0) (i32.const 4) (i32.const 4) (i32.const 1)) (i32.const 1)) (then unreachable))
      (if (i32.ne (call $set (i32.const 0) (i32.const 4) (i32.const 5) (i32.const 1)) (i32.const 1)) (then unreachable))
      (if (i32.ne (call $set (i32.const 0) (i32.const 4) (i32.const 6) (i32.const 1)) (i32.const 1)) (then unreachable))
      (if (i32.ne (call $set (i32.const 0) (i32.const 4) (i32.const 7) (i32.const 1)) (i32.const 1)) (then unreachable))
      (if (i32.ne (call $set (i32.const 0) (i32.const 4) (i32.const 8) (i32.const 1)) (i32.const -3)) (then unreachable)))
    ${empty})`,false,0],
  ['storage_key_limit',`(module
    (import "platform" "kv_set" (func $set (param i32 i32 i32 i32) (result i32)))
    (memory 1 4) (data (i32.const 0) "k0k1k2k3k4k5k6k7k8") (data (i32.const 32) "012345678")
    (global $index (mut i32) (i32.const 0))
    (func (export "app_init"))
    (func (export "app_event") (param i32 i32 i32) (local $result i32)
      (local.set $result (call $set
        (i32.mul (global.get $index) (i32.const 2)) (i32.const 2)
        (i32.add (i32.const 32) (global.get $index)) (i32.const 1)))
      (if (i32.lt_u (global.get $index) (i32.const 8))
        (then (if (i32.ne (local.get $result) (i32.const 1)) (then unreachable)))
        (else (if (i32.ne (local.get $result) (i32.const -3)) (then unreachable))))
      (global.set $index (i32.add (global.get $index) (i32.const 1)))))`,false,1],
  ['future_platform',`(module ${init} ${empty})`,true,0],
  ['no_permission',`(module (import "platform" "clear" (func $clear (param i32))) (func (export "app_init") (call $clear (i32.const 0))) ${empty})`,true,0],
];
const folder=path.join(root,'dist','fixtures'); fs.mkdirSync(folder,{recursive:true});
for (const [name,wat] of cases) {
  const mod=wabt.parseWat(name,wat); mod.resolveNames(); mod.validate();
  fs.writeFileSync(path.join(folder,`${name}.wasm`),Buffer.from(mod.toBinary({}).buffer)); mod.destroy();
}
fs.writeFileSync(path.join(folder,'cases.json'),JSON.stringify(cases.map(([name,_,fail,event])=>({name,fail,event})),null,2));
console.log(`Built ${cases.length} device test fixtures (not executed on host)`);
