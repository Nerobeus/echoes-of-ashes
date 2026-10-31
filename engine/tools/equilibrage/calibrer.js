// Étalonnage des fiches : facteur k sur PV et ATQ de chaque ennemi (profil gardé) selon son rang
//   normal : combat d'environ 40 s à parité (4 membres) ; élite (boss de zone, sorts) : 88 % de victoire à parité ;
//   boss (plan.js) : victoire visée à son niveau d'étape, avec l'équipe attendue. Écrit fiches.json.
const C = require('./combat.js'), R = require('./regles.js'), { BOSS, NOUVEAUX, niveauEtape } = require('./plan.js');
const fs = require('fs');
const SORTS = {}; C.W.sorts.forEach((s, i) => { SORTS[['foudre', 'brasier', 'givre', 'sacre', 'meteore', 'neant', 'poison', 'terre', 'eau', 'vent', 'neutre', 'soin'][Math.floor(i / 3)] + (i % 3 + 1)] = s; });
for (const [nom, d] of Object.entries(NOUVEAUX)) C.ENNEMIS[nom] = { ...C.ENNEMIS[d.modele], nom, poids: 1, zones: [], sorts: d.sorts.map(id => SORTS[id]), magie: d.magie, sortsId: d.sorts };
function fit(e, L, team, mesure, cible) {   // mesure décroissante quand k croît
  let lo = 0.02, hi = 40;
  for (let i = 0; i < 22; i++) {
    const k = Math.sqrt(lo * hi); C.ENNEMIS.__t = R.stats({ ...e, hp: e.hp * k, atk: e.atk * k }, L);
    if (mesure(C.winRate(team, L, '__t', 3, 300)) > cible) lo = k; else hi = k;
  }
  return Math.sqrt(lo * hi);
}
const out = {};
for (const e0 of Object.values(C.ENNEMIS)) {
  if (e0.nom.startsWith('__')) continue;
  let rang = 'normal', L = Math.max(5, e0.lvl), team = [0, 1, 2, 3], k, e = e0;
  const B = BOSS[e0.nom];
  if (B) {
    rang = niveauEtape(B[0], B[1]) >= 90 ? 'hardcore' : 'boss'; L = niveauEtape(B[0], B[1]); team = B[2];
    e = { ...e0, hp: e0.hp * (B[4] || 1), atk: e0.atk * (B[5] || 1) };
    k = fit(e, L, team, r => r.p, B[3]);
  } else if (e0.sorts.length) { rang = 'élite'; k = fit(e, L, team, r => r.p, 0.88); }
  else k = fit(e, L, team, r => (r.p < 0.95 ? -1e9 : -r.t), -40);   // perdu : trop fort
  const f = R.stats({ ...e, hp: e.hp * k, atk: e.atk * k }, L);
  C.ENNEMIS.__t = f; const r = C.winRate(team, L, '__t', 3, 600), r2 = C.winRate(team, Math.max(1, L - 3), '__t', 3, 300);
  out[e0.nom] = { rang, lvl: L, hp: f.hp, atk: f.atk, spd: e0.spd, sorts: e0.sortsId || null, magie: e0.magie };
  console.log(`${e0.nom.padEnd(22)} ${rang.padEnd(8)} niv ${String(L).padStart(2)} · PV ${String(f.hp).padStart(7)} ATQ ${String(f.atk).padStart(5)} · ${(r.p * 100).toFixed(0).padStart(3)} % en ${r.t.toFixed(0).padStart(3)} s · à niv ${L - 3} : ${(r2.p * 100).toFixed(0)} %`);
}
fs.writeFileSync(__dirname + '/fiches.json', JSON.stringify(out, null, 1));
