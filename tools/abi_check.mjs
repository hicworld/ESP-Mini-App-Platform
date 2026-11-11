/* Verify a built .wasm matches ABI v1 and the runtime load policy.
 *
 * This mirrors the checks firmware/main/runtime.c performs at load time, so a
 * module rejected here would also be rejected on the device. It exists because
 * "the compiler accepted it" says nothing about whether the reactor ABI holds:
 * a new language backend can easily emit a start section, an imported memory,
 * or a constructor export, all of which the runtime refuses.
 *
 * Usage: node tools/abi_check.mjs dist/snake.wasm [...]
 * Exit code 0 when every module passes, 1 otherwise.
 */
import fs from 'node:fs';
import path from 'node:path';

const NAMES = {0x7f: 'i32', 0x7e: 'i64', 0x7d: 'f32', 0x7c: 'f64'};
const FORBIDDEN_EXPORTS = ['__post_instantiate', '__wasm_call_ctors', '_initialize'];
const MAX_PAGES = 4;

class Reader {
    constructor(bytes) { this.b = bytes; this.p = 0; }
    get done() { return this.p >= this.b.length; }
    u8() { return this.b[this.p++]; }
    u32() {
        let value = 0, shift = 0, byte;
        do {
            byte = this.u8();
            value |= (byte & 0x7f) << shift;
            shift += 7;
        } while (byte & 0x80);
        return value >>> 0;
    }
    bytes(n) {
        const slice = this.b.subarray(this.p, this.p + n);
        this.p += n;
        return slice;
    }
    name(len) { return Buffer.from(this.bytes(len)).toString('utf8'); }
}

function limits(r) {
    const flags = r.u32();
    const min = r.u32();
    const max = flags & 1 ? r.u32() : null;
    return {flags, min, max};
}

function parse(bytes) {
    const r = new Reader(bytes);
    const magic = r.bytes(4).toString('hex');
    if (magic !== '0061736d') throw new Error('not a WebAssembly module (bad magic)');
    /* The version field is a fixed little-endian uint32, not a LEB128. */
    const vb = r.bytes(4);
    const version = vb[0] | (vb[1] << 8) | (vb[2] << 16) | (vb[3] << 24);
    if (version !== 1) throw new Error(`unsupported wasm version ${version}`);

    const out = {types: [], imports: [], funcs: [], memories: [], exports: [], hasStart: false};
    while (!r.done) {
        const id = r.u8();
        const size = r.u32();
        const body = new Reader(bytes.subarray(r.p, r.p + size));
        r.p += size;
        if (id === 1) {
            for (let i = body.u32(); i > 0; i--) {
                if (body.u8() !== 0x60) throw new Error('non-function type entry');
                const params = [];
                for (let n = body.u32(); n > 0; n--) params.push(NAMES[body.u8()] ?? '?');
                const results = [];
                for (let n = body.u32(); n > 0; n--) results.push(NAMES[body.u8()] ?? '?');
                out.types.push({params, results});
            }
        } else if (id === 2) {
            for (let i = body.u32(); i > 0; i--) {
                body.name(body.u32());
                const field = body.name(body.u32());
                const kind = body.u8();
                if (kind === 0x00) out.imports.push({kind: 'function', field, type: body.u32()});
                else if (kind === 0x01) { body.u8(); limits(body); out.imports.push({kind: 'table', field}); }
                else if (kind === 0x02) { out.imports.push({kind: 'memory', field, ...limits(body)}); }
                else if (kind === 0x03) { body.u8(); body.u8(); out.imports.push({kind: 'global', field}); }
                else throw new Error(`unknown import kind 0x${kind.toString(16)}`);
            }
        } else if (id === 3) {
            for (let i = body.u32(); i > 0; i--) out.funcs.push(body.u32());
        } else if (id === 5) {
            for (let i = body.u32(); i > 0; i--) out.memories.push(limits(body));
        } else if (id === 7) {
            for (let i = body.u32(); i > 0; i--) {
                const name = body.name(body.u32());
                const kind = body.u8();
                const index = body.u32();
                const kindName = {0x00: 'func', 0x01: 'table', 0x02: 'memory', 0x03: 'global'}[kind];
                if (!kindName) throw new Error(`unknown export kind 0x${kind.toString(16)}`);
                out.exports.push({name, kind: kindName, index});
            }
        } else if (id === 8) {
            out.hasStart = true;
        }
    }
    return out;
}

/* Resolve the signature of a function index: imported functions occupy the low
 * indices, and the function section lists types for the rest. */
function signature(m, funcIndex) {
    const imported = m.imports.filter(i => i.kind === 'function');
    if (funcIndex < imported.length) return m.types[imported[funcIndex].type] ?? null;
    const typeIndex = m.funcs[funcIndex - imported.length];
    return typeIndex === undefined ? null : m.types[typeIndex] ?? null;
}

function checkBytes(bytes, label) {
    const errors = [];
    let m;
    try {
        m = parse(bytes);
    } catch (e) {
        return {file: label, errors: [`cannot parse: ${e.message}`]};
    }

    if (m.hasStart) {
        errors.push('start section present; the runtime rejects it (executes before the budget is attached)');
    }

    for (const name of FORBIDDEN_EXPORTS) {
        if (m.exports.some(e => e.name === name)) {
            errors.push(`exports "${name}"; automatic constructor exports are not permitted`);
        }
    }

    const nonFunction = m.imports.filter(i => i.kind !== 'function');
    for (const imp of nonFunction) {
        errors.push(`imports a ${imp.kind} ("${imp.field}"); ABI v1 allows function imports only`);
    }

    if (m.memories.length > 1) {
        errors.push(`${m.memories.length} memory declarations; at most one is allowed`);
    }
    for (const mem of m.memories) {
        if (mem.flags > 1) errors.push(`memory flags ${mem.flags} out of range`);
        if (mem.min > MAX_PAGES) {
            errors.push(`initial memory ${mem.min} pages exceeds the ${MAX_PAGES}-page limit`);
        }
        if (mem.max !== null && mem.max < mem.min) errors.push(`memory maximum ${mem.max} below initial ${mem.min}`);
    }

    const byName = new Map(m.exports.map(e => [e.name, e]));
    for (const required of ['app_init', 'app_event']) {
        const exp = byName.get(required);
        if (!exp) {
            errors.push(`missing required export "${required}"`);
            continue;
        }
        if (exp.kind !== 'func') {
            errors.push(`"${required}" is exported as ${exp.kind}, not a function`);
            continue;
        }
        const sig = signature(m, exp.index);
        if (!sig) {
            errors.push(`cannot resolve signature of "${required}"`);
            continue;
        }
        const want = required === 'app_init' ? [] : ['i32', 'i32', 'i32'];
        const got = sig.params.join(',');
        if (got !== want.join(',') || sig.results.length !== 0) {
            errors.push(`"${required}" has signature (${got}) -> (${sig.results.join(',')}); ` +
                        `expected (${want.join(',')}) -> ()`);
        }
    }

    return {file: label, errors};
}

function check(file) {
    return checkBytes(fs.readFileSync(file), path.basename(file));
}

/* A checker that never fails is worthless, so prove each rule fires on a
 * module purpose-built to break it. Modules are generated with wabt instead of
 * committed as binaries, so the fixtures cannot drift out of date. */
const SELFTEST_CASES = [
    {name: 'start section', expect: 'start section', wat: `
        (module (func $boot) (start $boot)
                (memory (export "memory") 1)
                (func (export "app_init"))
                (func (export "app_event") (param i32 i32 i32)))`},
    {name: 'constructor export', expect: 'constructor', wat: `
        (module (memory (export "memory") 1)
                (func (export "app_init"))
                (func (export "app_event") (param i32 i32 i32))
                (func (export "_initialize")))`},
    {name: 'imported memory', expect: 'function imports only', wat: `
        (module (import "platform" "memory" (memory 1))
                (func (export "app_init"))
                (func (export "app_event") (param i32 i32 i32)))`},
    {name: 'initial memory over 4 pages', expect: 'exceeds the 4-page limit', wat: `
        (module (memory (export "memory") 5)
                (func (export "app_init"))
                (func (export "app_event") (param i32 i32 i32)))`},
    {name: 'missing app_event', expect: 'missing required export', wat: `
        (module (memory (export "memory") 1) (func (export "app_init")))`},
    {name: 'wrong app_event signature', expect: 'signature', wat: `
        (module (memory (export "memory") 1)
                (func (export "app_init"))
                (func (export "app_event") (param i32 i32)))`},
];

const SELFTEST_VALID = `
    (module (import "platform" "clear" (func $clear (param i32)))
            (memory (export "memory") 1)
            (func (export "app_init"))
            (func (export "app_event") (param i32 i32 i32)))`;

async function selftest() {
    const {default: wabtFactory} = await import('wabt');
    const wabt = await wabtFactory();
    const compile = (wat) => {
        const mod = wabt.parseWat('selftest.wat', wat);
        mod.resolveNames();
        mod.validate();
        const binary = Buffer.from(mod.toBinary({}).buffer);
        mod.destroy();
        return binary;
    };

    let failed = 0;
    for (const c of SELFTEST_CASES) {
        const {errors} = checkBytes(compile(c.wat), c.name);
        const hit = errors.some(e => e.includes(c.expect));
        if (hit) {
            console.log(`PASS rejects ${c.name}`);
        } else {
            failed++;
            console.log(`FAIL did not reject ${c.name} (errors: ${JSON.stringify(errors)})`);
        }
    }
    const ok = checkBytes(compile(SELFTEST_VALID), 'valid reactor');
    if (ok.errors.length === 0) {
        console.log('PASS accepts a conforming reactor module');
    } else {
        failed++;
        console.log(`FAIL rejected a conforming module: ${ok.errors.join('; ')}`);
    }
    const total = SELFTEST_CASES.length + 1;
    console.log(`RESULT ${total - failed}/${total} passed`);
    return failed === 0 ? 0 : 1;
}

const args = process.argv.slice(2);
if (args.includes('--selftest')) {
    process.exit(await selftest());
}
const files = args.filter(a => !a.startsWith('-'));
if (files.length === 0) {
    console.error('usage: node tools/abi_check.mjs <module.wasm> [...]');
    process.exit(2);
}

let failed = 0;
for (const file of files) {
    const {errors} = check(file);
    const label = path.basename(file);
    if (errors.length === 0) {
        const m = parse(fs.readFileSync(file));
        console.log(`PASS ${label} (${m.exports.length} exports, ${m.imports.length} imports, ` +
                    `${m.memories.length ? m.memories[0].min : 0} initial pages)`);
    } else {
        failed++;
        console.log(`FAIL ${label}`);
        for (const e of errors) console.log(`     - ${e}`);
    }
}
console.log(failed === 0 ? `RESULT ${files.length}/${files.length} passed` : `RESULT ${files.length - failed}/${files.length} passed`);
process.exit(failed === 0 ? 0 : 1);
