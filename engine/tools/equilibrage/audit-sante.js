// Audit de la santé, des PM et des soins (02/11/2026) sur la partie simulée (progression.js) : règles d'avant (équipe soignée
// après chaque combat, ancienne courbe, réserve de PM qui suit le niveau, niveaux d'origine des étapes) et d'après (PV et PM
// gardés d'un combat à l'autre, nouvelle courbe, réserve fixe, niveaux de niveaux.json), joueur prudent ou insouciant.
// Mesures : niveau à la fin de chaque acte, durée, combats, défaites et REPRENDRE (risque de mourir sur un tronçon entre deux
// cibles), état de l'équipe à l'arrivée des combats de l'histoire, part des crédits en soins et en équipement, usage de chaque
// moyen de se soigner (sorts hors combat, potions, éthers, cliniques, hôpital, verre, repli par le téléport).
//   node audit-sante.js [parties]          (CAMPS_SOINS=40 : la tente du troc des campements soigne, prix d'une clinique)
const C = require('./combat.js'), PL = require('./plan.js'), P = require('./progression.js');
const n = +(process.argv[2] || 6);
const AVANT = require('fs').existsSync(__dirname + '/niveaux.json') ? JSON.parse(require('fs').readFileSync(__dirname + '/niveaux.json', 'utf8')).AVANT : null;
const NOUV = {}; for (const q of Object.keys(PL.ETAPES)) NOUV[q] = PL.ETAPES[q].slice();
const niveaux = av => { for (const q of Object.keys(PL.ETAPES)) PL.ETAPES[q].forEach((v, i) => { PL.ETAPES[q][i] = av ? ((AVANT[q] || {})[i + 1] ?? v) : NOUV[q][i]; }); };
const moy = (R, f) => R.reduce((s, r) => s + f(r), 0) / R.length;
const pc = v => (100 * v).toFixed(0) + ' %';
const ACTES = ['prologue', 'acte1', 'acte2a', 'acte2', 'acte3', 'acte4', 'acte5', 'a61', 'a62', 'a63'];
function bilan(lab, R) {
  const h = moy(R, r => r.t) / 3600, S = k => moy(R, r => r.soins[k]);
  const dep = moy(R, r => r.dep_pot + r.dep_eth + r.dep_soins + r.dep_reprendre + (r.dep_rappel || 0)), eq = moy(R, r => r.dep_eq);
  const rev = moy(R, r => r.cr_combat + r.cr_cristaux + r.cr_coffres + r.cr_quete + r.cr_vente);
  const eb = R.flatMap(r => r.etatBoss);
  console.log(`\n== ${lab} (${R.length} parties)`);
  console.log(`  niveau à la fin : ${ACTES.map(q => q + ' ' + moy(R, r => (r.actes.find(x => x.q === q) || { L: 0 }).L).toFixed(0)).join(' · ')}`);
  console.log(`  ${h.toFixed(1)} h · ${moy(R, r => r.fights).toFixed(0)} combats joués (${moy(R, r => r.renc).toFixed(0)} rencontres dont ${moy(R, r => r.groupes).toFixed(0)} en groupe, ${moy(R, r => r.patr).toFixed(0)} patrouilles) · défaites ${moy(R, r => r.defaites).toFixed(0)}, REPRENDRE ${moy(R, r => r.morts).toFixed(1)} (dont ${moy(R, r => r.morts_hc || 0).toFixed(1)} dans les quêtes hardcore, tentées sans monter à 90)`);
  console.log(`  tronçons entre deux cibles : ${moy(R, r => r.troncons).toFixed(0)}, dont ${moy(R, r => r.troncons_mort).toFixed(1)} avec un REPRENDRE en chemin (${(100 * moy(R, r => r.troncons_mort) / moy(R, r => r.troncons)).toFixed(1)} %)`);
  console.log(`  arrivée aux combats de l'histoire : PV ${pc(eb.reduce((a, x) => a + x[1], 0) / eb.length)}, PM ${pc(eb.reduce((a, x) => a + x[2], 0) / eb.length)}`);
  console.log(`  crédits gagnés ${rev.toFixed(0)} · équipement ${eq.toFixed(0)} (${pc(eq / rev)}) · soins ${dep.toFixed(0)} (${pc(dep / rev)} : potions ${moy(R, r => r.dep_pot).toFixed(0)}, éthers ${moy(R, r => r.dep_eth).toFixed(0)}, lieux ${moy(R, r => r.dep_soins).toFixed(0)}, REPRENDRE ${moy(R, r => r.dep_reprendre).toFixed(0)}, seringues ${moy(R, r => r.dep_rappel || 0).toFixed(0)}) · reste ${moy(R, r => r.cr).toFixed(0)}`);
  console.log(`  moyens de soin : sorts hors combat ${S('magie').toFixed(0)} (${(S('pv_magie') / 1000).toFixed(0)} k PV) · potions ${S('potion').toFixed(0)} (${(S('pv_potion') / 1000).toFixed(0)} k PV hors combat) · éthers ${S('ether').toFixed(1)} · cliniques ${S('clinique').toFixed(1)}, hôpital ${S('hopital').toFixed(1)}, verre ${S('verre').toFixed(1)} (${(S('pv_lieu') / 1000).toFixed(0)} k PV) · replis par le téléport ${S('retraite').toFixed(1)}`);
  console.log(`  à terre : ${moy(R, r => r.ko).toFixed(0)} membres à la fin d'un combat gagné · relevés par la seringue ${S('rappel').toFixed(0)}, par le CHANT DE RAPPEL ${S('releve_sort').toFixed(0)} hors combat et ${S('releve_combat').toFixed(0)} en combat, dans un lieu de soins ${S('releve_lieu').toFixed(0)} · ${moy(R, r => r.ko_combats).toFixed(1)} combats engagés avec un membre à terre`);
  const B = {}; R.forEach(r => r.boss.forEach(([tag, nom, L, Le, e]) => { const k = tag + ' ' + nom; (B[k] = B[k] || { L, e: 0, n: 0 }); B[k].e += e; B[k].n++; }));
  const durs = Object.entries(B).filter(([k, b]) => b.e / b.n >= 1).sort((a, b) => b[1].e / b[1].n - a[1].e / a[1].n).slice(0, 12);
  if (durs.length) console.log('  combats de l\'histoire les plus perdus (défaites moyennes) : ' + durs.map(([k, b]) => `${k} ${(b.e / b.n).toFixed(1)}`).join(' · '));
}
const parties = o => { const R = []; for (let i = 0; i < n; i++) R.push(P.run(o)); return R; };
if (AVANT && !process.env.SANS_AVANT) {
  const fin = P.reglesAvant(); niveaux(true);
  bilan('AVANT : règles et niveaux d\'avant, histoire seule, prudent', parties({ annexes: false, ancien: true }));
  bilan('AVANT : règles et niveaux d\'avant, toutes les annexes, prudent', parties({ annexes: true, ancien: true }));
  fin(); niveaux(false);
}
if (process.env.SEUL_AVANT) process.exit(0);   // HISTOIRE=…/Backup/02_11/histoire.avant-exp.js : les fiches d'avant, pour l'« avant »
if (process.env.UN_PV) {   // la règle précédente (02/11/2026, première partie) : un membre à terre relevé à 1 PV après le combat
  bilan('RELEVÉ À 1 PV : histoire seule, prudent', parties({ annexes: false, unPv: true }));
  bilan('RELEVÉ À 1 PV : toutes les annexes, prudent', parties({ annexes: true, unPv: true }));
  bilan('RELEVÉ À 1 PV : toutes les annexes, insouciant', parties({ annexes: true, joueur: 'insouciant', unPv: true }));
}
bilan('APRÈS : histoire seule, prudent', parties({ annexes: false }));
bilan('APRÈS : la moitié des annexes, prudent (référence depuis le 02/11/2026 au soir)', parties({ annexes: 0.5 }));
bilan('APRÈS : la moitié des annexes, insouciant', parties({ annexes: 0.5, joueur: 'insouciant' }));
bilan('APRÈS : la moitié des annexes, prudent, Lyse au combat dès qu’elle est là', parties({ annexes: 0.5, soigneuse: true }));
bilan('APRÈS : toutes les annexes, prudent', parties({ annexes: true }));
