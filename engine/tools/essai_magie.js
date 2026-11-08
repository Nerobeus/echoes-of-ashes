/*
 * Essai de la magie (03/11/2026 : progression des sorts par job, cristaux du Chœur) sans régénérer le monde : à une copie
 * d'un paquet (monde.pak entier ou paquet léger monde_esp32.pak) dont la section JOBS est encore en version 2, ajoute la
 * version 3 : pour chaque job, le niveau où il enseigne chaque sort (js/equipement.js de l'Atelier : NIV_JOBS, par le nom
 * du job), et les points d'apprentissage des objets nommés en argument (des cristaux pour l'essai : leur sort s'apprend).
 * L'ancienne JOBS garde sa place sous le nom « JOBx » (le moteur ne la lit plus), la nouvelle va juste avant END : les
 * décalages absolus de CHNK et CRTD ne bougent pas. Format dans engine/core/pak.h.
 *
 *   node tools/essai_magie.js source.pak sortie.pak ["ÉPAULIÈRE DE CRISTAL=8" …]
 *   rendu_test sortie.pak x.bmp "finscript,recrues:1022,niveau:40,magie"
 *   ECHOES_SD=data/monde.pak rendu_test sortie_esp32.pak x.bmp "…"   (paquet léger)
 */
const fs = require('fs');
const [src, dst, ...pts] = process.argv.slice(2);
if (!src || !dst) { console.error('usage : node tools/essai_magie.js source.pak sortie.pak ["NOM=points" …]'); process.exit(1); }
const ATELIER = 'D:/Documents/Stellar Exodus/tileset-generator-v4';
global.TilesetGen = { Scenario: { MEMBERS: [], norm: s => String(s).trim().toUpperCase().normalize('NFD').replace(/[\u0300-\u036f]/g, '') } };
require(ATELIER + '/js/equipement.js');
const EQ = TilesetGen.Equipement, norm = TilesetGen.Scenario.norm;

const full = fs.readFileSync(src);
const S = [];
for (let o = 8; o + 8 <= full.length;) {
    const tag = full.toString('latin1', o, o + 4), len = full.readUInt32LE(o + 4);
    S.push({ tag, off: o, data: o + 8, len });
    if (tag === 'END ') break;
    o += 8 + ((len + 3) & ~3);
}
const get = t => S.find(s => s.tag === t);
const J = get('JOBS'), fin = get('END ');
if (!J || !fin) { console.error('paquet sans JOBS ou sans END'); process.exit(1); }
const b = Buffer.from(full.subarray(J.data, J.data + J.len));
const nt = b[0], nj = b[1], nm = b[2], ver = b[3];
if (ver !== 2) { console.error('JOBS version ' + ver + ' : il faut la version 2'); process.exit(1); }
const ts = 24, oj = 4 + nt * ts, om = oj + nj * 52, oi = om + nm * 8, ni = b.readUInt32LE(oi), os = oi + 4 + ni * 12, ns = b[os], om2 = os + 4 + ns * 24;
const cstr = (o, n) => { let e = o; while (e < o + n && b[e]) e++; return b.toString('utf8', o, e); };
const jobs = [], sorts = [];
for (let k = 0; k < nj; k++) jobs.push(cstr(oj + k * 52, 16));
for (let k = 0; k < ns; k++) sorts.push({ nom: cstr(os + 4 + k * 24, 16), niv: b[os + 4 + k * 24 + 22] });
// Points des cristaux d'essai : par rang d'objet (« 205=8 ») ou par son nom dans la liste des objets du projet (même ordre
// que la table des objets du paquet exporté)
const objets = JSON.parse(fs.readFileSync(ATELIER + '/projets/principal/bases/AtelierEchoesV4/kv/objets.json', 'utf8'))[1];
const pointsDe = {};
for (const a of pts) {
    const [n, p] = a.split('='); const k = /^\d+$/.test(n) ? +n : objets.findIndex(o => norm(o.nom) === norm(n));
    if (k < 0) { console.error('objet inconnu : ' + n); process.exit(1); }
    pointsDe[k] = Math.max(0, Math.min(15, +p || 0));
}
let vus = 0;
for (let e = 0; e < ni; e++) { const it = b[oi + 4 + e * 12]; if (pointsDe[it] !== undefined) { b[om2 + (nj + e) * 12 + 10] = pointsDe[it]; vus++; } }
// Table des niveaux : NIV_JOBS de l'Atelier (id du sort -> niveau) par le nom du job ; un sort du job absent : le sien
const ID = []; ['foudre', 'brasier', 'givre', 'sacre', 'meteore', 'neant', 'poison', 'terre', 'eau', 'vent', 'neutre', 'soin'].forEach(el => { for (let n = 1; n <= 3; n++) ID.push(el + n); });
ID.push('rappel');
const table = Buffer.alloc(((nj * ns) + 3) & ~3);
for (let k = 0; k < nj; k++) {
    const d = EQ.DEFAUT_JOBS.find(j => norm(j.nom) === norm(jobs[k])), mq = BigInt(b.readUInt32LE(om2 + k * 12)) | BigInt(b.readUInt32LE(om2 + k * 12 + 4)) << 32n;
    let masque = mq;
    for (let x = 0; x < ns; x++) {
        const id = ID[x], nv = d && d.niv && id && d.niv[id] !== undefined ? d.niv[id] : null;
        if (nv !== null) masque |= 1n << BigInt(x);   // le job l'enseigne désormais (NIV_JOBS)
        table[k * ns + x] = nv !== null ? nv : sorts[x].niv;
    }
    b.writeUInt32LE(Number(masque & 0xFFFFFFFFn), om2 + k * 12); b.writeUInt32LE(Number(masque >> 32n), om2 + k * 12 + 4);
}
b[3] = 3;
const neuf = Buffer.concat([b, table]);
const sec = Buffer.alloc(8 + ((neuf.length + 3) & ~3));
sec.write('JOBS', 0, 'latin1'); sec.writeUInt32LE(neuf.length, 4); neuf.copy(sec, 8);
const out = Buffer.concat([full.subarray(0, fin.off), sec, full.subarray(fin.off)]);
out.write('JOBx', J.off, 'latin1');
fs.writeFileSync(dst, out);
console.log(dst + ' : JOBS version 3 (' + nj + ' jobs × ' + ns + ' sorts), ' + vus + ' cristal(aux) d\'essai');
