// Lecteur des sections combat de monde.pak (format engine/core/pak.h)
const fs = require('fs');
function sections(file) {
  const fd = fs.openSync(file, 'r'), hdr = Buffer.alloc(8), S = {};
  let off = 8;
  for (;;) {
    if (fs.readSync(fd, hdr, 0, 8, off) < 8) break;
    const tag = hdr.toString('latin1', 0, 4), len = hdr.readUInt32LE(4);
    if (tag === 'CDAT') break;
    if (['RENC','ENNE','ENSO','JOBS','SCEN','PERS','PVIS','ZONE','HEAD','CART','PASS','PORT','PCAR','COMM','TELE'].includes(tag)) { const b = Buffer.alloc(len); fs.readSync(fd, b, 0, len, off + 8); S[tag] = b; }
    off += 8 + ((len + 3) & ~3);
  }
  return S;
}
function cstr(b, o, n) { let e = o; while (e < o + n && b[e]) e++; return b.toString('utf8', o, e); }
function parse(file) {
  const S = sections(file), W = {};
  // PERS : noms
  { const b = S.PERS; let n = b.readUInt32LE(0), o = 4; W.persos = [];
    for (let i = 0; i < n; i++) { const fl = b[o + 1], nr = b[o + 2]; o += 4; if (fl & 4) o += 256;
      const strs = []; for (let k = 0; k <= nr; k++) { let e = o; while (b[e]) e++; strs.push(b.toString('utf8', o, e)); o = e + 1; }
      W.persos.push(strs[0]); } }
  W.renc = S.RENC ? S.RENC.readUInt16LE(0) : 0;
  { const b = S.ENNE, n = b.readUInt32LE(0); W.enemies = [];
    for (let i = 0; i < n; i++) { const o = 4 + i * 16;
      const pr = b[o] | (b[o + 3] >> 7) << 8; W.enemies.push({ nom: W.persos[pr], perso: pr, lvl: b[o + 1], poids: b[o + 2], coeur: b[o + 3] & 1, hp: b.readUInt32LE(o + 4), atk: b.readUInt16LE(o + 8), spd: b.readUInt16LE(o + 10), zones: b.readUInt32LE(o + 12) }); } }
  if (S.ENSO) { const b = S.ENSO; for (let i = 0; i < W.enemies.length; i++) { const o = 4 + i * 12; W.enemies[i].sorts = BigInt(b.readUInt32LE(o)) | BigInt(b.readUInt32LE(o + 4)) << 32n; W.enemies[i].magie = b[o + 8]; } }
  { const b = S.JOBS, nt = b[0], nj = b[1], nm = b[2], ver = b[3]; let o = 4; W.types = []; W.jobs = []; W.membres = []; W.eq = [];
    for (let t = 0; t < nt; t++) { W.types.push({ slot: b[o], effet: b[o + 1], nom: cstr(b, o + 4, 16) }); o += ver ? 24 : 20; }
    for (let j = 0; j < nj; j++) { W.jobs.push({ nom: cstr(b, o, 16), pv: b.readInt8(o + 16), pm: b.readInt8(o + 17), atq: b.readInt8(o + 18), vit: b.readInt8(o + 19), aff: [...b.slice(o + 20, o + 52)] }); o += 52; }
    for (let m = 0; m < nm; m++) { W.membres.push({ job: b[o], jobs: b.readUInt16LE(o + 2), eq: [b[o + 4], b[o + 5], b[o + 6]] }); o += 8; }
    const ne = b.readUInt32LE(o); o += 4;
    for (let e = 0; e < ne; e++) { W.eq.push({ item: b[o], cat: b[o + 1], vit: b.readInt8(o + 3), atq: b.readInt16LE(o + 4), def: b.readInt16LE(o + 6), pv: b.readInt16LE(o + 8), pm: b.readInt16LE(o + 10) }); o += 12; }
    W.sorts = [];
    if (ver >= 2) { const ns = b[o]; o += 4;
      for (let s = 0; s < ns; s++) { W.sorts.push({ nom: cstr(b, o, 16), fx: b[o + 16], soin: b[o + 17], pm: b.readUInt16LE(o + 18), mult: b.readUInt16LE(o + 20) / 100, niv: b[o + 22] }); o += 24; }
      for (let j = 0; j < nj; j++) { W.jobs[j].sorts = BigInt(b.readUInt32LE(o)) | BigInt(b.readUInt32LE(o + 4)) << 32n; W.jobs[j].mag = b.readInt8(o + 8); o += 12; }
      for (let e = 0; e < ne; e++) { W.eq[e].sorts = BigInt(b.readUInt32LE(o)) | BigInt(b.readUInt32LE(o + 4)) << 32n; W.eq[e].mag = b.readInt16LE(o + 8); W.eq[e].pts = b[o + 10]; o += 12; }
      // version 3 (03/11/2026) : pour chaque job, le niveau où il enseigne chaque sort (nivs : index du sort -> niveau)
      if (ver >= 3) for (let j = 0; j < nj; j++) { W.jobs[j].nivs = {}; for (let x = 0; x < ns; x++) if ((W.jobs[j].sorts >> BigInt(x)) & 1n) W.jobs[j].nivs[x] = b[o + x]; o += ns; } }
  }
  if (S.ZONE) { const b = S.ZONE, M = b.readUInt32LE(0); W.zoneN = M; W.zone = b.slice(4, 4 + M * M); }
  // Cartes secondaires (CART : nom, côté), passages (PASS), bâtiments qu'on visite (PORT : centre, commerce, nom ; PCAR :
  // carte), commerces (COMM : soins, verre, objets vendus) : les lieux où l'équipe se soigne et achète (audit de la santé)
  W.cartes = []; if (S.CART) { const b = S.CART, n = b.readUInt32LE(0); for (let i = 0; i < n; i++) { const o = 4 + i * 80; W.cartes.push({ nom: cstr(b, o, 24), N: b.readUInt32LE(o + 24) }); } }
  W.passages = []; if (S.PASS) { const b = S.PASS, n = b.readUInt32LE(0); for (let i = 0; i < n; i++) { const o = 4 + i * 28;
    W.passages.push({ carte: b[o], vers: b[o + 1], sorte: b[o + 2], x: b.readFloatLE(o + 4), z: b.readFloatLE(o + 8), r: b.readFloatLE(o + 12), ax: b.readFloatLE(o + 16), az: b.readFloatLE(o + 20), cond: b.readUInt16LE(o + 24) }); } }
  W.comms = []; if (S.COMM) { const b = S.COMM, n = b.readUInt32LE(0); let o = 4; for (let i = 0; i < n; i++) { const nv = b[o + 7];
    W.comms.push({ qui: b.readUInt16LE(o), soins: b.readUInt16LE(o + 2), verre: b.readUInt16LE(o + 4), rachete: b[o + 6], vend: [...b.slice(o + 8, o + 8 + nv)] }); o += 8 + nv; } }
  W.batiments = []; if (S.PORT) { const b = S.PORT, nb = b.readUInt32LE(0), np = b.readUInt32LE(4); const noms = 8 + nb * 16 + np * 20;
    for (let i = 0; i < nb; i++) { const o = 8 + i * 16; W.batiments.push({ x: b.readFloatLE(o), z: b.readFloatLE(o + 4), acces: b[o + 8] & 3, comm: b[o + 13] - 1, nom: cstr(b, noms + i * 24, 24), carte: S.PCAR ? S.PCAR[4 + i] : 0 }); } }
  W.tele = []; if (S.TELE) { const b = S.TELE, n = b.readUInt32LE(0); for (let i = 0; i < n; i++) { const o = 4 + i * 36; W.tele.push({ nom: cstr(b, o, 24), sorte: b[o + 24], carte: b[o + 25], x: b.readFloatLE(o + 28), z: b.readFloatLE(o + 32) }); } }
  W.S = S;
  return W;
}
module.exports = { parse, sections };
if (require.main === module) {
  const W = parse(process.argv[2]);
  delete W.S; const z = W.zone; delete W.zone;
  console.log(JSON.stringify(W, (k, v) => typeof v === 'bigint' ? v.toString(2) : v, 0).replace(/\},\{/g, '},\n{'));
  const hist = {}; for (const t of z) hist[t] = (hist[t] || 0) + 1; console.log('zones', W.zoneN, hist);
}
