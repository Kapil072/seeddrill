// Packs the Vite dashboard build into firmware/data/www as gzipped flat files.
// LittleFS doesn't handle subdirectories well at small partition sizes, so
// assets/foo.js becomes assets_foo.js.gz, and index.html is patched to match.
//
// Usage: node firmware/tools/pack_www.mjs
import { createGzip } from 'zlib';
import { createReadStream, createWriteStream, mkdirSync, writeFileSync, readFileSync, rmSync } from 'fs';
import { join, basename, relative, dirname } from 'path';
import { fileURLToPath } from 'url';
import { readdirSync, statSync } from 'fs';
import { pipeline } from 'stream/promises';

const __dir = dirname(fileURLToPath(import.meta.url));
const DIST  = join(__dir, '../../dashboard/dist');
const OUT   = join(__dir, '../data/www');

// Clean output dir
try { rmSync(OUT, { recursive: true }); } catch {}
mkdirSync(OUT, { recursive: true });

function walk(dir) {
  const results = [];
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) results.push(...walk(p));
    else results.push(p);
  }
  return results;
}

const files = walk(DIST);

// Map: original relative path → flat name
const nameMap = {};
for (const f of files) {
  const rel = relative(DIST, f).replace(/\\/g, '/');
  const flat = rel.replace(/\//g, '_');
  nameMap[rel] = flat;
}

// Patch index.html: replace asset paths like assets/foo.js → assets_foo.js
let html = readFileSync(join(DIST, 'index.html'), 'utf8');
for (const [rel, flat] of Object.entries(nameMap)) {
  if (rel === 'index.html') continue;
  // Replace both ./assets/foo and assets/foo patterns
  html = html.replaceAll(`./${rel}`, flat).replaceAll(rel, flat);
}

// Also patch sw.js which lists the asset files
let sw = readFileSync(join(DIST, 'sw.js'), 'utf8');
for (const [rel, flat] of Object.entries(nameMap)) {
  if (rel === 'index.html' || rel === 'sw.js') continue;
  sw = sw.replaceAll(rel, flat).replaceAll(`./${rel}`, flat);
}

// Write patched files
writeFileSync(join(DIST, 'index.html'), html);
writeFileSync(join(DIST, 'sw.js'), sw);

// Gzip everything
let total = 0;
for (const f of walk(DIST)) {
  const rel  = relative(DIST, f).replace(/\\/g, '/');
  const flat = nameMap[rel] || rel.replace(/\//g, '_');
  const out  = join(OUT, flat + '.gz');
  await pipeline(createReadStream(f), createGzip({ level: 9 }), createWriteStream(out));
  const size = statSync(out).size;
  total += size;
  console.log(`  ${flat.padEnd(50)} ${size} B`);
}
console.log(`\nTotal: ${(total/1024).toFixed(1)} KB → firmware/data/www/`);
