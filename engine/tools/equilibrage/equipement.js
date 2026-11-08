// Équipement attendu et son effet : paliers de js/paliers.js de l'Atelier (noms, statistiques, sorts, prix), pondérés par
// l'affinité du job de chaque membre (AFF_F de battle.c). node equipement.js : victoire des combats d'étalonnage de
// l'histoire à leur niveau sans équipement, avec celui de la boutique du moment (paliers jusqu'au niveau + AVANCE) et avec
// un palier de retard ; durée des combats normaux ; prix d'un équipement complet contre les crédits d'un palier.
const C = require('./combat.js');
global.TilesetGen = global.TilesetGen || {};
TilesetGen.Scenario = TilesetGen.Scenario || { norm: s => String(s).trim().toUpperCase().normalize('NFD').replace(/[̀-ͯ]/g, '') };
TilesetGen.Objets = TilesetGen.Objets || { list: [] };
const PALIERS = process.env.PALIERS || C.ATELIER + '/js/paliers.js';
delete require.cache[require.resolve(PALIERS)];
require(PALIERS);
const P = TilesetGen.Paliers;
const CATS = ['lame', 'fouet', 'feu', 'canon', 'arc', 'baton', 'poings', 'legere', 'lourde', 'tissu', 'accessoire'];
const SLOT = c => c === 'legere' || c === 'lourde' || c === 'tissu' ? 1 : c === 'accessoire' ? 2 : 0;
const AFF_F = [0, 0.7, 0.85, 1, 1.2];
// Objets de la table (palier ≤ VENDU_MAX : la boutique ; 15 : butin seulement), statistiques et sorts de la table
const tous = () => P.TABLE.map(e => ({ nom: e[1], palier: e[2], sorte: e[3], fin: !!e[4], sorts: (e[6] || []).map(C.sortIndex),
  prix: P.prix(e[3], P.NIV[e[2]], e[4] ? 1.2 : 1, e[6]), ...P.stats(e[3], P.NIV[e[2]], P.QUALITE[e[4] ? 1 : 0]) }));
let OBJETS = tous();
// Score d'un objet pour son emplacement : arme l'ATQ (MAG pour un mage) ; armure et accessoire, DÉF, PV, ATQ, MAG et VIT
const score = (o, f, mage) => f * (SLOT(o.cat) === 0 ? o.atq * (mage ? 0.4 : 1) + o.mag * (mage ? 1 : 0.3) : o.def * 4 + o.pv / 10 + o.atq * (mage ? 0.4 : 1) + o.mag * (mage ? 1 : 0.3) + o.pm / 20 + o.vit * 25 + o.sorts.length * 30);
// Meilleur équipement du membre k parmi les objets dont le palier a un niveau au plus Lmax (vendus : palier ≤ VENDU_MAX,
// sauf butin : le palier 15 aussi) ; jobNom : un autre job que celui du départ
function equipe(k, Lmax, butin, jobNom) {
  const j = C.jobDe(k, jobNom), mage = (j.mag || 0) > 0 || (j.sorts || 0n) !== 0n, out = { atq: 0, def: 0, pv: 0, pm: 0, mag: 0, vit: 0, sorts: [], noms: [], prix: 0 };
  for (let s = 0; s < 3; s++) {
    let best = null, bf = 0;
    for (const o of OBJETS) {
      if (SLOT(o.cat) !== s || P.NIV[o.palier] > Lmax || (o.palier > P.VENDU_MAX && !butin)) continue;
      const f = AFF_F[Math.min(4, j.aff[CATS.indexOf(o.cat)] | 0)];
      if (f && (!best || score(o, f, mage) > score(best, bf, mage))) { best = o; bf = f; }
    }
    if (best) { for (const q of ['atq', 'def', 'pv', 'pm', 'mag', 'vit']) out[q] += (best[q] || 0) * bf; out.sorts.push(...best.sorts); out.noms.push(best.nom); out.prix += best.prix; }
  }
  return out;
}
// Équipement de départ du membre k (JOBS du paquet) ; un membre que le paquet laisse sans rien (paquet d'avant le
// 01/11/2026 : Sera, Varn, Bors et Lyse) prend celui de DEFAUT_MEMBRES (js/equipement.js de l'Atelier), d'après la table
let DEFM = null;
function departAtelier(k, out) {
  if (!DEFM) { TilesetGen.Scenario.MEMBERS = TilesetGen.Scenario.MEMBERS || []; const f = C.ATELIER + '/js/equipement.js'; delete require.cache[require.resolve(f)]; require(f); DEFM = TilesetGen.Equipement.DEFAUT_MEMBRES || {}; }
  const d = DEFM[C.NOMS[k]] || {}, j = C.jobDe(k);
  for (const n of [d.arme, d.armure, d.accessoire]) {
    const o = n && OBJETS.find(x => TilesetGen.Scenario.norm(x.nom) === TilesetGen.Scenario.norm(n)); if (!o) continue;
    const f = AFF_F[Math.min(4, j.aff[CATS.indexOf(o.cat)] | 0)];
    for (const q of ['atq', 'def', 'pv', 'pm', 'mag', 'vit']) out[q] += (o[q] || 0) * f;
    out.sorts.push(...o.sorts); out.noms.push(o.nom);
  }
  return out;
}
function depart(k) {
  const j = C.jobDe(k), out = { atq: 0, def: 0, pv: 0, pm: 0, mag: 0, vit: 0, sorts: [], noms: [], prix: 0 }, m = C.W.membres[k];
  // depuis le 02/11/2026 : toujours celui de l'Atelier (DEFAUT_MEMBRES), source du paquet, qui suit les niveaux d'arrivée ;
  // le paquet n'a le nouveau qu'après une réexportation
  if (process.env.DEPART_PAQUET !== '1' || !m || !(m.eq || []).some(i => i !== 255)) return departAtelier(k, out);
  for (const i of (m && m.eq) || []) {
    const e = C.W.eq.find(x => x.item === i); if (!e) continue;
    const f = AFF_F[Math.min(4, j.aff[e.cat] | 0)];
    for (const q of ['atq', 'def', 'pv', 'pm', 'mag', 'vit']) out[q] += (e[q] || 0) * f;
    for (let s = 0; s < C.W.sorts.length; s++) if (e.sorts && (e.sorts >> BigInt(s)) & 1n) out.sorts.push(s);
  }
  return out;
}
// L'équipement de tous les membres (recrues comprises) : celui des paliers jusqu'au niveau Lmax ; nus : ces membres n'ont
// que leur équipement de départ (arrivés à l'étape même du combat)
const gear = (Lmax, butin, jobs, nus) => ({ Lmax, stats: C.BASE.map((b, k) => nus && nus.includes(k) ? depart(k) : equipe(k, Lmax, butin, jobs && jobs[k])) });
// Boutique du moment au niveau L : paliers jusqu'à L + AVANCE ; un palier de retard : jusqu'à L − 7
const boutique = L => gear(L + P.AVANCE);
const retard = L => gear(L - 7);
// Prix de l'équipement d'une équipe (trois pièces par membre)
const prixEquipe = (team, Lmax) => team.reduce((s, k) => s + equipe(k, Lmax).prix, 0);
function recharger() { delete require.cache[require.resolve(PALIERS)]; require(PALIERS); Object.assign(module.exports.P, TilesetGen.Paliers); OBJETS = tous(); }
// Noms de l'équipement de départ du membre k (DEFAUT_MEMBRES de js/equipement.js de l'Atelier) : progression.js le porte
const departNoms = k => departAtelier(k, { atq: 0, def: 0, pv: 0, pm: 0, mag: 0, vit: 0, sorts: [], noms: [], prix: 0 }).noms;
// Cristaux du Chœur (03/11/2026, js/paliers.js : CRISTAUX) : { nom, sort (index), palier, pts (points à réunir), dispo (niveau où
// on les trouve : celui de leur palier ; élites et boss, coffres, trésors, boutiques avancées pour les prismes), objet (statistiques
// de l'accessoire porté le temps de l'apprendre) }
const CRISTAUX = () => (P.CRISTAUX || []).map(e => { const sorte = e[2] >= 13 ? 'eclat' : 'cristal', L = P.NIV[e[2]];
  return { nom: e[0], sort: C.sortIndex(e[1]), palier: e[2], pts: e[3], dispo: L, vendu: !!e[7],
    objet: { nom: e[0], palier: e[2], sorte, cat: 'accessoire', fin: false, sorts: [C.sortIndex(e[1])], prix: P.prix(sorte, L, 1, [e[1]]), ...P.stats(sorte, L, 1) } }; });
// Sorts des cristaux qu'un membre a pu apprendre au niveau L (collection complète : chaque cristal trouvé à son niveau et appris
// en un niveau ; borne haute de ce que la magie apporte à un joueur qui les cherche tous)
const apprisAuNiveau = L => CRISTAUX().filter(c => c.sort >= 0 && c.dispo + 1 <= L).map(c => c.sort);
module.exports = { P, equipe, depart, departNoms, gear, boutique, retard, prixEquipe, OBJETS: () => OBJETS, recharger, SLOT, CATS, CRISTAUX, apprisAuNiveau };

if (require.main === module) {
  const PL = require('./plan.js'), R = PL.rangsDe(C.ENNEMIS);
  console.log('combat d\'étalonnage (équipe par défaut) | niveau | sans | boutique (niv + ' + P.AVANCE + ') | un palier de retard (niv − 7)');
  for (const [nom, r] of Object.entries(R)) {
    if (r.rang === 'ordinaire') continue;
    const c = r.ref, L = c.niv, team = PL.equipeDefaut(PL.bandeDe(c)); if (!L) continue;
    const w = eq => C.winRate(team, L, c.noms, 3, 200, 'bon', eq, c.mode);
    const nus = PL.arrivesDe(c), w0 = w(null), w1 = w(gear(L + P.AVANCE, false, null, nus)), w2 = w(gear(L - 7, false, null, nus));   // un membre arrivé à l'étape : son équipement de départ
    console.log(`${nom.padEnd(24)} ${r.rang.padEnd(9)} ${team.length} | ${String(L).padStart(2)} | ${(w0.p * 100).toFixed(0).padStart(3)} % ${w0.t.toFixed(0).padStart(3)} s | ${(w1.p * 100).toFixed(0).padStart(3)} % ${w1.t.toFixed(0).padStart(3)} s | ${(w2.p * 100).toFixed(0).padStart(3)} % ${w2.t.toFixed(0).padStart(3)} s`);
  }
  console.log('\npalier | niveau | équipement de 4 membres (12 pièces) | crédits des combats du palier (+ cœurs)');
  for (let p = 1; p <= P.VENDU_MAX; p++) {
    const L0 = P.NIV[p], L1 = p < P.MAX_PALIER ? P.NIV[p + 1] : 100;
    let cr = 0, coeurs = 0; for (let L = L0; L < L1; L++) { const n = C.combatsNiveau(L); cr += n * C.credits_of(L); coeurs += n * 0.6 * 100; }
    console.log(`${String(p).padStart(2)} ${P.NOMS[p].padEnd(10)} | ${String(L0).padStart(2)} | ${String(prixEquipe([0, 1, 2, 3], L0)).padStart(7)} CR | ${Math.round(cr).toString().padStart(8)} CR (+ ${Math.round(coeurs)})`);
  }
}
