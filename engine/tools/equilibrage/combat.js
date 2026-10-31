// Simulateur d'équilibrage (combats de engine/core/battle.c, parties complètes sur les vraies positions) : node seuils.js | analyse.js | progression.js | balayage.js
// Simulation des combats, fidèle à engine/core/battle.c (mêmes formules, même timeline CTB, même IA ennemie)
const fs = require('fs');
const { parse } = require('./pak.js');
const W = parse('D:/Documents/ESP32/engine/data/monde.pak');

// PARTY de hud.c (valeurs du niveau 20) ; techniques : nom, PM, soin, mult, retard, effet propre
const BASE = [
  { name: 'SERA', hp: 3621, mp: 310, atk: 430, spd: 12, techs: [{ n: 'TIR LASER', mp: 40, mult: 1.9 }, { n: 'PIRATAGE', mp: 60, mult: 0.5, delay: 1.6 }] },
  { name: 'VARN', hp: 4287, mp: 210, atk: 540, spd: 9, techs: [{ n: 'LAME-FOUDRE', mp: 30, mult: 2.1, sort: true }] },
  { name: 'BORS', hp: 4980, mp: 180, atk: 500, spd: 7, techs: [{ n: 'OBUS', mp: 25, mult: 2.4 }] },
  { name: 'LYSE', hp: 2963, mp: 420, atk: 380, spd: 11, techs: [{ n: 'SOIN', mp: 35, heal: 1400 }, { n: 'FLÈCHE-PLASMA', mp: 30, mult: 1.8 }] },
];
const AFF_F = [0, 0.7, 0.85, 1, 1.2];
const exp_next = L => Math.round(40 * Math.pow(L, 1.7));
const exp_of = l => Math.round(10 * Math.pow(l, 1.45) + 10);
const credits_of = l => Math.round(5 * Math.pow(l, 1.3) + 10);

// Équipement : { af } « type » (arme +40 ATQ, armure +20 DÉF, × af), ou { stats: [membre] -> { atq, def, pv, pm, vit } }
// (bonus déjà pondérés par l'affinité du job : equipement.js, paliers de js/paliers.js de l'Atelier)
function member(k, L, equip) {
  const b = BASE[k], f = (L + 6) / 26, j = W.jobs[W.membres[k].job];
  let hp = b.hp * f, mp = b.mp * f, atk = b.atk * f, def = 0, spd = b.spd;
  hp *= 1 + j.pv / 100; mp *= 1 + j.pm / 100; atk *= 1 + j.atq / 100; spd += j.vit;
  if (equip && equip.stats) { const e = equip.stats[k] || {}; atk += e.atq || 0; def += e.def || 0; hp += e.pv || 0; mp += e.pm || 0; spd += e.vit || 0; }
  else if (equip) { atk += 40 * (equip.af || 1); def += 20 * (equip.af || 1); }
  const m = { k, name: b.name, max: Math.round(hp), maxmp: Math.round(mp), atk: Math.round(atk), def: Math.round(def), spd: Math.max(1, spd) };
  m.techs = b.techs.map(t => ({ ...t, heal: t.heal ? Math.round(t.heal * f) : 0 }));
  return m;
}

let seed = 0x2545F491;
function rand() { seed ^= seed << 13; seed >>>= 0; seed ^= seed >>> 17; seed ^= seed << 5; seed >>>= 0; return (seed >>> 8) / 16777216; }
const rnd = (a, b) => a + rand() * (b - a);

const SORT_FIN = fx => { const el = Math.floor((fx - 10) / 3), n = (fx - 10) % 3 + 1; const fin = [1.5, 1.7, 1.45, 1.65, 1.7, 1.6, 1.5, 1.5, 1.5, 1.5, 1.65, 1.35][el]; return fin * (n === 1 ? 0.8 : n === 2 ? 1 : 1.2); };

/*
 * Un combat. team : index des membres (0 SERA … 3 LYSE) ; en : fiche (hp, atk, spd, sorts [{mult, fx}], magie) ;
 * inv : { potions } (consommées) ; style : 'bon' (soins à 40 %, meilleures techniques) ou 'simple' (ATTAQUE, potion à 30 %).
 * Rend { win, t (secondes), tours, pot }.
 */
function battle(team, L, en, inv, style = 'bon', equip = null) {
  const P = team.map(k => member(k, L, equip));
  P.forEach(m => { m.hp = m.max; m.mp = m.maxmp; m.ct = 100 / m.spd * (0.3 + rand() * 0.7); m.def0 = m.def; });
  const TP = [1, 0.75, 0.8, 0.9, 1], TA = [1, 0.65, 0.75, 0.85, 1];   // petite équipe (battle_start de battle.c)
  const f = (P.length + 1) / 5, E = { hp: Math.max(1, Math.round(en.hp * f * TP[P.length])), atk: Math.round(en.atk * f * TA[P.length]), spd: en.spd || 1 };
  E.ct = 100 / E.spd * (0.3 + rand() * 0.7);
  let t = 6, tours = 0, pot0 = inv.potions;
  const lyse = P.find(m => m.k === 3);
  for (let guard = 0; guard < 4000; guard++) {
    const alive = P.filter(m => m.hp > 0);
    if (E.hp <= 0) return { win: true, t, tours, pot: pot0 - inv.potions };
    if (!alive.length) return { win: false, t, tours, pot: pot0 - inv.potions };
    let a = E; for (const m of alive) if (m.ct < a.ct) a = m;   // égalité : l'équipe d'abord (ordre de PARTY), comme battle_timeline
    if (alive.some(m => m.ct === E.ct) && a === E) a = alive.find(m => m.ct === E.ct);
    if (a === E) {
      tours++;
      const ls = (en.sorts || []);
      if (ls.length && rand() * 100 < (en.magie || 0)) {
        const sp = ls[Math.floor(rand() * ls.length) % ls.length], tous = (sp.fx - 10) % 3 === 2;
        const tg = tous ? alive : [alive[Math.floor(rand() * alive.length) % alive.length]];
        for (const m of tg) { let d = Math.round(E.atk * sp.mult * (tous ? 0.6 : 1) * rnd(0.9, 1.1) * 200 / (200 + m.def)); if (m.defending) d = Math.round(d / 2); m.hp = Math.max(0, m.hp - d); }
        t += 0.8 + SORT_FIN(sp.fx);
      } else {
        const all = rand() < 0.3, tg = all ? alive : [alive[Math.floor(rand() * alive.length) % alive.length]];
        for (const m of tg) { const fa = all ? rnd(0.6, 0.8) : rnd(1.2, 1.6); let d = Math.round(E.atk * fa * 200 / (200 + m.def)); if (m.defending) d = Math.round(d / 2); m.hp = Math.max(0, m.hp - d); }
        t += 0.8 + 1.05;
      }
      E.ct += 100 / E.spd;
      continue;
    }
    // Tour d'un membre : choix du joueur
    a.defending = false;
    const weak = alive.reduce((w, m) => m.hp / m.max < w.hp / w.max ? m : w);
    const seuil = style === 'bon' ? 0.4 : 0.3;
    let act = null;
    if (weak.hp / weak.max < seuil) {
      const soin = a.techs.find(x => x.heal && a.mp >= x.mp);
      if (soin && style === 'bon') act = { heal: soin.heal, mp: soin.mp, think: 2.2 };
      else if (inv.potions > 0) { inv.potions--; act = { heal: 1500, think: 2.2 }; }
    }
    if (!act) {
      let best = { mult: 1, mp: 0, think: 1.6 };
      if (style === 'bon') for (const x of a.techs) {
        if (!x.mult || x.delay) continue;
        const reserve = a === lyse && P.length > 1 ? 35 : 0;
        if (a.mp - x.mp >= reserve && x.mult > best.mult) best = { mult: x.mult, mp: x.mp, think: 2.2, sort: x.sort };
      }
      act = best;
    }
    a.mp -= act.mp || 0;
    if (act.heal) { const h = Math.min(act.heal, weak.max - weak.hp); weak.hp += h; t += act.think + 1.05; }
    else {
      const crit = rand() < 0.1;
      E.hp = Math.max(0, E.hp - Math.round(a.atk * act.mult * rnd(0.9, 1.1) * (crit ? 1.6 : 1)));
      t += act.think + (act.sort ? 1.5 : 1.05);
    }
    a.ct += 100 / a.spd;
  }
  return { win: false, t, tours, pot: pot0 - inv.potions };
}

// Fiche d'ennemi : celles du paquet, complétées par la bibliothèque (nouveaux ennemis et sorts des boss)
const K = 'D:/Documents/Stellar Exodus/tileset-generator-v4/projets/principal/bases/AtelierEchoesV4/kv/';
const LIB = JSON.parse(fs.readFileSync(K + 'personnages.json', 'utf8'))[1].filter(p => p.role === 'ennemi' && p.ennemi);
const SORTS = {}; W.sorts.forEach((s, i) => { const id = ['foudre', 'brasier', 'givre', 'sacre', 'meteore', 'neant', 'poison', 'terre', 'eau', 'vent', 'neutre', 'soin'][Math.floor(i / 3)] + (i % 3 + 1); SORTS[id] = s; });
const ENNEMIS = {};
for (const p of LIB) { const e = p.ennemi; ENNEMIS[p.nom] = { nom: p.nom, lvl: e.lvl, hp: e.hp, atk: e.atk, spd: e.spd, poids: e.poids || 1, zones: e.zones || [], coeur: !!e.coeur, magie: e.magie || 0, sorts: (e.sorts || []).map(id => SORTS[id]).filter(Boolean) }; }
for (const e of W.enemies) if (!ENNEMIS[e.nom]) ENNEMIS[e.nom] = { ...e, sorts: [] };

// Probabilité de victoire (n combats) avec un stock de potions donné
function winRate(team, L, nom, potions, n = 400, style = 'bon', equip = null) {
  const en = ENNEMIS[nom]; let w = 0, t = 0, pot = 0;
  for (let i = 0; i < n; i++) { const inv = { potions }; const r = battle(team, L, en, inv, style, equip); if (r.win) { w++; t += r.t; pot += r.pot; } }
  return { p: w / n, t: w ? t / w : 0, pot: w ? pot / w : 0 };
}
// Plus petit niveau où la victoire passe un seuil
function levelFor(team, nom, potions, seuil, style = 'bon', equip = null) {
  for (let L = 1; L <= 99; L++) if (winRate(team, L, nom, potions, 300, style, equip).p >= seuil) return L;
  return '>99';
}
module.exports = { W, BASE, member, battle, winRate, levelFor, ENNEMIS, exp_next, exp_of, credits_of, rand };
