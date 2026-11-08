// Le monde vu par l'équilibrage : cible de chaque étape des quêtes (kv quetes et personnages du projet : lieu, ou place du
// personnage), carte, terrain des rencontres sous un point (ZONE du paquet sur la planète ; un terrain par autre carte),
// ennemis possibles d'un terrain (encounter_pick_at de game.c : zones, enc_ok), niveau d'histoire où l'on découvre chaque
// terrain (en marchant d'une cible à la suivante, quêtes principales).
const fs = require('fs');
const C = require('./combat.js'), PL = require('./plan.js');
const Q = JSON.parse(fs.readFileSync(C.K + 'quetes.json', 'utf8'))[1];
const PERS = JSON.parse(fs.readFileSync(C.K + 'personnages.json', 'utf8'))[1];
const pid = {}; PERS.forEach(p => pid[p.id] = p);
const LAND = ['mer', 'lac', 'rive', 'plaine', 'champs', 'foret', 'montagne', 'haute-montagne', 'urbain', 'gravats', 'industriel', 'rue', 'cendre', 'eau-claire',
  'neige', 'desert', 'marais', 'foret-regeneree', 'caverne', 'regolithe', 'desert-rouge', 'jungle', 'espace'];
// Terrain des autres cartes (js/cartes.js : souterrain, Lune, Mars, planète luxuriante, système solaire)
const TERRAIN_CARTE = { souterrain: 'caverne', lune: 'regolithe', mars: 'desert-rouge', luxuriante: 'jungle', venus: 'jungle', systeme: 'espace', europe: 'regolithe', station: 'espace', ceinture: 'regolithe', phobos: 'regolithe' };
const N = 1024;   // côté de la planète (cases)
function zoneAt(x, z, carte) {
  if (carte) return TERRAIN_CARTE[carte] || null;
  const i = Math.floor(Math.floor(x) * C.W.zoneN / N), j = Math.floor(Math.floor(z) * C.W.zoneN / N);
  if (i < 0 || j < 0 || i >= C.W.zoneN || j >= C.W.zoneN) return null;
  const t = C.W.zone[j * C.W.zoneN + i];
  return t < LAND.length ? LAND[t] : null;
}
// Cible d'une étape : [x, z, carte] ('' : la planète)
function cible(e) {
  const c = e.cible || {};
  if (c.mode === 'lieu') return [c.x, c.z, c.carte || ''];
  if (c.mode === 'perso' && pid[c.perso] && (pid[c.perso].places || [])[0]) { const p = pid[c.perso].places[0]; return [p[0], p[1], p[3] || '']; }
  return null;
}
const ETAPES_POS = {};   // id de quête (histoire) -> [cible de chaque étape]
for (const q of Q) if (q.histoire) ETAPES_POS[q.histoire] = q.etapes.map(cible);
// Ennemis possibles sur un terrain à l'histoire h avec m membres (enc_ok de game.c)
const RENC_ECART = 2, RENC_MIN = 5, ELITE_EQUIPE = 3;
function table(z, h, m) {
  const ici = Object.values(C.ENNEMIS).filter(e => !e.legende && (e.zones || []).includes(z) && !(C.elite(e) && m < ELITE_EQUIPE));
  // Un terrain qui a des ennemis n'est jamais vide (enc_histoire de game.c)
  const bas = ici.length ? Math.min(...ici.map(e => e.lvl)) : 0, hh = h > 0 ? Math.max(h, bas - RENC_ECART) : h;
  return ici.filter(e => !(hh > 0 && e.lvl > Math.max(hh + RENC_ECART, RENC_MIN)));
}
// Niveau d'histoire où l'on découvre chaque terrain : le long du chemin des quêtes principales (pas d'une case)
const ZONE_NIV = {};
{
  let pos = null;
  for (const id of PL.PRINC) (ETAPES_POS[id] || []).forEach((p, i) => {
    const niv = PL.ETAPES[id][i] || 1;
    if (!p) return;
    if (pos && pos[2] === p[2]) {
      const d = Math.hypot(p[0] - pos[0], p[1] - pos[1]), n = Math.ceil(d);
      for (let s = 0; s <= n; s++) { const z = zoneAt(pos[0] + (p[0] - pos[0]) * s / Math.max(1, n), pos[1] + (p[1] - pos[1]) * s / Math.max(1, n), p[2]); if (z && ZONE_NIV[z] === undefined) ZONE_NIV[z] = niv; }
    } else { const z = zoneAt(p[0], p[1], p[2]); if (z && ZONE_NIV[z] === undefined) ZONE_NIV[z] = niv; }
    pos = p;
  });
  // les cibles de toutes les quêtes (annexes comprises), puis les terrains qu'aucune cible ne touche (d'après la carte de
  // l'histoire : champs près de Lucerne, rives et eau claire de l'acte II, neiges et sommets du col des Cendres)
  for (const q of PL.H.QUETES) (ETAPES_POS[q.id] || []).forEach((p, i) => {
    const z = p && zoneAt(p[0], p[1], p[2]), n = PL.ETAPES[q.id][i];
    if (z && n && (ZONE_NIV[z] === undefined || n < ZONE_NIV[z])) ZONE_NIV[z] = n;
  });
  for (const [z, n] of Object.entries({ champs: 8, rive: 14, 'eau-claire': 32, neige: 37, 'haute-montagne': 40 })) if (ZONE_NIV[z] === undefined) ZONE_NIV[z] = PL.remap(n);   // niveaux d'origine, remis à l'échelle du 02/11/2026
}
// Niveau d'étalonnage d'un ennemi des rencontres : celui du premier de ses terrains qu'on découvre
const niveauZones = e => Math.min(...(e.zones || []).map(z => ZONE_NIV[z] === undefined ? 99 : ZONE_NIV[z]));
// Cartes du paquet (CART : la k-ième est la carte k) par l'identifiant de l'Atelier (js/cartes.js, cibles des étapes)
const CARTE_IDS = ['', 'souterrain', 'lune', 'mars', 'luxuriante', 'systeme', 'station', 'ceinture', 'phobos', 'europe', 'circuits-friches', 'circuits-sables'];
const carteId = k => k === 0 ? '' : CARTE_IDS[k] || ('carte' + k);
// Lieux de soins (02/11/2026, audit de la santé) : bâtiments de PORT dont le commerce (COMM) soigne (cliniques, hôpital : PV et
// PM de l'équipe) ou sert un verre (20 % des PV) ; boutiques qui vendent la POTION (objet 0 de la table) ; points du
// téléport (TELE : monde, ville, lieu) ; passages entre cartes (PASS)
const SOINS = [], BOUTIQUES = [];
for (const b of C.W.batiments) {
  const c = b.comm >= 0 ? C.W.comms[b.comm] : null; if (!c) continue;
  const p = { x: b.x, z: b.z, carte: carteId(b.carte), nom: b.nom, acces: b.acces };
  if (c.soins) SOINS.push({ ...p, sorte: c.soins >= 80 ? 'hopital' : 'clinique', base: c.soins });
  else if (c.verre) SOINS.push({ ...p, sorte: 'verre', base: c.verre });
  if (c.vend.includes(0)) BOUTIQUES.push(p);
}
// Soins des campements (02/11/2026 : la tente du troc soigne, js/commerces.js TYPES.troc.soins) : ajoutés d'après js/commerces.js
// tant que le paquet n'est pas réexporté (CAMPS=0 : sans eux)
{
  global.TilesetGen = global.TilesetGen || {};
  const f = C.ATELIER + '/js/commerces.js'; delete require.cache[require.resolve(f)]; require(f);
  const troc = process.env.CAMPS_SOINS !== undefined ? +process.env.CAMPS_SOINS : (TilesetGen.Commerces.TYPES.troc || {}).soins || 0;   // CAMPS_SOINS : essai d'un autre prix
  if (troc && process.env.CAMPS !== '0') for (const b of C.W.batiments) if (b.nom === 'TENTE DU TROC' && !SOINS.some(s => s.x === b.x && s.z === b.z))
    SOINS.push({ x: b.x, z: b.z, carte: carteId(b.carte), nom: b.nom, acces: 0, sorte: 'clinique', base: troc });
  // Infirmeries de la Lune et de Tharsis (02/11/2026, Cartes.batir de js/cartes.js : cliniques, toujours ouvertes sur les autres
  // cartes) : places relevées à l'aperçu des cartes, tant que le paquet n'est pas réexporté (INFIRMERIES=0 : sans elles)
  if (process.env.INFIRMERIES !== '0') for (const s of [{ nom: 'Infirmerie des Veilleurs', carte: 'lune', x: 376.5, z: 158.5 }, { nom: 'Infirmerie de Tharsis', carte: 'mars', x: 330.5, z: 244.5 }])
    if (!SOINS.some(q => q.nom === s.nom)) SOINS.push({ ...s, acces: 0, sorte: 'clinique', base: 40 });
}
const TELE = C.W.tele.map(t => ({ ...t, carte: carteId(t.carte) }));
const PASSAGES = C.W.passages.map(p => ({ ...p, carte: carteId(p.carte), vers: carteId(p.vers) }));
module.exports = { LAND, zoneAt, cible, ETAPES_POS, table, ZONE_NIV, niveauZones, TERRAIN_CARTE, RENC_ECART, RENC_MIN, CARTE_IDS, carteId, SOINS, BOUTIQUES, TELE, PASSAGES };
if (require.main === module) {
  console.log('Terrains découverts (niveau d\'histoire) :', Object.entries(ZONE_NIV).sort((a, b) => a[1] - b[1]).map(([z, n]) => z + ' ' + n).join(', '));
  for (const [z, n] of Object.entries(ZONE_NIV).sort((a, b) => a[1] - b[1])) console.log(`  ${z.padEnd(16)} ${String(n).padStart(2)} : ${table(z, n, 4).map(e => `${e.nom} (${e.lvl}, p${e.poids}${C.elite(e) ? ', élite' : ''})`).join(', ')}`);
}
