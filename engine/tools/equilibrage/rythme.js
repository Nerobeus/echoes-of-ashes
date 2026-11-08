// Rythme sur la trame du document : heures par acte → combats (part du temps en combat, ~45 s par combat), niveau de
// l'équipe à la fin de chaque acte, avec la règle des rencontres (min(N, L + 1) − 0..2) et l'XP des boss (× 4)
const R = require('./regles.js');
const A = +(process.env.A || 15), B = +(process.env.B || 0.1), exp_next = L => Math.round((A + B * L) * R.expOf(L));   // exp_next de battle.c par défaut
const ACTES = [['Prologue', 2, 5], ['Acte I', 8, 18], ['Acte II', 9, 32], ['Acte III', 8, 45], ['Acte IV', 7, 54], ['Acte V', 6, 60]];
function partie(parHeure, annexes, farm) {
  let L = 1, xp = 0, t = 0; const out = [];
  const gain = n => { xp += n; while (L < 100 && xp >= exp_next(L)) { xp -= exp_next(L); L++; } };
  for (const [nom, h, N] of ACTES) {
    const n = Math.round(h * (1 + (annexes ? 0.55 : 0)) * parHeure);
    for (let i = 0; i < n; i++) { const lv = Math.max(1, Math.min(N, L + 1) - Math.floor(Math.random() * 3)); gain(R.expOf(lv)); }
    for (let b = 0; b < 3; b++) gain(R.expOf(N) * 4);   // 3 boss par acte
    let f = 0; while (farm && L < N) { gain(R.expOf(Math.min(N, L + 1) - 1)); f++; }
    out.push(`${nom} ${L}${f ? ` (+${f} combats de farm)` : ''}`); t += f;
  }
  return out.join(' · ') + (farm ? ` · farm ${t} combats ≈ ${(t * 50 / 3600).toFixed(1)} h` : '');
}
for (const ph of [22, 25, 28]) {
  console.log(`${ph} combats/h, histoire        : ${partie(ph, false, false)}`);
  console.log(`${ph} combats/h, histoire + farm : ${partie(ph, false, true)}`);
  console.log(`${ph} combats/h, avec annexes    : ${partie(ph, true, false)}`);
}
let s = 0; for (let L = 60; L < 90; L++) s += A + B * L; console.log('60 → 90 à parité :', s.toFixed(0), 'combats ≈', (s * 50 / 3600).toFixed(0), 'h ; 90 → 100 :', (() => { let q = 0; for (let L = 90; L < 100; L++) q += A + B * L; return q.toFixed(0) + ' combats ≈ ' + (q * 55 / 3600).toFixed(0) + ' h'; })());
