const C = require('./combat.js');
const FIGHTS = [
  ['GLANEUR (dalles)', [0], 'GLANEUR'], ['SENTINELLE', [0], 'SENTINELLE'], ['GLANEUR BLANC (H01)', [0], 'GLANEUR BLANC'],
  ['INQUISITRICE', [0, 1], 'INQUISITRICE'], ['ESSAIM (H02, Sera+Varn)', [0, 1], 'ESSAIM'], ['PATROUILLE', [0, 1, 2], 'PATROUILLE'],
  ['FOUISSEUR', [0, 1, 2], 'FOUISSEUR'], ['RÔDEUR (H04)', [0, 1, 2], 'RÔDEUR'], ['TRAQUEUSE', [0, 1, 2], 'TRAQUEUSE'],
  ['GARDIENNE', [0, 1, 2, 3], 'GARDIENNE'], ['GRAND INQUISITEUR', [0, 1, 2, 3], 'GRAND INQUISITEUR'], ['MALDEN', [0, 1, 2, 3], 'MALDEN'],
];
console.log('combat | niv. ennemi | 50% (3 pot.) | 90% (3 pot.) | 90% (0 pot.) | 90% (10 pot.) | 90% joueur simple | 90% équipé');
for (const [lab, team, nom] of FIGHTS) {
  const e = C.ENNEMIS[nom];
  console.log([lab, e.lvl, C.levelFor(team, nom, 3, 0.5), C.levelFor(team, nom, 3, 0.9), C.levelFor(team, nom, 0, 0.9), C.levelFor(team, nom, 10, 0.9), C.levelFor(team, nom, 3, 0.9, 'simple'), C.levelFor(team, nom, 3, 0.9, 'bon', { af: 1 })].join(' | '));
}
