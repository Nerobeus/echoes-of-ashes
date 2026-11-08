// Plan de l'histoire, lu dans js/histoire.js de l'Atelier (01/11/2026 ; avant : tables écrites à la main, en retard sur
// l'histoire) : quêtes et niveau de chaque étape (niv), combats de chaque événement (groupes compris : « combat A x3 »,
// « combat A, B », « combat BOSS avec S x2 »), recrues (rejoindre) au fil des quêtes principales, rang de chaque ennemi
// de l'histoire et victoire visée. Sans dépendance au paquet : appliquer.js s'en sert seul.
//
//   ETAPES : { quête : [niveau de chaque étape] }, ceux de js/histoire.js, puis NIV (corrections de l'équilibrage)
//   COMBATS : [{ quete, type, etape, niv, ev, noms, mode, ordre }] dans l'ordre de l'histoire (quêtes principales, puis
//             les annexes rangées à leur niveau)
//   BANDE(quete, etape) / bandeAuNiveau(L) : membres de la bande (0 SERA … 9 AMBRE) ; equipeDefaut(bande) : au combat
//             par la règle du moteur (rejoindre : au combat tant qu'il reste une place, sinon en réserve)
//   RANGS : { ennemi : { rang, cible, ref } } (ref : le combat d'étalonnage) ; CIBLES : victoire visée par rang
//   BOSS : { ennemi : [quête, étape, équipe, cible] } (forme d'avant, pour les anciens scripts)
const ATELIER = 'D:/Documents/Stellar Exodus/tileset-generator-v4';
function charger(f) {
  global.TilesetGen = global.TilesetGen || {}; global.window = global; global.document = global.document || { getElementById: () => null };
  f = f || process.env.HISTOIRE || ATELIER + '/js/histoire.js';
  delete require.cache[require.resolve(f)];
  require(f);
  return TilesetGen.Histoire;
}
const H = charger();
const norm = s => String(s).trim().toUpperCase().normalize('NFD').replace(/[\u0300-\u036f]/g, '');
const MEMBRES = ['SERA', 'VARN', 'BORS', 'LYSE', 'JADE', 'SAULE', 'ÉTUDIANT', 'ONYX', 'AZUR', 'AMBRE'];
const MEMBRE = {}; MEMBRES.forEach((m, i) => MEMBRE[norm(m)] = i);
// Partie d'une héroïne (03/11/2026) : js/histoire.js a le prologue de chaque héroïne au choix (« prologue » pour Sera ; pjade,
// psaule, ponyx, pazur, pambre), les variantes des quêtes de compagnons quand leur héroïne est jouée (c10h… à la place de
// c10…) et les Éveils de chacune, jouée (eveil-jade) ou recrue (eveil-jade-recrue) ; une partie ne compte que les siens (avant,
// les six prologues s'enchaînaient : six Sentinelles, rangées « garde »). HEROINE=JADE… : la partie d'une autre (Sera par défaut)
const HEROINE = norm(process.env.HEROINE || 'SERA');
function heroineDe(q, QUETES) {   // [héroïne, jouée] : la quête n'est que pour cette héroïne jouée (true) ou recrue (false) ; null : toutes
  if (!q) return null;
  if (q.type === 'principale' && /^prologue/i.test(q.nom || '')) return [q.id === 'prologue' ? 'SERA' : norm(q.id.slice(1)), true];
  let m = /^eveil-([a-z]+)(-recrue)?$/.exec(q.id);
  if (m) return [norm(m[1]), !m[2]];
  m = /^(.+)h$/.exec(q.id); const base = m && QUETES.find(x => x.id === m[1]);
  if (base && / · /.test(base.nom || '')) return [norm(base.nom.split(' · ')[0]), true];
  const vari = QUETES.find(x => x.id === q.id + 'h');
  if (vari && / · /.test(q.nom || '')) return [norm(q.nom.split(' · ')[0]), false];
  return null;
}
const dansPartie = (id, h = HEROINE) => { const q = H.QUETES.find(x => x.id === id), r = heroineDe(q, H.QUETES); return !r || (r[0] === h) === r[1]; };
// Événement de la partie : de sa quête, et pas auprès d'un personnage présent pour une autre héroïne seulement (visible :
// drapeau heroine_X ; « pjade-chasse » d'acte1, auprès d'OCRE de la troupe de Jade)
const VISIBLE_H = {}; for (const p of H.PERSOS || []) if (p.visible && /^heroine_/.test(p.visible.flag || '')) VISIBLE_H[p.nom] = norm(p.visible.flag.slice(8));
const evDansPartie = (ev, h = HEROINE) => (!ev.quete || dansPartie(ev.quete, h)) && !(ev.perso && VISIBLE_H[ev.perso] && VISIBLE_H[ev.perso] !== h);

// Corrections des niveaux d'étape (quête -> { étape : niveau }), reportées dans js/histoire.js par appliquer.js ; depuis le
// 02/11/2026, celles de niveaux.json (écrit par « progression.js niveaux » : le niveau réel d'un joueur à chaque étape) ;
// NIVEAUX=0 : sans elles (les niveaux de js/histoire.js)
const fichierNiv = process.env.NIVEAUX_JSON || require('path').join(__dirname, 'niveaux.json');   // NIVEAUX_JSON=… : un autre fichier (comparaisons)
const fNiv = process.env.NIVEAUX !== '0' && require('fs').existsSync(fichierNiv) ? JSON.parse(require('fs').readFileSync(fichierNiv, 'utf8')) : {};
const NIV = fNiv.NIV || {};
// Correspondance ancien niveau -> nouveau (02/11/2026) : sur les étapes des quêtes (hors hardcore), le niveau d'origine (AVANT de
// niveaux.json) et le niveau réel ; croissante, linéaire entre deux points, constante au-delà du dernier. Pour ce qui était
// réglé sur l'ancienne échelle : niveau des fiches d'ennemis (il ouvre les rencontres), terrains sans cible (monde.js)
function remapDe(f) {
  const P = [];
  for (const q of H.QUETES) if (q.type === 'principale' && f.AVANT && f.AVANT[q.id] && f.NIV[q.id])   // les principales (les annexes ont été fondues le 02/11/2026 au soir)
    for (const [st, a] of Object.entries(f.AVANT[q.id])) { const n = f.NIV[q.id][st]; if (a > 0 && n > 0) P.push([a, n]); }
  if (!P.length) return L => L;
  const par = {}; for (const [a, n] of P) (par[a] = par[a] || []).push(n);
  let pts = Object.entries(par).map(([a, l]) => [+a, l.reduce((s, x) => s + x, 0) / l.length]).sort((x, y) => x[0] - y[0]);
  for (let i = 1; i < pts.length; i++) pts[i][1] = Math.max(pts[i][1], pts[i - 1][1]);
  pts = [[0, 0]].concat(pts);
  return L => { if (L >= pts[pts.length - 1][0]) return Math.round(pts[pts.length - 1][1]);
    for (let i = 1; i < pts.length; i++) if (L <= pts[i][0]) { const [a0, n0] = pts[i - 1], [a1, n1] = pts[i]; return Math.max(1, Math.round(n0 + (n1 - n0) * (L - a0) / (a1 - a0))); }
    return L; };
}
const remap = remapDe(fNiv);

const ETAPES = {};
for (const q of H.QUETES) ETAPES[q.id] = q.etapes.map((e, i) => (NIV[q.id] && NIV[q.id][i + 1]) || e.niv || 0);
const QUETE = {}; H.QUETES.forEach(q => QUETE[q.id] = q);
const niveauEtape = (q, st) => (ETAPES[q] || [])[st - 1] || 0;

// « combat … » (Scenario.combatGroupe de js/scenario.js) : noms, le chef d'abord ; sbires après « avec »
function combatGroupe(texte) {
  const fretin = /\s+fretin\s*$/i.test(String(texte));   // « fretin » : expérience d'une annexe
  const parts = String(texte).replace(/\s+fretin\s*$/i, '').trim().split(/\s+avec\s+/i), noms = [];
  const lire = p => (p || '').split(',').forEach(it => {
    const t = it.trim(), mm = /^(.+?)\s+[x×]\s*(\d+)$/i.exec(t), k = mm ? Math.max(1, parseInt(mm[2], 10)) : 1;
    for (let i = 0; i < k && t; i++) noms.push(mm ? mm[1].trim() : t);
  });
  lire(parts[0]); if (parts.length === 2) lire(parts[1]);
  return { noms: noms.slice(0, 4), mode: parts.length === 2 ? 'sbires' : 'parts', fretin };
}
const lignes = ev => (Array.isArray(ev.script) ? ev.script : [ev.script || '']).join('\n').split('\n').map(l => l.trim());
// Lignes jouées d'un événement (02/11/2026, partie simulée) : dans les blocs « si … / sinon / fin », la branche que prend un
// joueur de référence : le choix donné (1 par défaut), fuite réussie (« si fuite » vrai, « si non fuite » faux), héroïne
// Sera (« si heroine X » faux, « si non heroine » vrai), drapeaux, objets, quêtes et crédits vrais
function lignesJouees(ev, choix = 1) {
  const out = [], pile = [];
  for (const l of lignes(ev)) {
    let m;
    if ((m = /^si\s+(.+)$/i.exec(l))) {
      const c = m[1].trim().toLowerCase();
      let v = true;
      if ((m = /^choix\s+(\d+)/.exec(c))) v = +m[1] === choix;
      else if (/^non\s+fuite/.test(c)) v = false;
      else if ((m = /^(non\s+)?heroine\s+(\S+)/.exec(c))) v = (norm(m[2]) === HEROINE) !== !!m[1];   // 03/11/2026 : l'héroïne jouée (avant : toujours faux)
      pile.push({ on: v });
    } else if (/^sinon$/i.test(l)) { if (pile.length) pile[pile.length - 1].on = !pile[pile.length - 1].on; }
    else if (/^fin$/i.test(l)) pile.pop();
    else if (pile.every(b => b.on)) out.push(l);
  }
  return out;
}

// Ordre de l'histoire : les quêtes principales dans leur ordre, étape par étape ; une annexe se range à son premier niveau
const PRINC = H.QUETES.filter(q => q.type === 'principale' && dansPartie(q.id)).map(q => q.id);
const rangEtape = {};   // « quête#étape » -> rang dans l'histoire (nombre réel)
{
  let r = 0;
  for (const id of PRINC) ETAPES[id].forEach((n, i) => { rangEtape[id + '#' + (i + 1)] = r++; });
  rangEtape.fin = r;
}
// Rang d'une étape d'annexe : juste après la dernière étape principale de niveau au plus le sien
function rangAuNiveau(L) {
  let best = 0;
  for (const id of PRINC) ETAPES[id].forEach((n, i) => { if (n && n <= L) best = Math.max(best, rangEtape[id + '#' + (i + 1)]); });
  return best + 0.5;
}
const rangDe = (q, st) => QUETE[q] && QUETE[q].type === 'principale' ? rangEtape[q + '#' + Math.max(1, st)] - (st ? 0 : 0.5) : rangAuNiveau(niveauEtape(q, Math.max(1, st)) || 1);

// Combats et recrues de chaque événement
const COMBATS = [], RECRUES = [];   // RECRUES : [{ membre, rang }]
H.EVENTS.forEach((ev, ie) => {
  if (!evDansPartie(ev)) return;   // prologue, variantes et scènes d'une autre héroïne
  const q = QUETE[ev.quete], st = ev.etape || 0, niv = q ? niveauEtape(ev.quete, Math.max(1, st)) : 0;
  let j = 0;
  for (const l of lignes(ev)) {
    let m;
    if ((m = /^combat\s+(.+)$/i.exec(l))) {
      const g = combatGroupe(m[1]);
      COMBATS.push({ quete: ev.quete || '', type: q ? q.type : '', etape: st, niv, ev: ev.id, noms: g.noms, mode: g.mode, adulte: !!(ev.adulte || (q && q.adulte)),
        si: false, ordre: (q ? rangDe(ev.quete, st) : 1e6) + (j++) * 1e-3 + ie * 1e-7 });
    } else if ((m = /^(rejoindre|equipe)\s+(.+)$/i.exec(l)) && q && q.type === 'principale') {
      for (const nm of m[2].split(/[\s,]+/)) if (MEMBRE[norm(nm)] > 0) RECRUES.push({ membre: MEMBRE[norm(nm)], rang: rangDe(ev.quete, st) + (j++) * 1e-3 + ie * 1e-7 });
    }
  }
});
COMBATS.sort((a, b) => a.ordre - b.ordre);
// Bande au rang r de l'histoire : Sera et les recrues arrivées avant ; dans l'ordre d'arrivée
const bandeAuRang = r => [0].concat(RECRUES.filter(x => x.rang < r).sort((a, b) => a.rang - b.rang).map(x => x.membre).filter((m, i, t) => t.indexOf(m) === i));
const bandeDe = c => bandeAuRang(c.ordre);
// Membres arrivés à l'étape même du combat, avant lui (Varn au contrôle de l'acte I) : ils n'ont que leur équipement de départ
const arrivesDe = c => c.type !== 'principale' ? [] : RECRUES.filter(x => x.rang < c.ordre && Math.floor(x.rang + 1e-6) === Math.floor(c.ordre + 1e-6)).map(x => x.membre);
const bandeAuNiveau = L => bandeAuRang(rangAuNiveau(L));
// Au combat par la règle du moteur : les trois premières recrues (rejoindre : au combat s'il reste une place)
const equipeDefaut = bande => bande.slice(0, 4).sort((a, b) => a - b);
// Arrivée de chaque recrue (niveau de l'étape)
const ARRIVEE = {};
for (const x of RECRUES.sort((a, b) => a.rang - b.rang)) if (ARRIVEE[x.membre] === undefined) {
  for (const id of PRINC) ETAPES[id].forEach((n, i) => { if (Math.abs(rangEtape[id + '#' + (i + 1)] - Math.floor(x.rang)) < 1e-9) ARRIVEE[x.membre] = n; });
}

/*
 * Rang des ennemis de l'histoire et victoire visée au premier essai (équipe à son niveau, équipement de la boutique du
 * moment, joueur qui soigne et use de ses techniques, 3 potions) ; depuis le 03/11/2026 (demande de l'utilisateur : « même en
 * farmant beaucoup, le jeu est très difficile »), tout combat de l'histoire principale au moins 9 fois sur 10 au niveau où
 * l'histoire amène l'équipe (avant : boss 60 %, élite 75 %, garde 90 %, annexe 60 %, arc final 50 %) :
 *   ordinaire : ennemi des rencontres (zones, pas une élite) que l'histoire fait aussi combattre : étalonné comme
 *               rencontre (calibrer.js : environ 40 s à niveau égal, jamais une menace)
 *   garde     : soldats et gardiens que l'histoire oppose plusieurs fois (trois combats ou plus) : 95 %
 *   elite     : élite des rencontres (sorts, PV d'un boss de zone) qui est aussi un boss de l'histoire : 93 %
 *   boss      : boss de l'histoire principale : 93 % (au-dessus de 90 % : la marge du tirage et de l'équipement acheté)
 *   annexe    : boss d'une quête annexe : 80 % (un défi, RÉESSAYER LE COMBAT existe)
 *   final     : arc final (après la Source) : 93 %
 *   hardcore  : quêtes de l'après-jeu (90 et plus) : 25 %
 * Les autres combats d'un ennemi de l'histoire principale : au moins PRINC_MIN (90 %) ; ceux des annexes : sa cible − 10 points.
 * Arc final après a61 et après-jeu : l'étalonnage compte Sera transformée (ÉVEIL : quête eveil-sera).
 */
// Récompenses « experience » des annexes (événement -> combats d'un ennemi seul du niveau de son étape ; les courses, sans
// niveau : celui de l'histoire à leur ouverture), reportées par appliquer.js dans les lignes qui existent
const XP_ANNEXES = { 'h01-prime': 1.0, 'h04-prime': 1.5, 'e04-fin': 1.5, 'v01-fin': 1.0, 'v02-fin': 1.25, 'v04-fin': 1.25, 'v07-fin': 0.75, 'v09-horloge': 1.0,
  'k04-essai': 0.75, 'k05-course': 1.25, 'k06-finale': 2.0, 'k07-fin': 1.0, 'k08-course': 1.5, 'h10-prime': 2.0, 'h11-prime': 2.0, 'r05-colosse': 2.0 };
const CIBLES = { ordinaire: 0.97, garde: 0.95, elite: 0.93, boss: 0.93, annexe: 0.8, final: 0.93, hardcore: 0.25, eliteZone: 0.85 };
const PRINC_MIN = 0.9;   // 03/11/2026 : victoire au moins de tout combat de l'histoire principale au niveau prévu
const FINAL = new Set(['a61', 'a62', 'a63']);
// Corrections du rang (ennemi -> rang)
const RANG = { 'ÉCHO DU CHŒUR': 'annexe', 'SENTINELLE D\'ÉLITE': 'garde', 'MAMMOUTH DE FER': 'ordinaire', 'TRAQUEUSE': 'boss' };
// Victoire visée propre à un ennemi (03/11/2026 : l'Écho du Chœur de l'Éveil de Sera, sur le chemin de l'arc final, qui est
// étalonné avec elle transformée ; avant, la Sentinelle et le Glaneur blanc à 70 %, en haut de la fourchette d'alors)
const CIBLE = { 'ÉCHO DU CHŒUR': 0.93 };
function rangsDe(ENN) {   // ENN : fiches (combat.js) pour savoir qui a des zones et qui est une élite
  const R = {}, chefs = {};
  for (const c of COMBATS) { const n = c.noms[0]; (chefs[n] = chefs[n] || []).push(c); }
  for (const [nom, l] of Object.entries(chefs)) {
    const e = ENN && ENN[nom], zones = e ? (e.zones || []).length > 0 : false, el = e ? ((e.sorts || []).length > 0 || e.hp > 600 * (e.lvl + 6)) : false;
    const princ = l.filter(c => c.type === 'principale' && !FINAL.has(c.quete));
    const ref = princ[0] || l.filter(c => FINAL.has(c.quete))[0] || l[0];
    let rang;
    if (RANG[nom]) rang = RANG[nom];
    else if (zones && !el) rang = 'ordinaire';
    else if (zones && el) rang = princ.length ? 'elite' : 'eliteZone';
    else if (l.length >= 3) rang = 'garde';
    else if (FINAL.has(ref.quete)) rang = 'final';
    else if (ref.type === 'principale') rang = 'boss';
    else if (ref.niv >= 90) rang = 'hardcore';
    else rang = 'annexe';
    R[nom] = { rang, cible: CIBLE[nom] || CIBLES[rang], ref, combats: l };
  }
  return R;
}
// Forme d'avant : { ennemi : [quête, étape, équipe par défaut, victoire visée] } (boss seulement)
function bossDe(ENN) {
  const R = rangsDe(ENN), B = {};
  for (const [nom, r] of Object.entries(R)) if (r.rang !== 'ordinaire') B[nom] = [r.ref.quete, r.ref.etape, equipeDefaut(bandeDe(r.ref)), r.cible];
  return B;
}
module.exports = { H, HEROINE, dansPartie, evDansPartie, charger, norm, lignes, lignesJouees, XP_ANNEXES, remap, MEMBRES, ETAPES, NIV, QUETE, PRINC, COMBATS, RECRUES, ARRIVEE, niveauEtape, combatGroupe, rangDe, rangAuNiveau,
  bandeAuRang, bandeDe, arrivesDe, bandeAuNiveau, equipeDefaut, CIBLES, PRINC_MIN, CIBLE, RANG, remapDe, rangsDe, bossDe, FINAL };

if (require.main === module) {
  console.log(H.QUETES.length + ' quêtes, ' + COMBATS.length + ' combats ; arrivées : ' + Object.entries(ARRIVEE).map(([m, n]) => MEMBRES[m] + ' ' + n).join(', '));
  const E = require('./combat.js').ENNEMIS, R = rangsDe(E);
  for (const c of COMBATS) console.log(`${(c.quete + '#' + c.etape).padEnd(18)} niv ${String(c.niv).padStart(2)} ${c.noms.join(c.mode === 'sbires' ? ' + ' : ', ').padEnd(44)} ${(R[c.noms[0]] || {}).rang || ''} · bande ${bandeDe(c).map(m => MEMBRES[m]).join(' ')}`);
}
