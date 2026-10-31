// Reporte le plan dans js/histoire.js de l'Atelier : niveau des étapes (niv), fiches des ennemis de l'histoire
// (fiches.json de calibrer.js), récompenses « experience » (en combats du niveau de l'étape), table EQUILIBRE des
// ennemis de la bibliothèque. node appliquer.js [chemin de histoire.js]
const fs = require('fs'), R = require('./regles.js'), { ETAPES } = require('./plan.js');
const P = process.argv[2] || 'D:/Documents/Stellar Exodus/tileset-generator-v4/js/histoire.js';
const F = JSON.parse(fs.readFileSync(__dirname + '/fiches.json', 'utf8'));
let s = fs.readFileSync(P, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const esc = t => t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), jsName = n => n.replace(/'/g, "\\'");

// 1. Fiches des ennemis de PERSOS : lvl, hp, atk (les autres champs restent)
const persos = s.slice(s.indexOf('TilesetGen.Histoire.PERSOS = ['), s.indexOf('TilesetGen.Histoire.OBJETS'));
let np = persos, nf = 0;
for (const [nom, f] of Object.entries(F)) {
  const re = new RegExp(`(\\{ nom: '${esc(jsName(nom))}',[^\\n]*\\n?[^\\n]*?ennemi: \\{ )lvl: \\d+, hp: \\d+, atk: \\d+`);
  if (re.test(np)) { np = np.replace(re, `$1lvl: ${f.lvl}, hp: ${f.hp}, atk: ${f.atk}`); nf++; }
}
s = s.replace(persos, np);

// 2. Niveau des étapes : « niv: N » dans chaque étape des quêtes du plan
const qs = s.indexOf('TilesetGen.Histoire.QUETES = ['), qe = s.indexOf('\n];', qs);
let quetes = s.slice(qs, qe), ne = 0;
for (const [id, niv] of Object.entries(ETAPES)) {
  const a = quetes.indexOf(`{ id: '${id}',`); if (a < 0) continue;
  let b = quetes.indexOf("{ id: '", a + 5); if (b < 0) b = quetes.length;
  let bloc = quetes.slice(a, b), k = 0;
  bloc = bloc.replace(/\{ texte: '((?:[^'\\]|\\.)*)'((?:, (?:perso|lieu): '[^']*')?)(?:, niv: \d+)? \}/g, (m, t, c) => `{ texte: '${t}'${c}${niv[k] ? `, niv: ${niv[k++]}` : (k++, '')} }`);
  quetes = quetes.slice(0, a) + bloc + quetes.slice(b); ne += k;
}
s = s.slice(0, qs) + quetes + s.slice(qe);

// 3. Récompenses : experience = combats × XP d'un ennemi du niveau de l'étape
const XP = { 'odric-coeur': [5, 6], 'egouts': [18, 10], 'h01-prime': [3, 4], 'h02-prime': [7, 4], 'h04-prime': [19, 6], 'e04-fin': [8, 6],
  'v01-fin': [12, 4], 'v02-fin': [12, 5], 'v04-fin': [14, 5], 'v07-fin': [11, 3], 'v09-horloge': [11, 4],
  'k04-essai': [8, 3], 'k05-course': [13, 5], 'k06-finale': [16, 8], 'k07-fin': [12, 4], 'k08-course': [25, 6],
  'h10-prime': [90, 8], 'h11-prime': [96, 8], 'r05-colosse': [99, 8] };
let nx = 0;
for (const [id, [niv, n]] of Object.entries(XP)) {
  const a = s.indexOf(`{ id: '${id}',`); if (a < 0) continue;
  const b = s.indexOf('] },', a), v = Math.min(65535, n * R.expOf(niv));
  s = s.slice(0, a) + s.slice(a, b).replace(/'(\s*)experience \d+'/g, (m, sp) => { nx++; return `'${sp}experience ${v}'`; }) + s.slice(b);
}

// 4. Table EQUILIBRE : fiches des ennemis de la bibliothèque qui ne sont pas dans PERSOS
const dans = new Set([...np.matchAll(/\{ nom: '((?:[^'\\]|\\.)*)'/g)].map(m => m[1].replace(/\\'/g, "'")));
const lignes = Object.entries(F).filter(([nom]) => !dans.has(nom)).map(([nom, f]) => `    '${jsName(nom)}': [${f.lvl}, ${f.hp}, ${f.atk}${f.rang === 'hardcore' ? ', true' : ''}],   // ${f.rang}`);
const table = `TilesetGen.Histoire.EQUILIBRE = {\n${lignes.join('\n').replace(/,(\s*\/\/[^\n]*)$/, '$1')}\n};`;
s = s.replace(/TilesetGen\.Histoire\.EQUILIBRE = \{[\s\S]*?\n\};/, table);
fs.writeFileSync(P, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log(`${nf} fiches, ${ne} étapes, ${nx} récompenses, ${lignes.length} fiches de la bibliothèque`);
