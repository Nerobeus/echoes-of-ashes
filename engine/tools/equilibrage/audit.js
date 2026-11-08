// Audit de difficulté (01/11/2026) : node audit.js [courbe] [histoire] [equipes] [rencontres] [economie] (tout sans argument)
//   courbe     : partie simulée (progression.js, modèle du 02/11/2026) : histoire seule et toutes les annexes, joueur prudent ;
//                niveau à la fin de chaque acte, défaites (la santé et les soins : audit-sante.js)
//   histoire   : chaque combat de l'histoire et des annexes à son niveau, équipe par défaut (règle du moteur) : victoire au
//                premier essai et PV perdus, sans équipement, avec celui du palier du moment (« normal ») et de la boutique
//                (palier jusqu'au niveau + 2) ; Sera transformée (ÉVEIL) dans l'arc final après a61 et l'après-jeu
//   equipes    : écart entre les équipes possibles (toutes celles de la bande) pour les boss
//   rencontres : chaque terrain à son niveau de découverte et plus loin, seul et en groupe (à parité, équipe par défaut)
//   economie   : crédits par heure, prix des paliers, coffres, amendes
// N=… : combats par mesure (200)
const C = require('./combat.js'), PL = require('./plan.js'), Q = require('./equipement.js'), M = require('./monde.js');
const N = +(process.env.N || 200), args = process.argv.slice(2), voir = s => !args.length || args.includes(s);
const pct = v => (v * 100).toFixed(0).padStart(3) + ' %';
const R = PL.rangsDe(C.ENNEMIS);
// Équipement normal : celui du palier du moment, Sera seule comprise (01/11/2026 : le prologue l'équipe, Odric et la
// Guilde du secteur 6 ; avant, Sera seule n'avait rien) ; un membre arrivé à l'étape du combat : son équipement de départ
const normal = (L, team, nus) => Q.gear(L, false, null, nus);
// Sera transformée (quête eveil-sera, ouverte après a61) : arc final sur Europe et après-jeu (calibrer.js, 02/11/2026)
const eveilDe = c => (PL.FINAL.has(c.quete) && c.quete !== 'a61') || (c.type !== 'principale' && c.niv >= 90) ? [0] : null;

if (voir('courbe')) {
  const P = require('./progression.js');
  for (const [lab, o] of [['histoire seule', { annexes: false }], ['toutes les annexes', { annexes: true }]]) {
    const runs = []; for (let i = 0; i < +(process.env.PARTIES || 3); i++) runs.push(P.run(o));
    const m = f => runs.reduce((a, r) => a + f(r), 0) / runs.length;
    console.log(`\n== Courbe de niveau, histoire principale (${lab} ; ${runs.length} parties)`);
    runs[0].log.forEach(l => console.log('  ' + l));
    const h = m(r => r.t) / 3600;
    console.log(`  ${(m(r => r.fights) / h).toFixed(1)} combats/h (${m(r => r.renc).toFixed(0)} rencontres dont ${m(r => r.groupes).toFixed(0)} en groupe, ${m(r => r.patr).toFixed(0)} patrouilles) · défaites ${m(r => r.defaites).toFixed(1)} · niveau final ${m(r => r.L).toFixed(1)}`);
    console.log(`  crédits : combats ${m(r => r.cr_combat).toFixed(0)}, cristaux ${m(r => r.cr_cristaux).toFixed(0)}, coffres ${m(r => r.cr_coffres).toFixed(0)}, quêtes ${m(r => r.cr_quete).toFixed(0)}, reventes ${m(r => r.cr_vente).toFixed(0)} ; dépensés : équipement ${m(r => r.dep_eq).toFixed(0)}, potions ${m(r => r.dep_pot).toFixed(0)} ; reste ${m(r => r.cr).toFixed(0)}`);
    const B = {}; runs.forEach(r => r.boss.forEach(([tag, nom, L, Le, e]) => { const k = tag + ' ' + nom; B[k] = B[k] || { L, Le: 0, e: 0, n: 0 }; B[k].Le += Le; B[k].e += e; B[k].n++; }));
    const lignes = Object.entries(B).filter(([k, b]) => b.e / b.n > 0 || ['boss', 'elite'].includes((R[k.split(' ').slice(1).join(' ')] || {}).rang));
    console.log('  boss : niveau de l\'étape / de l\'équipe, défaites avant la victoire : ' + lignes.map(([k, b]) => `${k} ${b.L}/${(b.Le / b.n).toFixed(0)} ${(b.e / b.n).toFixed(1)}`).join(' · '));
  }
}

if (voir('histoire')) {
  console.log('\n== Combats de l\'histoire à leur niveau, équipe par défaut : victoire au premier essai (PV perdus) · sans | normal | boutique' + ' (Sera transformée dès 82)');
  const vus = new Set();
  for (const c of PL.COMBATS) {
    const key = c.noms.join('+') + c.niv + c.mode; if (vus.has(key)) continue; vus.add(key);
    const L = c.niv, team = PL.equipeDefaut(PL.bandeDe(c)), r = R[c.noms[0]] || {};
    if (!L || !C.ENNEMIS[c.noms[0]]) continue;
    const w = eq => C.winRate(team, L, c.noms, 3, N, 'bon', eq, c.mode, { eveil: eveilDe(c) });
    const nus = PL.arrivesDe(c), a = w(null), b = w(normal(L, team, nus)), d = w(Q.gear(L + Q.P.AVANCE, false, null, nus));
    console.log(`${(c.quete + '#' + c.etape).padEnd(18)} ${String(L).padStart(2)} ${(c.noms[0] + (c.noms.length > 1 ? (c.mode === 'sbires' ? ' +' : ' ×') + c.noms.length : '')).padEnd(28)} ${(r.rang || '').padEnd(9)} vise ${pct(r.cible || 0)} | ${pct(a.p)} | ${pct(b.p)} (${pct(b.perte)}) ${b.t.toFixed(0).padStart(4)} s | ${pct(d.p)} (${pct(d.perte)})${eveilDe(c) ? ` · sans ÉVEIL ${pct(C.winRate(team, L, c.noms, 3, N, 'bon', normal(L, team), c.mode).p)}, les quatre transformés ${pct(C.winRate(team, L, c.noms, 3, N, 'bon', normal(L, team), c.mode, { eveil: team }).p)}` : ''}`);
  }
}

if (voir('equipes')) {
  console.log('\n== Écart entre équipes (boss, équipement normal) : défaut, médiane, [pire, meilleure]');
  const combos = bande => { const o = [], r = bande.filter(m => m); for (let a = 0; a < r.length; a++) for (let b = a + 1; b < r.length; b++) for (let c = b + 1; c < r.length; c++) o.push([0, r[a], r[b], r[c]]); return o.length ? o : [[0].concat(r)]; };
  for (const [nom, r] of Object.entries(R)) {
    if (!['boss', 'elite', 'final', 'hardcore'].includes(r.rang)) continue;
    const c = r.ref, L = c.niv, bande = PL.bandeDe(c), def = PL.equipeDefaut(bande), nus = PL.arrivesDe(c);
    let ts = combos(bande); if (ts.length > 24) ts = ts.filter((t, i) => i % Math.ceil(ts.length / 24) === 0);
    const res = ts.map(t => [t, C.winRate(t, L, c.noms, 3, 120, 'bon', normal(L, t, nus), c.mode, { eveil: eveilDe(c) }).p]).sort((x, y) => x[1] - y[1]);
    const d = C.winRate(def, L, c.noms, 3, N, 'bon', normal(L, def, nus), c.mode, { eveil: eveilDe(c) }).p;
    const nm = t => t.slice(1).map(m => C.NOMS[m].slice(0, 3)).join('-');
    console.log(`${nom.padEnd(24)} ${String(L).padStart(2)} défaut ${pct(d)} · médiane ${pct(res[res.length >> 1][1])} · [${pct(res[0][1])} ${nm(res[0][0])}, ${pct(res[res.length - 1][1])} ${nm(res[res.length - 1][0])}] (${res.length} équipes)`);
  }
}

if (voir('rencontres')) {
  console.log('\n== Rencontres à parité (équipe par défaut de ce niveau, équipement normal) : seul | groupes (taille de game.c) | élites');
  const tire = tb => { let r = C.rand() * tb.reduce((a, e) => a + e.poids, 0); for (const e of tb) { r -= e.poids; if (r <= 0) return e; } return tb[tb.length - 1]; };
  for (const [z, n0] of Object.entries(M.ZONE_NIV).sort((a, b) => a[1] - b[1])) {
    for (const L of [n0, n0 + 15, n0 + 35].filter(l => l <= 96)) {
      const team = PL.equipeDefaut(PL.bandeAuNiveau(L)), m = team.length, tb = M.table(z, L, m);
      if (!tb.length) continue;
      const seul = { p: 0, t: 0, pe: 0, n: 0 }, grp = { p: 0, t: 0, pe: 0, n: 0 }, el = { p: 0, n: 0 };
      for (let i = 0; i < N * 2; i++) {
        const e = tire(tb), k = C.tailleGroupe(L, m, e), noms = [e.nom];
        const comp = tb.filter(q => q !== e && !C.elite(q) && !!q.coeur === !!e.coeur);
        for (let j = 1; j < k; j++) noms.push(comp.length && C.rand() >= 0.6 ? tire(comp).nom : e.nom);
        const lv = Math.max(1, L - Math.floor(C.rand() * 3));   // à parité : min(h, L + 1) − 0..2, h = L
        const fs = noms.map(x => C.auNiveau(C.ENNEMIS[x], lv)), rr = C.battle(team, L, fs.length > 1 ? fs : fs[0], { potions: 3 }, 'bon', normal(L, team));
        const s = C.elite(e) ? el : k > 1 ? grp : seul;
        s.n++; s.p += rr.win; if (s !== el) { s.t += rr.t; s.pe += rr.perte; }
      }
      const f = s => s.n ? `${pct(s.p / s.n)} ${(s.t / s.n).toFixed(0).padStart(3)} s, PV −${pct(s.pe / s.n)}` : '—';
      console.log(`${z.padEnd(16)} niv ${String(L).padStart(2)} (${m} membres) : seul ${f(seul)} | groupes ${(100 * grp.n / (N * 2)).toFixed(0)} % ${f(grp)} | élites ${el.n ? pct(el.p / el.n) + ' (' + (100 * el.n / (N * 2)).toFixed(0) + ' %)' : '—'}`);
    }
  }
}

// Magie (03/11/2026 : progression des sorts par job, cristaux du Chœur, maîtres des jobs ; node audit.js magie) : chaque combat de
// l'histoire à son niveau, équipe par défaut, équipement normal ; A la règle d'avant (sorts du paquet, MAGIE.jobs faux), B les
// sorts des jobs par niveau, C B et tous les cristaux trouvés jusque-là appris par toute l'équipe (borne haute), D C et Sera
// MAGICIEN dès le maître des archives (niveau MAITRE_MAG) ; puis les rencontres de chaque terrain (A, B, C)
if (args.includes('magie')) {
  const MAITRE_MAG = +(process.env.MAITRE_MAG || 45), R2 = {};
  const cfg = (L, team, lettre) => {
    C.MAGIE.jobs = lettre !== 'A';
    const ap = lettre >= 'C' ? Q.apprisAuNiveau(L) : [], appris = {}; team.forEach(k => { appris[k] = ap; });
    const jobs = lettre === 'D' && L >= MAITRE_MAG ? { 0: 'MAGICIEN' } : null;
    return { appris, jobs, eq: Q.gear(L, false, jobs, null) };
  };
  console.log('\n== Magie : combats de l\'histoire, victoire au premier essai (PV perdus, durée) · A avant | B sorts des jobs | C + cristaux | D + Sera MAGICIEN (dès ' + MAITRE_MAG + ')');
  const vus = new Set(), S = { A: [], B: [], C: [], D: [] };
  for (const c of PL.COMBATS) {
    const key = c.noms.join('+') + c.niv + c.mode; if (vus.has(key)) continue; vus.add(key);
    const L = c.niv, team = PL.equipeDefaut(PL.bandeDe(c)), r = R[c.noms[0]] || {};
    if (!L || !C.ENNEMIS[c.noms[0]]) continue;
    const res = {};
    for (const lt of ['A', 'B', 'C', 'D']) {
      const g = cfg(L, team, lt);
      res[lt] = C.winRate(team, L, c.noms, 3, N, 'bon', g.eq, c.mode, { eveil: eveilDe(c), appris: g.appris, jobs: g.jobs });
      S[lt].push({ rang: r.rang || 'autre', p: res[lt].p, t: res[lt].t, perte: res[lt].perte });
    }
    if (process.env.DETAIL) console.log(`${(c.quete + '#' + c.etape).padEnd(18)} ${String(L).padStart(2)} ${c.noms[0].padEnd(24)} ${(r.rang || '').padEnd(9)} ` +
      ['A', 'B', 'C', 'D'].map(lt => `${lt} ${pct(res[lt].p)} (${pct(res[lt].perte)}) ${res[lt].t.toFixed(0).padStart(3)} s`).join(' | '));
  }
  C.MAGIE.jobs = true;
  const rangs = [...new Set(S.A.map(x => x.rang))];
  for (const rg of rangs.concat(['tous'])) {
    const f = (l, k) => { const v = l.filter(x => rg === 'tous' || x.rang === rg); return v.length ? v.reduce((a, x) => a + x[k], 0) / v.length : 0; };
    const n = S.A.filter(x => rg === 'tous' || x.rang === rg).length;
    console.log(`  ${rg.padEnd(10)} (${String(n).padStart(3)}) : ` + ['A', 'B', 'C', 'D'].map(lt => `${lt} ${pct(f(S[lt], 'p'))} PV −${pct(f(S[lt], 'perte'))} ${f(S[lt], 't').toFixed(0)} s`).join(' | '));
  }
  // EQUIPES=1 : les mêmes combats (sauf les ordinaires) avec d'autres équipes, A et B : Lyse au combat (la bergère et ses
  // soins), deux magiciens (l'Étudiant et Azur, avec Lyse ou Varn)
  if (process.env.EQUIPES) for (const [nomT, team] of [['avec Lyse', [0, 1, 2, 3]], ['Lyse et deux magiciens', [0, 3, 6, 8]], ['Varn et deux magiciens', [0, 1, 6, 8]]]) {
    const T2 = { A: [], B: [] }, vus2 = new Set();
    for (const c of PL.COMBATS) {
      const key = c.noms.join('+') + c.niv + c.mode, r = R[c.noms[0]] || {}; if (vus2.has(key) || !c.niv || !C.ENNEMIS[c.noms[0]] || r.rang === 'ordinaire' || c.niv < 30) continue; vus2.add(key);
      for (const lt of ['A', 'B']) { const g = cfg(c.niv, team, lt); T2[lt].push({ rang: r.rang || 'autre', p: C.winRate(team, c.niv, c.noms, 3, N, 'bon', g.eq, c.mode, { eveil: eveilDe(c) }).p }); }
    }
    C.MAGIE.jobs = true;
    const moy = (l, rg) => { const v = l.filter(x => rg === 'tous' || x.rang === rg); return v.length ? pct(v.reduce((a, x) => a + x.p, 0) / v.length) : '  —'; };
    console.log(`  équipe ${nomT.padEnd(24)} (niveau 30 et plus) : ` + ['boss', 'annexe', 'elite', 'final', 'hardcore', 'tous'].map(rg => `${rg} ${moy(T2.A, rg)} → ${moy(T2.B, rg)}`).join(' · '));
  }
  console.log('\n== Magie : rencontres à parité (équipe par défaut, équipement normal), victoire et PV perdus · A | B | C');
  const tire = tb => { let x = C.rand() * tb.reduce((a, e) => a + e.poids, 0); for (const e of tb) { x -= e.poids; if (x <= 0) return e; } return tb[tb.length - 1]; };
  const T = { A: { p: 0, pe: 0, t: 0, n: 0 }, B: { p: 0, pe: 0, t: 0, n: 0 }, C: { p: 0, pe: 0, t: 0, n: 0 } };
  for (const [z, n0] of Object.entries(M.ZONE_NIV).sort((a, b) => a[1] - b[1])) {
    for (const L of [n0, n0 + 15, n0 + 35].filter(l => l <= 96)) {
      const team = PL.equipeDefaut(PL.bandeAuNiveau(L)), m = team.length, tb = M.table(z, L, m);
      if (!tb.length) continue;
      const ligne = [];
      for (const lt of ['A', 'B', 'C']) {
        const g = cfg(L, team, lt), s = { p: 0, pe: 0, t: 0, n: 0 };
        for (let i = 0; i < N; i++) {
          const e = tire(tb), k = C.tailleGroupe(L, m, e), noms = [e.nom];
          for (let j = 1; j < k; j++) noms.push(e.nom);
          const lv = Math.max(1, L - Math.floor(C.rand() * 3)), fs = noms.map(x => C.auNiveau(C.ENNEMIS[x], lv));
          const rr = C.battle(team, L, fs.length > 1 ? fs : fs[0], { potions: 3 }, 'bon', g.eq, 'parts', { appris: g.appris, garde: 0.35 });
          s.n++; s.p += rr.win; s.pe += rr.perte; s.t += rr.t;
        }
        T[lt].p += s.p; T[lt].pe += s.pe; T[lt].t += s.t; T[lt].n += s.n;
        ligne.push(`${lt} ${pct(s.p / s.n)} PV −${pct(s.pe / s.n)} ${(s.t / s.n).toFixed(0)} s`);
      }
      if (process.env.DETAIL) console.log(`${z.padEnd(16)} niv ${String(L).padStart(2)} : ${ligne.join(' | ')}`);
    }
  }
  C.MAGIE.jobs = true;
  console.log('  toutes : ' + ['A', 'B', 'C'].map(lt => `${lt} ${pct(T[lt].p / T[lt].n)} PV −${pct(T[lt].pe / T[lt].n)} ${(T[lt].t / T[lt].n).toFixed(0)} s`).join(' | '));
}

if (voir('economie')) {
  const P = Q.P;
  console.log('\n== Économie : palier, niveau, équipement des quatre au combat (12 pièces), crédits des combats d\'un palier à parité (2 + 18 (L − 1) / 58 par niveau, 22 par heure), amendes');
  for (let p = 1; p <= P.VENDU_MAX; p++) {
    const L0 = P.NIV[p], L1 = P.NIV[p + 1] || 100;
    let cr = 0, n = 0; for (let L = L0; L < L1; L++) { const k = C.combatsNiveau(L); cr += k * (C.credits_of(L) + 60) * 1.12; n += k; }
    const prix = Q.prixEquipe(PL.equipeDefaut(PL.bandeAuNiveau(L0)), L0), h = n / 22;
    // amendes (police.c, interieur.c, 01/11/2026) : vol (2 + recherche) × max(100, crédits d'un combat), effraction (1 + serrure) × max(50, …)
    const vol = 3 * Math.max(100, C.credits_of(L0)), eff = 3 * Math.max(50, C.credits_of(L0));
    console.log(`${String(p).padStart(2)} ${P.NOMS[p].padEnd(10)} niv ${String(L0).padStart(2)} : équipement ${String(prix).padStart(7)} CR · ${Math.round(n)} combats (${h.toFixed(1)} h) = ${Math.round(cr).toString().padStart(7)} CR (${Math.round(cr / h)} CR/h) → ${(100 * prix / cr).toFixed(0)} % · un combat ${C.credits_of(L0)} CR · amende d'un vol (recherche 1) ${vol}, d'une effraction (serrure 2) ${eff} (${(vol / C.credits_of(L0)).toFixed(1)} combats)`);
  }
}
