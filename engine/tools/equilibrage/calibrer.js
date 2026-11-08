// Étalonnage des fiches (01/11/2026) : facteur k sur PV et ATQ de chaque ennemi (profil gardé) selon son rang (plan.js) ;
// équipe par défaut de la bande au moment du combat (règle du moteur), équipement du palier du moment (equipement.js :
// gear(L), « normal »), joueur qui soigne et use de ses techniques et de ses sorts ; dans l'arc final après a61 et l'après-jeu,
// Sera transformée (ÉVEIL, quête eveil-sera). Depuis le 02/11/2026 (PV et PM gardés d'un combat à l'autre), les cibles se
// lisent dans l'état où le chemin amène l'équipe : un combat de l'histoire PV pleins et les PM moyens d'arrivée de la partie de
// référence (etat-boss.json de « progression.js niveaux », 0,76 à 1 selon l'acte), une rencontre à pleine forme, chaque membre
// gardant 35 % de ses PM. Depuis le 03/11/2026 (référence : le joueur qui ne fait que l'histoire et les rencontres de son
// chemin), un combat de l'histoire principale se mesure avec l'équipement que ce joueur porte (equipement-ref.json), les PM de
// son étape, et doit aussi tenir PRINC_MIN à RETARD niveaux sous l'étape (voir engine/README.md, « Équilibrage : l'histoire
// donne l'expérience »). Ne pas charger ce fichier par require : il étalonne dès qu'il est lu et réécrit fiches.json.
//   ordinaire, normal : ennemi des rencontres, étalonné à quatre (la fiche vaut pour quatre membres) au niveau où l'on
//                       découvre son premier terrain (monde.js) : un combat d'environ 40 s, gagné 95 fois sur 100 au moins
//   eliteZone         : élite des rencontres qui n'est pas un boss de l'histoire : 85 % à quatre, à son terrain
//   garde, elite, boss, annexe, final, hardcore : victoire visée (plan.js : CIBLES) au combat de référence (le premier de
//                       l'histoire principale, sinon le premier), à son niveau d'étape, avec ses sbires
// La fiche garde son niveau (lvl : il ouvre les rencontres, enc_ok de game.c) ; étalonnée à niv (rencontres : le plus haut
// de lvl, 5 et du niveau de découverte de son premier terrain), PV et ATQ ramenés à lvl comme le fait battle_start.
// Écrit fiches.json (nom -> { rang, lvl, niv, hp, atk, spd, sorts, magie, cible, mesure }) ; node calibrer.js [NOM…]
const C = require('./combat.js'), PL = require('./plan.js'), Q = require('./equipement.js'), M = require('./monde.js');
const fs = require('fs');
const N = +(process.env.N || 240), ITER = 15;
const R = PL.rangsDe(C.ENNEMIS), seuls = process.argv.slice(2);
const T4 = [0, 1, 2, 3], DUREE = 40, PLANCHER = 0.1, RETARD = +(process.env.RETARD || 5);
// Sera transformée (ÉVEIL : quête eveil-sera, ouverte après a61) : l'arc final après a61 et l'après-jeu (02/11/2026 : d'après
// la place dans l'histoire ; avant, au niveau 86 et plus)
const eveilDe = (rang, c) => (rang === 'hardcore' || rang === 'final') && c.quete !== 'a61' ? [0] : null;
// État d'arrivée (02/11/2026 : PV et PM gardés d'un combat à l'autre) : un combat de l'histoire se prend PV pleins (le joueur
// se soigne avant, menu ou lieu de soins : la sauvegarde automatique est juste avant la scène) et les PM où le chemin les
// laisse, moyenne de la partie de référence par quête (etat-boss.json de progression.js niveaux ; 60 % sans elle) ; une
// rencontre se prend à pleine forme, chaque membre gardant RES_GARDE de ses PM pour la suite
const EB = fs.existsSync(__dirname + '/etat-boss.json') ? JSON.parse(fs.readFileSync(__dirname + '/etat-boss.json', 'utf8')) : {};
const PM_ANNEXE = 0.84;   // annexes : la moyenne des leurs (0,84 le 02/11/2026 au soir, joueur prudent, la moitié des annexes), faites de peu de combats, trop bruitées une à une
// (03/11/2026 : celui de l'étape, « acte5#1 », quand etat-boss.json l'a ; sinon celui de la quête)
const pmDe = (q, st) => { const pr = PL.QUETE[q] && PL.QUETE[q].type === 'principale', v = EB[q + '#' + st] !== undefined ? EB[q + '#' + st] : EB[q]; return pr && v !== undefined ? Math.max(0.35, v) : PM_ANNEXE; };
const etatDe = (c, team) => { const mp = pmDe(c.quete, c.etape), e = { hp: {}, mp: {} }; for (const k of team) { e.hp[k] = 1; e.mp[k] = mp; } return e; };
// Niveau du combat d'étalonnage : celui de l'étape ; pour l'arc final, s'il était affiché au-dessus du niveau réel (figé à 68-76 un
// temps, le 02/11/2026 ; il est à son niveau réel depuis) : le niveau qu'y a vraiment le joueur (REEL de niveaux.json), car le combat se joue à min(étape,
// équipe) ; les quêtes hardcore gardent le leur (on y monte en combattant)
const REEL = (fs.existsSync(__dirname + '/niveaux.json') ? JSON.parse(fs.readFileSync(__dirname + '/niveaux.json', 'utf8')).REEL : null) || {};
const nivDe = c => PL.FINAL.has(c.quete) && REEL[c.quete] && REEL[c.quete][c.etape] ? Math.min(c.niv, REEL[c.quete][c.etape]) : c.niv;
const GARDE = 0.35;
// REMAP=1 (une fois, le 02/11/2026) : le niveau des fiches passe à la nouvelle échelle (plan.js remap ; hardcore gardé)
// REMAP_DE=fichier (03/11/2026, une fois, quand les niveaux des étapes changent d'échelle) : le niveau des fiches passe de
// l'échelle de ce niveaux.json (son NIV) à celle d'aujourd'hui (plan.js remapDe sur les étapes des quêtes principales)
const REMAP2 = process.env.REMAP_DE ? PL.remapDe({ AVANT: JSON.parse(fs.readFileSync(process.env.REMAP_DE, 'utf8')).NIV, NIV: PL.NIV }) : null;
const nivFiche = (e, rang) => rang === 'hardcore' ? e.lvl : REMAP2 ? REMAP2(e.lvl) : process.env.REMAP === '1' ? PL.remap(e.lvl) : e.lvl;
// Équipement normal : celui du palier du moment, Sera seule comprise (01/11/2026 : le prologue l'équipe ; avant : aucun)
// 03/11/2026 : un combat de l'histoire principale, avec l'équipement que porte à son arrivée le joueur de référence (equipement-ref.json
// de « progression.js niveaux » : ce que ses crédits lui ont permis d'acheter ; au premier acte, un ou deux paliers sous celui de
// son niveau) ; EQREF=0 : celui du palier du moment
const EQREF = process.env.EQREF !== '0' && fs.existsSync(__dirname + '/equipement-ref.json') ? JSON.parse(fs.readFileSync(__dirname + '/equipement-ref.json', 'utf8')) : {};
const equipDe = cfg => cfg.tag && EQREF[cfg.tag] ? { stats: EQREF[cfg.tag] } : Q.gear(cfg.L, false, null, cfg.nus);
// Combat d'essai : la fiche e du chef (au niveau L) et le reste du groupe tel quel
function mesure(e, cfg, n = N) {
  const fs_ = cfg.noms.map((nm, i) => C.auNiveau(i === 0 || nm === cfg.noms[0] ? e : C.ENNEMIS[nm], cfg.L));
  return C.winRate(cfg.team, cfg.L, fs_, 3, n, 'bon', equipDe(cfg), cfg.mode, { eveil: cfg.eveil, etat: cfg.etat, garde: cfg.garde });
}
function fit(e0, cfg, critere) {   // critere(r) vrai : l'ennemi peut être plus fort
  let lo = 0.02, hi = 60;
  for (let i = 0; i < ITER; i++) { const k = Math.sqrt(lo * hi); if (critere(mesure({ ...e0, hp: e0.hp * k, atk: e0.atk * k }, cfg))) lo = k; else hi = k; }
  return Math.sqrt(lo * hi);
}
const out = fs.existsSync(__dirname + '/fiches.json') && seuls.length ? JSON.parse(fs.readFileSync(__dirname + '/fiches.json', 'utf8')) : {};
// Ordre : les ennemis des rencontres d'abord (ils sont aussi des sbires et des compagnons), puis les gardes, puis les boss
const ordre = r => ({ ordinaire: 0, normal: 0, eliteZone: 1, garde: 2 })[r] ?? 3;
const liste = Object.values(C.ENNEMIS).filter(e => !e.nom.startsWith('__') && (!seuls.length || seuls.includes(e.nom)))
  .map(e => { const r = R[e.nom]; return { e, rang: r ? r.rang : C.elite(e) ? 'eliteZone' : 'normal', r }; }).sort((a, b) => ordre(a.rang) - ordre(b.rang));
for (const { e: e0, rang, r } of liste) {
  let cfg, k, crit;
  const lvl = nivFiche(e0, rang);
  if (rang === 'ordinaire' || rang === 'normal' || rang === 'eliteZone') {
    const z = M.niveauZones(e0), L = Math.max(5, lvl, Math.min(99, isFinite(z) ? z : r ? r.ref.niv : lvl));
    cfg = { noms: [e0.nom], mode: 'parts', L, team: T4, garde: GARDE };
    crit = rang === 'eliteZone' ? x => x.p > PL.CIBLES.eliteZone : x => x.p >= 0.95 && x.t < DUREE;
  } else {
    const c = r.ref, L = nivDe(c);
    const team = PL.equipeDefaut(PL.bandeDe(c));
    cfg = { noms: c.noms, mode: c.mode, L, team, eveil: eveilDe(rang, c), nus: PL.arrivesDe(c), etat: etatDe(c, team), tag: c.quete + '#' + c.etape };
    crit = x => x.p > r.cible;
  }
  const e1 = C.auNiveau(e0, cfg.L);
  k = fit(e1, cfg, crit);
  // Ses autres combats (comme chef, à d'autres niveaux, seul ou en groupe) : aucun sous le plancher (cible − PLANCHER) ;
  // sinon la fiche baisse jusqu'à ce que le plus dur l'atteigne (les autres deviennent plus faciles que la cible)
  const autres = [];
  if (r && !['ordinaire', 'normal', 'eliteZone'].includes(rang)) {
    const vus = new Set([r.ref.niv + '|' + r.ref.noms.join('+') + '|' + r.ref.mode]);
    for (const c of r.combats) {
      const key = c.niv + '|' + c.noms.join('+') + '|' + c.mode; if (vus.has(key) || !c.niv) continue; vus.add(key);
      const tc = PL.equipeDefaut(PL.bandeDe(c));
      const cc = { noms: c.noms, mode: c.mode, L: nivDe(c), team: tc, eveil: eveilDe(rang, c), nus: PL.arrivesDe(c), etat: etatDe(c, tc), tag: c.quete + '#' + c.etape };
      // 03/11/2026 : un combat de l'histoire principale au moins à PRINC_MIN (90 %) ; une annexe, la cible moins PLANCHER
      const plancher = c.type === 'principale' && PL.PRINC_MIN ? Math.min(r.cible, PL.PRINC_MIN) : Math.max(0.15, r.cible - PLANCHER);
      // la fiche au niveau de ce combat : même rapport que la fiche de référence (× (niv + 6) / (L + 6))
      const kc = fit(C.auNiveau(e1, cc.L), cc, x => x.p > plancher);
      autres.push({ cc, kc });
      if (kc < k) k = kc;
    }
    // 03/11/2026 : le joueur qui arrive RETARD niveaux sous l'étape (moins de rencontres, l'équipement d'un palier plus bas) : le
    // combat suit son niveau (BOSS_ECART 0), il le gagne encore au moins à PRINC_MIN
    if (r.ref.type === 'principale' && PL.PRINC_MIN && cfg.L > RETARD) {
      const cc = { ...cfg, L: cfg.L - RETARD, lib: cfg.tag + ' à −' + RETARD };   // son équipement : celui de la référence
      const kc = fit(C.auNiveau(e1, cc.L), cc, x => x.p > PL.PRINC_MIN);
      autres.push({ cc, kc }); if (kc < k) k = kc;
    }
  }
  // la fiche garde son niveau (il ouvre les rencontres : enc_ok) ; PV et ATQ ramenés à ce niveau (× (lvl + 6) / (L + 6))
  const ra = (lvl + 6) / (cfg.L + 6), ficheK = kk => ({ lvl, hp: Math.max(1, Math.round(e1.hp * kk * ra)), atk: Math.max(1, Math.round(e1.atk * kk * ra)) });
  let f = ficheK(k);
  Object.assign(C.ENNEMIS[e0.nom], f);   // la suite (sbires, compagnons) voit la fiche faite
  let v = mesure(C.ENNEMIS[e0.nom], cfg, 600);
  // 03/11/2026 : la recherche se fait sur N combats, au bruit près ; un combat de l'histoire principale vérifié sur 600 et repris
  // (× 0,97) tant qu'il reste plus de 1,5 point sous sa cible (avant : jusqu'à 4 points sous les 93 % visés)
  for (let i = 0; i < 8 && r && r.ref.type === 'principale' && rang !== 'hardcore' && !['ordinaire', 'normal', 'eliteZone'].includes(rang) && v.p < r.cible - 0.015; i++) {
    k *= 0.97; f = ficheK(k); Object.assign(C.ENNEMIS[e0.nom], f); v = mesure(C.ENNEMIS[e0.nom], cfg, 600);
  }
  out[e0.nom] = { rang, lvl: f.lvl, niv: cfg.L, hp: f.hp, atk: f.atk, spd: e0.spd, sorts: e0.sortsId && e0.sortsId.length ? e0.sortsId : null, magie: e0.magie, cible: r ? r.cible : null,
    mesure: { p: +v.p.toFixed(3), t: Math.round(v.t), perte: +v.perte.toFixed(2) } };
  const plus = autres.map(a => { const v2 = mesure(C.auNiveau(C.ENNEMIS[e0.nom], a.cc.L), a.cc, 300); return `${a.cc.lib || a.cc.tag} ${(v2.p * 100).toFixed(0)} %`; });
  if (plus.length) out[e0.nom].autres = plus;
  console.log(`${e0.nom.padEnd(24)} ${rang.padEnd(9)} niv ${String(cfg.L).padStart(2)} (fiche ${String(f.lvl).padStart(2)}) · PV ${String(f.hp).padStart(7)} ATQ ${String(f.atk).padStart(5)} (× ${k.toFixed(2)}) · ${(v.p * 100).toFixed(0).padStart(3)} % en ${v.t.toFixed(0).padStart(3)} s, PV perdus ${(v.perte * 100).toFixed(0)} % · équipe ${cfg.team.map(m => C.NOMS[m].slice(0, 3)).join('-')}${cfg.eveil ? ' (ÉVEIL)' : ''}${plus.length ? ' · ailleurs : ' + plus.join(', ') : ''}`);
}
fs.writeFileSync(__dirname + '/fiches.json', JSON.stringify(out, null, 1));
console.log(Object.keys(out).length + ' fiches dans fiches.json');
