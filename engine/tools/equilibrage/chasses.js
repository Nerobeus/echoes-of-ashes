// Étalonnage des grandes chasses (js/chasses.js de l'Atelier, 06/11/2026) : chaque bête, profil de son modèle, PV et ATQ pour 80 % de victoire à son
// niveau, la bande du moment, équipement du palier. Écrit ses PV et son ATQ dans js/chasses.js.   node chasses.js [--ecrire]
const fs = require('fs'), C = require('./combat.js'), Q = require('./equipement.js');
const F = C.ATELIER + '/js/chasses.js';
global.TilesetGen = global.TilesetGen || {};
let src = fs.readFileSync(F, 'utf8');
new Function('TilesetGen', src.split('TilesetGen.Chasses.persos = function')[0])(TilesetGen);
const L = TilesetGen.Chasses.LISTE, CIBLE = 0.8, N = 300;
const equipe = lvl => lvl < 17 ? [0, 1, 2] : [0, 1, 2, 3];
for (const c of L) {
  const base = C.ENNEMIS[c.modele];
  if (!base) { console.log(c.nom, ': modèle inconnu', c.modele); continue; }
  const at = C.auNiveau(base, c.lvl), eq = Q.gear(c.lvl), T = equipe(c.lvl);
  const essai = k => C.winRate(T, c.lvl, { ...at, nom: c.nom, spd: c.spd, hp: Math.round(at.hp * k), atk: Math.round(at.atk * k) }, 3, N, 'bon', eq, 'parts');
  let lo = 0.5, hi = 8, r = null;
  for (let i = 0; i < 12; i++) { const m = (lo + hi) / 2; r = essai(m); if (r.p > CIBLE) lo = m; else hi = m; }
  const k = (lo + hi) / 2, hp = Math.round(at.hp * k / 100) * 100, atk = Math.round(at.atk * k / 10) * 10;
  r = essai(k);
  console.log(c.nom.padEnd(22), 'niv', c.lvl, 'hp', hp, 'atk', atk, 'k', k.toFixed(2), Math.round(r.p * 100) + ' %', Math.round(r.t) + ' s', 'perte', Math.round(r.perte * 100) + ' %');
  if (process.argv.includes('--ecrire')) {
    const re = new RegExp("(nom: '" + c.nom.replace(/'/g, "\\\\'") + "',[^\\n]*?lvl: \\d+, hp: )\\d+(, atk: )\\d+");
    if (!re.test(src)) console.log('  introuvable'); else src = src.replace(re, '$1' + hp + '$2' + atk);
  }
}
if (process.argv.includes('--ecrire')) fs.writeFileSync(F, src);
