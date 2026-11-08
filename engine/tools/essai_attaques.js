/*
 * Essai des attaques des ennemis sans régénérer le monde : ajoute à une copie d'un paquet (monde.pak entier ou paquet léger
 * monde_esp32.pak) la section ENAT faite par js/attaques.js de l'Atelier sur la bibliothèque du projet, comme l'export de
 * l'émulateur (Personnages.ennemiExport, section ENAT ; format dans engine/core/pak.h). Ennemis retrouvés par leur nom
 * dans PERS, dans l'ordre de ENNE. La section va juste avant END (les décalages absolus de CHNK et CRTD ne bougent pas) ;
 * une ENAT déjà là est remplacée.
 *
 *   node tools/essai_attaques.js source.pak sortie.pak [projet]
 *   rendu_test sortie.pak x.bmp "repertoire"                        (paquet entier)
 *   ECHOES_SD=data/monde.pak rendu_test sortie_esp32.pak x.bmp "…"   (paquet léger)
 */
const fs = require('fs');

const [src, dst, projet = 'principal'] = process.argv.slice(2);
if (!src || !dst) { console.error('usage : node tools/essai_attaques.js source.pak sortie.pak [projet]'); process.exit(1); }
const ATELIER = 'D:/Documents/Stellar Exodus/tileset-generator-v4';
global.TilesetGen = global.TilesetGen || {};
require(ATELIER + '/js/attaques.js');
const A = TilesetGen.Attaques;
const LIB = JSON.parse(fs.readFileSync(ATELIER + '/projets/' + projet + '/bases/AtelierEchoesV4/kv/personnages.json', 'utf8'))[1];

const full = fs.readFileSync(src);
const S = [];
for (let o = 8; o + 8 <= full.length;) {
    const tag = full.toString('latin1', o, o + 4), len = full.readUInt32LE(o + 4);
    S.push({ tag, off: o, data: o + 8, len, fin: o + 8 + ((len + 3) & ~3) });
    if (tag === 'END ') break;
    o += 8 + ((len + 3) & ~3);
}
const get = t => S.find(s => s.tag === t);
const cstr = (b, o) => { let e = o; while (b[e]) e++; return [b.toString('utf8', o, e), e + 1]; };

// Noms des personnages (PERS)
const noms = [];
{
    const P = get('PERS'); let q = P.data + 4;
    for (let i = 0, n = full.readUInt32LE(P.data); i < n; i++) {
        const fl = full[q + 1], nl = full[q + 2]; q += 4;
        if (fl & 4) q += 256;
        let nom; [nom, q] = cstr(full, q); noms.push(nom);
        for (let k = 0; k < nl; k++) q = cstr(full, q)[1];
    }
}
// ENAT, dans l'ordre de ENNE
const E = get('ENNE'), ne = full.readUInt32LE(E.data), REC = 4 + 4 * 32;
const enat = Buffer.alloc(4 + ne * REC);
enat.writeUInt32LE(ne, 0);
let sans = [];
for (let i = 0; i < ne; i++) {
    const nom = noms[full[E.data + 4 + i * 16]], p = LIB.find(x => x.nom === nom && x.role === 'ennemi' && !x.palette && !x.brouillon);
    if (!p) { sans.push(nom); continue; }
    const at = A.exporter(p, LIB).filter(a => a.fx >= 0 && a.poids > 0).slice(0, 4), o = 4 + i * REC;
    enat[o] = at.length;
    at.forEach((a, k) => {
        const q = o + 4 + k * 32, e = Buffer.from(a.nom, 'utf8');
        let l = Math.min(19, e.length); while (l > 0 && (e[l] & 0xC0) === 0x80) l--;
        e.copy(enat, q, 0, l);
        enat[q + 20] = a.fx; enat[q + 21] = a.tous ? 1 : 0; enat[q + 22] = a.poids; enat[q + 23] = a.sig ? 1 : 0;
        enat.writeUInt16LE(Math.max(1, Math.min(65535, Math.round(a.puissance * 1000))), q + 24);
        const c = /^#?([0-9a-f]{6})$/i.exec(a.couleur || ''), v = c ? parseInt(c[1], 16) : 0;
        enat[q + 26] = v >> 16 & 255; enat[q + 27] = v >> 8 & 255; enat[q + 28] = v & 255;
    });
}
const sec = (tag, data) => { const pad = (4 - data.length % 4) % 4, h = Buffer.alloc(8); h.write(tag, 0, 'latin1'); h.writeUInt32LE(data.length + pad, 4); return Buffer.concat([h, data, Buffer.alloc(pad)]); };
const fin = get('END '), vieille = get('ENAT');
const parts = [];
if (vieille) parts.push(full.subarray(0, vieille.off), full.subarray(vieille.fin, fin.off));
else parts.push(full.subarray(0, fin.off));
parts.push(sec('ENAT', enat), full.subarray(fin.off));
fs.writeFileSync(dst, Buffer.concat(parts));
console.log(`${dst} : ENAT de ${ne} ennemis (${enat.length} octets)${sans.length ? ' ; sans fiche dans la bibliothèque : ' + sans.join(', ') : ''}`);
