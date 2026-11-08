// Combat contre les forces de l'ordre (police.c : police_combat ; policeCombat de l'émulateur, 01/11/2026) : quand la police
// attrape Sera, n = niveau de recherche + 1 agents (2 à 4), au niveau de l'équipe + POL_COMBAT_NIV × le niveau de recherche
// (2, 4 ou 6 niveaux au-dessus), groupe à parts égales sur la fiche de la Patrouille de l'Ordre (PATROUILLE du paquet, un
// « garde » étalonné à 90 % à son niveau), PV et ATQ × POL_FORCE(niveau de l'équipe) (table par niveau, interpolée).
// Victoire au simulateur (combat.js) à chaque étape de l'histoire (bande par défaut à ce niveau, PL.bandeAuNiveau), avec
// l'équipement du palier du moment (gear(L) : « bien équipé »), un palier de retard (L − 7) et sans équipement ; 3 potions.
// Réponse très raide (quelques % de force font passer de 100 à 60 % de victoires) : la table du moteur est celle de
// « calibrer » (65 % visé en recherche 2) lissée, un peu plus douce (01/11/2026).
//   node police.js              : la table POL_FORCE du moteur (FORCE ci-dessous), mesurée
//   node police.js calibrer     : la table cherchée pour CIBLE (recherche 2, trois agents, bien équipée), à recopier dans
//                                 POL_FORCE de engine/core/police.c et de l'émulateur
//   ECART=k, FORCE=a,b,… , N=combats, POTIONS=n : autres réglages
const C = require('./combat.js'), Q = require('./equipement.js'), PL = require('./plan.js');
const ECART = +(process.env.ECART || 2);   // POL_COMBAT_NIV de police.c
const N = +(process.env.N || 200), POT = +(process.env.POTIONS || 3), CIBLE = +(process.env.CIBLE || 0.65);
// POL_FORCE de police.c : niveaux de l'équipe et facteur sur PV et ATQ des agents (interpolé entre deux niveaux)
const FORCE_NIV = [1, 8, 12, 16, 20, 25, 30, 40, 50, 60, 75, 90, 100];
let FORCE = (process.env.FORCE || '1,1,1,1,1.02,1.05,1.07,1.08,1.12,1.12,1.18,1.2,1.22').split(',').map(Number);
const fiche = C.W.enemies.find(e => e.nom === 'PATROUILLE');
if (!fiche) { console.log('pas de PATROUILLE dans le paquet'); process.exit(1); }
const force = L => { for (let i = 1; i < FORCE_NIV.length; i++) if (L <= FORCE_NIV[i]) { const a = FORCE_NIV[i - 1], b = FORCE_NIV[i], t = (L - a) / (b - a); return FORCE[i - 1] + (FORCE[i] - FORCE[i - 1]) * t; } return FORCE[FORCE.length - 1]; };
// n agents au niveau de l'équipe L + ECART × niv, × k ; l'équipe à son niveau L
function mesure(L, niv, k, eq, n = N) {
  const Lc = Math.min(C.NIV_MAX, L + ECART * niv), f = C.auNiveau({ ...fiche, sorts: [] }, Lc), g = Array(niv + 1).fill({ ...f, hp: f.hp * k, atk: f.atk * k });
  const team = PL.equipeDefaut(PL.bandeAuNiveau(L));
  let ok = 0, t = 0;
  for (let i = 0; i < n; i++) { const r = C.battle(team, L, g, { potions: POT }, 'bon', eq, 'parts'); if (r.win) { ok++; t += r.t; } }
  return { p: ok / n, t: ok ? t / ok : 0, team };
}
const pct = v => (v * 100).toFixed(0).padStart(3) + ' %';
console.log(`fiche PATROUILLE : niveau ${fiche.lvl}, PV ${fiche.hp}, ATQ ${fiche.atk}, VIT ${fiche.spd} ; ${N} combats, ${POT} potions`);
if (process.argv[2] === 'calibrer') {
  const out = [];
  for (const L of FORCE_NIV) {
    let lo = 0.1, hi = 8;
    for (let i = 0; i < 9; i++) { const k = Math.sqrt(lo * hi); if (mesure(L, 2, k, Q.gear(L), 120).p > CIBLE) lo = k; else hi = k; }
    const k = Math.round(Math.sqrt(lo * hi) * 100) / 100;
    out.push(k);
    console.log(`niveau ${String(L).padStart(3)} : × ${k.toFixed(2)} (recherche 2, ${(CIBLE * 100).toFixed(0)} % visé)`);
  }
  console.log('POL_FORCE = { ' + out.join(', ') + ' }');
  FORCE = out;
}
console.log('POL_FORCE ' + FORCE_NIV.map((L, i) => L + ':' + FORCE[i]).join(' '));
console.log('niveau | équipe      | recherche 1 (2 agents)      | recherche 2 (3 agents)      | recherche 3 (4 agents)');
console.log('       |             | équipé retard  sans  durée  | équipé retard  sans  durée  | équipé retard  sans  durée');
for (const L of [3, 6, 10, 14, 18, 22, 27, 35, 45, 55, 70, 85, 95]) {
  let ligne = '', team;
  for (let niv = 1; niv <= 3; niv++) {
    const k = force(L), a = mesure(L, niv, k, Q.gear(L)), b = mesure(L, niv, k, Q.gear(L - 7)), c = mesure(L, niv, k, null);
    team = a.team;
    ligne += ` ${pct(a.p)} ${pct(b.p)} ${pct(c.p)} ${a.t.toFixed(0).padStart(4)} s |`;
  }
  console.log(`${String(L).padStart(6)} | ${team.map(m => C.NOMS[m].slice(0, 2)).join('-').padEnd(11)} |` + ligne);
}
