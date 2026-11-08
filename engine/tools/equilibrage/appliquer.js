// Reporte l'étalonnage dans js/histoire.js de l'Atelier (01/11/2026) : fiches des ennemis de PERSOS (PV, ATQ et, depuis le
// 02/11/2026, le niveau de fiche de fiches.json ; avant, le niveau
// de la fiche reste le sien), niveau des étapes corrigé par plan.js (NIV ; une étape absente du plan garde le sien, rien
// n'est effacé), récompenses « experience » des annexes de la table XP (en combats du niveau de leur étape ; seulement les
// lignes qui existent), table EQUILIBRE des ennemis de la bibliothèque (fiches.json de calibrer.js).
//   node appliquer.js [chemin de histoire.js] [--essai]   (--essai : écrit à côté, histoire.essai.js, et montre le bilan)
const fs = require('fs'), R = require('./regles.js'), PL = require('./plan.js');
const args = process.argv.slice(2), essai = args.includes('--essai');
const P = args.find(a => !a.startsWith('--')) || 'D:/Documents/Stellar Exodus/tileset-generator-v4/js/histoire.js';
const F = JSON.parse(fs.readFileSync(__dirname + '/fiches.json', 'utf8'));
let s = fs.readFileSync(P, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const avant = s;
const esc = t => t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), jsName = n => n.replace(/'/g, "\\'");

// 1. Fiches des ennemis de PERSOS : hp, atk (lvl et les autres champs restent)
const p0 = s.indexOf('TilesetGen.Histoire.PERSOS = ['), p1 = s.indexOf('TilesetGen.Histoire.OBJETS');
const persos = s.slice(p0, p1);
let np = persos, nf = 0, nl = 0;
for (const [nom, f] of Object.entries(F)) {
  // la fiche de ce personnage : la première « ennemi: { lvl: » après son nom et avant le personnage suivant (avant le
  // 01/11/2026, une expression trop gourmande pouvait prendre celle de la ligne suivante)
  const a = np.indexOf(`{ nom: '${jsName(nom)}',`); if (a < 0) continue;
  let b = np.indexOf('{ nom: \'', a + 8); if (b < 0) b = np.length;
  const re = /ennemi: \{ lvl: (\d+), hp: \d+, atk: \d+/, bloc = np.slice(a, b), m = re.exec(bloc);
  if (!m) continue;
  // niveau de fiche : celui de fiches.json (02/11/2026 : la nouvelle échelle, calibrer.js REMAP=1 ; avant, gardé)
  if (+m[1] !== f.lvl) nl++;
  np = np.slice(0, a) + bloc.replace(re, `ennemi: { lvl: ${f.lvl}, hp: ${f.hp}, atk: ${f.atk}`) + np.slice(b); nf++;
}
s = s.slice(0, p0) + np + s.slice(p1);

// 2. Niveau des étapes : seulement celles que plan.js corrige (NIV), « niv: N » de l'étape remplacé ; les autres restent
const qs = s.indexOf('TilesetGen.Histoire.QUETES = ['), qe = s.indexOf('\n];', qs);
let quetes = s.slice(qs, qe), ne = 0, nt = 0;
for (const [id, corr] of Object.entries(PL.NIV)) {
  const a = quetes.search(new RegExp(`\\{ id: ['"]${esc(id)}['"],`)); if (a < 0) { console.log('  quête ' + id + ' introuvable'); continue; }
  let b = quetes.indexOf('{ id: ', a + 5); if (b < 0) b = quetes.length;
  let bloc = quetes.slice(a, b), k = 0;
  const re2 = /\{ texte: '((?:[^'\\]|\\.)*)'((?:, (?:perso|lieu): '[^']*')?)(?:, niv: (\d+))?( ?)\}/g;
  // garde-fou (02/11/2026) : les étapes reconnues doivent être toutes celles de la quête, sinon la k-ième ne serait pas la bonne
  const vues = (bloc.match(re2) || []).length, attendues = PL.QUETE[id] ? PL.QUETE[id].etapes.length : -1;
  if (vues !== attendues) { console.log(`  quête ${id} : ${vues} étapes reconnues sur ${attendues}, laissée telle quelle`); continue; }
  // le niveau écrit dans les textes : « (niv 92) » d'une étape, « Niveau 66 conseillé » de la description (celui de la
  // première étape)
  bloc = bloc.replace(re2, (m, t, c, n, sp) => {
    k++;
    if (!corr[k]) return m;
    if (+n !== +corr[k]) ne++;
    return `{ texte: '${t.replace(/\(niv \d+\)/, '(niv ' + corr[k] + ')')}'${c}, niv: ${corr[k]}${sp}}`;
  });
  if (corr[1]) bloc = bloc.replace(/Niveau \d+ conseillé/g, () => { nt++; return `Niveau ${corr[1]} conseillé`; });
  quetes = quetes.slice(0, a) + bloc + quetes.slice(b);
}
s = s.slice(0, qs) + quetes + s.slice(qe);

// 3. Récompenses des annexes : experience = combats × XP d'un ennemi du niveau de l'étape (lignes existantes seulement) ;
//    depuis le 02/11/2026, le nombre de combats vient de plan.js (XP_ANNEXES) et le niveau de l'étape de l'événement (les
//    courses, sans niveau : celui de l'étape principale après laquelle elles s'ouvrent, ouverture.json)
const OUV = JSON.parse(fs.readFileSync(__dirname + '/ouverture.json', 'utf8')).etapes;
const nivEv = id => { const ev = PL.H.EVENTS.find(e => e.id === id); if (!ev) return 0;
  const n = PL.niveauEtape(ev.quete, Math.max(1, ev.etape || 0)); if (n) return n;
  const o = (OUV[ev.quete] || [])[Math.max(0, (ev.etape || 1) - 1)]; if (!o) return 0; const [q, st] = o.split('#'); return PL.niveauEtape(q, +st); };
const XP = Object.fromEntries(Object.entries(PL.XP_ANNEXES).map(([id, n]) => [id, [nivEv(id), n]]).filter(([, [niv]]) => niv > 0));
let nx = 0;
for (const [id, [niv, n]] of Object.entries(XP)) {
  const a = s.search(new RegExp(`\\{ id: ['"]${esc(id)}['"],`)); if (a < 0) continue;
  const b = s.indexOf('] },', a), v = Math.min(65535, Math.round(n * R.expOf(niv) / 10) * 10);
  s = s.slice(0, a) + s.slice(a, b).replace(/'(\s*)experience \d+'/g, (m, sp) => { nx++; return `'${sp}experience ${v}'`; }) + s.slice(b);
}

// 4. Table EQUILIBRE : fiches des ennemis de la bibliothèque qui ne sont pas dans PERSOS ; true : légende de l'après-jeu
//    (sans zone, retirée des rencontres)
const dans = new Set([...np.matchAll(/\{ nom: '((?:[^'\\]|\\.)*)'/g)].map(m => m[1].replace(/\\'/g, "'")));
const lignes = Object.entries(F).filter(([nom]) => !dans.has(nom)).map(([nom, f]) => `    '${jsName(nom)}': [${f.lvl}, ${f.hp}, ${f.atk}${f.rang === 'hardcore' ? ', true' : ''}],   // ${f.rang}${f.niv && f.niv !== f.lvl ? ', étalonné au niveau ' + f.niv : ''}`);
const table = `TilesetGen.Histoire.EQUILIBRE = {\n${lignes.join('\n').replace(/,(\s*\/\/[^\n]*)$/, '$1')}\n};`;
if (!/TilesetGen\.Histoire\.EQUILIBRE = \{[\s\S]*?\n\};/.test(s)) throw new Error('EQUILIBRE introuvable');
s = s.replace(/TilesetGen\.Histoire\.EQUILIBRE = \{[\s\S]*?\n\};/, table);

// Bilan : rien d'autre ne doit changer que les lignes des fiches, des niveaux corrigés, des récompenses et de la table
const la = avant.split('\n'), lb = s.split('\n');
let diff = 0; for (let i = 0; i < Math.max(la.length, lb.length); i++) if (la[i] !== lb[i]) diff++;
const sortie = essai ? P.replace(/\.js$/, '.essai.js') : P;
fs.writeFileSync(sortie, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log(`${nf} fiches de PERSOS (${nl} niveaux de fiche changés), ${ne} niveaux d'étape changés (${nt} « Niveau N conseillé »), ${nx} récompenses, ${lignes.length} fiches de la bibliothèque (EQUILIBRE) ; ${diff} lignes changées (${la.length} → ${lb.length}) → ${sortie}`);
