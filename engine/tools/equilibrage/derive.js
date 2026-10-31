const C = require('./combat.js');
// Ennemi type (profil de la Herse) mis à l'échelle (L+6)/(lvl+6) : facteur k sur PV et ATQ pour 80 % de victoire à parité (4 membres)
const base = C.ENNEMIS['HERSE'];
const at = (L, k) => ({ ...base, lvl: L, hp: Math.round(base.hp * k * (L + 6) / (base.lvl + 6)), atk: Math.round(base.atk * k * (L + 6) / (base.lvl + 6)) });
for (const L of [1, 3, 6, 10, 20, 30, 45, 60, 80, 95]) {
  let lo = 0.2, hi = 20;
  for (let i = 0; i < 18; i++) { const k = Math.sqrt(lo * hi); C.ENNEMIS.__t = at(L, k); const r = C.winRate([0, 1, 2, 3], L, '__t', 3, 300); if (r.p > 0.8) lo = k; else hi = k; }
  C.ENNEMIS.__t = at(L, lo); const r = C.winRate([0, 1, 2, 3], L, '__t', 3, 400);
  C.ENNEMIS.__s = at(L, lo); const r1 = C.winRate([0], L, '__s', 3, 400);
  console.log(`niv ${String(L).padStart(2)} : k = ${lo.toFixed(2)} · ${(r.t).toFixed(0)} s · Sera seule ${(r1.p * 100).toFixed(0)} %`);
}
