// Partie complète simulée : histoire principale (et annexes), positions réelles, XP, potions, durée
const C = require('./combat.js');
const fs = require('fs');
const K = 'D:/Documents/Stellar Exodus/tileset-generator-v4/projets/principal/bases/AtelierEchoesV4/kv/';
const S = JSON.parse(fs.readFileSync(K + 'scenario.json', 'utf8'))[1];
const SCRIPT = {}; S.forEach(e => SCRIPT[e.histoire || e.id] = e.script);
// Temps d'un script hors combat : 3 s par réplique, 4 s par choix, 3 s par carton (titre), attentes écrites
function scriptTime(id) {
  const s = SCRIPT[id] || ''; let t = 0;
  for (const l of s.split('\n').map(x => x.trim())) {
    if (/^[A-ZÉÈÀÂÎÔÛÇ' -]+:/.test(l)) t += 3; else if (/^choix /.test(l)) t += 4; else if (/^titre /.test(l)) t += 3;
    else if (/^attendre /.test(l)) t += parseFloat(l.split(' ')[1]) || 0;
  }
  return t;
}
const LANDS = ['mer', 'lac', 'rive', 'plaine', 'champs', 'foret', 'montagne', 'haute-montagne', 'urbain', 'gravats', 'industriel', 'rue', 'cendre', 'eau-claire'];
const W = C.W, START = [181.5, 227.5], ECH = 1.3, VIT = 3.6 /* cases/s : course la plupart du temps */, DETOUR = 1.3;
const P = {
  odric: [159.5, 225.5], kess: [163.5, 226.5], toll: [160.5, 218.5], dalles: [159.5, 234.5], epave: [148.5, 226.5], blanc: [140.5, 233.5], depot: [153.5, 211.5],
  echo1: [156.5, 240.5], echo2: [144.5, 218.5], echo3: [208.5, 225.5], garde: [229.5, 211.5], marche: [269.5, 221.5], controle: [250.5, 208.5],
  bors: [272.5, 221.5], marchande: [267.5, 221.5], usurier: [269.5, 225.5], entrepot: [271.5, 235.5], egouts_e: [275.5, 203.5], galerie: [264.5, 243.5],
  sous_brasier: [264.5, 250.5], rodeur: [257.5, 251.5], sortie: [263.5, 258.5], camp: [256.5, 409.5], ruines: [210.5, 440.5], ancienne: [256.5, 407.5],
  arche: [67.5, 440.5], porte: [229.5, 211.5], brasier: [266.5, 205.5], pretre: [264.5, 203.5], nell: [263.5, 185.5], pip: [291.5, 201.5],
  quartier: [263.5, 188.5], horloger: [260.5, 207.5], horloge: [259.5, 206.5], anneau: [102.5, 108.5],
};
function zoneAt(x, z) { const i = Math.floor(x) >> 1, j = Math.floor(z) >> 1; const t = W.zone[j * W.zoneN + i]; return t < 32 ? LANDS[t] : null; }

function run(opt) {
  const g = { L: 1, xp: 0, cr: 0, potions: 3, pos: START.slice(), t: 0, team: [0], log: [], marche: 0, fights: 0, defaites: 0, renc: 0, t_walk: 0, t_fight: 0, t_talk: 0, t_race: 0, encLeft: 0, coeurs: 0 };
  const tables = {};
  for (const z of LANDS) tables[z] = Object.values(C.ENNEMIS).filter(e => (opt.lib ? e.zones.includes(z) : false) && (!opt.pakOnly || C.W.enemies.some(p => p.nom === e.nom)) && (!opt.sansBoss || !e.sorts.length));
  const gain = xp => { g.xp += xp; while (g.L < 99 && g.xp >= C.exp_next(g.L)) { g.xp -= C.exp_next(g.L); g.L++; } };
  const fight = (nom, scripted) => {
    const en = C.ENNEMIS[nom];
    for (let essai = 0; essai < 50; essai++) {
      const inv = { potions: g.potions }, r = C.battle(g.team, g.L, en, inv, opt.style || 'bon');
      g.potions = inv.potions; g.t += r.t; g.t_fight += r.t; g.fights++;
      if (r.win) { gain(C.exp_of(en.lvl)); g.cr += C.credits_of(en.lvl); if (en.coeur) g.coeurs++; return { ok: true, essai }; }
      g.defaites++; g.t += 5; g.t_fight += 5;
      if (!scripted) { const back = g.pos.slice(); g.pos = START.slice(); walk(back, true); return { ok: false }; }
      // défaite d'un combat de l'histoire : retour au départ, puis on revient (le joueur rachète des potions en passant)
      const back = g.pos.slice(); g.pos = START.slice(); shop(); walk(back, true);
    }
    return { ok: false, essai: 50 };
  };
  const walk = (to, noEnc) => {
    const d = Math.hypot(to[0] - g.pos[0], to[1] - g.pos[1]) * DETOUR, steps = Math.max(1, Math.ceil(d));
    const from = g.pos.slice();
    for (let s = 1; s <= steps; s++) {
      const x = from[0] + (to[0] - from[0]) * s / steps, z = from[1] + (to[1] - from[1]) * s / steps, dd = d / steps;
      g.t += dd / VIT; g.t_walk += dd / VIT; g.marche += dd; g.pos = [x, z];
      if (!opt.renc || noEnc === 'tp') continue;
      if (g.encLeft <= 0) g.encLeft = -Math.log(1 - C.rand()) * opt.renc;
      g.encLeft -= dd * ECH; if (g.encLeft > 0) continue;
      g.encLeft = 0;
      const tb = tables[zoneAt(x, z)] || []; if (!tb.length) continue;
      let r = C.rand() * tb.reduce((a, e) => a + e.poids, 0), pick = tb[tb.length - 1];
      for (const e of tb) { r -= e.poids; if (r <= 0) { pick = e; break; } }
      g.renc++;
      const res = fight(pick.nom, false);
      if (!res.ok) { g.log.push(`  ✗ défaite contre ${pick.nom} (niv. ${pick.lvl}) au niveau ${g.L}, retour au départ`); return walk(to, noEnc); }
    }
    g.pos = to.slice();
  };
  const talk = id => { const t = scriptTime(id); g.t += t; g.t_talk += t; };
  // Boutique (Odric, Halo) : potions à 60 crédits, jusqu'à 12 en réserve
  const shop = () => { while (g.cr >= 60 && g.potions < (opt.potMax || 12)) { g.cr -= 60; g.potions++; } };
  const step = (lieu, id, combat, xp, extra) => {
    if (lieu) walk(P[lieu]);
    talk(id);
    if (combat) { const L0 = g.L; const r = fight(combat, true); g.log.push(`${combat.padEnd(18)} équipe ${g.team.length} · niveau ${L0} → ${g.L}${r.essai ? ` · ${r.essai} défaite(s) avant` : ''} · ${(g.t / 60).toFixed(0)} min`); }
    if (xp) gain(xp);
    if (extra) extra();
  };
  const race = (n, xp, cr) => { const t = n * 170 * 1.5; g.t += t; g.t_race += t; if (xp) gain(xp); g.cr += cr || 0; };
  const A = opt.annexes;

  g.t += 90; g.t_talk += 90;   // ouverture : cartons, choix de l'héroïne
  talk('prologue');
  step('odric', 'odric-debut');
  if (A) { step('kess', 'h01'); step('blanc', 'h01-chasse', 'GLANEUR BLANC'); step('kess', 'h01-prime', null, 80, () => g.cr += 300); }
  step('dalles', 'dalles', 'GLANEUR');
  step('odric', 'odric-vente');
  step('epave', 'epave', 'SENTINELLE');
  step('odric', 'odric-coeur', null, 100, shop);
  if (A) {
    step('kess', 'h02'); step('kess', 'e04'); step('depot', 'h02-depot', 'ESSAIM'); step('kess', 'h02-prime', null, 100, () => g.cr += 400);
    step('echo1', 'e04-1'); step('echo2', 'e04-2'); step('echo3', 'e04-3'); step('kess', 'e04-fin', null, 120);
    shop();
    // L'Anneau : course d'essai (podium)
    walk(P.anneau); talk('c01'); talk('c01-carcasse'); talk('c01-essai'); race(1, 60, 150);
    talk('c03'); talk('c03-manche1'); race(1, 0, 200); talk('c03-manche2'); race(1, 0, 250); talk('c03-manche3'); race(1, 0, 300); talk('c03-finale'); race(1, 300, 1000);
    walk(P.odric); talk('c04'); walk(P.anneau); talk('c04-chrono'); race(1, 0, 0); walk(P.odric); talk('c04-fin'); gain(120); g.cr += 350; shop();
  }
  for (let k = 0; k < (opt.grindOdric || 0); k++) { talk('odric-chasse'); fight('GLANEUR', true); }
  step('garde', 'porte', null, 0, () => g.coeurs = Math.max(0, g.coeurs - 2));
  step('marche', 'marche');
  step('controle', 'controle', null, 0, () => g.team = [0, 1]);
  { const L0 = g.L; const r = fight('INQUISITRICE', true); g.log.push(`${'INQUISITRICE'.padEnd(18)} équipe 2 · niveau ${L0} → ${g.L}${r.essai ? ` · ${r.essai} défaite(s) avant` : ''} · ${(g.t / 60).toFixed(0)} min`); }
  shop();
  step('bors', 'bors', null, 0, () => g.team = [0, 1, 2]);
  if (A) {
    step('marchande', 'v01'); step('quartier', 'v01-quartier', 'GLANEUR'); step('marchande', 'v01-fin', null, 100, () => g.cr += 250);
    step('pretre', 'v02'); step('brasier', 'v02-brasier'); step('pretre', 'v02-fin', null, 100);
    step('nell', 'v04'); step('pip', 'v04-pip', 'PATROUILLE'); step('nell', 'v04-fin', null, 120);
    step('horloger', 'v09'); step('horloger', 'v09-pieces'); step('horloge', 'v09-horloge', null, 120);
    step('usurier', 'v07'); step('toll', 'v07-toll'); step('kess', 'h04'); step('usurier', 'v07-fin', null, 80, () => g.cr += 500);
    walk(P.anneau); talk('c02'); talk('c02-course'); race(1, 150, 300); walk(P.marche); shop();
  }
  step('entrepot', 'entrepot', 'PATROUILLE');
  step('egouts_e', 'egouts-entree'); g.pos = P.galerie.slice();
  if (A) { step('rodeur', 'h04-rodeur', 'RÔDEUR'); }
  step('sous_brasier', 'sous-brasier', 'FOUISSEUR');
  step('sortie', 'egouts', null, 300);
  if (A) { step('kess', 'h04-prime', null, 150, () => g.cr += 500); shop(); }
  step('camp', 'camp', 'TRAQUEUSE', 0, () => g.team = [0, 1, 2, 3]);
  step('ruines', 'ruines');
  step('ancienne', 'ancienne');
  if (A) { walk(P.anneau); talk('c05'); talk('c05-course'); race(1, 250, 0); walk(P.odric); shop(); }
  for (let k = 0; k < (opt.grindTo ? 400 : 0) && g.L < opt.grindTo; k++) walk([g.pos[0] + (k % 2 ? 20 : -20), g.pos[1]]);   // farm sur place
  const Lavant = g.L;
  step('arche', 'arche', 'GARDIENNE'); g.pos = P.porte.slice();
  step(null, 'siege', 'GRAND INQUISITEUR');
  step('brasier', 'brasier', 'MALDEN');
  if (A) { step('odric', 'odric-aveu'); step('odric', 'odric-fin'); }
  return { ...g, Lavant };
}
module.exports = { run };
if (require.main === module) {
  const cfgs = [
    ['A. Jeu exporté, histoire seule (pas de rencontres)', { annexes: false }],
    ['B. Jeu exporté, histoire + annexes + courses', { annexes: true }],
    ['C. Histoire seule, rencontres tous les 60 (défaut de l\'Atelier), bibliothèque actuelle', { annexes: false, renc: 60, lib: true }],
    ['D. Tout, rencontres à 60, bibliothèque actuelle', { annexes: true, renc: 60, lib: true }],
  ];
  for (const [lab, o] of cfgs) {
    const R = []; for (let i = 0; i < 40; i++) R.push(run(o));
    const m = f => R.reduce((a, r) => a + f(r), 0) / R.length, r0 = R[0];
    console.log(`\n== ${lab}`);
    r0.log.slice(0, 30).forEach(l => console.log('  ' + l));
    console.log(`  moyenne sur 40 parties : ${(m(r => r.t) / 60).toFixed(0)} min (marche ${(m(r => r.t_walk) / 60).toFixed(0)}, combats ${(m(r => r.t_fight) / 60).toFixed(0)}, dialogues ${(m(r => r.t_talk) / 60).toFixed(0)}, courses ${(m(r => r.t_race) / 60).toFixed(0)}) · ${m(r => r.marche).toFixed(0)} cases · ${m(r => r.fights).toFixed(1)} combats dont ${m(r => r.renc).toFixed(1)} rencontres · ${m(r => r.defaites).toFixed(1)} défaites · niveau final ${m(r => r.L).toFixed(1)} · avant l'Arche ${m(r => r.Lavant).toFixed(1)}`);
  }
}
