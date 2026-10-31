const C = require('./combat.js');
const cum = L => { let s = 0; for (let l = 1; l < L; l++) s += C.exp_next(l); return s; };
console.log('XP cumulée : niv 6', cum(6), '· 9', cum(9), '· 13', cum(13), '· 18', cum(18), '· 20', cum(20), '· 40', cum(40));
console.log('\nVictoire % selon le niveau (3 potions, bon joueur)');
const B = [['TRAQUEUSE', [0, 1, 2]], ['GARDIENNE', [0, 1, 2, 3]], ['GRAND INQUISITEUR', [0, 1, 2, 3]], ['MALDEN', [0, 1, 2, 3]]];
console.log('niveau ' + B.map(b => b[0]).join(' | '));
for (const L of [4, 6, 8, 10, 12, 14, 16, 18, 20, 25]) console.log(String(L).padEnd(6), B.map(([n, t]) => { const r = C.winRate(t, L, n, 3, 400); return (r.p * 100).toFixed(0).padStart(3) + '% ' + (r.t / 60).toFixed(1) + ' min'; }).join(' | '));
// Zones : tables de rencontres de la bibliothèque, part des combats perdus selon le niveau et l'équipe
const LANDS = ['rive', 'plaine', 'champs', 'foret', 'urbain', 'gravats', 'industriel', 'cendre'];
console.log('\nRencontres : % de défaites par zone (équipe du moment)');
const stages = [['Sera seule niv 3', [0], 3], ['Sera+Varn niv 6', [0, 1], 6], ['3 membres niv 8', [0, 1, 2], 8], ['4 membres niv 10', [0, 1, 2, 3], 10], ['4 membres niv 15', [0, 1, 2, 3], 15]];
for (const z of LANDS) {
  const tb = Object.values(C.ENNEMIS).filter(e => e.zones.includes(z)), tot = tb.reduce((a, e) => a + e.poids, 0);
  const row = stages.map(([lab, t, L]) => { let p = 0; for (const e of tb) p += e.poids / tot * (1 - C.winRate(t, L, e.nom, 3, 150).p); return (p * 100).toFixed(0) + '%'; });
  console.log(z.padEnd(11), row.join(' | '), ' · ', tb.map(e => `${e.nom}(${e.lvl},p${e.poids})`).join(' '));
}
console.log('\nPar ennemi des zones : niveau pour 90 % (équipe de 4 / de 1), durée à ce niveau, XP');
for (const e of Object.values(C.ENNEMIS).filter(e => e.zones.length).sort((a, b) => a.lvl - b.lvl)) {
  const L4 = C.levelFor([0, 1, 2, 3], e.nom, 3, 0.9), L1 = C.levelFor([0], e.nom, 3, 0.9);
  const r = C.winRate([0, 1, 2, 3], typeof L4 === 'number' ? L4 : 99, e.nom, 3, 200);
  console.log(`${e.nom.padEnd(22)} niv ${String(e.lvl).padStart(2)} · 90% à 4 : niv ${L4} · seule : niv ${L1} · ${r.t.toFixed(0)} s · ${C.exp_of(e.lvl)} XP · ${e.zones.join(',')}${e.sorts.length ? ' · sorts' : ''}`);
}
