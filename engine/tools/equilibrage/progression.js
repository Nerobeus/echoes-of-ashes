// Partie simulée réaliste (02/11/2026 ; avant : 22 combats par heure répartis sur les heures du document). Une partie suit
// les étapes des quêtes principales dans l'ordre, à leurs vraies cibles (monde.js), fait les étapes des annexes à leur
// ouverture (ouverture.json : l'étape principale après laquelle chacune se fait, d'après les niveaux d'origine) et joue :
//   - le chemin d'une cible à la suivante : à pied, × DETOUR ; sur la planète, au-delà de VEH_SEUIL cases, une part VEH_PART
//     du reste en véhicule (sans rencontre) à partir de l'acte II ; téléport (lieux visités) quand il raccourcit le trajet ;
//     d'une carte à l'autre, les passages (puits, astroport, vol sur la carte du système solaire) ou le téléport ;
//   - les rencontres comme encounter_step de game.c (distance du paquet × RENC_FOIS du réglage NORMALES, au moins la moitié
//     de la moyenne ; terrain, enc_ok, taille du groupe, niveau min(histoire, L + 1) − 0..2), les patrouilles des zones
//     dangereuses ;
//   - les combats des événements de chaque étape (la branche que prend un joueur de référence : plan.js lignesJouees) au
//     niveau min(étape, équipe), récompense × BOSS_RW, recrues, objets, crédits ;
//   - la santé : PV et PM gardés d'un combat à l'autre (02/11/2026), membre à terre relevé à 1 PV après le combat ; soins hors
//     combat (sorts et techniques de soin, POTION), cliniques et hôpital quand le chemin passe à côté, verre au bar, retraite
//     par le téléport jusqu'au plus proche lieu de soins visité ; défaites : RÉESSAYER LE COMBAT (même état), puis
//     REPRENDRE (dernier point de sauvegarde, équipe soignée contre le prix de l'hôpital) ;
//   - achats (potions, puis l'équipement de la boutique du moment), butin, cristaux, coffres.
// Joueurs : 'prudent' (soigne après chaque combat sous 60 %, remet l'équipe à 95 % avant un combat de l'histoire, passe à
// l'hôpital en ville, se replie par le téléport) ou 'insouciant' (soigne sous 30 %, rien avant les boss, hôpital seulement
// quand la cible de l'étape y est).
//   node progression.js [audit] [parties]   : audit (profils, santé, économie)
//   node progression.js niveaux [parties]   : niveau réel de chaque étape (point fixe), écrit niveaux.json
const fs = require('fs');
const C = require('./combat.js'), PL = require('./plan.js'), M = require('./monde.js'), Q = require('./equipement.js');
const P = Q.P;
const OUV = JSON.parse(fs.readFileSync(__dirname + '/ouverture.json', 'utf8')).etapes;
const BOSS_RW_ANNEXE = +(process.env.BOSS_RW_ANNEXE || 4), RENC_XP = +(process.env.RENC_XP || 0.45), CRISTAL_RENC = +(process.env.CRISTAL_RENC || 0.5);

// Hypothèses du modèle (rapport du 02/11/2026)
const HYP = {
  VIT: 3.6,            // cases par seconde à pied (Sera court)
  DETOUR: 1.35,        // chemin réel à pied / ligne droite : rues, obstacles, détours aux boutiques
  VEH_SEUIL: 150, VEH_PART: 0.5, VEH_DES: 'acte2a', VEH_VIT: 10,   // long trajet sur la planète : la moitié de ce qui dépasse 150 cases en véhicule, dès l'acte II
  ECH: 1.3, RENC_FOIS: 1.5,                               // game.c : distance × ech ; NORMALES = distance du paquet × 0,75 (04/11/2026 ; × 1,5 le 03/11, × 3 avant)
  TELE_R: { 0: 0, 1: 25, 2: 8 },                           // lieux visités : monde à l'arrivée, ville à 25 cases, lieu à 8
  PRES: 20,            // un lieu de soins ou une boutique à moins de 20 cases du chemin : on s'y arrête
  CLIN_OUVERT: 0.65,   // cliniques (accès « horaires ») ouvertes le jour : 65 % du temps
  T_PARLE: 3, T_ANNEXE: 120, T_SOIN: 45, T_TELE: 20,       // secondes : réplique, annexe (dialogues, menus), soins, téléport
  POT_STOCK: 20, POT_PRIX: 40, POT_PV: 1500,               // potions gardées au sac, prix, PV rendus
  ETH_STOCK: 10, ETH_PRIX: 150, ETH_PM: 60,
  RAPPEL_STOCK: 3, RAPPEL_PRIX: 200, RELEVE_PART: 0.25,       // SERINGUE DE RAPPEL : gardées au sac (prudent ; insouciant 1), prix, PV rendus
  RES_GARDE: 0.35,     // rencontre ordinaire : chaque membre garde 35 % de ses PM (sorts et techniques d'attaque)
  REESSAIS: 2,         // rencontre perdue : RÉESSAYER deux fois, puis REPRENDRE
  T_REPRENDRE: 120,
};
const DROP_BASE = 0.06, DROP_RARE = 0.15, DROP_BOSS = 0.35, RARE_POIDS = 2, CRISTAL = 100;
// Soin lâché (04/11/2026, DROP_SOIN de internal.h : une victoire sur trois ou presque) : compté comme une POTION de plus
// (la seule qui sert en combat ; aux niveaux où le jeu en lâche une plus chère, c'est une borne basse)
const DROP_SOIN = 0.30;
const COFFRES = 0.12;   // coffres : environ 12 % du revenu des combats (coffres.js)
const PATR = { zones: new Set(['gravats', 'cendre', 'desert', 'marais']), pas: 4, chance: 0.12, niv: 6, engage: 0.5 };
// Prix des soins (02/11/2026, à garder identique à prix_soins de hud.c et prixSoins de l'émulateur) : le prix du commerce
// (COMM : clinique 40, hôpital 80, verre 15 ou 25) jusqu'au niveau où un combat rapporte 100 crédits, puis × crédits d'un
// combat / 100 (une clinique vaut 0,4 combat, l'hôpital 0,8)
const prixSoins = (base, L) => Math.max(base, Math.floor((base * C.credits_of(Math.max(1, L)) + 50) / 100));
// Récompenses « experience » des annexes : en combats d'un ennemi seul du niveau de leur étape (plan.js XP_ANNEXES)
const xpLigne = (ev, v, L) => PL.XP_ANNEXES && PL.XP_ANNEXES[ev.id] ? Math.round(PL.XP_ANNEXES[ev.id] * C.exp_of(Math.max(1, L)) / 10) * 10 : v;

function run(opt = {}) {
  const prudent = (opt.joueur || 'prudent') === 'prudent';
  const g = { parLieu: {}, morts_hc: 0, L: 1, xp: 0, cr: 0, potions: 3, ethers: 2, rappels: 0, band: [0], ko: 0, ko_combats: 0, dep_rappel: 0, t: 0, h: 1, pos: null, log: [], fights: 0, renc: 0, patr: 0, groupes: 0,
    defaites: 0, reessais: 0, morts: 0, t_marche: 0, t_combat: 0, t_parle: 0, cr_combat: 0, cr_quete: 0, cr_cristaux: 0, cr_coffres: 0, cr_vente: 0,
    dep_eq: 0, dep_pot: 0, dep_eth: 0, dep_soins: 0, dep_reprendre: 0, xp_combat: 0, xp_quete: 0, eq: C.BASE.map(() => [null, null, null]), drops: 0,
    encLeft: 0, boss: [], arrivee: {}, etatBoss: [], vus: new Set(), faites: new Set(),
    hp: C.BASE.map(() => 1), mp: C.BASE.map(() => 1),
    soins: { hopital: 0, clinique: 0, verre: 0, retraite: 0, potion: 0, ether: 0, magie: 0, pv_magie: 0, pv_potion: 0, pv_lieu: 0, rappel: 0, releve_lieu: 0, releve_sort: 0, releve_combat: 0 },
    troncons: 0, troncons_mort: 0, actes: [], rangStory: 0 };
  const parNom = n => Q.OBJETS().find(o => PL.norm(o.nom) === PL.norm(n));
  const equiperDepart = k => { for (const n of Q.departNoms(k)) { const o = parNom(n); if (o && (!g.eq[k][Q.SLOT(o.cat)] || g.eq[k][Q.SLOT(o.cat)].palier < o.palier)) g.eq[k][Q.SLOT(o.cat)] = o; } };
  equiperDepart(0);
  // au combat : la règle du moteur (les trois premières recrues) ; opt.soigneuse : le joueur prudent y met Lyse dès qu'elle est là
  // (essai du 02/11/2026 : plus faible contre les boss que l'équipe par défaut, 19 % au lieu de 49 % contre l'Inquisitrice de c01)
  const team = () => { const d = PL.equipeDefaut(g.band); if (!prudent || !opt.soigneuse || !g.band.includes(3) || d.includes(3)) return d; return [0, ...g.band.filter(k => k && k !== 3).slice(0, 2), 3].sort((a, b) => a - b); };
  const gear = () => ({ stats: C.BASE.map((b, k) => { const s = { atq: 0, def: 0, pv: 0, pm: 0, mag: 0, vit: 0, sorts: [] };
    const j = C.jobDe(k), A = [0, 0.7, 0.85, 1, 1.2];
    for (const o of g.eq[k]) if (o) { const f = A[Math.min(4, j.aff[Q.CATS.indexOf(o.cat)] | 0)]; for (const q of ['atq', 'def', 'pv', 'pm', 'mag', 'vit']) s[q] += (o[q] || 0) * f; s.sorts.push(...o.sorts); }
    return s; }) });
  // Cristaux du Chœur (03/11/2026 ; opt.cristaux ou CRISTAUX=1 : un joueur qui les cherche tous) : chaque cristal trouvé à son
  // niveau (Q.CRISTAUX, dispo) est porté par un membre au combat qui ne connaît pas encore son sort, un cristal par membre ; une
  // victoire où il est debout lui donne 3 points (combat de l'histoire), 2 (rare ou élite) ou 1 (CRISTAL_PT_* de battle.c) ; à
  // son total, le sort est appris pour de bon et le cristal passe au suivant. La place de l'accessoire n'est pas comptée
  // (borne haute)
  const CRX = opt.cristaux || process.env.CRISTAUX === '1' ? Q.CRISTAUX().filter(c => c.sort >= 0) : [];
  g.appris = C.BASE.map(() => new Set()); g.porte = C.BASE.map(() => null); g.cristauxAppris = 0;
  const cristauxPoser = () => { for (const k of team()) { if (g.porte[k]) continue;
    const c = CRX.find(x => x.dispo <= g.L && !g.appris[k].has(x.sort) && !g.porte.some(p => p && p.c === x)); if (c) g.porte[k] = { c, pts: 0 }; } };
  const cristauxPoints = (pt, T) => { for (const k of T) { const p = g.porte[k]; if (!p || g.hp[k] <= 0) continue;
    p.pts += pt; if (p.pts >= p.c.pts) { g.appris[k].add(p.c.sort); g.porte[k] = null; g.cristauxAppris++; } } cristauxPoser(); };
  const apprisDe = T => Object.fromEntries(T.map(k => [k, [...g.appris[k]].concat(g.porte[k] ? [g.porte[k].c.sort] : [])]));
  const membre = k => C.member(k, g.L, gear(), null, apprisDe([k])[k]);
  const gain = xp => { g.xp += xp; while (g.L < C.NIV_MAX && g.xp >= C.exp_next(g.L)) { g.xp -= C.exp_next(g.L); g.L++; } };
  const moyHp = () => { const t = team(); return t.reduce((a, k) => a + g.hp[k], 0) / t.length; };
  const moyMp = () => { const t = team(); return t.reduce((a, k) => a + g.mp[k], 0) / t.length; };
  const soignerTout = () => { for (const k of g.band) { g.hp[k] = 1; g.mp[k] = 1; } };

  // ---------- Soins hors combat ----------
  // Sorts et techniques de soin des membres au combat (MAG × puissance, soin des techniques), du moins cher en PM par PV ;
  // le soigneur garde pmMin de ses PM. Puis les potions (1 500 PV). Jusqu'à « vise » des PV max du membre le plus faible
  const soigner = (seuil, vise, pmMin) => {
    const T = team(), Ms = {}; for (const k of T) Ms[k] = membre(k);
    for (let garde = 0; garde < 60; garde++) {
      // à terre (02/11/2026) : ni soigné ni soigneur, on le relève (relever)
      let w = -1; for (const k of T) if (g.hp[k] > 0 && g.hp[k] < seuil && (w < 0 || g.hp[k] < g.hp[w])) w = k;
      if (w < 0) return;
      const mw = Ms[w], manque = (vise - g.hp[w]) * mw.max;
      let best = null;
      for (const k of T) { const m = Ms[k]; if (g.hp[k] <= 0) continue;
        const opts = m.techs.filter(x => x.heal).map(x => ({ pm: x.mp, pv: x.heal, tous: false }))
          .concat(m.sorts.filter(s => s.soin && s.soin !== 3).map(s => ({ pm: s.pm, pv: Math.round(m.mag * s.mult), tous: s.soin === 2 })));
        for (const o of opts) {
          if ((g.mp[k] * m.maxmp - o.pm) < pmMin * m.maxmp) continue;
          const utile = Math.min(o.pv, manque) * (o.tous ? T.filter(j => g.hp[j] > 0 && g.hp[j] < vise).length : 1), v = utile / o.pm;
          if (!best || v > best.v) best = { k, o, v };
        }
      }
      if (best) {
        const m = Ms[best.k]; g.mp[best.k] -= best.o.pm / m.maxmp; g.soins.magie++;
        for (const j of best.o.tous ? T.filter(j => g.hp[j] > 0) : [w]) { const a = Math.min(best.o.pv / Ms[j].max, 1 - g.hp[j]); g.hp[j] += a; g.soins.pv_magie += a * Ms[j].max; }
        g.t += 4; continue;
      }
      if (g.potions > 0) { g.potions--; g.soins.potion++; const a = Math.min(HYP.POT_PV / mw.max, 1 - g.hp[w]); g.hp[w] += a; g.soins.pv_potion += a * mw.max; g.t += 2; continue; }
      return;
    }
  };
  // Relever ceux qui sont à terre : la SERINGUE DE RAPPEL (un quart des PV) ; sans elle, ils attendent un lieu de soins
  const aTerre = () => team().filter(k => g.hp[k] <= 0);
  // d'abord le CHANT DE RAPPEL (02/11/2026) d'un membre debout qui l'a appris et garde de quoi combattre (prudent : 30 % de ses PM),
  // puis la seringue
  const releverSort = () => {
    for (const k of aTerre()) {
      let fait = false;
      for (const c of team()) { if (g.hp[c] <= 0) continue; const m = membre(c), s = m.sorts.find(x => x.soin === 3); if (!s) continue;
        if ((g.mp[c] * m.maxmp - s.pm) < (prudent ? 0.3 : 0) * m.maxmp) continue;
        g.mp[c] -= s.pm / m.maxmp; g.hp[k] = Math.min(1, m.mag * s.mult / membre(k).max); g.soins.releve_sort++; g.t += 4; fait = true; break; }
      if (!fait) return;
    }
  };
  const relever = () => { releverSort(); for (const k of aTerre()) { if (g.rappels <= 0) break; g.rappels--; g.soins.rappel++; g.hp[k] = HYP.RELEVE_PART; g.t += 3; } };
  const apresCombat = () => { relever(); if (prudent) soigner(0.6, 0.85, 0.5); else soigner(0.3, 0.6, 0.2); };
  const avantBoss = () => {
    if (!prudent) return;
    relever();
    if (aTerre().length && g.pos) replier(g.pos);   // encore à terre (plus de seringue) : se replier jusqu'à un lieu de soins
    soigner(0.95, 1, 0.35);
    // PM : les boss en dépendent beaucoup (à 30 % de PM, 1 à 11 % de victoires) ; éthers jusqu'à 70 %, et repli par le téléport si
    // l'équipe arrive avec moins de 45 % de PM et que la bourse le permet
    if (moyMp() < 0.45 && g.pos && replier(g.pos)) { relever(); soigner(0.95, 1, 0.35); }   // 03/11/2026 : revenu à pied du repli, de nouveau entamée
    const T = team(); for (const k of T) { const m = membre(k); while (m.maxmp && g.mp[k] < 0.7 && g.ethers > 0) { g.ethers--; g.soins.ether++; g.mp[k] = Math.min(1, g.mp[k] + HYP.ETH_PM / m.maxmp); } }
  };
  // Lieu de soins (clinique, hôpital : PV et PM de la bande ; verre : 20 % des PV de l'équipe)
  const auLieu = (s, force) => {
    if (s.sorte !== 'verre' && s.acces === 1 && !force && C.rand() > HYP.CLIN_OUVERT) return false;
    const prix = prixSoins(s.base, g.L);
    if (g.cr < prix) return false;
    if (s.sorte === 'verre') {
      if (moyHp() > 0.75) return false;
      g.cr -= prix; g.dep_soins += prix; g.soins.verre++; for (const k of team().filter(k => g.hp[k] > 0)) { const m = membre(k), a = Math.min(0.2, 1 - g.hp[k]); g.hp[k] += a; g.soins.pv_lieu += a * m.max; }
      g.t += HYP.T_SOIN; return true;
    }
    if (!force && !aTerre().length && moyHp() > (prudent ? 0.85 : 0.7) && moyMp() > (prudent ? 0.6 : 0.3)) return false;
    g.soins.releve_lieu += aTerre().length;
    g.cr -= prix; g.dep_soins += prix; g.soins[s.sorte]++; g.parLieu[s.nom + (s.carte ? " (" + s.carte + ")" : "")] = (g.parLieu[s.nom + (s.carte ? " (" + s.carte + ")" : "")] || 0) + 1;
    for (const k of team()) g.soins.pv_lieu += (1 - g.hp[k]) * membre(k).max;
    soignerTout(); g.t += HYP.T_SOIN; return true;
  };
  // Boutique : potions jusqu'au stock, quelques éthers, puis l'équipement (le meilleur de chaque emplacement des membres au
  // combat parmi les paliers vendus : jusqu'à max(équipe, histoire) + AVANCE, gamme du lieu : villages 4, Lucerne 10, astroport 14)
  const gamme = () => g.rangStory < PL.rangDe('acte1', 2) ? 4 : g.rangStory < PL.rangDe('acte4', 4) ? 10 : 14;
  const potions = n => { while (g.cr >= HYP.POT_PRIX && g.potions < n) { g.cr -= HYP.POT_PRIX; g.dep_pot += HYP.POT_PRIX; g.potions++; } };
  const rappels = n => { while (g.cr >= HYP.RAPPEL_PRIX + 2 * HYP.POT_PRIX && g.rappels < n) { g.cr -= HYP.RAPPEL_PRIX; g.dep_rappel += HYP.RAPPEL_PRIX; g.rappels++; } };
  const shop = () => { potions(5); if (!opt.unPv) rappels(1); equiper(); potions(HYP.POT_STOCK); if (!opt.unPv) rappels(prudent ? HYP.RAPPEL_STOCK : 1); };
  const equiper = () => {
    if (opt.sansEquipement) return;
    const cap = gamme(), lim = Math.max(g.L, g.h) + P.AVANCE;
    for (const k of team()) for (let s = 0; s < 3; s++) {
      const best = Q.equipe(k, Math.min(lim, P.NIV[cap]), false);
      const nomV = best.noms.find(n => { const o = Q.OBJETS().find(x => x.nom === n); return o && Q.SLOT(o.cat) === s; });
      const o = nomV && Q.OBJETS().find(x => x.nom === nomV), cur = g.eq[k][s];
      if (!o || (cur && cur.palier >= o.palier) || o.palier > cap) continue;
      const net = o.prix - (cur ? Math.round(cur.prix / 2) : 0);
      if (g.cr - net < 2 * HYP.POT_PRIX) continue;
      g.cr -= net; g.dep_eq += o.prix; if (cur) g.cr_vente += Math.round(cur.prix / 2);
      g.eq[k][s] = o;
    }
    while (prudent && g.cr >= 10 * HYP.ETH_PRIX && g.ethers < HYP.ETH_STOCK) { g.cr -= HYP.ETH_PRIX; g.dep_eth += HYP.ETH_PRIX; g.ethers++; }
  };
  // Butin d'équipement : un objet du palier du combat ; pris s'il vaut mieux que ce que porte un membre, sinon revendu
  const butin = (lvl, ch) => {
    if (C.rand() >= ch) return;
    const p = P.palierDe(lvl), l = Q.OBJETS().filter(o => o.palier === p);
    if (!l.length) return;
    const o = l[Math.floor(C.rand() * l.length) % l.length]; g.drops++;
    for (const k of team()) { const s = Q.SLOT(o.cat), cur = g.eq[k][s], j = C.jobDe(k); if (j.aff[Q.CATS.indexOf(o.cat)] > 0 && (!cur || cur.palier < o.palier)) { if (cur) g.cr_vente += Math.round(cur.prix / 2), g.cr += Math.round(cur.prix / 2); g.eq[k][s] = o; return; } }
    g.cr += Math.round(o.prix / 2); g.cr_vente += Math.round(o.prix / 2);
  };
  // REPRENDRE : le dernier point de sauvegarde, l'équipe soignée contre le prix de l'hôpital (si la bourse le permet)
  const reprendre = hc => { const f = opt.ancien ? 0 : Math.min(g.cr, prixSoins(80, g.L)); g.cr -= f; g.dep_reprendre += f; soignerTout(); g.morts++; if (hc) g.morts_hc++; g.t += HYP.T_REPRENDRE; };
  // Un combat ; scripted : combat de l'histoire (au niveau min(étape, équipe), récompense × BOSS_RW, butin DROP_BOSS)
  const fight = (noms, mode, lvl, scripted, tag, fretin) => {
    const ens = noms.map(n => C.ENNEMIS[n]).filter(Boolean); if (!ens.length) return true;
    if (scripted) avantBoss();
    const L = Math.max(1, Math.min(lvl, scripted ? g.L : 999));
    const fs_ = ens.map(e => C.auNiveau(e, L)), T = team();
    const etat0 = { hp: {}, mp: {} }; for (const k of T) { etat0.hp[k] = g.hp[k]; etat0.mp[k] = g.mp[k]; }
    if (T.some(k => g.hp[k] <= 0)) g.ko_combats++;   // un combat engagé avec un membre à terre
    if (scripted && tag) g.etatBoss.push([tag, moyHp(), moyMp(), gear()]);   // 03/11/2026 : et l'équipement porté (equipement-ref.json)
    const pot0 = g.potions;
    for (let essai = 0; essai < 40; essai++) {
      const inv = { potions: pot0 };   // RÉESSAYER et REPRENDRE rendent le sac du début du combat
      const r = C.battle(T, g.L, fs_.length > 1 ? fs_ : fs_[0], inv, 'bon', gear(), mode,
        { eveil: g.faites.has('eveil-sera') ? [0] : null, etat: etat0, garde: scripted ? 0 : HYP.RES_GARDE, appris: CRX.length ? apprisDe(T) : null });
      g.t += r.t; g.t_combat += r.t; g.fights++; g.soins.releve_combat += r.releves || 0;
      if (r.win) {
        g.soins.potion += pot0 - inv.potions; g.potions = inv.potions;
        // à terre : il le reste (02/11/2026 ; opt.unPv : la règle d'avant, relevé à 1 PV après le combat)
        for (const k of T) { const ko = !(r.etat.hp[k] > 0); if (ko) g.ko++; g.hp[k] = ko && !opt.unPv ? 0 : Math.max(1 / membre(k).max, r.etat.hp[k]); g.mp[k] = r.etat.mp[k]; }
        // xp_histoire (03/11/2026) : la part des combats de l'histoire
        const parts = noms.length > 1 ? C.partGroupe(noms.length) : 1, rw = scripted ? C.BOSS_RW : 1;
        // 06/11/2026 (internal.h) : expérience d'un combat d'annexe × BOSS_RW_ANNEXE (ses crédits restent × BOSS_RW), rencontres × RENC_XP
        const xp0 = Math.round(C.exp_of(L) * parts), princ = !!(tag && PL.QUETE[tag.split('#')[0]] && PL.QUETE[tag.split('#')[0]].type === 'principale');
        const xp = opt.ancien ? xp0 * rw : scripted ? xp0 * (princ && !fretin ? C.BOSS_RW : BOSS_RW_ANNEXE) : Math.round(xp0 * RENC_XP), cr = Math.round(C.credits_of(L) * parts) * rw;
        gain(xp); g.xp_combat += xp; g.cr += cr; g.cr_combat += cr; if (scripted) g.xp_histoire = (g.xp_histoire || 0) + xp;
        const coeurs = ens.filter(e => e.coeur).length * (noms.length > 1 ? parts / noms.length : 1) * (scripted ? 1 : CRISTAL_RENC);
        g.cr += Math.round(coeurs * CRISTAL); g.cr_cristaux += Math.round(coeurs * CRISTAL);
        const cof = Math.round((cr + coeurs * CRISTAL) * COFFRES * (scripted ? 0.25 : 1)); g.cr += cof; g.cr_coffres += cof;
        const e0 = ens[0]; butin(L, scripted ? DROP_BOSS : (C.elite(e0) || e0.poids <= RARE_POIDS) ? DROP_RARE : DROP_BASE);
        if (!opt.ancien && Math.random() < DROP_SOIN) { g.potions++; g.soins.lachees = (g.soins.lachees || 0) + 1; }
        if (CRX.length) { cristauxPoser(); cristauxPoints(scripted ? 3 : (C.elite(e0) || e0.poids <= RARE_POIDS) ? 2 : 1, T); }
        if (scripted && tag) g.boss.push([tag, noms[0], L, g.L, essai, r.perte]);
        if (opt.ancien) soignerTout(); else apresCombat();   // règles d'avant le 02/11/2026 : l'équipe soignée après chaque combat
        return true;
      }
      // défaite : RÉESSAYER LE COMBAT (même état d'entrée, potions du début) ; au-delà, REPRENDRE (soignée, prix de l'hôpital)
      g.defaites++; if (essai) g.reessais++;
      g.t += 6; g.t_combat += 6;
      if (essai >= (scripted ? 3 : HYP.REESSAIS) - 1) { reprendre(scripted && HARDCORE.includes((tag || '').split('#')[0])); for (const k of T) { etat0.hp[k] = 1; etat0.mp[k] = 1; } g.troncMort = true; if (!scripted) return false; }
    }
    if (scripted && tag) g.boss.push([tag, noms[0], L, g.L, 40, 1]);
    return false;
  };
  const tire = tb => { let r = C.rand() * tb.reduce((a, e) => a + e.poids, 0); for (const e of tb) { r -= e.poids; if (r <= 0) return e; } return tb[tb.length - 1]; };
  const niveauRenc = () => Math.max(1, Math.min(g.h, g.L + 1) - Math.floor(C.rand() * 3));
  // Lieux visités du téléport, lieux de soins et boutiques au bord du chemin
  const visiter = (x, z, carte) => { for (let i = 0; i < M.TELE.length; i++) { const t = M.TELE[i]; if (t.carte === carte && t.sorte && Math.hypot(t.x - x, t.z - z) < HYP.TELE_R[t.sorte]) g.vus.add(i); } };
  const arriverCarte = carte => { M.TELE.forEach((t, i) => { if (t.sorte === 0 && t.carte === carte) g.vus.add(i); }); };
  const proche = (l, x, z, carte, r) => { let b = null, bd = r; for (const s of l) if (s.carte === carte) { const d = Math.hypot(s.x - x, s.z - z); if (d < bd) { bd = d; b = s; } } return b; };
  const escale = (x, z, carte) => {
    const b = proche(M.BOUTIQUES, x, z, carte, HYP.PRES); if (b) shop();
    const soinsIci = M.SOINS.filter(s => s.carte === carte && Math.hypot(s.x - x, s.z - z) < HYP.PRES).sort((a, b2) => (b2.sorte === 'hopital') - (a.sorte === 'hopital'));
    for (const s of soinsIci) if (prudent && auLieu(s)) break;
  };
  // Repli : le téléport jusqu'au lieu de soins visité le plus proche (sa ville visitée), soins, puis retour (le point visité
  // le plus proche de la cible)
  const replier = cible => {
    let best = null;
    for (const s of M.SOINS) { if (s.sorte === 'verre') continue; const v = [...g.vus].map(i => M.TELE[i]).find(t => t.sorte >= 1 && t.carte === s.carte && Math.hypot(t.x - s.x, t.z - s.z) < 70); if (v && (!best || s.sorte === 'hopital')) best = s; }
    if (!best || g.cr < prixSoins(best.base, g.L)) return false;
    g.t += 2 * HYP.T_TELE; g.soins.retraite++;
    if (!auLieu(best, true)) return false;
    shop();
    g.pos = null; aller(cible, true);
    return true;
  };
  // Marche de a à b (cases réelles d), sur la carte : rencontres, patrouilles, lieux visités, escales
  const marche = (a, b, d, carte) => {
    if (!(d > 0)) return;
    const vehicule = !carte && g.rangStory >= PL.rangDe(HYP.VEH_DES, 1) && d > HYP.VEH_SEUIL;
    const aPied = vehicule ? HYP.VEH_SEUIL + (d - HYP.VEH_SEUIL) * (1 - HYP.VEH_PART) : d;
    const n = Math.max(1, Math.ceil(d)), dd = aPied / n;
    g.t += aPied / HYP.VIT + (d - aPied) / HYP.VEH_VIT; g.t_marche += aPied / HYP.VIT + (d - aPied) / HYP.VEH_VIT;
    let tp = 0;
    for (let s = 1; s <= n; s++) {
      const x = a[0] + (b[0] - a[0]) * s / n, z = a[1] + (b[1] - a[1]) * s / n;
      tp += dd / HYP.VIT;
      if (s % 8 === 0 || s === n) { visiter(x, z, carte); escale(x, z, carte); }
      const zone = M.zoneAt(x, z, carte), m = team().length;
      if (opt.patrouilles !== false && zone && PATR.zones.has(zone) && !carte && m >= 2 && g.h >= PATR.niv && tp >= PATR.pas) {
        tp = 0;
        if (C.rand() < PATR.chance * PATR.engage) {
          const tb = M.table(zone, g.h, m).filter(e => !C.elite(e) && e.groupe !== 1);
          if (tb.length) { const e = tire(tb); let k = 2 + (g.h >= 25 && m >= 3 && C.rand() < 0.5); if (k === 3 && g.h >= 45 && m >= 4 && C.rand() < 0.35) k = 4;
            const noms = [e.nom]; for (let i = 1; i < k; i++) noms.push(C.rand() < 0.6 ? e.nom : tire(tb).nom);
            g.patr++; fight(noms, 'parts', niveauRenc(), false); }
        }
      }
      if (opt.renc === false) continue;
      const pas = C.W.renc * HYP.RENC_FOIS * (opt.rencFois || 1);
      if (g.encLeft <= 0) g.encLeft = pas * (0.5 - 0.5 * Math.log(1 - C.rand()));
      g.encLeft -= dd * HYP.ECH; if (g.encLeft > 0) continue;
      g.encLeft = 0;
      const tb = zone ? M.table(zone, g.h, m) : []; if (!tb.length) continue;
      const e = tire(tb), k = C.tailleGroupe(g.h, m, e), noms = [e.nom];
      const comp = tb.filter(q => q !== e && !C.elite(q) && !!q.coeur === !!e.coeur && q.groupe !== 1);
      for (let i = 1; i < k; i++) noms.push(comp.length && C.rand() >= 0.6 ? tire(comp).nom : e.nom);
      g.renc++; if (k > 1) g.groupes++;
      fight(noms, 'parts', niveauRenc(), false);
      // prudent : trop entamée loin de tout, repli
      if (prudent && (moyHp() < 0.45 && g.potions < 2 || moyMp() < 0.12 || (aTerre().length && g.rappels <= 0)) && replier(b.concat([carte]))) return;
    }
  };
  // Aller jusqu'au point p = [x, z, carte] : téléport si un lieu visité raccourcit (ou sur une autre carte), sinon les passages
  const teleVers = p => { let b = null, bd = 1e9; for (const i of g.vus) { const t = M.TELE[i]; if (t.carte !== (p[2] || '')) continue; const d = Math.hypot(t.x - p[0], t.z - p[1]); if (d < bd) { bd = d; b = t; } } return b ? { t: b, d: bd } : null; };
  function aller(p, depuisTele) {
    if (!p) return;
    const carte = p[2] || '';
    g.troncons++; g.troncMort = false;
    if (g.pos && (g.pos[2] || '') === carte) {
      const d = Math.hypot(p[0] - g.pos[0], p[1] - g.pos[1]), tv = teleVers(p);
      if (tv && d > 120 && tv.d < 0.5 * d) { g.t += HYP.T_TELE; marche([tv.t.x, tv.t.z], p, tv.d * HYP.DETOUR, carte); }
      else marche(g.pos, p, d * HYP.DETOUR, carte);
    } else {
      const tv = teleVers(p);
      if (tv || !g.pos) { if (tv) { g.t += HYP.T_TELE; marche([tv.t.x, tv.t.z], p, tv.d * HYP.DETOUR, carte); } }
      else {   // les passages : directement, ou par la carte du système solaire (vol)
        const de = g.pos[2] || '', direct = M.PASSAGES.filter(q => q.carte === de && q.vers === carte);
        const route = direct.length ? [direct] : [M.PASSAGES.filter(q => q.carte === de && q.vers === 'systeme'), M.PASSAGES.filter(q => q.carte === 'systeme' && q.vers === carte)];
        let pos = g.pos, ok = route.every(l => l.length);
        if (ok) for (const l of route) {
          const q = l.reduce((a, b) => Math.hypot(b.x - pos[0], b.z - pos[1]) < Math.hypot(a.x - pos[0], a.z - pos[1]) ? b : a);
          marche(pos, [q.x, q.z], Math.hypot(q.x - pos[0], q.z - pos[1]) * (pos[2] === 'systeme' || (pos[2] || '') === 'systeme' ? 1 : HYP.DETOUR), pos[2] || '');
          pos = [q.ax, q.az, q.vers]; arriverCarte(q.vers); g.t += HYP.T_TELE;
        }
        if (ok) marche(pos, p, Math.hypot(p[0] - pos[0], p[1] - pos[1]) * (carte === 'systeme' ? 1 : HYP.DETOUR), carte);
        else g.t += 40;
      }
      arriverCarte(carte);
    }
    g.pos = p.slice(); g.pos[2] = carte;
    visiter(p[0], p[1], carte); escale(p[0], p[1], carte);
    if (g.troncMort) g.troncons_mort++;
  }
  // Les événements d'une étape (quête q, étape st) : combats, récompenses, recrues ; T_PARLE par réplique
  const etape = (q, st) => {
    const niv = PL.niveauEtape(q, st) || g.h;
    for (const ev of C.H.EVENTS) {
      if (ev.quete !== q || (ev.etape || 0) !== st || (opt.adulte === false && ev.adulte) || (PL.evDansPartie && !PL.evDansPartie(ev))) continue;   // 03/11/2026 : scènes de l'héroïne jouée seulement
      for (const l of PL.lignesJouees(ev, opt.choix || 1)) {
        let m;
        if (/^[A-ZÉÈÀÂÎÔÛÇ'’ -]+:/.test(l)) { g.t += HYP.T_PARLE; g.t_parle += HYP.T_PARLE; }
        else if ((m = /^combat\s+(.+)$/i.exec(l))) { const c = PL.combatGroupe(m[1]); fight(c.noms, c.mode, niv, true, q + '#' + st, c.fretin); }
        else if ((m = /^(?:experience|expérience)\s+(\d+)/i.exec(l))) { const v = xpLigne(ev, +m[1], niv); gain(v); g.xp_quete += v; }
        else if ((m = /^donner credits\s+(-?\d+)/i.exec(l))) { g.cr = Math.max(0, g.cr + +m[1]); if (+m[1] > 0) g.cr_quete += +m[1]; }
        else if ((m = /^donner POTION\s+(\d+)/i.exec(l))) g.potions += +m[1];
        else if ((m = /^donner\s+(.+?)\s+(\d+)$/i.exec(l)) && parNom(m[1])) { const o = parNom(m[1]); for (const k of team()) { const sl = Q.SLOT(o.cat), cur = g.eq[k][sl]; if ((C.jobDe(k).aff[Q.CATS.indexOf(o.cat)] | 0) && (!cur || cur.palier < o.palier)) { g.eq[k][sl] = o; break; } } }
        else if ((m = /^(?:rejoindre|equipe)\s+(.+)$/i.exec(l))) for (const nm of m[1].split(/[\s,]+/)) { const k = PL.MEMBRES.map(PL.norm).indexOf(PL.norm(nm)); if (k > 0 && !g.band.includes(k)) { g.band.push(k); equiperDepart(k); g.hp[k] = 1; g.mp[k] = 1; } }
        else if (/^soigner/i.test(l)) soignerTout();
      }
    }
  };
  // Annexes faites (opt.annexes : true toutes, un nombre entre 0 et 1 : cette part, au hasard) ; les Éveils des autres
  // héroïnes (eveil-jade… sans « -recrue ») ne sont pas pour Sera
  const annexeOk = id => {
    const q = PL.QUETE[id];
    if (!q || q.type === 'principale' || (opt.adulte === false && q.adulte) || /^eveil-(?!sera)(?!.*-recrue)/.test(id) || (PL.dansPartie && !PL.dansPartie(id))) return false;   // 03/11/2026 : eveil-sera-recrue, c10h… hors de la partie de Sera
    if (/^eveil-.*-recrue$/.test(id)) { const nm = id.split('-')[1].toUpperCase(); const k = PL.MEMBRES.map(PL.norm).indexOf(PL.norm(nm)); if (!g.band.includes(k)) return false; }
    if (id === 'eveil-sera' && opt.eveil !== false) return true;   // l'Éveil de Sera : l'arc final après a61 est étalonné avec lui (02/11/2026 au soir)
    if (opt.annexes === true) return true;
    if (!opt.annexes) return false;
    return (g.tirage[id] = g.tirage[id] !== undefined ? g.tirage[id] : C.rand() < opt.annexes);
  };
  g.tirage = {};
  const ANNEXES = Object.keys(OUV).filter(id => PL.QUETE[id]);
  const faireAnnexesApres = point => {
    for (const id of ANNEXES) {
      const l = OUV[id];
      if (!annexeOk(id)) continue;
      for (let i = 0; i < l.length; i++) {
        const cle = id + '#' + (i + 1);
        if (l[i] !== point || g.arrivee[cle] !== undefined) continue;
        if (i > 0 && g.arrivee[id + '#' + i] === undefined) break;   // l'étape d'avant n'est pas faite
        if (i === 0) { etape(id, 0); g.t += HYP.T_ANNEXE; }
        aller((M.ETAPES_POS[id] || [])[i]);
        g.arrivee[cle] = g.L;
        etape(id, i + 1);
        if (i === l.length - 1) g.faites.add(id);
        // la suite de l'annexe, si elle se fait au même point
        if (i + 1 < l.length && l[i + 1] !== point) break;
      }
    }
  };
  g.t += 90;
  const PRINC = PL.PRINC.filter(q => opt.final !== false || !PL.FINAL.has(q));
  for (const q of PRINC) {
    const Qd = PL.QUETE[q];
    etape(q, 0);
    Qd.etapes.forEach((e, i) => {
      g.rangStory = PL.rangDe(q, i + 1);
      const niv = PL.ETAPES[q][i];
      if (niv) g.h = Math.max(g.h, niv);
      aller((M.ETAPES_POS[q] || [])[i]);
      g.arrivee[q + '#' + (i + 1)] = g.L;
      if (opt.trace) opt.trace(q + '#' + (i + 1), g);
      etape(q, i + 1);
      faireAnnexesApres(q + '#' + (i + 1));
    });
    const pals = team().flatMap(k => g.eq[k].map(o => o ? o.palier : 0)), pmoy = pals.reduce((x, y) => x + y, 0) / pals.length;
    g.actes.push({ q, t: g.t, L: g.L, h: g.h, fights: g.fights, cr: g.cr, palier: pmoy });
    g.log.push(`${q.padEnd(9)} fin à ${(g.t / 3600).toFixed(1)} h · niveau ${g.L} (histoire ${g.h}) · ${g.fights} combats · ${g.cr} CR · équipement palier ${pmoy.toFixed(1)} (boutique ${Math.min(14, P.palierDe(Math.max(g.L, g.h) + P.AVANCE))}) · ${g.morts} REPRENDRE`);
  }
  return g;
}

// ---------- Niveau réel de chaque étape (point fixe) ----------
// Les combats d'une étape se font à min(niveau de l'étape, équipe), les rencontres suivent min(histoire, L + 1) : le niveau
// des étapes change ce que la partie gagne. On part des niveaux du plan, on joue N parties, chaque étape prend le niveau
// moyen de l'équipe à son arrivée, et ainsi de suite jusqu'à ce que plus rien ne bouge.
// Quêtes aux niveaux figés par l'utilisateur (02/11/2026 au soir) : l'après-jeu hardcore (90-99), ceux de js/histoire.js (l'arc
// final, figé un temps à 68-76, prend son niveau réel d'arrivée, comme le reste de l'histoire) ; leur niveau réel est noté (REEL) ;
// FIGER=… : une autre liste (virgules)
const FIGEES = new Set((process.env.FIGER || 'h10,h11,r05').split(',').filter(Boolean));
// EB : PM moyens à l'arrivée des combats de l'histoire, par quête (dernier tour) : etat-boss.json, lu par calibrer.js
const EB = { S: {}, N: {} }, REEL = {};
// EQ (03/11/2026) : équipement porté à l'arrivée des combats de l'histoire, par étape (moyenne des statistiques de chaque membre,
// sorts portés une fois sur deux au moins) : equipement-ref.json, lu par calibrer.js (l'équipement du moment d'un joueur qui ne fait
// que l'histoire : le palier que ses crédits lui ont permis d'acheter, pas toujours celui de son niveau)
const EQ = {};
const eqDe = () => { const o = {}; for (const [tag, l] of Object.entries(EQ)) o[tag] = l[0].stats.map((s0, k) => { const m = { atq: 0, def: 0, pv: 0, pm: 0, mag: 0, vit: 0 }, cpt = {};
  for (const g of l) { const s = g.stats[k]; for (const q of Object.keys(m)) m[q] += (s[q] || 0) / l.length; for (const x of new Set(s.sorts)) cpt[x] = (cpt[x] || 0) + 1; }
  for (const q of Object.keys(m)) m[q] = Math.round(m[q]); m.sorts = Object.keys(cpt).filter(x => cpt[x] * 2 >= l.length).map(Number); return m; }); return o; };
function niveaux(opt, n = 8, tours = 5, log = console.log) {
  let res = null;
  for (let it = 0; it < tours; it++) {
    const S = {}, N = {};
    EB.S = {}; EB.N = {}; for (const k of Object.keys(EQ)) delete EQ[k];
    for (let i = 0; i < n; i++) { const g = run(opt); for (const [k, L] of Object.entries(g.arrivee)) { S[k] = (S[k] || 0) + L; N[k] = (N[k] || 0) + 1; }
      for (const [tag, hp, mp, eq] of g.etatBoss) { for (const q of [tag.split("#")[0], tag]) { EB.S[q] = (EB.S[q] || 0) + mp; EB.N[q] = (EB.N[q] || 0) + 1; } if (eq) (EQ[tag] = EQ[tag] || []).push(eq); } }   // par quête et, depuis le 03/11/2026, par étape (« acte5#1 »)
    let bouge = 0;
    res = {};
    for (const k of Object.keys(S)) {
      const [q, st] = k.split('#'), v = Math.max(1, Math.round(S[k] / N[k]));
      if (!PL.QUETE[q] || !PL.QUETE[q].etapes[st - 1]) continue;
      if (PL.ETAPES[q][st - 1] === 0) continue;   // courses : sans niveau
      if (FIGEES.has(q)) { REEL[k] = v; continue; }   // niveaux décidés par l'utilisateur : arc final 68-76, hardcore 90-99 (02/11/2026) ; REEL : le niveau qu'y a vraiment le joueur
      res[k] = v;
      if (PL.ETAPES[q][st - 1] !== v) { bouge += Math.abs(PL.ETAPES[q][st - 1] - v); PL.ETAPES[q][st - 1] = v; }
    }
    log(`  tour ${it + 1} : ${Object.keys(res).length} étapes, écart cumulé ${bouge}`);
    if (bouge < 3) break;
  }
  // étapes non jouées (Éveils des autres héroïnes, quêtes sans cible) : celles de l'Éveil de Sera, ou gardées
  return res;
}

// Quêtes hardcore de l'après-jeu : niveaux de conception gardés (90 à 99)
const HARDCORE = ['h10', 'h11', 'r05'];
module.exports = { run, niveaux, HYP, prixSoins, HARDCORE };

if (require.main === module) {
  const a = process.argv.slice(2), mode = a[0] && isNaN(+a[0]) ? a[0] : 'audit', n = +(a.find(x => !isNaN(+x)) || 6);
  const moy = (R, f) => R.reduce((s, r) => s + f(r), 0) / R.length;
  if (mode === 'niveaux') {
    // Joueur de référence (03/11/2026, demande de l'utilisateur : « les combats de l'histoire donnent la plus grande majorité de
    // l'expérience nécessaire pour continuer l'aventure ») : prudent, l'histoire principale et les rencontres de son chemin, sans
    // annexe (l'Éveil de Sera compris : l'arc final est fait pour elle transformée). Les étapes d'une annexe : au niveau de ce joueur
    // quand il la fait, et elle seule : son niveau à l'étape principale qui l'ouvre (ouverture.json), plus l'expérience des étapes
    // d'avant de l'annexe (ses combats, gagnés, et ses récompenses) ; qui fait d'autres annexes la fait plus fort. REFERENCE=annexes :
    // la référence du 02/11/2026 au soir (toutes les étapes au niveau du joueur qui fait la moitié des annexes, ANNEXES=…)
    const PART = process.env.ANNEXES ? +process.env.ANNEXES : 0.5, optA = { annexes: PART >= 1 ? true : PART };
    const ebDe = () => { const o = {}; for (const q of Object.keys(EB.S)) o[q] = +(EB.S[q] / EB.N[q]).toFixed(2); return o; };
    let res, eb;
    if (process.env.REFERENCE === 'annexes') {
      console.log('Niveau réel des étapes : joueur prudent, ' + (PART >= 1 ? 'toutes les' : Math.round(PART * 100) + ' % des') + ' annexes à leur ouverture');
      res = niveaux(optA, n); eb = ebDe();
    } else {
      console.log('Étapes principales : joueur prudent, l\'histoire seule et les rencontres de son chemin' + (process.env.CRISTAUX !== '0' ? ', cristaux du Chœur' : ''));
      // la magie des jobs et, depuis le 03/11/2026 au soir, les cristaux du Chœur qu'il trouve (CRISTAUX=0 : sans)
      res = niveaux({ annexes: false, cristaux: process.env.CRISTAUX !== '0' }, n); eb = ebDe();   // l'état d'arrivée des combats de l'histoire : celui de ce joueur
      fs.writeFileSync(__dirname + '/equipement-ref.json', JSON.stringify(eqDe()));
      console.log('Étapes des annexes : ce joueur, quand il fait l\'annexe et elle seule');
      // expérience cumulée au milieu du niveau L, et niveau d'une expérience cumulée
      const xpCum = L => { let x = 0; for (let l = 1; l < L; l++) x += C.exp_next(l); return x + C.exp_next(L) / 2; };
      const niveauDe = x => { let L = 1; while (L < C.NIV_MAX && x >= C.exp_next(L)) { x -= C.exp_next(L); L++; } return L; };
      const rwx = C.BOSS_RW, joue = new Set(Object.keys(res).map(k => k.split('#')[0]));
      for (const id of Object.keys(OUV)) {
        const q = PL.QUETE[id]; if (!q || joue.has(id) || FIGEES.has(id)) continue;
        let xa = 0;
        for (let i = 0; i < OUV[id].length && i < q.etapes.length; i++) {
          const [oq, ost] = OUV[id][i].split('#'), Lm = res[oq + '#' + ost] || PL.niveauEtape(oq, +ost) || 1;
          const L = niveauDe(xpCum(Lm) + xa);
          if (PL.ETAPES[id][i] !== 0) res[id + '#' + (i + 1)] = L;   // courses : sans niveau
          for (const ev of C.H.EVENTS) {
            if (ev.quete !== id || !((ev.etape || 0) === i + 1 || (i === 0 && !ev.etape)) || (PL.evDansPartie && !PL.evDansPartie(ev))) continue;
            for (const l of PL.lignesJouees(ev, 1)) {
              let m;
              if ((m = /^combat\s+(.+)$/i.exec(l))) { const c = PL.combatGroupe(m[1]);   // au niveau de l'étape (min(étape, équipe))
                xa += Math.round(C.exp_of(L) * (c.noms.length > 1 ? C.partGroupe(c.noms.length) : 1)) * rwx; }
              else if ((m = /^(?:experience|expérience)\s+(\d+)/i.exec(l))) xa += xpLigne(ev, +m[1], L);
            }
          }
        }
      }
    }
    const NIV = {};
    for (const [k, v] of Object.entries(res)) { const [q, st] = k.split('#'); (NIV[q] = NIV[q] || {})[st] = v; }
    // Parties des autres héroïnes (03/11/2026) : leur prologue comme celui de Sera, les variantes (c10h…) comme leur quête
    for (const q of PL.H.QUETES) {
      const m = /^(.+)h$/.exec(q.id), base = m && PL.QUETE[m[1]];
      if (!NIV[q.id] && base && NIV[base.id] && base.etapes.length === q.etapes.length) NIV[q.id] = { ...NIV[base.id] };
      if (!NIV[q.id] && q.type === 'principale' && /^p[a-z]+$/.test(q.id) && NIV.prologue && q.etapes.length === PL.QUETE.prologue.etapes.length) NIV[q.id] = { ...NIV.prologue };
    }
    // Éveils des autres héroïnes : comme celui de Sera
    for (const q of PL.H.QUETES) if (/^eveil-/.test(q.id) && !NIV[q.id] && NIV['eveil-sera']) NIV[q.id] = { ...NIV['eveil-sera'] };
    // Niveaux d'origine (avant le 02/11/2026 : Backup/02_11/histoire.avant-exp.js de l'Atelier) : la correspondance ancien ->
    // nouveau niveau (plan.js remap : fiches des ennemis, terrains), et les quêtes hardcore de l'après-jeu, qui gardent
    // leurs niveaux de conception (90 à 99 : HARDCORE ; la partie y arrive vers 75, le reste se gagne en combattant)
    const avant = PL.charger(process.env.HISTOIRE_AVANT || C.ATELIER + '/Backup/02_11/histoire.avant-exp.js'), AVANT = {};
    for (const q of avant.QUETES) AVANT[q.id] = Object.fromEntries(q.etapes.map((e, i) => [i + 1, e.niv || 0]));
    for (const q of HARDCORE) if (AVANT[q]) NIV[q] = { ...AVANT[q] };
    for (const q of FIGEES) if (PL.ETAPES[q]) NIV[q] = Object.fromEntries(PL.ETAPES[q].map((v, i) => [i + 1, v]));   // figées : telles quelles
    const note = process.env.REFERENCE === 'annexes' ? 'joueur prudent, la moitié des annexes à leur ouverture' : '03/11/2026 : quêtes principales au niveau d\'un joueur prudent qui ne fait que l\'histoire et les rencontres de son chemin, annexes à celui d\'un joueur qui en fait la moitié à leur ouverture';
    fs.writeFileSync(__dirname + '/niveaux.json', JSON.stringify({ note: 'Niveau réel du joueur à chaque étape (progression.js niveaux, ' + note + ' ; quêtes hardcore gardées) : lu par plan.js (NIV), reporté dans js/histoire.js par appliquer.js ; AVANT : les niveaux d\'origine', NIV, AVANT, REEL: Object.fromEntries(Object.entries(REEL).reduce((m, [k, v]) => { const [q, st] = k.split('#'); (m.find(x => x[0] === q) || m[m.push([q, {}]) - 1])[1][st] = v; return m; }, [])) }, null, 1));
    fs.writeFileSync(__dirname + '/etat-boss.json', JSON.stringify(eb, null, 1));
    console.log('niveaux.json et etat-boss.json écrits');
    process.exit(0);
  }
  const cfgs = [
    ['A. Histoire seule, joueur prudent', { annexes: false }],
    ['B. Histoire et la moitié des annexes, prudent', { annexes: 0.5 }],
    ['C. Histoire et toutes les annexes, prudent (référence)', { annexes: true }],
    ['D. Histoire et toutes les annexes, insouciant', { annexes: true, joueur: 'insouciant' }],
  ];
  for (const [lab, o] of cfgs) {
    const R = []; for (let i = 0; i < n; i++) R.push(run(o));
    const r0 = R[0];
    console.log(`\n== ${lab}`);
    r0.log.forEach(l => console.log('  ' + l));
    const h = moy(R, r => r.t) / 3600;
    console.log(`  moyenne (${n} parties) : ${h.toFixed(1)} h · ${moy(R, r => r.fights).toFixed(0)} combats (${moy(R, r => r.renc).toFixed(0)} rencontres dont ${moy(R, r => r.groupes).toFixed(0)} groupes, ${moy(R, r => r.patr).toFixed(0)} patrouilles) · défaites ${moy(R, r => r.defaites).toFixed(1)}, REPRENDRE ${moy(R, r => r.morts).toFixed(1)} · niveau final ${moy(R, r => r.L).toFixed(1)}`);
    console.log(`  niveau à la fin : ${PL.PRINC.map(q => q + ' ' + moy(R, r => (r.actes.find(x => x.q === q) || { L: 0 }).L).toFixed(0)).join(' · ')}`);
    console.log(`  crédits : combats ${moy(R, r => r.cr_combat).toFixed(0)}, cristaux ${moy(R, r => r.cr_cristaux).toFixed(0)}, coffres ${moy(R, r => r.cr_coffres).toFixed(0)}, quêtes ${moy(R, r => r.cr_quete).toFixed(0)}, reventes ${moy(R, r => r.cr_vente).toFixed(0)} · dépensés : équipement ${moy(R, r => r.dep_eq).toFixed(0)}, potions ${moy(R, r => r.dep_pot).toFixed(0)}, éthers ${moy(R, r => r.dep_eth).toFixed(0)}, soins ${moy(R, r => r.dep_soins).toFixed(0)}, REPRENDRE ${moy(R, r => r.dep_reprendre).toFixed(0)} · reste ${moy(R, r => r.cr).toFixed(0)}`);
    const S = k => moy(R, r => r.soins[k]);
    console.log(`  soins : hôpital ${S('hopital').toFixed(1)}, clinique ${S('clinique').toFixed(1)}, verre ${S('verre').toFixed(1)}, replis par le téléport ${S('retraite').toFixed(1)} · potions ${S('potion').toFixed(0)}, éthers ${S('ether').toFixed(1)}, sorts de soin hors combat ${S('magie').toFixed(0)} · PV rendus : sorts ${(S('pv_magie') / 1000).toFixed(0)} k, potions ${(S('pv_potion') / 1000).toFixed(0)} k, lieux ${(S('pv_lieu') / 1000).toFixed(0)} k`);
    console.log(`  tronçons (d'une cible à la suivante) ${moy(R, r => r.troncons).toFixed(0)}, avec un REPRENDRE ${moy(R, r => r.troncons_mort).toFixed(1)} (${(100 * moy(R, r => r.troncons_mort) / moy(R, r => r.troncons)).toFixed(1)} %)`);
    const eb = R.flatMap(r => r.etatBoss);
    console.log(`  à l'arrivée des combats de l'histoire : PV ${(100 * eb.reduce((a, x) => a + x[1], 0) / eb.length).toFixed(0)} %, PM ${(100 * eb.reduce((a, x) => a + x[2], 0) / eb.length).toFixed(0)} %`);
    const B = {}; R.forEach(r => r.boss.forEach(([tag, nom, L, Le, essais]) => { const k = tag + ' ' + nom; (B[k] = B[k] || { L, Le: 0, e: 0, n: 0 }); B[k].Le += Le; B[k].e += essais; B[k].n++; }));
    const durs = Object.entries(B).filter(([k, b]) => b.e / b.n >= 0.5);
    if (durs.length) console.log('  combats de l\'histoire perdus en moyenne une fois ou plus : ' + durs.map(([k, b]) => `${k} (niv ${b.L}, équipe ${(b.Le / b.n).toFixed(0)}, ${(b.e / b.n).toFixed(1)})`).join(' · '));
  }
}
// Règles d'avant le 02/11/2026, pour comparer (audit « avant ») : ancienne courbe (15 + 0,1 L combats par niveau), réserve de
// PM qui suit le niveau, équipe soignée après chaque combat, REPRENDRE sans frais ; avec NIVEAUX=0 : les niveaux d'origine
function reglesAvant() {
  const exp0 = C.exp_next, pm0 = C.SANTE.pmNiveau;
  C.exp_next = L => Math.round((15 + 0.1 * L) * C.exp_of(L)); C.SANTE.pmNiveau = true;
  return () => { C.exp_next = exp0; C.SANTE.pmNiveau = pm0; };
}
module.exports.reglesAvant = reglesAvant;
