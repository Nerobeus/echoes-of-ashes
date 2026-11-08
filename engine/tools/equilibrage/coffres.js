// Coffres de trésor des intérieurs (engine/core/coffres.c, coffreDe de l'émulateur) : ce que rapporte la fouille complète
// d'un quartier de Lucerne ou d'un village, à chaque palier de sa gamme, contre ce que rapportent les combats du même
// palier (crédits, plus les cœurs revendus). Visé : 10 à 20 %.
//
//   node coffres.js [monde-emulateur.v8]   (par défaut : projets/principal/sorties de l'Atelier)
//
// Coffres : un par bâtiment au plus, chance COFFRE_CHANCE du type (sinon COFFRE_HABITAT pour les logements, COFFRE_DEFAUT),
// au moins COFFRE_VILLAGE dans un village, s'il a un meuble coffre (ancre « coffre » de js/interieurs.js). Butin, au palier
// p = celui de l'équipe borné à la gamme du lieu (+ 1 fermé à clé, + 2 scellé) : crédits × 0,6 à 1,2 de credits_of(NIV[p]),
// un soin (deux scellé) parmi les trois plus chers à 1,5 fois ces crédits au plus, un équipement du palier (COF_EQ), du
// palier suivant (COF_RARE). Valeur : crédits, soins au prix de la boutique, équipement à la revente (moitié du prix).
const fs = require('fs'), v8 = require('v8'), path = require('path');
const ATELIER = 'D:/Documents/Stellar Exodus/tileset-generator-v4';
global.TilesetGen = { Scenario: { norm: s => String(s) }, Objets: { list: [] } };
require(ATELIER + '/js/paliers.js');
const P = TilesetGen.Paliers;
// Mêmes constantes que coffreDe de l'émulateur et coffres.c
const COFFRE_CHANCE = { preteur: 1, bunker: 1, archives: 1, comptoir: 0.6, caserne: 0.5, brasier: 0.5, poste_garde: 0.4, temple: 0.3,
  clinique: 0.25, hopital: 0.25, souterrain: 0.5, dortoir: 0.15 };
const HABITAT = new Set(['immeuble', 'tour', 'maison', 'cabane']);
const COFFRE_MODELE = { boxLarge: 1, boxLargeOpen: 1, bookcaseClosedDoors: 2 };   // caisse, armoire ou casier
const COFFRE_HABITAT = 0.012, COFFRE_DEFAUT = 0.12, COFFRE_VILLAGE = 0.6;
const COF_EQ = [0.08, 0.15, 0.25], COF_RARE = [0, 0.04, 0.08];
const coffreChance = b => {
  let c = COFFRE_CHANCE[b.type] !== undefined ? COFFRE_CHANCE[b.type] : HABITAT.has(b.type) ? COFFRE_HABITAT : COFFRE_DEFAUT;
  if (b.kind === 'village') c = Math.max(c, COFFRE_VILLAGE);
  return Math.min(1, c);
};
module.exports = { COFFRE_MODELE, COFFRE_CHANCE, HABITAT, COFFRE_HABITAT, COFFRE_DEFAUT, COFFRE_VILLAGE, coffreChance };
if (require.main !== module) return;

const fichier = process.argv[2] || path.join(ATELIER, 'projets', process.env.PROJET || 'principal', 'sorties', 'monde-emulateur.v8');
// Le monde (220 Mo) : seulement ce qui sert, gardé à côté dans le dossier temporaire tant que le fichier n'a pas changé
const cache = path.join(require('os').tmpdir(), 'echoes-coffres-' + Math.round(fs.statSync(fichier).mtimeMs) + '.json');
let w;
if (fs.existsSync(cache)) w = JSON.parse(fs.readFileSync(cache, 'utf8'));
else {
  const W = v8.deserialize(fs.readFileSync(fichier)).world;
  w = { scenario: W.scenario, buildings: (W.buildings || []).filter(b => b.interieur && b.plan).map(b => ({ type: b.type, kind: b.kind, zone: b.zone, x0: b.x0, z0: b.z0, x1: b.x1, z1: b.z1, acces: b.acces,
    plan: 1, interieur: { ancres: b.interieur.ancres, meubles: (b.interieur.meubles || []).map(m => [0, 0, 0, 0, 0, 0, 0, 0, 0, m[9]]),
      etages: (b.interieur.etages || []).map(e => ({ ancres: e.ancres, meubles: (e.meubles || []).map(m => [0, 0, 0, 0, 0, 0, 0, 0, 0, m[9]]) })) } })) };
  fs.writeFileSync(cache, JSON.stringify(w));
}
const credits_of = L => Math.round(5 * Math.pow(Math.max(1, L), 1.3) + 10);
const S = w.scenario || {}, str = k => (S.strings || [])[k] || '';
const items = (S.items || []).map((o, i) => ({ i, nom: str(o.nom), type: o.type, pv: o.pv, pm: o.pm, prix: o.prix, adulte: !!o.adulte }));
const recs = {}; for (const r of ((S.equip || {}).items || [])) recs[r.item] = r;
const soins = items.filter(it => it.type === 0 && (it.pv > 0 || it.pm > 0) && it.prix > 0 && !recs[it.i]);
// Valeur moyenne des soins d'un coffre de crédits c (cof_soin)
const soinMoyen = c => {
  const ok = soins.filter(s => s.prix <= 1.5 * c).sort((a, b) => b.prix - a.prix).slice(0, 3);
  return ok.length ? ok.reduce((t, s) => t + s.prix, 0) / ok.length : Math.min(...soins.map(s => s.prix));
};
// Revente moyenne d'un équipement du palier p, vendu ou lâché (cof_equip) ; 0 : aucun
const eqMoyen = p => { const l = items.filter(it => recs[it.i] && recs[it.i].palier === p && (recs[it.i].vendu || recs[it.i].butin)); return l.length ? l.reduce((t, it) => t + it.prix / 2, 0) / l.length : 0; };
const npal = Math.min(15, (S.equip && S.equip.paliers || P.NIV.slice(1)).length);
const NIV = P.NIV;
// Valeur attendue d'un coffre au palier p (a : 0 ouvert, 1 fermé, 2 scellé) ; crédits seuls
const valeur = (p, a) => {
  const c = credits_of(NIV[p]), cr = 0.9 * c;
  const v = cr + (a === 2 ? 2 : 1) * soinMoyen(c) + COF_EQ[a] * eqMoyen(p) + (p < npal ? COF_RARE[a] * eqMoyen(p + 1) : 0);
  return { cr, v };
};
// Revenu des combats d'un palier (equipement.js) : 2 + 18 (L − 1) / 58 combats par niveau (courbe du 02/11/2026 ; 15 + 0,1 L avant) ; crédits et cœurs (0,6 × 100), crédits seuls
const revenu = p => { let r = 0, rc = 0; for (let L = NIV[p]; L < (p < 15 ? NIV[p + 1] : 100); L++) { const n = 2 + 18 * (L - 1) / 58; r += n * (credits_of(L) + 60); rc += n * credits_of(L); } return { r, rc }; };
const MODE = { ferme: 1, scelle: 2 };
// Bâtiments qu'on visite qui ont où cacher un coffre
const lieux = new Map();
const villages = [];
for (const b of w.buildings || []) {
  if (!b.interieur || !b.plan) continue;
  const niv = [b.interieur].concat(b.interieur.etages || []);
  // Places (coffreDe de l'émulateur) : meubles coffre, sinon caisses, sinon armoires et casiers
  if (!niv.some(n => (n.ancres || []).some(a => a.t === 'coffre') || (n.meubles || []).some(m => COFFRE_MODELE[m[9]]))) continue;
  const cx = (b.x0 + b.x1) / 2, cz = (b.z0 + b.z1) / 2, a = MODE[(b.acces || {}).mode] || 0;
  let cle;
  if (b.kind === 'village') {   // un village : les bâtiments à moins de 40 cases les uns des autres
    let v = villages.find(q => Math.hypot(q.x - cx, q.z - cz) < 40);
    if (!v) { v = { x: cx, z: cz, k: villages.length }; villages.push(v); }
    cle = 'village ' + v.k;
  } else cle = (b.kind || '?') + ' · ' + (b.zone || '?');
  if (!lieux.has(cle)) lieux.set(cle, { cle, gamme: b.kind === 'village' ? P.GAMMES.village : P.GAMMES[b.zone] || P.GAMMES.ville, bats: [] });
  lieux.get(cle).bats.push({ type: b.type, a, ch: coffreChance(b) });
}
console.log('Coffres : bâtiments à meuble coffre, coffres attendus, valeur d\'une fouille complète (crédits seuls) / revenu des combats du palier');
const lignes = [...lieux.values()].sort((x, y) => y.bats.length - x.bats.length);
const parPalier = {};
for (const L of lignes) {
  const n = L.bats.reduce((t, b) => t + b.ch, 0);
  const types = {}; for (const b of L.bats) types[b.type] = (types[b.type] || 0) + 1;
  console.log(`\n${L.cle} : ${L.bats.length} bâtiments (${Object.entries(types).map(e => e[0] + ' ' + e[1]).join(', ')}) ; ${n.toFixed(1)} coffres attendus ; gamme ${L.gamme.join('-')}`);
  for (let p = L.gamme[0]; p <= L.gamme[1]; p++) {
    let v = 0, cr = 0;
    for (const b of L.bats) { const q = Math.min(npal, p + b.a), x = valeur(q, b.a); v += b.ch * x.v; cr += b.ch * x.cr; }
    const { r, rc } = revenu(p), pc = 100 * v / r, pcc = 100 * v / rc;
    (parPalier[p] = parPalier[p] || []).push([pc, pcc]);
    console.log(`  palier ${String(p).padStart(2)} (niv ${String(NIV[p]).padStart(2)}) : ${Math.round(v).toString().padStart(6)} CR (crédits ${Math.round(cr)}) / ${Math.round(r)} = ${pc.toFixed(1)} % (${pcc.toFixed(1)} % des crédits seuls)`);
  }
}
const vil = lignes.filter(L => L.cle.startsWith('village'));
if (vil.length) {
  const n = vil.map(L => L.bats.reduce((t, b) => t + b.ch, 0));
  console.log(`\nVillages : ${vil.length}, de ${Math.min(...n).toFixed(1)} à ${Math.max(...n).toFixed(1)} coffres attendus (moyenne ${(n.reduce((a, b) => a + b, 0) / n.length).toFixed(1)})`);
}
console.log('\nPar palier (tous les lieux de cette gamme) : part du revenu (crédits et cœurs), moyenne [min, max] ; des crédits seuls');
for (const [p, l] of Object.entries(parPalier)) {
  const a = l.map(x => x[0]), c = l.map(x => x[1]), moy = t => (t.reduce((x, y) => x + y, 0) / t.length).toFixed(1);
  console.log(`  ${String(p).padStart(2)} : ${moy(a)} % [${Math.min(...a).toFixed(1)}, ${Math.max(...a).toFixed(1)}] ; ${moy(c)} % [${Math.min(...c).toFixed(1)}, ${Math.max(...c).toFixed(1)}] sur ${l.length} lieux`);
}
