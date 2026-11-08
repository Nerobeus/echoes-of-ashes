// Simulateur d'équilibrage (combats de engine/core/battle.c) : node audit.js | seuils.js | progression.js | groupes.js | equipement.js
// Simulation des combats, fidèle à engine/core/battle.c (mêmes formules, même timeline CTB, même IA ennemie, groupes de 1 à 4
// ennemis, MAG et sorts des jobs et de l'équipement, actions de zone, transformation ÉVEIL et attaque ULTIME)
const fs = require('fs');
const { parse } = require('./pak.js');
const W = parse(process.env.PAK || 'D:/Documents/ESP32/engine/data/monde.pak');
const ATELIER = 'D:/Documents/Stellar Exodus/tileset-generator-v4';

// PARTY de hud.c (valeurs du niveau 20 ; même table dans l'émulateur) ; techniques : nom, PM, soin, mult, retard, effet propre
// (fx : FX_* de internal.h, sorts FX_SORT(élément, niveau) = 10 + 3 × élément + niveau − 1) ; une technique frappe toujours avec
// l'ATQ (bt_act_t.sort = 0 : seul un sort du menu MAGIE frappe avec le MAG). 4 à 9 : les recrues (01/11/2026) ; job : celui du
// départ quand le paquet n'a pas ce membre (DEFAUT_MEMBRES de js/equipement.js)
const BASE = [
  { name: 'SERA', hp: 3621, mp: 310, atk: 430, spd: 12, techs: [{ n: 'TIR LASER', mp: 40, mult: 1.9 }, { n: 'PIRATAGE', mp: 60, mult: 0.5, delay: 1.6 }] },
  { name: 'VARN', hp: 4287, mp: 210, atk: 540, spd: 9, techs: [{ n: 'LAME-FOUDRE', mp: 30, mult: 2.1, fx: 11 }] },
  { name: 'BORS', hp: 4980, mp: 180, atk: 500, spd: 7, techs: [{ n: 'OBUS', mp: 25, mult: 2.4 }] },
  { name: 'LYSE', hp: 2963, mp: 420, atk: 380, spd: 11, techs: [{ n: 'SOIN', mp: 35, heal: 1400 }, { n: 'FLÈCHE-PLASMA', mp: 30, mult: 1.8 }] },
  { name: 'JADE', job: 'COGNEUR', hp: 3600, mp: 260, atk: 440, spd: 12, techs: [{ n: 'JET DE TORCHES', mp: 30, mult: 2.0, fx: 14 }, { n: 'NUMÉRO DE FEU', mp: 45, mult: 0.5, delay: 1.5 }] },
  { name: 'SAULE', job: 'ARTILLEUR', hp: 4700, mp: 220, atk: 500, spd: 9, techs: [{ n: 'MORTIER', mp: 30, mult: 2.4, fx: 4 }, { n: 'RONCES', mp: 35, mult: 1.4, delay: 1.2, fx: 32 }] },
  { name: 'ÉTUDIANT', job: 'MAGICIEN', hp: 3400, mp: 460, atk: 400, spd: 11, techs: [{ n: 'SCEAU D\'ARCHIVE', mp: 35, mult: 1.9, fx: 41 }, { n: 'LECTURE', mp: 40, mult: 0.4, delay: 1.6 }] },
  { name: 'ONYX', job: 'TIREUR', hp: 3800, mp: 220, atk: 460, spd: 10, techs: [{ n: 'CARREAU PERÇANT', mp: 30, mult: 2.1, fx: 5 }, { n: 'TIR DE BARRAGE', mp: 40, mult: 0.6, delay: 1.4, fx: 3 }] },
  { name: 'AZUR', job: 'MAGICIEN', hp: 3400, mp: 440, atk: 410, spd: 11, techs: [{ n: 'ORAGE RETENU', mp: 40, mult: 2.2, fx: 11 }, { n: 'COURT-CIRCUIT', mp: 35, mult: 0.6, delay: 1.5, fx: 10 }] },
  { name: 'AMBRE', job: 'INQUISITEUR', hp: 4300, mp: 210, atk: 480, spd: 9, techs: [{ n: 'DÉFAUT DE CUIRASSE', mp: 30, mult: 2.2, fx: 1 }, { n: 'GERBE D\'ÉTINCELLES', mp: 40, mult: 1.2, delay: 1.3, fx: 13 }] },
];
const NOMS = BASE.map(b => b.name);
// Job du membre k : celui du paquet (JOBS), sinon celui de son départ par son nom (BASE[k].job) ; jobNom : un autre job
const JOB0 = { nom: '?', pv: 0, pm: 0, atq: 0, vit: 0, mag: 0, aff: [], sorts: 0n };
const jobDe = (k, jobNom) => (jobNom && W.jobs.find(j => j.nom === jobNom)) || (W.membres[k] ? W.jobs[W.membres[k].job] : W.jobs.find(j => j.nom === BASE[k].job)) || JOB0;
const AFF_F = [0, 0.7, 0.85, 1, 1.2];
// Niveau (battle.c) : expérience d'un ennemi ; d'un niveau au suivant, combatsNiveau(L) ennemis seuls de ce niveau (02/11/2026,
// consigne de l'utilisateur : 2 du niveau 1 au 2, 20 du 59 au 60, en ligne droite ; 15 + 0,1 L avant) ; crédits
const exp_of = l => Math.round(10 * Math.pow(l, 1.45) + 10);
const combatsNiveau = L => 2 + 18 * (L - 1) / 58;
const exp_next = L => Math.floor(((116 + 18 * (L - 1)) * exp_of(L) + 29) / 58);   // calcul entier de exp_next (battle.c)
const credits_of = l => Math.round(0.7 * (5 * Math.pow(l, 1.3) + 10));   // CR_X de battle.c (06/11/2026 : 30 % de moins)
// Combat de l'histoire (boss de battle_start) : expérience et crédits × BOSS_RW (BOSS_RW de internal.h) ; 12 depuis le 03/11/2026
// (demande de l'utilisateur : les combats de l'histoire donnent l'essentiel de l'expérience ; les crédits suivent, pour que
// l'équipement du moment reste à la portée d'un joueur qui ne fait que l'histoire) ; 4 avant (BOSS_RW=4 : pour comparer)
const NIV_MAX = 100, BOSS_RW = +(process.env.BOSS_RW || 12);

// Sorts (JOBS v2 du paquet) : nom, fx, soin (1 le plus blessé, 2 l'équipe), PM, puissance ; ids « foudre1 » … « soin3 »
const ELEM = ['foudre', 'brasier', 'givre', 'sacre', 'meteore', 'neant', 'poison', 'terre', 'eau', 'vent', 'neutre', 'soin'];
const SORTS = {}; W.sorts.forEach((s, i) => { s.i = i; s.niv = s.niv || 0; SORTS[ELEM[Math.floor(i / 3)] + (i % 3 + 1)] = s; });
// Sorts du catalogue de l'Atelier absents du paquet (02/11/2026 : CHANT DE RAPPEL, soin 3, appris au niveau 30), en attendant
// sa réexportation : ajoutés à la fin, avec leur niveau, et appris par les jobs qui les enseignent (DEFAUT_JOBS)
{
  global.TilesetGen = global.TilesetGen || {}; TilesetGen.Scenario = TilesetGen.Scenario || {}; TilesetGen.Scenario.MEMBERS = TilesetGen.Scenario.MEMBERS || [];
  TilesetGen.Scenario.norm = TilesetGen.Scenario.norm || (s => String(s).trim().toUpperCase().normalize('NFD').replace(/[\u0300-\u036f]/g, ''));
  const f = ATELIER + '/js/equipement.js'; delete require.cache[require.resolve(f)]; require(f);
  const E = TilesetGen.Equipement, FX = ['impact', 'taille', 'fouet', 'tir', 'obus', 'fleche', 'arcane', 'frappe', 'laser', 'balayage'];
  E.DEFAUT_MAGIES.forEach((m, i) => {
    if (i < W.sorts.length) { W.sorts[i].niv = m.niv || 0; return; }
    const el = ELEM.indexOf(m.effet.replace(/\d$/, '')), nv = +m.effet.slice(-1) || 1;
    const s = { nom: m.nom, fx: 10 + el * 3 + nv - 1, soin: m.soin, pm: m.pm, mult: m.puiss, niv: m.niv || 0, i: W.sorts.length, id: m.id };
    W.sorts.push(s); SORTS[m.id] = s;
    for (const j of E.DEFAUT_JOBS) if ((j.magies || []).includes(m.id)) { const wj = W.jobs.find(x => x.nom === j.nom); if (wj) wj.sorts = (wj.sorts || 0n) | 1n << BigInt(s.i); }
  });
}
const sortIndex = id => (SORTS[id] ? SORTS[id].i : -1);
// Progression des sorts par job (03/11/2026 : js/equipement.js, NIV_JOBS ; section JOBS version 3, que le paquet n'a qu'après
// une réexportation) : niveau où chaque job enseigne chaque sort (nivs, index du sort -> niveau), les sorts de sa table ajoutés
// au job ; sinon le niveau du sort. MAGIE.jobs faux (ou MAGIE=0) : la règle d'avant, pour comparer (audit.js magie)
const MAGIE = { jobs: process.env.MAGIE !== '0' };
{
  const E = TilesetGen.Equipement;
  // PM et puissance du catalogue de l'Atelier (MÉGA SOIN et GIGA SOIN changés le 03/11/2026) : ceux des sorts du joueur avec la
  // règle nouvelle ; les ennemis gardent ceux du paquet (ils ne lancent pas de soins)
  E.DEFAUT_MAGIES.forEach((m, i) => { if (i < W.sorts.length) { W.sorts[i].multN = m.puiss; W.sorts[i].pmN = m.pm; } });
  for (const wj of W.jobs) {
    wj.sorts0 = wj.sorts || 0n; wj.nivs0 = wj.nivs || null; wj.sortsN = wj.sorts || 0n; wj.nivsN = Object.assign({}, wj.nivs || {});
    const d = E.DEFAUT_JOBS.find(j => j.nom === wj.nom);
    if (d && d.niv) for (const [id, nv] of Object.entries(d.niv)) { const s = SORTS[id]; if (s) { wj.sortsN |= 1n << BigInt(s.i); wj.nivsN[s.i] = nv; } }
  }
}
const jobSorts = j => (MAGIE.jobs ? j.sortsN || j.sorts || 0n : j.sorts0 !== undefined ? j.sorts0 : j.sorts || 0n);
const sortNivJob = (j, s) => { const t = MAGIE.jobs ? j.nivsN : j.nivs0; return t && t[s.i] !== undefined ? t[s.i] : (s.niv || 0); };
// Fin d'une action (fx_fin de battle.c) : sort ou technique à l'effet d'un sort, MAGIE[élément].fin × MG_TS(niveau) ; sinon 1,05 s
const MG_FIN = [1.5, 1.7, 1.45, 1.65, 1.7, 1.6, 1.5, 1.5, 1.5, 1.5, 1.65, 1.35];
const isSort = fx => fx >= 10 && fx < 46;
const fxNiv = fx => (fx - 10) % 3 + 1, fxElem = fx => Math.floor((fx - 10) / 3);
const fxFin = fx => (isSort(fx) ? MG_FIN[fxElem(fx)] * (fxNiv(fx) === 1 ? 0.8 : fxNiv(fx) === 2 ? 1 : 1.2) : 1.05);
const SORT_FIN = fxFin;
const CUT_S = 1.2, CUT_S_PLEIN = 1.6, CUT_S_TRANSFO = 3.0, CUT_S_ULTIME = 2.8, ULTIME_PM = 80;

// Membre k au niveau L (party_set_level, party_refresh) : PV × (L + 6) / 26, ATQ et MAG × (7 + GAIN_ATQ (L − 1)) / 26, DÉF
// GAIN_DEF (L − 1) (06/11/2026, internal.h ; GAIN_ATQ=1 GAIN_DEF=0 : ATQ d'avant à une constante près), job (PV, PM, ATQ, MAG en %, VIT), puis
// l'équipement selon l'affinité. equip : { stats: [membre] -> { atq, def, pv, pm, mag, vit, sorts: [index] } } (bonus déjà
// pondérés par l'affinité : equipement.js), ou { af } (ancien « type » : arme +40 ATQ, armure +20 DÉF) ; jobs : { membre: nom }
// SANTE : règles de la santé et des PM (02/11/2026) : pmNiveau (la réserve de PM suit le niveau, × (L + 6) / 26 ; faux depuis le
// 02/11/2026 : party_set_level de battle.c ne la change plus, une rencontre en prend environ un quart à tous les niveaux) ; coutTech,
// coutSort : facteurs des coûts en PM (techniques de PARTY, sorts de JOBS) pour les essais de l'audit
const GAIN_ATQ = +(process.env.GAIN_ATQ || 1.5), GAIN_DEF = +(process.env.GAIN_DEF ?? 3);
const SANTE = { pmNiveau: false, coutTech: 1, coutSort: 1 };   // 02/11/2026 : réserve de PM fixe (valeurs de PARTY, job, équipement)
// appris : index des sorts que le membre a appris pour de bon (cristaux du Chœur : E.appris du moteur)
function member(k, L, equip, jobs, appris) {
  const b = BASE[k], f = (L + 6) / 26, j = jobDe(k, jobs && jobs[k]);
  const latk = b.atk * (7 + GAIN_ATQ * (L - 1)) / 26;
  let hp = b.hp * f, mp = b.mp * (SANTE.pmNiveau ? f : 1), atk = latk, mag = latk, def = GAIN_DEF * (L - 1), spd = b.spd;
  hp *= 1 + j.pv / 100; mp *= 1 + j.pm / 100; atk *= 1 + j.atq / 100; mag *= 1 + (j.mag || 0) / 100; spd += j.vit;
  let bits = 0n;   // équipement et sorts appris (au niveau du sort) ; ceux du job : au niveau où il les enseigne
  for (const s of appris || []) if (s >= 0) bits |= 1n << BigInt(s);
  if (equip && equip.stats) {
    const e = equip.stats[k] || {};
    atk += e.atq || 0; def += e.def || 0; hp += e.pv || 0; mp += e.pm || 0; mag += e.mag || 0; spd += e.vit || 0;
    for (const s of e.sorts || []) if (s >= 0) bits |= 1n << BigInt(s);
  } else if (equip) { atk += 40 * (equip.af || 1); def += 20 * (equip.af || 1); }
  const m = { k, name: b.name, job: j.nom, max: Math.max(1, Math.round(hp)), maxmp: Math.max(0, Math.round(mp)), atk: Math.max(1, Math.round(atk)),
    mag: Math.max(1, Math.round(mag)), def: Math.max(0, Math.round(def)), spd: Math.max(1, Math.round(spd)) };
  m.techs = b.techs.map(t => ({ ...t, mp: Math.max(1, Math.round(t.mp * SANTE.coutTech)), heal: t.heal ? Math.round(t.heal * f) : 0 }));
  const jb = jobSorts(j);
  m.sorts = W.sorts.filter(s => (((jb >> BigInt(s.i)) & 1n) && sortNivJob(j, s) <= L) || (((bits >> BigInt(s.i)) & 1n) && (s.niv || 0) <= L))
    .map(s => MAGIE.jobs && s.multN !== undefined && (s.multN !== s.mult || s.pmN !== s.pm) ? { ...s, mult: s.multN, pm: s.pmN } : s)
    .map(s => SANTE.coutSort === 1 ? s : { ...s, pm: Math.max(1, Math.round(s.pm * SANTE.coutSort)) });
  return m;
}

let seed = 0x2545F491;
function rand() { seed ^= seed << 13; seed >>>= 0; seed ^= seed >>> 17; seed ^= seed << 5; seed >>>= 0; return (seed >>> 8) / 16777216; }
const rnd = (a, b) => a + rand() * (b - a);

/*
 * Groupes (battle_start_groupe de engine/core/battle.c, startBattle de l'émulateur) : 1 à 4 ennemis. Parts égales : chacun
 * garde sa fiche (au niveau du combat, réglée sur la taille de l'équipe), PV × GROUPE_PV[n], ATQ × GROUPE_ATQ[n] ; boss et
 * sbires ('sbires') : un sbire a SBIRE_PART des PV et de l'ATQ du boss, le boss lui cède ses PV (il en garde BOSS_GARDE au
 * moins) et BOSS_CEDE de son ATQ (moins : les sbires tombent vite, leurs coups avec eux) ;
 * récompense × GROUPE_XP[n]. Tables étalonnées par groupes.js ; action de zone (sort de niveau III, ULTIME) : tout le groupe,
 * × ZONE_F à plusieurs. L'équipe frappe l'ennemi visé : le plus faible (le moins de PV), comme la cible par défaut du jeu.
 */
const GROUPE_PV = [1, 1, 0.5, 0.34, 0.26], GROUPE_ATQ = [1, 1, 0.63, 0.43, 0.32], GROUPE_XP = [1, 1, 1.1, 1.25, 1.35];
// R_.ZONE_SEUL : sort d'attaque de niveau III contre un seul ennemi (03/11/2026, battle.c : ZONE_SEUL)
const SBIRE_PART = 0.15, BOSS_GARDE = 0.55, ZONE_F = 0.6, R_ = { BOSS_CEDE: 0.04, ZONE_SEUL: 1 };
const TAILLE_PV = [1, 0.75, 1.05, 1, 1], TAILLE_ATQ = [1, 0.65, 1, 0.95, 1];
// Ennemis du combat (fiches au niveau du combat) : PV, ATQ, VIT, sorts, part de la récompense
function groupe(ens, nTeam, mode) {
  const f = (nTeam + 1) / 5, n = ens.length;   // petite équipe (battle_start de battle.c)
  const G = ens.map(en => ({ nom: en.nom, hp: Math.round(Math.round(en.hp) * f * TAILLE_PV[nTeam]), atk: Math.round(Math.round(en.atk) * f * TAILLE_ATQ[nTeam]),
    spd: en.spd || 1, sorts: (en.sorts || []).filter(s => !s.soin), magie: en.magie || 0, attaques: en.attaques || [], part: 1 }));
  if (n > 1 && mode === 'sbires') {
    const hb = G[0].hp, ab = G[0].atk, ns = n - 1;
    for (let k = 1; k < n; k++) { G[k].hp = Math.round(hb * SBIRE_PART); G[k].atk = Math.round(ab * SBIRE_PART); G[k].sbire = 1; }
    G[0].hp = Math.round(hb * Math.max(BOSS_GARDE, 1 - SBIRE_PART * ns)); G[0].atk = Math.round(ab * Math.max(BOSS_GARDE, 1 - R_.BOSS_CEDE * ns));
  } else if (n > 1) G.forEach(e => { e.hp = Math.round(e.hp * GROUPE_PV[n]); e.atk = Math.round(e.atk * GROUPE_ATQ[n]); });
  G.forEach(e => { e.hp = Math.max(1, e.hp); e.max = e.hp; });
  const tot = G.reduce((a, e) => a + e.hp, 0);
  if (n > 1) G.forEach(e => { e.part = GROUPE_XP[n] * e.hp / tot; });
  return G;
}
// Récompense d'une victoire : part du groupe (somme des parts : GROUPE_XP[n], 1 seul)
const partGroupe = n => GROUPE_XP[Math.max(1, Math.min(4, n))];
// Taille d'un groupe de rencontre (encounter_taille de game.c) : seul sous le niveau d'histoire GROUPE_NIV (8), avec Sera
// seule ou pour une élite ; deux avec la chance min(0,5, (h - 8) / 40), trois (trois membres) pour min(0,5, (h - 20) / 50)
// de ces groupes
const ELITE_PV = 600, G_ = { NIV: 8, PENTE: 40, P2: 0.5, NIV3: 20, PENTE3: 50, P3: 0.5 };
const elite = e => !!(e.sorts || []).length || e.hp > ELITE_PV * (e.lvl + 6);
function tailleGroupe(h, membres, e) {
  if (h < G_.NIV || membres < 2 || elite(e) || e.groupe === 1) return 1;
  let n = rand() < Math.min(G_.P2, (h - G_.NIV) / G_.PENTE) ? 2 : 1;
  if (n === 2 && membres >= 3 && rand() < Math.min(G_.P3, (h - G_.NIV3) / G_.PENTE3)) n = 3;
  return e.groupe > 1 && n > e.groupe ? e.groupe : n;
}

/*
 * Un combat. team : index des membres (0 SERA … 3 LYSE, 4 JADE … 8 AZUR, 9 AMBRE ; quatre au plus, Sera d'abord) ; en : fiche
 * (hp, atk, spd, sorts [{mult, fx}], magie), ou un tableau de fiches (groupe ; mode 'parts' ou 'sbires') ;
 * inv : { potions } (consommées : POTION, 1 500 PV, la seule utilisable en combat, item_potion) ;
 * style : 'bon' (soins à 40 %, meilleures techniques et meilleurs sorts) ou 'simple' (ATTAQUE, potion à 30 %) ;
 * opt : { jobs: { membre: job }, eveil: [membres transformables] (ÉVEIL dès leur premier tour, puis ULTIME s'il vaut mieux) }.
 * Rend { win, t (secondes), tours, pot, perte (part des PV de l'équipe perdus à la fin), ko (membres à terre), n (ennemis) }.
 */
function battle(team, L, en, inv, style = 'bon', equip = null, mode = 'parts', opt = {}) {
  const ens = Array.isArray(en) ? en.slice(0, 4) : [en];
  const P = team.map(k => member(k, L, equip, opt.jobs, opt.appris && opt.appris[k]));
  // état d'entrée (opt.etat : { hp: { membre: part des PV max }, mp: { membre: part des PM max } } ; absent : équipe pleine) ;
  // un membre à terre (0) entre au combat à terre et ne joue pas ; toute l'équipe à terre : défaite (battle_start, 02/11/2026)
  const et = opt.etat;
  P.forEach(m => { m.hp = et && et.hp[m.k] !== undefined ? (et.hp[m.k] > 0 ? Math.max(1, Math.round(m.max * et.hp[m.k])) : 0) : m.max; m.mp = et && et.mp[m.k] !== undefined ? Math.round(m.maxmp * et.mp[m.k]) : m.maxmp;
    m.ct = 100 / m.spd * (0.3 + rand() * 0.7); m.transfo = false; m.eveil = !!(opt.eveil && opt.eveil.includes(m.k)); });
  const G = groupe(ens, P.length, mode);
  G.forEach(e => { e.ct = 100 / e.spd * (0.3 + rand() * 0.7); });
  let t = 2.2, tours = 0, pot0 = inv.potions, cible = -1, usePm = 0, releves = 0;   // INTRO_S ; usePm : PM dépensés
  const fin = win => ({ win, t, tours, pot: pot0 - inv.potions, n: G.length, ko: P.filter(m => m.hp <= 0).length,
    perte: 1 - P.reduce((a, m) => a + m.hp, 0) / P.reduce((a, m) => a + m.max, 0),
    etat: { hp: Object.fromEntries(P.map(m => [m.k, m.hp / m.max])), mp: Object.fromEntries(P.map(m => [m.k, m.maxmp ? m.mp / m.maxmp : 1])) },
    pm: P.reduce((a, m) => a + (m.maxmp ? 1 - m.mp / m.maxmp : 0), 0) / P.length, usePm, releves });
  // ennemi visé : gardé tant qu'il est debout, sinon le plus faible (cible_maj de battle.c)
  const viser = () => { if (cible >= 0 && G[cible].hp > 0) return; cible = -1; G.forEach((e, k) => { if (e.hp > 0 && (cible < 0 || e.hp < G[cible].hp)) cible = k; }); };
  const soigneur = m => m.techs.some(x => x.heal) || m.sorts.some(s => s.soin);
  for (let guard = 0; guard < 6000; guard++) {
    const alive = P.filter(m => m.hp > 0), debout = G.filter(e => e.hp > 0);
    if (!debout.length) return fin(true);
    if (!alive.length) return fin(false);
    // la plus petite valeur joue ; égalité : l'équipe d'abord, puis les ennemis dans leur ordre (battle_timeline)
    let a = null; for (const u of alive.concat(debout)) if (!a || u.ct < a.ct) a = u;
    if (G.includes(a)) {   // enemy_act, apply_enemy_hit
      tours++;
      const ls = a.sorts;
      if (ls.length && rand() * 100 < a.magie) {
        const sp = ls[Math.floor(rand() * ls.length) % ls.length], tous = fxNiv(sp.fx) === 3;
        const tg = tous ? alive : [alive[Math.floor(rand() * alive.length) % alive.length]];
        for (const m of tg) { let d = Math.round(a.atk * sp.mult * (tous ? 0.6 : 1) * rnd(0.9, 1.1) * 200 / (200 + m.def)); if (m.defending) d = Math.round(d / 2); if (m.transfo) d = Math.round(d * 0.7); m.hp = Math.max(0, m.hp - d); }
        t += 0.8 + fxFin(sp.fx);
      } else if (a.attaques.length) {   // une attaque du répertoire (ENAT), tirée au poids : ATQ × puissance × 6/7 à 8/7
        const at = a.attaques, tot = at.reduce((s, x) => s + x.poids, 0);
        let r = rand() * tot, x = at[at.length - 1];
        for (const y of at) { r -= y.poids; if (r < 0) { x = y; break; } }
        const tg = x.tous ? alive : [alive[Math.floor(rand() * alive.length) % alive.length]];
        for (const m of tg) { let d = Math.round(a.atk * x.puissance * rnd(6 / 7, 8 / 7) * 200 / (200 + m.def)); if (m.defending) d = Math.round(d / 2); if (m.transfo) d = Math.round(d * 0.7); m.hp = Math.max(0, m.hp - d); }
        t += 0.8 + fxFin(x.fx);
      } else {
        const all = rand() < 0.3, tg = all ? alive : [alive[Math.floor(rand() * alive.length) % alive.length]];
        for (const m of tg) { const fa = all ? rnd(0.6, 0.8) : rnd(1.2, 1.6); let d = Math.round(a.atk * fa * 200 / (200 + m.def)); if (m.defending) d = Math.round(d / 2); if (m.transfo) d = Math.round(d * 0.7); m.hp = Math.max(0, m.hp - d); }
        t += 0.8 + 1.05;
      }
      a.ct += 100 / a.spd;
      continue;
    }
    // Tour d'un membre : choix du joueur
    a.defending = false;
    // Relever (02/11/2026, soin 3 : CHANT DE RAPPEL) : un allié à terre, d'abord, si le membre a le sort et les PM ; il revient
    // avec MAG × puissance PV et reprend sa place dans la frise, un de ses tours après celui qui agit (apply_hit de battle.c)
    const ko = P.find(m => m.hp <= 0), rs = ko && style === 'bon' && a.sorts.find(s => s.soin === 3 && a.mp >= s.pm);
    if (rs) {
      a.mp -= rs.pm; usePm += rs.pm; ko.hp = Math.min(ko.max, Math.round(a.mag * rs.mult)); ko.ct = a.ct + 100 / ko.spd; releves++;
      t += 2.2 + fxFin(rs.fx); a.ct += 100 / a.spd; continue;
    }
    // ÉVEIL : la transformation d'abord (son cut-in), le reste du combat ATQ × 1,5 et dégâts reçus × 0,7
    if (style === 'bon' && a.eveil && !a.transfo && (debout.length > 1 || debout[0].hp > a.atk * 6)) {
      a.transfo = true; t += 1.6 + CUT_S_TRANSFO; a.ct += 100 / a.spd; continue;
    }
    const weak = alive.reduce((w, m) => m.hp / m.max < w.hp / w.max ? m : w);
    const seuil = style === 'bon' ? 0.4 : 0.3;
    let act = null;
    const k = a.transfo ? 1.5 : 1;   // transformée : ATQ (et MAG) × 1,5
    if (weak.hp / weak.max < seuil) {
      if (style === 'bon') {   // le meilleur soin : technique (SOIN de Lyse), sort de soin (MAG × puissance ; soin 2 : l'équipe)
        const blesses = alive.filter(m => m.hp / m.max < 0.6).length;
        let best = null, bv = 0;
        for (const x of a.techs) if (x.heal && a.mp >= x.mp) { const v = Math.min(x.heal, weak.max - weak.hp); if (v > bv) { bv = v; best = { heal: x.heal, mp: x.mp, think: 2.2, fin: 1.05 }; } }
        for (const s of a.sorts) if (s.soin && s.soin !== 3 && a.mp >= s.pm) {
          const h = Math.round(a.mag * s.mult), v = s.soin === 2 ? h * Math.max(1, blesses) * 0.8 : Math.min(h, weak.max - weak.hp);
          if (v > bv) { bv = v; best = { heal: h, mp: s.pm, think: 2.2, fin: fxFin(s.fx), team: s.soin === 2, cut: fxNiv(s.fx) === 3 ? CUT_S_PLEIN : 0 }; }
        }
        act = best;
      }
      // potion (1 500 PV) : le joueur avisé ne la boit que si elle rend au moins un quart des PV du membre (02/11/2026 : sans
      // cette règle, avec dix potions au sac il perdait ses tours à 15 % de PV rendus et le combat avec eux)
      if (!act && inv.potions > 0 && (style !== 'bon' || 1500 >= 0.25 * weak.max)) { inv.potions--; act = { heal: 1500, think: 2.2, fin: 1.05 }; }
    }
    if (!act) {
      viser();
      let best = { mult: 1, mp: 0, think: 1.6, fin: 1.05, val: Math.min(a.atk * k, G[cible].hp * 1.05), nom: 'ATTAQUE' };
      if (style === 'bon') {
        // réserve : un soigneur garde de quoi soigner (le soin le plus cher qu'il sait faire)
        const reserve = P.length > 1 && soigneur(a) ? Math.max(0, ...a.techs.filter(x => x.heal).map(x => x.mp), ...a.sorts.filter(s => s.soin).map(s => s.pm)) : 0;
        const nd = debout.length;
        const cand = [];
        for (const x of a.techs) if (x.mult && !x.delay) cand.push({ mult: x.mult, mp: x.mp, think: 2.2, fin: fxFin(x.fx || 0), base: a.atk, zone: false, cut: CUT_S, nom: x.n });
        for (const s of a.sorts) if (!s.soin) cand.push({ mult: s.mult, mp: s.pm, think: 2.2, fin: fxFin(s.fx), base: a.mag, zone: fxNiv(s.fx) === 3, sort: true, cut: fxNiv(s.fx) === 3 ? CUT_S_PLEIN : 0, nom: s.nom });
        if (a.transfo) cand.push({ mult: 6, mp: ULTIME_PM, think: 2.2, fin: fxFin(19 + 2), base: a.atk, zone: true, cut: CUT_S_ULTIME, nom: 'ULTIME' });
        // garde (opt.garde) : part de ses PM qu'un membre garde dans un combat ordinaire (pour les soins et les combats plus
        // durs) ; les sorts et techniques d'attaque ne la prennent pas
        const garde = opt.garde ? a.maxmp * opt.garde : 0;
        for (const c of cand) {
          if (a.mp - c.mp < Math.max(reserve, garde)) continue;
          // valeur : dégâts attendus (une action de zone : × ZONE_F × les ennemis debout s'ils sont plusieurs), sans dépasser les
          // PV de la cible (un sort ruineux pour achever un ennemi presque mort ne vaut pas mieux qu'une attaque)
          let v = c.base * c.mult * k * (c.zone && nd > 1 ? ZONE_F * nd : c.sort && c.zone ? R_.ZONE_SEUL : 1);
          viser(); const pv = c.zone ? debout.reduce((s, e) => s + e.hp, 0) : G[cible].hp;
          v = Math.min(v, pv * 1.05);
          if (v > best.val * 1.02 || (v >= best.val * 0.98 && c.mp < best.mp)) best = { ...c, val: v };
        }
      }
      act = best;
    }
    a.mp -= act.mp || 0; usePm += act.mp || 0;
    if (act.heal) {
      const tg = act.team ? alive : [weak];
      for (const m of tg) m.hp += Math.min(act.heal, m.max - m.hp);
      t += act.think + (act.cut || 0) + act.fin;
    } else {
      const crit = rand() < 0.1;
      viser();
      const tg = act.zone ? debout : [G[cible]];
      const base = act.base || a.atk;
      for (const e of tg) e.hp = Math.max(0, e.hp - Math.round(base * act.mult * rnd(0.9, 1.1) * (crit ? 1.6 : 1) * k * (tg.length > 1 ? ZONE_F : act.sort && act.zone ? R_.ZONE_SEUL : 1)));
      t += act.think + (act.cut || 0) + act.fin;
    }
    a.ct += 100 / a.spd;
  }
  return fin(false);
}

/*
 * Fiches d'ennemi : celles de la bibliothèque (kv personnages du projet), avec ce que « Charger l'histoire » y mettra
 * (js/histoire.js : EQUILIBRE, puis les fiches de PERSOS), complétées par le paquet. Sorts : objets du paquet ; sortsId : ids.
 * fiches(F) : remplace des fiches ({ nom: { lvl, hp, atk } }) pour un essai.
 */
const K = ATELIER + '/projets/' + (process.env.PROJET || 'principal') + '/bases/AtelierEchoesV4/kv/';
const TOUS = JSON.parse(fs.readFileSync(K + 'personnages.json', 'utf8'))[1];
const LIB = TOUS.filter(p => p.role === 'ennemi' && p.ennemi && !p.desactive);
// Attaques hors des sorts (03/11/2026, enemy_act de battle.c) : le répertoire du jeu de chaque ennemi, fait par js/attaques.js
// de l'Atelier comme à l'export (Personnages.ennemiExport : section ENAT) ; ATTAQUES=0 : la règle d'avant (rafale 70 %,
// balayage 30 %), pour comparer
{ const f = ATELIER + '/js/attaques.js'; delete require.cache[require.resolve(f)]; require(f); }
const ATQ_ON = process.env.ATTAQUES !== '0';
const attaquesDe = (nom, ennemi, modele) => {
  if (!ATQ_ON) return [];
  const p = LIB.find(q => q.nom === nom && !q.palette && !q.brouillon), m = modele && TOUS.find(q => q.nom === modele);
  return TilesetGen.Attaques.exporter(p || { nom, role: 'ennemi', ennemi, histoire: true, parent: m ? m.id : undefined }, TOUS);
};
function chargerHistoire() {
  global.TilesetGen = global.TilesetGen || {}; global.window = global; global.document = global.document || { getElementById: () => null };
  const f = process.env.HISTOIRE || ATELIER + '/js/histoire.js';
  delete require.cache[require.resolve(f)];
  require(f);
  return TilesetGen.Histoire;
}
const H = chargerHistoire();
const norm = s => String(s).trim().toUpperCase().normalize('NFD').replace(/[\u0300-\u036f]/g, '');
const ENNEMIS = {};
const ficheDe = (nom, e) => ({ nom, lvl: e.lvl, hp: e.hp, atk: e.atk, spd: e.spd || 9, poids: e.poids || 1, zones: e.zones || [], coeur: !!e.coeur, groupe: e.groupe || 0,
  magie: e.magie || 0, sortsId: (e.sorts || []).slice(), sorts: (e.sorts || []).map(id => SORTS[id]).filter(Boolean), attaques: e.attaques || [] });
for (const p of LIB) ENNEMIS[p.nom] = ficheDe(p.nom, { ...p.ennemi, attaques: attaquesDe(p.nom, p.ennemi) });
{
  const eq = {}; Object.entries(H.EQUILIBRE || {}).forEach(([k, v]) => eq[norm(k)] = v);
  const zt = {}; Object.entries(H.ZONES || {}).forEach(([k, v]) => zt[norm(k)] = v);
  for (const e of Object.values(ENNEMIS)) {
    const f = eq[norm(e.nom)], z = zt[norm(e.nom)];
    if (f) { e.lvl = f[0]; e.hp = f[1]; e.atk = f[2]; }
    if (f && f[3]) e.zones = []; else if (z) e.zones = e.zones.concat(z.filter(q => !e.zones.includes(q)));
    e.legende = !!(f && f[3]);
  }
  for (const d of H.PERSOS) if (d.ennemi) {
    const e = ENNEMIS[d.nom] ? Object.assign({ ...ENNEMIS[d.nom] }, ficheDe(d.nom, { ...ENNEMIS[d.nom], ...d.ennemi, sorts: d.ennemi.sorts || ENNEMIS[d.nom].sortsId, attaques: ENNEMIS[d.nom].attaques }))
      : ficheDe(d.nom, { ...d.ennemi, attaques: attaquesDe(d.nom, d.ennemi, d.modele) });
    e.histoire = true; ENNEMIS[d.nom] = e;
  }
}
for (const e of W.enemies) if (!ENNEMIS[e.nom]) ENNEMIS[e.nom] = { ...e, sorts: [], sortsId: [], attaques: attaquesDe(e.nom, e) };
function fiches(F) { for (const [nom, f] of Object.entries(F)) if (ENNEMIS[nom]) Object.assign(ENNEMIS[nom], { lvl: f.lvl, hp: f.hp, atk: f.atk }); }
// FICHES=chemin : les fiches d'un étalonnage (fiches.json de calibrer.js) à la place de celles de l'histoire, pour l'essai
if (process.env.FICHES) fiches(JSON.parse(fs.readFileSync(process.env.FICHES, 'utf8')));
// Fiche au niveau L (battle_start : PV et ATQ × (L + 6) / (niveau de la fiche + 6))
const auNiveau = (e, L) => ({ ...e, lvl: L, hp: Math.max(1, Math.round(e.hp * (L + 6) / (e.lvl + 6))), atk: Math.max(1, Math.round(e.atk * (L + 6) / (e.lvl + 6))) });

// Probabilité de victoire (n combats) avec un stock de potions donné ; nom : un ennemi, ou un groupe [noms] (mode)
function winRate(team, L, nom, potions, n = 400, style = 'bon', equip = null, mode = 'parts', opt = {}) {
  const ens = (Array.isArray(nom) ? nom : [nom]).map(x => typeof x === 'string' ? auNiveau(ENNEMIS[x], L) : x);
  let w = 0, t = 0, pot = 0, pe = 0, ko = 0;
  for (let i = 0; i < n; i++) { const inv = { potions }; const r = battle(team, L, ens.length > 1 ? ens : ens[0], inv, style, equip, mode, opt); if (r.win) { w++; t += r.t; pot += r.pot; pe += r.perte; ko += r.ko; } }
  return { p: w / n, t: w ? t / w : 0, pot: w ? pot / w : 0, perte: w ? pe / w : 1, ko: w ? ko / w : 0 };
}
// Plus petit niveau où la victoire passe un seuil
function levelFor(team, nom, potions, seuil, style = 'bon', equip = null) {
  for (let L = 1; L <= 99; L++) if (winRate(team, L, nom, potions, 300, style, typeof equip === 'function' ? equip(L) : equip).p >= seuil) return L;
  return '>99';
}
module.exports = { MAGIE, jobSorts, sortNivJob, W, H, BASE, NOMS, jobDe, member, battle, winRate, levelFor, ENNEMIS, fiches, auNiveau, exp_next, exp_of, credits_of, combatsNiveau, NIV_MAX, BOSS_RW, SANTE,
  rand, groupe, partGroupe, tailleGroupe, elite, G_, GROUPE_PV, GROUPE_ATQ, GROUPE_XP, SBIRE_PART, BOSS_GARDE, ZONE_F, R_, SORTS, sortIndex, ELEM, fxFin, SORT_FIN, norm, ATELIER, K };
