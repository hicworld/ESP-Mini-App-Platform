import wabtFactory from 'wabt';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const wabt = await wabtFactory();
fs.mkdirSync(path.join(root, 'dist'), {recursive:true});
for (const name of ['hello', 'touch']) {
  const wat = path.join(root, 'apps', name, 'main.wat');
  const mod = wabt.parseWat(wat, fs.readFileSync(wat, 'utf8'));
  mod.resolveNames(); mod.validate();
  fs.writeFileSync(path.join(root, 'dist', `${name}.wasm`), Buffer.from(mod.toBinary({}).buffer));
  mod.destroy();
  console.log(`Built ${name}.wasm`);
}
