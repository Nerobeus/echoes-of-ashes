const C = require('./combat.js'), R = require('./regles.js');
const base = { ...C.ENNEMIS['COLOSSE'], sorts: [C.W.sorts[13], C.W.sorts[22]], magie: 35 };
for (const [hpX, atkX] of [[1, 1], [3, 0.75], [6, 0.6], [10, 0.5]]) {
  let lo = 0.02, hi = 40, e = { ...base, hp: base.hp * hpX, atk: base.atk * atkX };
  for (let i = 0; i < 20; i++) { const k = Math.sqrt(lo * hi); C.ENNEMIS.__t = R.stats({ ...e, hp: e.hp * k, atk: e.atk * k }, 99); if (C.winRate([0, 1, 2, 3], 99, '__t', 3, 250).p > 0.8) lo = k; else hi = k; }
  C.ENNEMIS.__t = R.stats({ ...e, hp: e.hp * lo, atk: e.atk * lo }, 99);
  console.log(`PV×${hpX} ATQ×${atkX} :`, [99, 95, 90, 85, 80, 70].map(L => { const r = C.winRate([0, 1, 2, 3], L, '__t', 3, 300); return `niv ${L} ${(r.p * 100).toFixed(0)}%${L === 99 ? ' ' + (r.t / 60).toFixed(1) + ' min' : ''}`; }).join(' · '));
}
