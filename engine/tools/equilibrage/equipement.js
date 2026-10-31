// Équipement attendu et son effet : paliers de js/paliers.js de l'Atelier (noms, statistiques, prix), pondérés par
// l'affinité du job de chaque membre (AFF_F de battle.c). node equipement.js : victoire des combats de l'histoire à leur
// niveau sans équipement, avec celui de la boutique du moment (paliers jusqu'au niveau + AVANCE) et avec un palier de
// retard ; durée des combats normaux ; prix d'un équipement complet contre les crédits d'un palier.
const C = require('./combat.js'), { BOSS, niveauEtape } = require('./plan.js');
global.TilesetGen = { Scenario: { norm: s => String(s).trim().toUpperCase().normalize('NFD').replace(/[\u0300-\u036f]/g, '') }, Objets: { list: [] } };
require('D:/Documents/Stellar Exodus/tileset-generator-v4/js/paliers.js');
const P = TilesetGen.Paliers;
const CATS = ['lame', 'fouet', 'feu', 'canon', 'arc', 'baton', 'poings', 'legere', 'lourde', 'tissu', 'accessoire'];
const SLOT = c => c === 'legere' || c === 'lourde' || c === 'tissu' ? 1 : c === 'accessoire' ? 2 : 0;
const AFF_F = [0, 0.7, 0.85, 1, 1.2];
// Objets de la boutique (palier ≤ VENDU_MAX), statistiques de la table
const OBJETS = P.TABLE.filter(e => e[2] <= P.VENDU_MAX).map(e => ({ nom: e[1], palier: e[2], ...P.stats(e[3], P.NIV[e[2]], P.QUALITE[e[4] ? 1 : 0]) }));
// Score d'un objet pour son emplacement : arme l'ATQ ; armure et accessoire, DÉF, PV, ATQ et VIT
const score = (o, f) => f * (SLOT(o.cat) === 0 ? o.atq + 0.3 * o.mag : o.def * 4 + o.pv / 10 + o.atq + o.vit * 25);
// Meilleur équipement du membre k parmi les paliers dont le niveau ne dépasse pas Lmax
function equipe(k, Lmax) {
  const j = C.W.jobs[C.W.membres[k].job], out = { atq: 0, def: 0, pv: 0, pm: 0, vit: 0 };
  for (let s = 0; s < 3; s++) {
    let best = null, bf = 0;
    for (const o of OBJETS) {
      if (SLOT(o.cat) !== s || P.NIV[o.palier] > Lmax) continue;
      const f = AFF_F[Math.min(4, j.aff[CATS.indexOf(o.cat)] | 0)];
      if (f && (!best || score(o, f) > score(best, bf))) { best = o; bf = f; }
    }
    if (best) for (const q of Object.keys(out)) out[q] += (best[q] || 0) * bf;
  }
  return out;
}
const gear = Lmax => ({ stats: [0, 1, 2, 3].map(k => equipe(k, Lmax)) });
module.exports = { equipe, gear, OBJETS };

if (require.main === module) {
  console.log('combat (équipe) | niveau | sans | boutique (niv + ' + P.AVANCE + ') | un palier de retard (niv − 7)');
  for (const [nom, B] of Object.entries(BOSS)) {
    const L = niveauEtape(B[0], B[1]); if (!L || !C.ENNEMIS[nom]) continue;
    const w0 = C.winRate(B[2], L, nom, 3, 300), w1 = C.winRate(B[2], L, nom, 3, 300, 'bon', gear(L + P.AVANCE)), w2 = C.winRate(B[2], L, nom, 3, 300, 'bon', gear(L - 7));
    console.log(`${nom.padEnd(20)} ${String(B[2].length).padStart(1)} | ${String(L).padStart(2)} | ${(w0.p * 100).toFixed(0).padStart(3)} % ${w0.t.toFixed(0).padStart(3)} s | ${(w1.p * 100).toFixed(0).padStart(3)} % ${w1.t.toFixed(0).padStart(3)} s | ${(w2.p * 100).toFixed(0).padStart(3)} % ${w2.t.toFixed(0).padStart(3)} s`);
  }
  console.log('\nnormaux à parité (4 membres) : durée sans / avec la boutique');
  for (const nom of ['GLANEUR', 'HERSE', 'ÉCHASSIER', 'MAMMOUTH DE FER', 'GRIFFON D\'ACIER']) {
    const e = C.ENNEMIS[nom]; if (!e) continue;
    for (const L of [10, 30, 60, 90]) {
      const w0 = C.winRate([0, 1, 2, 3], L, nom, 3, 200), w1 = C.winRate([0, 1, 2, 3], L, nom, 3, 200, 'bon', gear(L + P.AVANCE));
      console.log(`${nom.padEnd(16)} niv ${String(L).padStart(2)} : ${w0.t.toFixed(0)} s (${(w0.p * 100).toFixed(0)} %) / ${w1.t.toFixed(0)} s (${(w1.p * 100).toFixed(0)} %)`);
    }
  }
  console.log('\npalier | niveau | équipement complet de l\'équipe (12 pièces) | crédits des combats du palier (+ cœurs)');
  for (let p = 1; p <= P.VENDU_MAX; p++) {
    const L0 = P.NIV[p], L1 = p < P.MAX_PALIER ? P.NIV[p + 1] : 100;
    let cr = 0, coeurs = 0; for (let L = L0; L < L1; L++) { const n = 15 + 0.1 * L; cr += n * C.credits_of(L); coeurs += n * 0.6 * 100; }
    const prix = [0, 1, 2, 3].reduce((s, k) => s + [0, 1, 2].reduce((t, sl) => {
      const j = C.W.jobs[C.W.membres[k].job];
      const l = P.TABLE.filter(e => e[2] === p && SLOT(P.SORTES[e[3]].cat) === sl && j.aff[CATS.indexOf(P.SORTES[e[3]].cat)] > 0);
      return t + (l.length ? P.prix(l[0][3], L0, 1, l[0][6]) : 0);
    }, 0), 0);
    console.log(`${String(p).padStart(2)} ${P.NOMS[p].padEnd(10)} | ${String(L0).padStart(2)} | ${String(prix).padStart(7)} CR | ${Math.round(cr).toString().padStart(8)} CR (+ ${Math.round(coeurs)})`);
  }
}
