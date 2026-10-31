// Paquet de test : répète les chunks d'un paquet sur un monde de K × K fois plus grand (même données, coordonnées
// relatives au chunk), pour essayer le chargement en cours de jeu. node tools/pavage.js entrée.pak sortie.pak K
const fs = require('fs');
const [, , src, dst, K = '4'] = process.argv;
const b = fs.readFileSync(src), k = +K;
const sections = [];
for (let o = 8; o + 8 <= b.length;) { const l = b.readUInt32LE(o + 4); sections.push({ tag: b.toString('latin1', o, o + 4), data: b.subarray(o + 8, o + 8 + l) }); o += 8 + l; }
const get = t => sections.find(s => s.tag === t).data;
const head = Buffer.from(get('HEAD')), chnk = get('CHNK'), cdat = get('CDAT');
const n = head.readUInt32LE(0), nc = chnk.readUInt32LE(4), NC = nc * k;
head.writeUInt32LE(n * k, 0);
const dir = Buffer.alloc(24 + NC * NC * 12);
chnk.copy(dir, 0, 0, 24); dir.writeUInt32LE(NC, 4);
for (let cz = 0; cz < NC; cz++) for (let cx = 0; cx < NC; cx++) chnk.copy(dir, 24 + (cz * NC + cx) * 12, 24 + ((cz % nc) * nc + cx % nc) * 12, 24 + ((cz % nc) * nc + cx % nc) * 12 + 12);
const parts = [b.subarray(0, 8)];
let len = 8, cdatAt = 0;
const add = (tag, data) => { const h = Buffer.alloc(8); h.write(tag, 0, 'latin1'); h.writeUInt32LE(data.length, 4); const pad = Buffer.alloc((4 - data.length % 4) % 4); parts.push(h, data, pad); len += 8 + data.length + pad.length; };
for (const s of sections) {
  if (s.tag === 'HEAD') add('HEAD', head);
  else if (s.tag === 'CHNK') { add('CHNK', dir); cdatAt = len + 8; dir.writeUInt32LE(cdatAt, 20); }
  else add(s.tag, s.data);
}
fs.writeFileSync(dst, Buffer.concat(parts));
console.log(`${dst} : monde ${n * k} × ${n * k}, ${NC} × ${NC} chunks`);
