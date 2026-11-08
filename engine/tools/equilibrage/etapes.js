// Rencontres nécessaires entre les étapes de l'histoire (03/11/2026, demande de l'utilisateur : « même en farmant beaucoup, le
// jeu est très difficile ; il faudrait que les combats de l'histoire donnent la plus grande majorité de l'expérience
// nécessaire pour continuer l'aventure ») : deux joueurs suivent les quêtes principales de l'héroïne jouée (plan.js, Sera par
// défaut ; HEROINE=…), étape par étape, avec les scènes que joue un joueur de référence (plan.js lignesJouees, choix 1) :
//   A, « histoire seule » : rien que les combats de l'histoire, et l'Éveil de Sera (l'arc final après a61 est fait pour elle
//      transformée) ;
//   B, « au plus juste » : avant chaque combat de l'histoire, des rencontres aléatoires jusqu'à le gagner au moins SEUIL fois
//      sur 10 (9 par défaut).
// Combat de l'histoire au niveau min(étape, équipe) (OP_BATTLE de script.c), récompense × BOSS_RW ; équipe par défaut de la
// bande (règle du moteur), équipement acheté à chaque étape avec ses crédits (paliers jusqu'à max(équipe, histoire) + AVANCE,
// gamme du lieu, comme progression.js), état d'arrivée : PV pleins, PM d'etat-boss.json (calibrer.js), 3 potions. Rencontres
// comme game.c : terrain de la cible de l'étape, niveau min(histoire, L + 1) − 0..2, groupes (tailleGroupe), récompense,
// cristaux et coffres comme progression.js. « Chemin » : rencontres qu'on fait en marchant de la cible d'avant à celle-ci
// (distance × DETOUR, à pied, NORMALES ; moitié du trajet au-delà de 150 cases en véhicule dès l'acte II), pour comparer.
//   node etapes.js [N]   (N : combats par mesure, 300 ; SEUIL=0.9 ; JSON=fichier : les lignes en JSON ; RW=… : essai d'une
//   autre récompense des combats de l'histoire)
const fs = require('fs');
const C = require('./combat.js'), PL = require('./plan.js'), Q = require('./equipement.js'), M = require('./monde.js');
const P = Q.P, HYP = require('./progression.js').HYP;
const N = +(process.argv[2] || 300), SEUIL = +(process.env.SEUIL || 0.9), CAP = 5000;
const RW = +(process.env.RW || C.BOSS_RW), RW_CR = RW;
const FEB = process.env.ETAT_BOSS || __dirname + '/etat-boss.json', EB = fs.existsSync(FEB) ? JSON.parse(fs.readFileSync(FEB, 'utf8')) : {};
const OUV = JSON.parse(fs.readFileSync(__dirname + '/ouverture.json', 'utf8')).etapes;
const pmArrivee = (q, st) => { const v = EB[q + '#' + st] !== undefined ? EB[q + '#' + st] : EB[q]; return PL.QUETE[q] && PL.QUETE[q].type === 'principale' && v !== undefined ? Math.max(0.35, v) : 0.84; };   // calibrer.js
const parNom = n => Q.OBJETS().find(o => PL.norm(o.nom) === PL.norm(n));
// Cristaux du Chœur (03/11/2026, comme progression.js ; CRISTAUX=0 : sans) : chaque cristal trouvé à son niveau est porté par un
// membre au combat qui ne connaît pas son sort ; une victoire où il combat lui donne 3 points (histoire) ou 1 (rencontre) ; à son
// total, le sort est appris pour de bon
const CRX = process.env.CRISTAUX !== '0' && Q.CRISTAUX ? Q.CRISTAUX().filter(c => c.sort >= 0) : [];
const poser = g => { for (const k of team(g)) { if (g.porte[k]) continue;
  const c = CRX.find(x => x.dispo <= g.L && !g.appris[k].has(x.sort) && !g.porte.some(p => p && p.c === x)); if (c) g.porte[k] = { c, pts: 0 }; } };
const points = (g, pt) => { if (!CRX.length) return; poser(g); for (const k of team(g)) { const p = g.porte[k]; if (!p) continue;
  p.pts += pt; if (p.pts >= p.c.pts) { g.appris[k].add(p.c.sort); g.porte[k] = null; } } poser(g); };
const apprisDe = g => CRX.length ? Object.fromEntries(team(g).map(k => [k, [...g.appris[k]].concat(g.porte[k] ? [g.porte[k].c.sort] : [])])) : null;

function joueur(nom) {
  const g = { nom, L: 1, xp: 0, cr: 0, band: [0], eq: C.BASE.map(() => [null, null, null]), h: 1, rang: 0, xpH: 0, xpR: 0, renc: 0, eveil: false,
    appris: C.BASE.map(() => new Set()), porte: C.BASE.map(() => null) };
  for (const n of Q.departNoms(0)) { const o = parNom(n); if (o) g.eq[0][Q.SLOT(o.cat)] = o; }
  return g;
}
const team = g => PL.equipeDefaut(g.band);
const gear = g => ({ stats: C.BASE.map((b, k) => { const s = { atq: 0, def: 0, pv: 0, pm: 0, mag: 0, vit: 0, sorts: [] }, j = C.jobDe(k);
  for (const o of g.eq[k]) if (o) { const f = [0, 0.7, 0.85, 1, 1.2][Math.min(4, j.aff[Q.CATS.indexOf(o.cat)] | 0)]; for (const q of ['atq', 'def', 'pv', 'pm', 'mag', 'vit']) s[q] += (o[q] || 0) * f; s.sorts.push(...o.sorts); }
  return s; }) });
const gain = (g, xp) => { g.xp += xp; while (g.L < C.NIV_MAX && g.xp >= C.exp_next(g.L)) { g.xp -= C.exp_next(g.L); g.L++; } };
// Boutique (equiper de progression.js) : le meilleur de chaque emplacement des membres au combat, si la bourse le permet
const gamme = g => g.rang < PL.rangDe('acte1', 2) ? 4 : g.rang < PL.rangDe('acte4', 4) ? 10 : 14;
function boutique(g) {
  const cap = gamme(g), lim = Math.max(g.L, g.h) + P.AVANCE;
  for (const k of team(g)) for (let s = 0; s < 3; s++) {
    const best = Q.equipe(k, Math.min(lim, P.NIV[cap]), false);
    const nomV = best.noms.find(n => { const o = Q.OBJETS().find(x => x.nom === n); return o && Q.SLOT(o.cat) === s; });
    const o = nomV && Q.OBJETS().find(x => x.nom === nomV), cur = g.eq[k][s];
    if (!o || (cur && cur.palier >= o.palier) || o.palier > cap) continue;
    const net = o.prix - (cur ? Math.round(cur.prix / 2) : 0);
    if (g.cr - net < 2 * HYP.POT_PRIX) continue;
    g.cr -= net; g.eq[k][s] = o;
  }
}
const eveilDe = (g, q) => g.eveil && PL.FINAL.has(q) && q !== 'a61' ? [0] : null;
function victoire(g, noms, mode, niv, q) {
  const T = team(g), Lf = Math.max(1, Math.min(niv, g.L)), fs_ = noms.map(n => C.auNiveau(C.ENNEMIS[n], Lf));
  const et = { hp: {}, mp: {} }; for (const k of T) { et.hp[k] = 1; et.mp[k] = pmArrivee(q, g.st); }
  return C.winRate(T, g.L, fs_, 3, N, 'bon', gear(g), mode, { eveil: eveilDe(g, q), etat: et, appris: apprisDe(g) });
}
// Butin d'équipement (comme progression.js, 03/11/2026 au soir) : un objet du palier du combat, 35 % pour un combat de l'histoire,
// 6 % pour une rencontre (15 % rare ou élite) ; porté s'il vaut mieux que celui d'un membre au combat, sinon revendu
function butin(g, lvl, ch) {
  if (C.rand() >= ch) return;
  const p = P.palierDe(lvl), l = Q.OBJETS().filter(o => o.palier === p); if (!l.length) return;
  const o = l[Math.floor(C.rand() * l.length) % l.length];
  for (const k of team(g)) { const s = Q.SLOT(o.cat), cur = g.eq[k][s]; if (C.jobDe(k).aff[Q.CATS.indexOf(o.cat)] > 0 && (!cur || cur.palier < o.palier)) { if (cur) g.cr += Math.round(cur.prix / 2); g.eq[k][s] = o; return; } }
  g.cr += Math.round(o.prix / 2);
}
const tire = tb => { let r = C.rand() * tb.reduce((a, e) => a + e.poids, 0); for (const e of tb) { r -= e.poids; if (r <= 0) return e; } return tb[tb.length - 1]; };
function rencontre(g, zone) {
  const m = team(g).length, tb = zone ? M.table(zone, g.h, m) : [];
  let k = 1, coeur = false, rare = false;
  if (tb.length) { const e = tire(tb); k = C.tailleGroupe(g.h, m, e); coeur = !!e.coeur; rare = C.elite(e) || e.poids <= 2; }
  const lv = Math.max(1, Math.min(g.h, g.L + 1) - Math.floor(C.rand() * 3)), parts = C.partGroupe(k);
  const xp = Math.round(C.exp_of(lv) * parts), cr = Math.round(C.credits_of(lv) * parts) + (coeur ? Math.round(parts * 100) : 0);
  gain(g, xp); g.xpR += xp; g.cr += Math.round(cr * 1.12); g.renc++; points(g, 1); butin(g, lv, rare ? 0.15 : 0.06);
}
// Combat de l'histoire : B fait d'abord les rencontres qu'il faut ; la victoire (RÉESSAYER au besoin) rapporte × RW
function combat(g, noms, mode, niv, q, zone, farm) {
  const L0 = g.L, r0 = g.renc;
  let w = victoire(g, noms, mode, niv, q);
  const w0 = w.p;
  if (farm) while (w.p < SEUIL && g.L < 99 && g.renc - r0 < CAP) {
    const Lp = g.L; while (g.L === Lp && g.renc - r0 < CAP) rencontre(g, zone);
    boutique(g); w = victoire(g, noms, mode, niv, q);
  }
  // niveau requis : le plus bas où l'équipe, avec cet équipement, gagne encore au moins SEUIL fois sur 10 (au plus bas, le niveau
  // de l'étape s'il est sous le sien : le combat suit l'équipe en dessous, BOSS_ECART 0)
  const Lc = g.L;
  let Lreq = Lc;
  if (farm && w.p >= SEUIL) { const L1 = g.L; while (Lreq > Math.min(niv, L1) && Lreq > 1) { g.L = Lreq - 1; if (victoire(g, noms, mode, niv, q).p < SEUIL) break; Lreq--; } g.L = L1; }
  const Lf = Math.max(1, Math.min(niv, g.L)), parts = noms.length > 1 ? C.partGroupe(noms.length) : 1;
  const xp = Math.round(C.exp_of(Lf) * parts) * RW, cr = Math.round(C.credits_of(Lf) * parts) * RW_CR;
  gain(g, xp); g.xpH += xp; g.cr += Math.round(cr * 1.03); points(g, 3); butin(g, Lf, 0.35);
  return { L0, Lc, Lreq, Lfin: g.L, renc: g.renc - r0, w0, w: w.p, ok: w.p >= SEUIL };
}
// Lignes d'une étape (quête q, étape st) jouées par g
function etape(g, q, st, zone, farm, out) {
  const niv = PL.niveauEtape(q, st) || g.h; g.st = st;
  for (const ev of C.H.EVENTS) {
    if (ev.quete !== q || (ev.etape || 0) !== st || !PL.evDansPartie(ev)) continue;
    for (const l of PL.lignesJouees(ev, 1)) {
      let m;
      if ((m = /^combat\s+(.+)$/i.exec(l))) {
        const c = PL.combatGroupe(m[1]); if (!c.noms.every(n => C.ENNEMIS[n])) continue;
        const Lav = g.L, r = combat(g, c.noms, c.mode, niv, q, zone, farm);
        out.push({ noms: c.noms, mode: c.mode, niv, Lav, ...r });
      } else if ((m = /^(?:experience|expérience)\s+(\d+)/i.exec(l))) { const v = +m[1]; gain(g, v); g.xpH += v; }
      else if ((m = /^donner credits\s+(-?\d+)/i.exec(l))) g.cr = Math.max(0, g.cr + +m[1]);
      else if ((m = /^donner\s+(.+?)\s+(\d+)$/i.exec(l)) && parNom(m[1])) { const o = parNom(m[1]); for (const k of team(g)) { const sl = Q.SLOT(o.cat), cur = g.eq[k][sl]; if ((C.jobDe(k).aff[Q.CATS.indexOf(o.cat)] | 0) && (!cur || cur.palier < o.palier)) { g.eq[k][sl] = o; break; } } }
      else if ((m = /^(?:rejoindre|equipe)\s+(.+)$/i.exec(l))) for (const nm of m[1].split(/[\s,]+/)) { const k = PL.MEMBRES.map(PL.norm).indexOf(PL.norm(nm)); if (k > 0 && !g.band.includes(k)) { g.band.push(k); for (const n of Q.departNoms(k)) { const o = parNom(n); if (o && (!g.eq[k][Q.SLOT(o.cat)] || g.eq[k][Q.SLOT(o.cat)].palier < o.palier)) g.eq[k][Q.SLOT(o.cat)] = o; } } }
    }
  }
}
// Rencontres du chemin, d'une cible à la suivante (même carte) : comme marche de progression.js, à pied
function chemin(a, b, rangQ) {
  if (!a || !b || (a[2] || '') !== (b[2] || '')) return 0;
  const d = Math.hypot(b[0] - a[0], b[1] - a[1]) * HYP.DETOUR, veh = !b[2] && rangQ >= PL.rangDe(HYP.VEH_DES, 1) && d > HYP.VEH_SEUIL;
  const aPied = veh ? HYP.VEH_SEUIL + (d - HYP.VEH_SEUIL) * (1 - HYP.VEH_PART) : d;
  return aPied * HYP.ECH / (C.W.renc * HYP.RENC_FOIS);
}

// C, « chemin » : l'histoire et les rencontres qu'on fait en marchant d'une cible à la suivante (chemin), sans en chercher d'autres ;
// D : comme C, et des rencontres en plus du chemin quand il le faut pour gagner au moins SEUIL fois sur 10
function partie() {
  const A = joueur('A'), B = joueur('B'), Cc = joueur('C'), D = joueur('D'), J = [A, B, Cc, D], lignes = [];
  let pos = null;
  const ESSAI_EVEIL = 'eveil-sera';
  Cc.acc = 0; D.acc = 0;
  for (const q of PL.PRINC) {
    const pts = M.ETAPES_POS[q] || [];
    for (const g of J) etape(g, q, 0, null, false, []);
    PL.QUETE[q].etapes.forEach((e, i) => {
      const st = i + 1, niv = PL.ETAPES[q][i], cible = pts[i] || pos;
      for (const g of J) { g.rang = PL.rangDe(q, st); if (niv) g.h = Math.max(g.h, niv); }
      const zone = cible ? M.zoneAt(cible[0], cible[1], cible[2]) : null, zone0 = pos ? M.zoneAt(pos[0], pos[1], pos[2]) : zone;
      const nat = chemin(pos, cible, A.rang);
      for (const g of [Cc, D]) for (g.acc += nat; g.acc >= 1; g.acc--) { rencontre(g, C.rand() < 0.5 ? zone0 : zone); g.chemin = (g.chemin || 0) + 1; }
      for (const g of J) boutique(g);
      const la = [], lb = [], lc = [], ld = [], LA = A.L, LB = B.L, LC = Cc.L, hb0 = B.xpH, rb0 = B.xpR;
      etape(A, q, st, zone, false, la);
      etape(B, q, st, zone, true, lb);
      etape(Cc, q, st, zone, false, lc);
      etape(D, q, st, zone, true, ld);
      if (la.length) lignes.push({ q, st, niv, nat, LA, LB, LC, zone, A: la, B: lb, C: lc, D: ld, xpH: B.xpH - hb0, xpR: B.xpR - rb0, partH: B.xpH / Math.max(1, B.xpH + B.xpR),
        partC: Cc.xpH / Math.max(1, Cc.xpH + Cc.xpR), renc: lb.reduce((s, x) => s + x.renc, 0), rencD: ld.reduce((s, x) => s + x.renc, 0), LAfin: A.L, LBfin: B.L });
      if (cible) pos = cible;
      // l'Éveil de Sera, à son ouverture (après a61#3) : l'arc final est étalonné avec elle transformée
      if (PL.HEROINE === 'SERA' && (OUV[ESSAI_EVEIL] || [])[0] === q + '#' + st) {
        const lx = { A: [], B: [], C: [], D: [] }, L0 = { A: A.L, B: B.L, C: Cc.L };
        for (const g of J) { etape(g, ESSAI_EVEIL, 0, null, false, []); etape(g, ESSAI_EVEIL, 1, zone, g === B || g === D, lx[g.nom]); g.eveil = true; }
        if (lx.B.length) lignes.push({ q: ESSAI_EVEIL, st: 1, niv: PL.niveauEtape(ESSAI_EVEIL, 1), nat: 0, LA: L0.A, LB: L0.B, LC: L0.C, zone, A: lx.A, B: lx.B, C: lx.C, D: lx.D, xpH: 0, xpR: 0,
          partH: B.xpH / Math.max(1, B.xpH + B.xpR), partC: Cc.xpH / Math.max(1, Cc.xpH + Cc.xpR), renc: lx.B.reduce((s, x) => s + x.renc, 0), rencD: lx.D.reduce((s, x) => s + x.renc, 0), LAfin: A.L, LBfin: B.L });
      }
    });
  }
  return { lignes, A, B, C: Cc, D };
}

if (require.main === module) {
  const t0 = Date.now(), r = partie(), pc = v => (100 * v).toFixed(0).padStart(3) + ' %';
  const ACTE = q => ({ prologue: 'Prologue', acte1: 'Acte I', acte2a: 'Acte II (Au large)', acte2: 'Acte II', acte3: 'Acte III', acte4: 'Acte IV', acte5: 'Acte V', a61: 'Arc final', a62: 'Arc final', a63: 'Arc final', 'eveil-sera': 'Éveil de Sera' })[q] || q;
  console.log(`Partie de ${PL.HEROINE} : combats de l'histoire × ${RW} d'expérience, victoire visée ${pc(SEUIL)}, ${N} combats par mesure, ${CRX.length ? CRX.length + ' cristaux du Chœur' : 'sans cristaux'}, magie des jobs ${C.MAGIE && C.MAGIE.jobs ? 'par niveau' : "d'avant"}`);
  console.log('A : histoire seule (niveau, victoire la plus basse de l\'étape) | C : histoire et rencontres du chemin | B : au plus juste, niveau à l\'arrivée → niveau du combat (niveau requis), rencontres faites avant, part de l\'histoire dans son expérience');
  console.log('D : comme C, et des rencontres en plus du chemin quand il le faut pour gagner au moins ' + pc(SEUIL).trim() + ' (colonne « D en plus »)');
  console.log('étape        niv |   A       |   C       |   B                                       | chemin | D en plus | combats (victoire de B à l\'arrivée → au combat)');
  let acte = '';
  const wmin = l => l.length ? Math.min(...l.map(x => x.w0)) : NaN, w = v => isNaN(v) ? '   — ' : pc(v);
  for (const l of r.lignes) {
    if (ACTE(l.q) !== acte) { acte = ACTE(l.q); console.log('-- ' + acte); }
    const Lc = Math.max(...l.B.map(x => x.Lc)), Lr = Math.max(...l.B.map(x => x.Lreq)), pire = l.B.some(x => !x.ok);
    console.log(`${(l.q + '#' + l.st).padEnd(12)} ${String(l.niv).padStart(2)} | ${String(l.LA).padStart(2)} ${w(wmin(l.A))} | ${String(l.LC).padStart(2)} ${w(wmin(l.C))} | ${String(l.LB).padStart(2)} → ${String(Lc).padStart(2)} (${String(Lr).padStart(2)})${pire ? ' jamais' : '       '} ${String(l.renc).padStart(4)} renc., ${pc(l.partH)} | ${l.nat.toFixed(1).padStart(5)}  | ${String(l.rencD).padStart(5)}     | ${l.B.map(x => x.noms[0] + (x.noms.length > 1 ? (x.mode === 'sbires' ? ' +' : ' ×') + x.noms.length : '') + ' ' + pc(x.w0).trim() + '→' + pc(x.w).trim()).join(', ')}`);
  }
  const tot = r.lignes.reduce((s, l) => s + l.renc, 0), nat = r.lignes.reduce((s, l) => s + l.nat, 0);
  console.log(`Total : B fait ${tot} rencontres, finit au niveau ${r.B.L}, ${pc(r.B.xpH / (r.B.xpH + r.B.xpR))} de son expérience par l'histoire ; C fait les ${r.C.renc} du chemin, finit au niveau ${r.C.L}, ${pc(r.C.xpH / (r.C.xpH + r.C.xpR))} par l'histoire ; D en fait ${r.D.renc - (r.D.chemin || 0)} en plus du chemin, ${pc(r.D.xpH / (r.D.xpH + r.D.xpR))} par l'histoire ; A finit au niveau ${r.A.L} · ${((Date.now() - t0) / 1000).toFixed(0)} s`);
  if (process.env.JSON) fs.writeFileSync(process.env.JSON, JSON.stringify(r.lignes, null, 1));
}
module.exports = { partie };
