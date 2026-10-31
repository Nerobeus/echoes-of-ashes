const { run } = require('./progression.js');
const stat = (o, n = 60) => { const R = []; for (let i = 0; i < n; i++) R.push(run(o)); R.sort((a, b) => a.t - b.t);
  const m = f => R.reduce((a, r) => a + f(r), 0) / n;
  return `${(m(r => r.t) / 60).toFixed(0)} min (${(R[3].t / 60).toFixed(0)}–${(R[n - 4].t / 60).toFixed(0)}) · combats ${(m(r => r.t_fight) / 60).toFixed(0)} min · ${m(r => r.renc).toFixed(0)} rencontres · ${m(r => r.defaites).toFixed(1)} défaites · niv. avant l'Arche ${m(r => r.Lavant).toFixed(1)} · final ${m(r => r.L).toFixed(1)}`; };
for (const [lab, o] of [
  ['histoire seule, paquet actuel, renc 60', { renc: 60, lib: true, pakOnly: true }],
  ['tout, paquet actuel, renc 60', { annexes: true, renc: 60, lib: true, pakOnly: true }],
  ['histoire seule, bibliothèque, renc 30', { renc: 30, lib: true }],
  ['histoire seule, bibliothèque, renc 60', { renc: 60, lib: true }],
  ['histoire seule, bibliothèque, renc 120', { renc: 120, lib: true }],
  ['histoire seule, bibliothèque, renc 200', { renc: 200, lib: true }],
  ['histoire seule, bibliothèque sans boss de zone, renc 60', { renc: 60, lib: true, sansBoss: true }],
  ['tout, bibliothèque sans boss de zone, renc 60', { annexes: true, renc: 60, lib: true, sansBoss: true }],
  ['tout, bibliothèque, renc 60, joueur simple (ATTAQUE)', { annexes: true, renc: 60, lib: true, style: 'simple' }],
  ['tout, bibliothèque, renc 120', { annexes: true, renc: 120, lib: true }],
]) console.log(lab.padEnd(58), stat(o));
