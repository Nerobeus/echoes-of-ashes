// Étalonnage des groupes (GROUPE_PV, GROUPE_ATQ, GROUPE_XP de engine/core/battle.c, de l'émulateur et de combat.js) :
//   un groupe de n ennemis ne vaut pas plus qu'un ennemi seul à niveau égal. PV × GROUPE_PV[n] (1 / n : la même somme de
//   PV) ; ATQ × GROUPE_ATQ[n] cherché pour que l'équipe perde autant de PV qu'contre l'ennemi seul (n ennemis jouent
//   chacun leur tour) ; GROUPE_XP[n] : la durée du combat par rapport à l'ennemi seul (l'expérience par heure tient).
//   Ennemis ordinaires du paquet (ni sorts ni PV d'élite), équipe complète à parité, niveaux NIVEAUX.
//   node groupes.js            : mesure avec les tables actuelles (combat.js) et propose les valeurs
//   node groupes.js verif      : seulement la mesure (seul, 2, 3, 4 ; parts égales et boss avec sbires)
const C = require('./combat.js'), R = require('./regles.js'), Q = require('./equipement.js');
const N = +(process.env.N || 300), NIVEAUX = [8, 15, 25, 40, 60], EQUIPE = [0, 1, 2, 3];
const ELITE_PV = 600;
const ORD = C.W.enemies.filter(e => C.ENNEMIS[e.nom] && !(C.ENNEMIS[e.nom].sorts || []).length && e.hp <= ELITE_PV * (e.lvl + 6)).map(e => e.nom);
const uniq = [...new Set(ORD)];
// équipement normal (palier du moment, equipement.js) depuis le 01/11/2026
function mesure(team, L, fiches, mode = 'parts', n = N) {
  let w = 0, t = 0, pe = 0;
  const eq = Q.gear(L);
  for (let i = 0; i < n; i++) { const r = C.battle(team, L, fiches, { potions: 3 }, 'bon', eq, mode); if (r.win) { w++; t += r.t; } pe += r.perte; }
  return { p: w / n, t: w ? t / w : 0, perte: pe / n };
}
// Moyennes sur les ennemis ordinaires et les niveaux : seul, puis groupes de n du même ennemi
function bilan(n, team = EQUIPE) {
  let s = { p: 0, t: 0, perte: 0 }, g = { p: 0, t: 0, perte: 0 }, k = 0;
  for (const nom of uniq) for (const L of NIVEAUX) {
    const f = R.stats(C.ENNEMIS[nom], L), a = mesure(team, L, f), b = mesure(team, L, Array(n).fill(f));
    for (const c of ['p', 't', 'perte']) { s[c] += a[c]; g[c] += b[c]; }
    k++;
  }
  for (const c of ['p', 't', 'perte']) { s[c] /= k; g[c] /= k; }
  return { s, g };
}
const pct = v => (v * 100).toFixed(1) + ' %';
console.log(`${uniq.length} ennemis ordinaires (${uniq.join(', ')}), niveaux ${NIVEAUX.join(', ')}, ${N} combats par mesure`);
const verif = process.argv[2] === 'verif';
const prop = { pv: C.GROUPE_PV.slice(), atq: C.GROUPE_ATQ.slice(), xp: C.GROUPE_XP.slice() };
for (let n = 2; n <= 4; n++) {
  if (!verif) {   // ATQ cherchée par dichotomie : même perte de PV que l'ennemi seul
    let lo = 0.1, hi = 1.2;
    const ref = bilan(n).s.perte;
    for (let i = 0; i < 9; i++) { const a = (lo + hi) / 2; C.GROUPE_ATQ[n] = a; if (bilan(n).g.perte > ref) hi = a; else lo = a; }
    C.GROUPE_ATQ[n] = Math.round((lo + hi) / 2 * 100) / 100; prop.atq[n] = C.GROUPE_ATQ[n];
  }
  const { s, g } = bilan(n);
  if (!verif) { prop.xp[n] = Math.round(g.t / s.t * 20) / 20; C.GROUPE_XP[n] = prop.xp[n]; }
  console.log(`groupe de ${n} : PV × ${C.GROUPE_PV[n]}, ATQ × ${C.GROUPE_ATQ[n]} · seul ${pct(s.p)} en ${s.t.toFixed(0)} s, perte ${pct(s.perte)} · groupe ${pct(g.p)} en ${g.t.toFixed(0)} s, perte ${pct(g.perte)} · durée × ${(g.t / s.t).toFixed(2)} (EXP × ${C.GROUPE_XP[n]})`);
}
// Équipe de 2 et de 3 (les groupes viennent avec deux membres au moins)
for (const team of [[0, 1], [0, 1, 2]]) for (let n = 2; n <= Math.min(3, team.length + 1); n++) {
  const { s, g } = bilan(n, team);
  console.log(`équipe de ${team.length}, groupe de ${n} : seul ${pct(s.p)} perte ${pct(s.perte)} · groupe ${pct(g.p)} perte ${pct(g.perte)} · durée × ${(g.t / s.t).toFixed(2)}`);
}
// Boss et sbires : les boss du paquet (sorts ou PV d'élite) avec deux sbires ordinaires, à leur niveau, équipe complète ;
// BOSS_CEDE (ATQ cédée par sbire) cherché pour la même victoire que le boss seul
const BOSS = C.W.enemies.filter(e => C.ENNEMIS[e.nom] && !ORD.includes(e.nom)).map(e => e.nom).slice(0, 8);
function bossBilan() {
  let s = 0, g = 0;
  BOSS.forEach((nom, k) => { const e = C.ENNEMIS[nom], L = Math.max(5, e.lvl), f = R.stats(e, L), sb = R.stats(C.ENNEMIS[uniq[k % uniq.length]], L);
    s += mesure(EQUIPE, L, f, 'parts', 200).p; g += mesure(EQUIPE, L, [f, sb, sb], 'sbires', 200).p; });
  return { s: s / BOSS.length, g: g / BOSS.length };
}
if (!verif) {
  let lo = -0.3, hi = 0.2;
  const ref = bossBilan().s;
  for (let i = 0; i < 8; i++) { C.R_.BOSS_CEDE = (lo + hi) / 2; if (bossBilan().g < ref) lo = C.R_.BOSS_CEDE; else hi = C.R_.BOSS_CEDE; }
  C.R_.BOSS_CEDE = Math.round((lo + hi) / 2 * 100) / 100; prop.cede = C.R_.BOSS_CEDE;
}
{
  let s = 0, g = 0, ps = 0, pg = 0, k = 0;
  for (const nom of BOSS) {
    const e = C.ENNEMIS[nom], L = Math.max(5, e.lvl), f = R.stats(e, L), sb = R.stats(C.ENNEMIS[uniq[k % uniq.length]], L);
    const a = mesure(EQUIPE, L, f, 'parts', 200), b = mesure(EQUIPE, L, [f, sb, sb], 'sbires', 200);
    s += a.p; g += b.p; ps += a.perte; pg += b.perte; k++;
  }
  console.log(`boss avec 2 sbires (${k} boss) : seul ${pct(s / k)} perte ${pct(ps / k)} · avec sbires ${pct(g / k)} perte ${pct(pg / k)} (SBIRE_PART ${C.SBIRE_PART}, BOSS_CEDE ${C.R_.BOSS_CEDE})`);
}
if (!verif) console.log(`proposé : GROUPE_ATQ = [${prop.atq.join(', ')}], GROUPE_XP = [${prop.xp.join(', ')}], BOSS_CEDE = ${prop.cede}`);
