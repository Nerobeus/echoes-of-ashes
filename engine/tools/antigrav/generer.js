// Paquet de la variante « course anti-gravité » (core/antigrav.c) : pistes à thème, leurs textures et leurs horizons, engins
// low-poly texturés aux livrées des écuries. Les polices, les icônes et les sons sont repris d'un paquet d'Echoes (FONT, ICON,
// AUDI : rien de propre au monde). Conception du jeu : docs/course-anti-gravite.md.
//
//   node tools/antigrav/generer.js [data/antigrav/antigrav.pak] [data/monde_esp32.pak]
//
// Tout est original : le genre (course de glisseurs anti-gravité façon PS1) et ses mécaniques, pas un jeu existant. Les
// écuries reprennent des marques de véhicules d'Echoes ; les pistes, des lieux du monde du Chœur des Cendres.
//
// Repère : x à droite, y en haut, z devant (celui du moteur : lacet h, devant = (sin h, 0, cos h), droite = (cos h, 0, -sin h)).
// Unités : cases du moteur ; un engin fait 1,4 de long, la piste 4 à 5,6 de large.
//
// Palette : index 0 transparent, 1 à PAL_COMMUN - 1 commune (écuries, engins, livrées), PAL_COMMUN à 255 propre au thème de la
// piste (chargée avec elle : antigrav.c, piste_charger). Les horizons ont leur palette à eux (RVB).
//
// Format des sections (lues par core/antigrav.c ; nombres petit-boutistes) :
//   AGJE  nom du jeu (24 octets UTF-8)
//   AGPS  pistes : version u8 (1), nombre u8, 0 u16 ; chaque piste : longueur du bloc u32 (la suite), nom (24), lieu (24),
//         hauteur du sol f32, tours u8, horizon u8 (rang dans AGHZ, 255 aucun), 0 u16, textures u16 × AGT_N (grande texture
//         DECO de chaque rôle AGT_*, puis leur moitié, puis leur quart : 3 × AGT_N), ciel haut, ciel à l'horizon, brouillard (r, g, b u8 × 3 chacun), lumière u8 (/100),
//         teinte r, g, b u8 (/100), 0 u8 × 2 ; couleurs du thème u8 (au plus 256 - PAL_COMMUN), 0 × 3, puis 4 octets chacune
//         (r, g, b, pleine lumière) à partir de l'index PAL_COMMUN ; sections : nombre u32, puis 32 octets chacune : centre
//         x, y, z f32, droite rx, ry, rz f32 (unitaire, dévers compris), demi-largeur f32, drapeaux u8 (1 ligne de départ,
//         2 pilier dessous), 0 × 3 ; plaques : nombre u32, puis 8 octets chacune : section u16, écart à l'axe i8 (1/16 de
//         case), demi-largeur u8 (1/16), longueur u8 (sections), sorte u8 (0 accélération, 1 arme), 0 u16
//   AGHZ  horizons : nombre u8, 0 × 3 ; chacun : largeur u16 (puissance de 2 : 360°), hauteur u16, ligne d'horizon u16 (rang de
//         la ligne de l'image posée sur l'horizon), couleurs u16, couleurs × (r, g, b), 0 jusqu'au multiple de 4, pixels
//         largeur × hauteur u8 (0 : transparent, le ciel ; 1 + rang de la couleur), 0 jusqu'au multiple de 4
//   AGCK  cockpits (facultatif) : écuries u8, 0 × 3 ; chaque écurie (ordre d'AGEC) : images u8, 0 × 3, puis ses images comme
//         celles d'AGHZ (ligne d'horizon 0), la plus petite d'abord (320 × 240 pour la LDK, 960 × 540) ; pixels 0 : la vitre
//   AGNA  engins (version 3) : version u8, nombre u8, 0 u16 ; chaque engin : nom (16), niveaux u8, 0 × 3 ; chaque niveau :
//         sommets u16, faces u16, distance f32 (au-delà : le suivant ; 0 le dernier), sommets f32 × 3, faces de 20 octets :
//         nombre de sommets u8, place u8 (AGC_*, 255 : couleur fixe), couleur fixe u8, drapeaux u8 (1 : texture de la livrée),
//         sommets u16 × 4, coordonnées de texture (u, v u8) × 4 dans la livrée (texels : 64 × 128, dessus en haut, flancs en bas ;
//         engin texturé : 256 × 256, l'atlas du modèle)
//   AGEC  écuries (version 2) : nombre u32, puis 64 octets chacune : nom (16), engin u8, livrée u8 (grande texture DECO),
//         numéro u8, 0, couleurs des places u8 × 8 (index de la palette commune), vitesse, accélération, maniabilité f32,
//         devise (24 octets UTF-8)
//   PAL   comme Echoes : nombre u32, 256 × (r, g, b, pleine lumière), 8 octets (eau, inutilisés) ; le thème de la première piste
//   TEX   tuile 0 (16 × 16) : la palette (texel i = index i, faces de couleur unie des engins), puis les grandes textures
//   DECO  comme Echoes : grande texture k = tuile TILE_BIG + k
'use strict';
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..', '..');
const SORTIE = path.resolve(process.argv[2] || path.join(root, 'data', 'antigrav', 'antigrav.pak'));
const SOURCE = path.resolve(process.argv[3] || path.join(root, 'data', 'monde_esp32.pak'));
const JEU = 'AntiGraV';   // (14/11/2026, demande de l'utilisateur ; titre provisoire avant : COURSE ANTI-GRAVITÉ)

// ---------------------------------------------------------------- Hasard reproductible
let graine = 1234567;
const hasard = () => { graine = (graine * 1103515245 + 12345) >>> 0; return ((graine >>> 8) & 0xFFFF) / 65536; };
const bruit = (x, y, s) => { let h = (x * 374761393 + y * 668265263 + s * 2147483647) >>> 0; h = ((h ^ (h >>> 13)) * 1274126177) >>> 0; return ((h ^ (h >>> 16)) & 0xFFFF) / 65536; };

// ---------------------------------------------------------------- Palette : commune, puis celle du thème en cours
const PAL_COMMUN = 112;
const COMMUN = [[255, 0, 255, 0]];   // 0 : transparent (aucune texture opaque ne l'emploie)
let THEME = null;                    // couleurs du thème en cours (index PAL_COMMUN + rang), null : on remplit la commune
const couleur = (r, g, b, plein = 0) => {
    const egal = c => c[0] === r && c[1] === g && c[2] === b && c[3] === plein;
    const i = COMMUN.findIndex((c, k) => k > 0 && egal(c));
    if (i > 0) return i;
    if (!THEME) {
        if (COMMUN.length >= PAL_COMMUN) throw new Error('palette commune pleine');
        COMMUN.push([r, g, b, plein]);
        return COMMUN.length - 1;
    }
    const j = THEME.findIndex(egal);
    if (j >= 0) return PAL_COMMUN + j;
    if (PAL_COMMUN + THEME.length >= 256) throw new Error('palette du thème pleine');
    THEME.push([r, g, b, plein]);
    return PAL_COMMUN + THEME.length - 1;
};
const hex = (h, plein = 0) => couleur(h >> 16 & 255, h >> 8 & 255, h & 255, plein);
const melange = (a, b, t) => { const m = (s) => Math.round((a >> s & 255) + ((b >> s & 255) - (a >> s & 255)) * t); return m(16) << 16 | m(8) << 8 | m(0); };
// Rampe de n couleurs de a à b (hex)
const rampe = (a, b, n, plein = 0) => Array.from({ length: n }, (_, k) => hex(melange(a, b, n > 1 ? k / (n - 1) : 0), plein));

// ---------------------------------------------------------------- Textures
class Tex {
    constructor(w, h, fond) { this.w = w; this.h = h; this.p = new Uint8Array(w * h).fill(fond); }
    set(x, y, c) { x = ((x % this.w) + this.w) % this.w; y = ((y % this.h) + this.h) % this.h; this.p[y * this.w + x] = c; }
    get(x, y) { x = ((x % this.w) + this.w) % this.w; y = ((y % this.h) + this.h) % this.h; return this.p[y * this.w + x]; }
    rect(x0, y0, w, h, c) { for (let y = y0; y < y0 + h; y++) for (let x = x0; x < x0 + w; x++) this.set(x, y, c); }
}
// Grain : chaque texel tiré dans la rampe autour de son rang (b = rang de base, e = écart)
const grain = (t, r, b, e, s) => { for (let y = 0; y < t.h; y++) for (let x = 0; x < t.w; x++) t.set(x, y, r[Math.max(0, Math.min(r.length - 1, b + Math.floor((bruit(x, y, s) - 0.5) * 2 * e + 0.5)))]); };

// Thèmes : ciel et brouillard (render.c, ambiance de la piste), lumière et teinte, rampes des textures, sol, horizon
const THEMES = {
    nuit: {   // NÉONS DE LUCERNE : la métropole la nuit
        ciel: [0x050616, 0x3c1650, 0x1a0a30], lum: 0.8, teinte: [0.92, 0.92, 1.12],
        chaussee: [0x15151f, 0x50526c], metal: [0x1c1e2c, 0x70749a], neonA: [0x0a4a70, 0x8ff6ff], neonB: [0x6a1060, 0xff8ee8],
        accent: [0x704010, 0xffd070], beton: [0x22243a, 0x76789a], sol: { style: 'ville', c: [0x0a0a14, 0x34344c], feux: [0xffd070, 0x80f0ff, 0xff70d0] },
        horizon: { style: 'ville', base: 0x0c0a24, feux: [0xffd070, 0x80f0ff, 0xff70d0, 0xff4040] },
        roche: { style: 'beton', c: [0x16182a, 0x5a5e7a] }, terre: [0x16161e, 0x4a4c5e], relief: { style: 'plat' }
    },
    crepuscule: {   // CRÊTE DE CENDRE : les Friches au soleil couchant
        ciel: [0x3a2468, 0xf6765a, 0xce8e8c], lum: 0.8, teinte: [1.15, 0.92, 0.88],
        chaussee: [0x1c1f27, 0x6c7386], metal: [0x2a2e38, 0xa8b0c0], neonA: [0x0c5a70, 0x9ff8ff], neonB: [0x6a1050, 0xff9ce8],
        accent: [0x7a3a08, 0xffe08a], beton: [0x3a3c42, 0x9a9890], sol: { style: 'cendre', c: [0x1e1a1c, 0x6e6058] },
        horizon: { style: 'collines', base: 0x2a1a2a, feux: [0xffa040] },
        roche: { style: 'basalte', c: [0x1c1618, 0x6e5a54] }, terre: [0x2a2220, 0x7e6a5c], relief: { style: 'collines', haut: 11, echelle: 38 }
    },
    midi: {   // CANYON DES SABLES : plein midi
        ciel: [0x3e7ed0, 0xe6dccb, 0xe2d2b8], lum: 1.0, teinte: [1.06, 1.0, 0.92],
        chaussee: [0x3c3632, 0xb0a494], metal: [0x4a4038, 0xd0c0a8], neonA: [0x803000, 0xffa040], neonB: [0x204070, 0x80c8ff],
        accent: [0x806000, 0xfff070], beton: [0x6a5a48, 0xdccab0], sol: { style: 'sable', c: [0x8a5a30, 0xf2ca92] },
        horizon: { style: 'mesas', base: 0x8a4a2a, feux: [] },
        roche: { style: 'gres', c: [0x6a2e16, 0xeeae70] }, terre: [0x8a5a30, 0xe6b884], relief: { style: 'mesas', haut: 18, echelle: 52 }
    },
    aube: {   // COL DES CENDRES : la neige à l'aube
        ciel: [0x2c3a78, 0xf8b8a8, 0xd8d0e2], lum: 0.92, teinte: [1.0, 0.97, 1.08],
        chaussee: [0x283040, 0x8c98b4], metal: [0x384060, 0xbccae2], neonA: [0x105a80, 0xa0f0ff], neonB: [0x703060, 0xffb0e0],
        accent: [0x704020, 0xffc890], beton: [0x5a6070, 0xccd4e4], sol: { style: 'neige', c: [0x7888a4, 0xf6f8ff] },
        horizon: { style: 'montagnes', base: 0x3a4a72, feux: [] },
        roche: { style: 'granite', c: [0x343c52, 0xf2f6ff] }, terre: [0x7484a2, 0xeef2fa], relief: { style: 'montagnes', haut: 38, echelle: 64 }
    },
    // Biomes des jeux (14/11/2026, demande de l'utilisateur : « créons un thème mars, lune, luxuriant, souterrain, île ») ; feuillage :
    // texture des décors végétaux (sorte 2), sinon pris de la roche ; eau : le sol est la mer (render.c : AG.eau)
    mars: {   // CANYONS DE THARSIS : Mars, poussière et buttes rouges
        ciel: [0x5a3a46, 0xe0a47c, 0xcc9070], lum: 0.95, teinte: [1.12, 0.95, 0.86],
        chaussee: [0x2a2224, 0x8a7a76], metal: [0x3a3030, 0xc0a8a0], neonA: [0x0a5060, 0x80f0ff], neonB: [0x702010, 0xff9060],
        accent: [0x804010, 0xffc070], beton: [0x4a3a36, 0xb8a094], sol: { style: 'mars', c: [0x4a2216, 0xd88a58] },
        horizon: { style: 'mesas', base: 0x6a3020, feux: [] },
        roche: { style: 'rouille', c: [0x341810, 0xd07a50] }, terre: [0x6a3420, 0xd8946a], relief: { style: 'mesas', haut: 16, echelle: 46 }
    },
    lune: {   // CRATÈRES DE LA LUNE : ciel noir, régolithe gris
        ciel: [0x020206, 0x18181f, 0x101016], lum: 1.05, teinte: [1.0, 1.0, 1.05],
        chaussee: [0x1a1c24, 0x7a8090], metal: [0x2a2e3a, 0xc0c8d8], neonA: [0x0a4a70, 0x8ff6ff], neonB: [0x504080, 0xd0b0ff],
        accent: [0x707070, 0xfffff0], beton: [0x40444e, 0xb0b4bc], sol: { style: 'regolithe', c: [0x26262a, 0xbcbcc0] },
        horizon: { style: 'montagnes', base: 0x30303a, feux: [] },
        roche: { style: 'regolithe', c: [0x18181e, 0xc4c4c8] }, terre: [0x3a3a40, 0xacacb2], relief: { style: 'crateres', haut: 12, echelle: 70 }
    },
    venus: {   // JUNGLE DE VÉNUS : la planète luxuriante, brume chaude, arbres géants
        ciel: [0x2e5a48, 0xd6e6a0, 0xa6c48e], lum: 0.95, teinte: [0.95, 1.08, 0.92],
        chaussee: [0x1c2420, 0x6a7a70], metal: [0x24302a, 0xa8c0b0], neonA: [0x105a30, 0xa0ffb0], neonB: [0x601070, 0xf0a0ff],
        accent: [0x706010, 0xfff080], beton: [0x34403a, 0x9cb0a4], sol: { style: 'jungle', c: [0x0c2612, 0x62aa4a] },
        horizon: { style: 'collines', base: 0x1e4a2a, feux: [] },
        roche: { style: 'mousse', c: [0x14241a, 0x8a9a7a] }, terre: [0x2a2418, 0x7a6a44], feuille: [0x1a1030, 0xb070e0],
        relief: { style: 'collines', haut: 14, echelle: 34 }
    },
    souterrain: {   // GALERIES DES PROFONDEURS : sous terre, cristaux qui luisent
        ciel: [0x050307, 0x1a1420, 0x110d16], lum: 0.78, teinte: [0.95, 0.98, 1.15],
        chaussee: [0x16161c, 0x5a5a6c], metal: [0x22222c, 0x8a8aa4], neonA: [0x0a4a70, 0x8ff6ff], neonB: [0x401070, 0xc090ff],
        accent: [0x0a5a70, 0x80ffff], beton: [0x2a2830, 0x7a7684], sol: { style: 'grotte', c: [0x0a080e, 0x4a4250], feux: [0x60e0ff, 0xa080ff] },
        horizon: { style: 'montagnes', base: 0x141018, feux: [] },
        roche: { style: 'cristal', c: [0x0e0a12, 0x5e546a] }, terre: [0x1a1620, 0x5a5262], relief: { style: 'montagnes', haut: 30, echelle: 30 }
    },
    ile: {   // RÉCIFS DU LAGON : archipel au milieu de la mer, plages, palmiers
        ciel: [0x2a7ad0, 0xc2ecf2, 0xa6dcea], lum: 1.05, teinte: [1.0, 1.02, 1.0], eau: true,
        chaussee: [0x2a2c30, 0x9a9ca4], metal: [0x3a3e44, 0xd0d4dc], neonA: [0x006070, 0x80ffe0], neonB: [0x802040, 0xffa0c0],
        accent: [0x806000, 0xfff070], beton: [0x6a6458, 0xe0d8c8], sol: { style: 'mer', c: [0x08405e, 0x6ad6dc] },
        horizon: { style: 'collines', base: 0x2a6a4a, feux: [] },
        roche: { style: 'basalte', c: [0x1a1614, 0x6a5a50] }, terre: [0xa08a5a, 0xf2e4ba], feuille: [0x0a3014, 0x6ad040],
        relief: { style: 'iles', haut: 9, echelle: 42 }
    }
};

// Chaussée 64 × 64 : u en travers (0 à 63 d'un bord à l'autre), v le long (16 texels par case)
function texPiste(R, depart) {
    const t = new Tex(64, 64, R.ch[2]);
    grain(t, R.ch, 2, 1, 11);
    for (let y = 0; y < 64; y++) {
        if (y % 32 === 0) for (let x = 4; x < 60; x++) t.set(x, y, R.ch[0]);          // joints des dalles
        for (const x of [21, 42]) if (y % 16 < 9) t.set(x, y, R.ch[4]);               // voies, discrètes
        for (let x = 0; x < 4; x++) t.set(x, y, x === 0 ? R.ch[0] : x === 3 ? R.ch[1] : R.na[x === 1 ? 2 : 3]);   // bord gauche néon
        for (let x = 60; x < 64; x++) t.set(x, y, x === 63 ? R.ch[0] : x === 60 ? R.ch[1] : R.nb[x === 62 ? 2 : 3]);   // bord droit
        if (y % 16 === 8) { t.set(4, y, R.na[3]); t.set(59, y, R.nb[3]); }          // feux de bord
    }
    if (depart) for (let y = 0; y < 64; y++) for (let x = 4; x < 60; x++) t.set(x, y, ((x - 4) >> 3 ^ y >> 3) & 1 ? R.blanc : R.noir);
    return t;
}
// Mur intérieur 64 × 32 : u le long, v du haut (0 : néon du haut) au pied (31)
function texMur(R) {
    const t = new Tex(64, 32, R.me[3]);
    grain(t, R.me, 3, 1, 21);
    for (let x = 0; x < 64; x++) {
        t.set(x, 0, R.me[1]); t.set(x, 1, R.na[3]); t.set(x, 2, R.na[2]); t.set(x, 3, R.me[1]);
        for (let y = 27; y < 32; y++) t.set(x, y, R.me[1 + (y & 1)]);
    }
    for (let y = 4; y < 27; y++) { t.set(0, y, R.me[0]); t.set(32, y, R.me[0]); t.set(1, y, R.me[5]); t.set(33, y, R.me[5]); }
    for (const x of [6, 26, 38, 58]) for (const y of [7, 23]) t.set(x, y, R.me[6]);   // rivets
    for (let x = 10; x < 23; x++) for (let y = 12; y < 18; y++) if ((x + y) % 4 < 2) t.set(x, y, R.ac[1]);   // bandes d'avertissement
    for (let x = 42; x < 55; x++) for (let y = 12; y < 18; y++) if ((x + y) % 4 < 2) t.set(x, y, R.nb[1]);
    return t;
}
// Dessous et faces extérieures 32 × 32 : métal sombre, poutres, feux
function texDessous(R) {
    const t = new Tex(32, 32, R.ch[1]);
    grain(t, R.ch, 1, 1, 31);
    for (let i = 0; i < 32; i++) { t.set(i, 0, R.ch[0]); t.set(i, 16, R.ch[0]); t.set(0, i, R.ch[3]); t.set(15, i, R.ch[0]); }
    t.set(8, 8, R.ac[3]); t.set(24, 24, R.ac[3]); t.set(8, 9, R.ac[1]); t.set(24, 25, R.ac[1]);
    return t;
}
// Sol 64 × 64 (une dalle de AG_SOL_PAS cases) selon le style du thème
function texSol(R, style) {
    const s = R.so, t = new Tex(64, 64, s[2]);
    if (style === 'ville') {   // la ville vue d'en haut, la nuit : îlots sombres, rues, fenêtres allumées
        for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) {
            const rue = x % 16 < 2 || y % 16 < 2;
            t.set(x, y, rue ? s[3] : s[(bruit(x >> 2, y >> 2, 61) * 3) | 0]);
            if (!rue && bruit(x, y, 62) < 0.05) t.set(x, y, R.feux[(bruit(x, y, 63) * R.feux.length) | 0]);
        }
        for (let y = 0; y < 64; y += 16) for (let x = 0; x < 64; x += 4) t.set(x, y, R.feux[0]);   // lampadaires
    } else if (style === 'sable') {   // dunes : rides
        for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) {
            const r = Math.sin((x * 0.35 + y * 0.12 + Math.sin(y * 0.2) * 2)) * 0.5 + 0.5, n = bruit(x, y, 71) * 0.3;
            t.set(x, y, s[Math.min(s.length - 1, 2 + Math.floor((r * 0.7 + n) * 4))]);
        }
    } else if (style === 'neige') {   // neige, rochers
        for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) t.set(x, y, s[Math.min(s.length - 1, 4 + Math.floor((bruit(x >> 2, y >> 2, 81) * 0.6 + bruit(x, y, 82) * 0.4) * 4))]);
        for (let k = 0; k < 6; k++) { const cx = (hasard() * 64) | 0, cy = (hasard() * 64) | 0, r = 2 + hasard() * 3; for (let y = -4; y <= 4; y++) for (let x = -4; x <= 4; x++) if (x * x + y * y < r * r) t.set(cx + x, cy + y, s[x + y < 0 ? 1 : 0]); }
    } else if (style === 'mars') {   // poussière rouge, rides, cailloux
        for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) {
            const r = Math.sin(x * 0.3 + y * 0.1 + bruit(x >> 3, y >> 3, 403) * 3) > 0.75 ? 1 : 0;
            t.set(x, y, s[Math.min(s.length - 1, 2 + r + Math.floor((bruit(x >> 2, y >> 2, 401) * 0.6 + bruit(x, y, 402) * 0.4) * 3.2))]);
        }
        for (let k = 0; k < 14; k++) { const cx = Math.floor(bruit(k, 1, 404) * 64), cy = Math.floor(bruit(k, 2, 404) * 64), r = 1 + bruit(k, 3, 404) * 2; for (let y = -3; y <= 3; y++) for (let x = -3; x <= 3; x++) if (x * x + y * y < r * r) t.set(cx + x, cy + y, s[x + y < 0 ? 6 : 1]); }
    } else if (style === 'regolithe') {   // régolithe : grain fin gris, petits cratères (bord éclairé, creux dans l'ombre)
        for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) t.set(x, y, s[2 + Math.floor((bruit(x >> 3, y >> 3, 411) * 0.5 + bruit(x, y, 412) * 0.5) * 3)]);
        for (let k = 0; k < 7; k++) {
            const cx = Math.floor(bruit(k, 1, 413) * 64), cy = Math.floor(bruit(k, 2, 413) * 64), r = 2.5 + bruit(k, 3, 413) * 4.5;
            for (let y = -8; y <= 8; y++) for (let x = -8; x <= 8; x++) { const d = Math.sqrt(x * x + y * y); if (d < r - 1) t.set(cx + x, cy + y, s[x + y > 0 ? 3 : 1]); else if (d < r + 0.6) t.set(cx + x, cy + y, s[x + y > 0 ? 1 : 6]); }
        }
    } else if (style === 'jungle') {   // mousse et herbe dense, touffes plus sombres
        for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) t.set(x, y, s[Math.min(s.length - 1, 2 + Math.floor(bruit(x >> 1, y >> 1, 421) * 2.5 + bruit(x, y, 422) * 1.6))]);
        for (let k = 0; k < 18; k++) { const cx = Math.floor(bruit(k, 1, 423) * 64), cy = Math.floor(bruit(k, 2, 423) * 64), r = 2 + bruit(k, 3, 423) * 3; for (let y = -5; y <= 5; y++) for (let x = -5; x <= 5; x++) if (x * x + y * y < r * r) t.set(cx + x, cy + y, s[x + y < -1 ? 5 : 1]); }
    } else if (style === 'grotte') {   // roche sombre, éclats de cristal qui luisent
        for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) t.set(x, y, s[1 + Math.floor(bruit(x >> 2, y >> 2, 431) * 2.5 + bruit(x, y, 432) * 1.2)]);
        for (let k = 0; k < 26; k++) { const cx = Math.floor(bruit(k, 1, 433) * 64), cy = Math.floor(bruit(k, 2, 433) * 64), c = R.feux.length ? R.feux[k % R.feux.length] : s[7]; t.set(cx, cy, c); if (k % 3 === 0) { t.set(cx + 1, cy, c); t.set(cx, cy + 1, c); } }
    } else if (style === 'mer') {   // la mer : vaguelettes, reflets
        for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) {
            const v = Math.sin(x * 0.39 + Math.sin(y * 0.2) * 2.2) * 0.5 + 0.5, w = Math.sin(y * 0.29 + x * 0.05) * 0.5 + 0.5;
            t.set(x, y, s[Math.min(s.length - 1, 2 + Math.floor(v * 1.6 + w * 1.4 + bruit(x, y, 441) * 1.0))]);
        }
        for (let k = 0; k < 30; k++) t.set(Math.floor(bruit(k, 1, 442) * 64), Math.floor(bruit(k, 2, 442) * 64), s[7]);
    } else {   // cendre : fissures, éclats
        for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) t.set(x, y, s[1 + Math.floor((bruit(x >> 2, y >> 2, 41) * 0.6 + bruit(x, y, 42) * 0.4) * 3.2)]);
        let x = 5, y = 7;
        for (let k = 0; k < 140; k++) { t.set(x, y, s[0]); x += Math.floor(hasard() * 3) - 1; y += hasard() < 0.6 ? 1 : 0; }
        x = 40; y = 30;
        for (let k = 0; k < 90; k++) { t.set(x, y, s[0]); x += hasard() < 0.6 ? 1 : 0; y += Math.floor(hasard() * 3) - 1; }
        for (let k = 0; k < 30; k++) t.set(Math.floor(hasard() * 64), Math.floor(hasard() * 64), s[6]);
    }
    return t;
}
// Feuillage 64 × 64 (décors végétaux, sorte 2) : touffes de feuilles éclairées par le haut, creux sombres (sans tirage au hasard :
// les textures des pistes suivantes ne changent pas)
function texFeuille(R) {
    const f = R.fe, t = new Tex(64, 64, f[1]);
    for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) t.set(x, y, f[1 + Math.floor(bruit(x >> 1, y >> 1, 491) * 2.2)]);
    for (let k = 0; k < 70; k++) {
        const cx = Math.floor(bruit(k, 1, 492) * 64), cy = Math.floor(bruit(k, 2, 492) * 64), r = 2 + bruit(k, 3, 492) * 2.6;
        for (let y = -5; y <= 5; y++) for (let x = -5; x <= 5; x++) { const d = x * x + y * y; if (d < r * r) t.set(cx + x, cy + y, f[d < r * r * 0.3 ? 6 : x + y < 0 ? 5 : 3]); }
    }
    return t;
}
// Pilier 32 × 32 : béton, rainures
function texPilier(R) {
    const t = new Tex(32, 32, R.be[3]);
    grain(t, R.be, 3, 1, 51);
    for (let y = 0; y < 32; y++) { t.set(0, y, R.be[1]); t.set(16, y, R.be[1]); t.set(8, y, R.be[5]); t.set(24, y, R.be[5]); }
    for (let x = 0; x < 32; x++) t.set(x, 31, R.be[0]);
    return t;
}
// Plaque d'accélération 64 × 64 : chevrons pointés vers l'avant (v croissant)
function texVitesse(R) {
    const t = new Tex(64, 64, R.ch[1]);
    for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) {
        const cx = Math.abs(x - 31.5), ph = ((y + cx * 0.9) % 32 + 32) % 32;   // (pointe au milieu, la plus avancée : v croissant ; avec « - », ils pointaient vers l'arrière)
        if (x < 3 || x > 60) t.set(x, y, R.ac[3]);
        else if (ph < 9) t.set(x, y, ph < 2 || ph > 6 ? R.ac[1] : R.ac[3]);
    }
    return t;
}
// Plaque d'arme 64 × 64 (14/11/2026, retour de l'utilisateur : les losanges « pas beaux », pas compréhensibles) : cadre à rayures de
// danger (accent du thème), fond sombre, viseur néon au milieu (anneau, croix, point) : l'arme, à ne pas confondre avec les
// chevrons d'accélération ; écrasée par la perspective, la croix reste lisible (sans tirage au hasard : les autres textures ne bougent pas)
function texArme(R) {
    const t = new Tex(64, 64, R.ch[0]);
    for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) {
        if (x < 6 || x > 57 || y < 6 || y > 57) { t.set(x, y, ((x + y) >> 2) % 2 ? R.ac[3] : R.ch[0]); continue; }
        if (x < 8 || x > 55 || y < 8 || y > 55) { t.set(x, y, R.nb[1]); continue; }
        const dx = x - 31.5, dy = y - 31.5, d = Math.sqrt(dx * dx + dy * dy), e = Math.abs(d - 16.5);
        let c = (x + y) % 8 === 0 ? R.ch[1] : R.ch[0];
        if (e < 2.4) c = R.nb[3]; else if (e < 3.6) c = R.nb[1];
        const bras = (a, b) => Math.abs(a) < 2.4 && Math.abs(b) > 6 && Math.abs(b) < 25;
        if (bras(dx, dy) || bras(dy, dx)) c = R.nb[3];
        if (d < 3) c = R.ac[3];
        t.set(x, y, c);
    }
    return t;
}
// Terre 64 × 64 (chaussée des portions naturelles) : sillons le long, cailloux, bords plus sombres, balises au bord
function texTerre(R) {
    const t = new Tex(64, 64, R.te[3]);
    for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) {
        const n = bruit(x >> 1, y >> 2, 151) * 0.5 + bruit(x, y, 152) * 0.5, sillon = (x % 21 === 10 || x % 21 === 11) ? -1 : 0;
        t.set(x, y, R.te[Math.max(0, Math.min(7, 2 + Math.floor(n * 4) + sillon))]);
        if (x < 3 || x > 60) t.set(x, y, R.te[1]);
        if ((x === 2 || x === 61) && y % 16 < 3) t.set(x, y, R.na[3]);
    }
    for (let k = 0; k < 40; k++) t.set(Math.floor(hasard() * 64), Math.floor(hasard() * 64), R.te[7]);
    return t;
}
// Roche 64 × 64 (parois, pentes, voûtes ; v vers le bas) selon le style du thème
function texRoche(R, style) {
    const r = R.ro, t = new Tex(64, 64, r[3]);
    for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) {
        let k;
        if (style === 'gres') k = 2 + Math.floor((Math.sin(y * 0.55 + Math.sin(x * 0.11) * 1.5) * 0.5 + 0.5) * 3 + bruit(x >> 2, y, 161) * 1.6);   // strates
        else if (style === 'granite') { k = 1 + Math.floor(bruit(x >> 2, y >> 2, 171) * 3 + bruit(x, y, 172) * 1.5); if (bruit(x >> 3, y >> 3, 173) > 0.72 && y % 32 < 12) k = 7; }   // plaques de neige
        else if (style === 'beton') { k = 3 + Math.floor(bruit(x, y, 181) * 1.5); if (y % 32 === 0 || x % 32 === 0) k = 1; }
        else if (style === 'rouille') k = 2 + Math.floor((Math.sin(y * 0.42 + bruit(x >> 3, y >> 3, 451) * 3) * 0.5 + 0.5) * 3 + bruit(x, y, 452) * 1.5);   // strates rouges
        else if (style === 'regolithe') k = 2 + Math.floor(bruit(x >> 2, y >> 2, 461) * 3 + bruit(x, y, 462) * 1.2);
        else if (style === 'mousse') { k = 1 + Math.floor(bruit(x >> 2, y >> 2, 471) * 2.5 + bruit(x, y, 472)); if (bruit(x >> 2, (y >> 2) + 1, 473) > 0.62) k = -1; }   // roche moussue
        else if (style === 'cristal') k = 1 + Math.floor(bruit(x >> 1, y >> 1, 481) * 2 + bruit(x, y, 482));
        else k = 1 + Math.floor(bruit(x >> 1, y >> 1, 191) * 2.5 + bruit(x, y, 192));   // basalte
        t.set(x, y, k < 0 ? R.so[3 + Math.floor(bruit(x, y, 474) * 3)] : r[Math.max(0, Math.min(7, k))]);
    }
    if (style === 'cristal' && R.feux.length) for (let v = 0; v < 6; v++) {   // filons de cristal qui luisent
        let x = Math.floor(bruit(v, 1, 483) * 64), y = Math.floor(bruit(v, 2, 483) * 64);
        const c = R.feux[v % R.feux.length];
        for (let j = 0; j < 14; j++) { t.set(x, y, c); x += bruit(v, j, 484) < 0.5 ? 1 : 0; y++; }
    }
    if (style === 'beton') for (let x = 4; x < 64; x += 32) for (let y = 14; y < 18; y++) for (let i = 0; i < 24; i++) t.set(x + i, y, R.ac[3]);   // rampes de lumière
    else for (let k = 0; k < 7; k++) { let x = Math.floor(hasard() * 64), y = Math.floor(hasard() * 64); for (let j = 0; j < 18; j++) { t.set(x, y, r[0]); x += Math.floor(hasard() * 3) - 1; y++; } }   // fissures
    return t;
}
// Textures réduites (13/11/2026, retour de l'utilisateur : « des textures qui clignotent » : sans brouillard, une texture fine vue de
// loin ou de biais scintille quand la caméra bouge, le tracé n'ayant pas de mipmaps) : la moitié et le quart (moyenne de chaque bloc
// de 2 × 2 et de 4 × 4 texels, la plus proche dans les palettes commune et du thème en cours) ; antigrav.c en fait la table MIPS que
// submit_poly de render.c consulte (niveau d'après les texels par pixel de la face, comme pour les bâtiments d'Echoes)
function rvbIndex(i) { return i < PAL_COMMUN ? COMMUN[i] : THEME[i - PAL_COMMUN]; }
function plusProche(r, g, b) {
    let best = 1, bd = 1e9;
    const essai = (c, i) => { if (!c || i === 0) return; const d = (c[0] - r) ** 2 + (c[1] - g) ** 2 + (c[2] - b) ** 2; if (d < bd) { bd = d; best = i; } };
    COMMUN.forEach((c, i) => essai(c, i));
    THEME.forEach((c, i) => essai(c, PAL_COMMUN + i));
    return best;
}
function moyenne(t, x0, y0, w, h) {
    let r = 0, g = 0, b = 0, n = 0;
    for (let y = y0; y < y0 + h; y++) for (let x = x0; x < x0 + w; x++) { const c = rvbIndex(t.get(x, y)); if (!c) continue; r += c[0]; g += c[1]; b += c[2]; n++; }
    return n ? plusProche(r / n, g / n, b / n) : t.get(x0, y0);
}
function texReduite(t, k) {
    const m = new Tex(Math.max(2, t.w / k), Math.max(2, t.h / k), 0), kx = t.w / m.w, ky = t.h / m.h;
    for (let y = 0; y < m.h; y++) for (let x = 0; x < m.w; x++) m.set(x, y, moyenne(t, x * kx, y * ky, kx, ky));
    return m;
}

// Rampes d'un thème dans la palette du thème en cours
function rampesTheme(T) {
    return {
        ch: rampe(T.chaussee[0], T.chaussee[1], 8), me: rampe(T.metal[0], T.metal[1], 8), be: rampe(T.beton[0], T.beton[1], 8),
        na: rampe(T.neonA[0], T.neonA[1], 4, 1), nb: rampe(T.neonB[0], T.neonB[1], 4, 1), ac: rampe(T.accent[0], T.accent[1], 4, 1),
        so: rampe(T.sol.c[0], T.sol.c[1], 8), ro: rampe(T.roche.c[0], T.roche.c[1], 8), te: rampe(T.terre[0], T.terre[1], 8),
        fe: T.feuille ? rampe(T.feuille[0], T.feuille[1], 8) : rampe(T.roche.c[0], T.roche.c[1], 8), feux: (T.sol.feux || []).map(h => hex(h, 1)), blanc: hex(0xe8eef4), noir: hex(0x08090c)
    };
}

// ---------------------------------------------------------------- Horizons (palette à eux, RVB)
const HZ_W = 1024, HZ_H = 128, HZ_BASE = 112;   // 360° sur 1 024 pixels ; ligne 112 posée sur l'horizon (16 lignes dessous)
// Bruit périodique (période HZ_W) : valeur lissée entre des points tous les pas pixels
const periodique = (u, pas, s) => {
    const n = HZ_W / pas, i = Math.floor(u / pas), f = u / pas - i, a = bruit(((i % n) + n) % n, 0, s), b = bruit((((i + 1) % n) + n) % n, 0, s);
    return a + (b - a) * (f * f * (3 - 2 * f));
};
function horizon(T) {
    const H = T.horizon, brume = T.ciel[2], px = new Uint8Array(HZ_W * HZ_H), cols = [];
    const ci = h => { let k = cols.indexOf(h); if (k < 0) { if (cols.length >= 255) throw new Error('horizon : trop de couleurs'); cols.push(h); k = cols.length - 1; } return k + 1; };
    const colonne = (u, haut, c) => { for (let y = Math.max(0, Math.round(HZ_BASE - haut)); y < HZ_H; y++) px[y * HZ_W + u] = c; };
    // Trois plans, du plus loin (pris dans la brume) au plus proche
    const plans = [0.62, 0.38, 0.12].map(k => melange(H.base, brume, k));
    if (H.style === 'ville') {
        for (let p = 0; p < 3; p++) {
            const c = ci(plans[p]);
            let u = 0;
            while (u < HZ_W) {
                const w = 6 + Math.floor(hasard() * (p === 0 ? 30 : 18)), h = (p === 0 ? 18 : p === 1 ? 10 : 4) + hasard() * (p === 0 ? 42 : p === 1 ? 46 : 26);
                for (let x = u; x < Math.min(HZ_W, u + w); x++) colonne(x, h, c);
                if (p > 0) for (let y = Math.round(HZ_BASE - h) + 2; y < HZ_BASE; y += 3) for (let x = u + 1; x < Math.min(HZ_W, u + w - 1); x += 2)
                    if (hasard() < (p === 1 ? 0.22 : 0.35)) px[y * HZ_W + x] = ci(H.feux[(hasard() * 3) | 0]);
                if (p === 1 && hasard() < 0.3) { const x = u + (w >> 1); for (let y = Math.round(HZ_BASE - h - 8); y < HZ_BASE - h; y++) px[y * HZ_W + x] = c; px[Math.round(HZ_BASE - h - 9) * HZ_W + x] = ci(H.feux[3]); }
                u += w + (p === 2 ? Math.floor(hasard() * 20) : 0);
            }
        }
    } else if (H.style === 'collines') {
        for (let p = 0; p < 3; p++) {
            const c = ci(plans[p]);
            for (let u = 0; u < HZ_W; u++) colonne(u, (p === 0 ? 34 : p === 1 ? 20 : 8) + periodique(u, p === 0 ? 128 : 64, 90 + p) * (p === 0 ? 30 : 22) + periodique(u, 16, 95 + p) * 4, c);
            if (p === 1) for (let k = 0; k < 14; k++) {   // ruines et cheminées
                const u0 = Math.floor(hasard() * HZ_W), w = 3 + Math.floor(hasard() * 10), h = 26 + hasard() * 28;
                for (let x = u0; x < u0 + w; x++) colonne(x % HZ_W, h - (x % 3 === 0 ? 4 : 0), c);
                if (w < 6) px[Math.round(HZ_BASE - h - 1) * HZ_W + (u0 % HZ_W)] = ci(H.feux[0]);
            }
        }
    } else if (H.style === 'mesas') {
        for (let p = 0; p < 3; p++) {
            const c = ci(plans[p]), clair = ci(melange(plans[p], 0xffe0b0, 0.25));
            for (let u = 0; u < HZ_W; u++) {
                const m = periodique(u, p === 0 ? 96 : 64, 110 + p), plateau = m > 0.55 ? 1 : m > 0.45 ? (m - 0.45) * 10 : 0;
                const h = (p === 0 ? 10 : 4) + plateau * (p === 0 ? 46 : p === 1 ? 34 : 18) + periodique(u, 8, 120 + p) * 2;
                colonne(u, h, c);
                if (plateau > 0 && plateau < 1) for (let y = Math.round(HZ_BASE - h); y < Math.round(HZ_BASE - h) + 6 && y < HZ_H; y++) px[y * HZ_W + u] = clair;   // flanc au soleil
            }
        }
    } else {   // montagnes : crêtes dentelées, neige sur les sommets
        for (let p = 0; p < 3; p++) {
            const c = ci(plans[p]), neige = ci(melange(0xf4f6ff, brume, p === 0 ? 0.5 : 0.25));
            for (let u = 0; u < HZ_W; u++) {
                const h = (p === 0 ? 30 : p === 1 ? 14 : 6) + Math.abs(periodique(u, p === 0 ? 96 : 48, 130 + p) - 0.5) * 2 * (p === 0 ? 62 : 44) + periodique(u, 8, 140 + p) * 5;
                colonne(u, h, c);
                const top = Math.round(HZ_BASE - h);
                if (h > (p === 0 ? 58 : 40)) for (let y = top; y < top + Math.min(8, (h - (p === 0 ? 58 : 40)) * 0.6 + 2) && y < HZ_H; y++) px[y * HZ_W + u] = neige;
            }
        }
    }
    return { w: HZ_W, h: HZ_H, base: HZ_BASE, cols, px };
}

// ---------------------------------------------------------------- Pistes
// Points de passage : x, z, hauteur y, demi-largeur facultative. La spline (Catmull-Rom centripète) passe par eux.
const PISTES = [
    { nom: 'NÉONS DE LUCERNE', lieu: 'LUCERNE · NUIT', theme: 'nuit', sol: 0, tours: 3, points: [
        [0, 0, 5, 2.8], [0, 70, 5, 2.6], [6, 100, 6], [30, 112, 7], [70, 112, 7], [95, 100, 8], [104, 75, 9], [100, 50, 8], [80, 40, 7],
        [60, 45, 5], [45, 30, 2.5, 2.4, { t: 4 }], [50, 0, 2.5, 2.4, { t: 4 }], [70, -20, 6], [100, -25, 9], [125, -10, 10], [140, -35, 9], [120, -70, 7], [80, -80, 6],
        [40, -78, 5], [15, -60, 5], [0, -30, 5, 2.6]] },
    { nom: 'CRÊTE DE CENDRE', lieu: 'FRICHES · CRÉPUSCULE', theme: 'crepuscule', sol: 0, tours: 3, points: [
        [0, 0, 6, 2.8], [0, 60, 6, 2.6], [10, 95, 8], [40, 115, 12], [80, 110, 14], [105, 85, 12], [110, 50, 8], [95, 20, 5],
        [115, -10, 4, 2.0], [140, -40, 7, 2.0], [130, -80, 12], [95, -100, 14], [60, -85, 8], [45, -55, 2, 2.4, { c: 6 }], [20, -50, 1.5, 2.4, { c: 7 }],
        [-5, -70, 4], [-35, -60, 9], [-42, -35, 7], [-16, -24, 6, 2.6]] },
    { nom: 'CANYON DES SABLES', lieu: 'LES SABLES · MIDI', theme: 'midi', sol: 0, tours: 3, points: [
        [0, 0, 8, 2.8], [0, 80, 8, 2.6], [20, 140, 10], [70, 170, 12], [130, 160, 12], [170, 120, 10], [175, 60, 6], [150, 20, 2, 2.6, { c: 8 }],
        [110, 10, 1.5, 2.4, { c: 10 }], [90, -20, 1.5, 2.4, { c: 9 }], [100, -60, 2, 2.4, { t: 5 }], [80, -100, 6], [30, -110, 12], [-20, -90, 11], [-40, -50, 9], [-25, -20, 8, 2.6]] },
    { nom: 'COL DES CENDRES', lieu: 'LE COL · AUBE', theme: 'aube', sol: 0, tours: 3, points: [
        [0, 0, 2, 2.8, { c: 7 }], [0, 50, 2.5, 2.6, { c: 8 }], [15, 80, 4, 2.4, { t: 5 }], [50, 85, 9], [60, 65, 13], [40, 50, 15], [30, 30, 17], [55, 15, 19], [85, 25, 21],
        [100, 55, 23], [90, 90, 25], [110, 115, 26], [145, 110, 25], [160, 80, 22], [150, 40, 18], [155, 0, 14], [135, -35, 10],
        [95, -50, 7], [55, -45, 4], [20, -30, 2.5, 2.6]] },
    // Biomes (14/11/2026)
    { nom: 'CANYONS DE THARSIS', lieu: 'MARS · CRÉPUSCULE', theme: 'mars', sol: 0, tours: 3, points: [
        [0, 0, 4, 2.8], [0, 70, 4, 2.6], [20, 110, 6], [60, 125, 9], [100, 110, 12], [120, 75, 10, 2.4, { c: 7 }], [115, 40, 8, 2.4, { c: 8 }],
        [130, 5, 7], [165, -20, 9], [170, -60, 12], [140, -90, 10], [100, -85, 7, 2.4, { c: 6 }], [70, -60, 5, 2.4, { c: 7 }], [40, -75, 4],
        [10, -50, 4, 2.6]] },
    { nom: 'CRATÈRES DE LA LUNE', lieu: 'LUNE · NUIT SANS FIN', theme: 'lune', sol: 0, tours: 3, points: [
        [0, 0, 3, 3.0], [0, 90, 3, 2.8], [30, 140, 5], [90, 160, 8], [150, 140, 10], [180, 90, 8], [170, 40, 5], [200, 0, 6],
        [210, -60, 9, 2.4, { t: 5 }], [170, -110, 12], [110, -120, 9], [60, -90, 6], [30, -50, 4], [5, -30, 3, 2.8]] },
    { nom: 'JUNGLE DE VÉNUS', lieu: 'VÉNUS · BRUME CHAUDE', theme: 'venus', sol: 0, tours: 3, points: [
        [0, 0, 5, 2.6], [0, 60, 5, 2.4], [15, 95, 7], [50, 105, 9], [75, 85, 9, 2.4, { t: 5 }], [70, 50, 8, 2.4, { t: 5 }], [95, 25, 7],
        [130, 30, 9], [150, 60, 11], [175, 40, 12], [170, 0, 10], [140, -30, 8, 2.4, { c: 6 }], [100, -45, 6, 2.4, { c: 7 }], [60, -35, 5],
        [30, -55, 4], [5, -35, 5, 2.6]] },
    { nom: 'GALERIES PROFONDES', lieu: 'SOUS TERRE', theme: 'souterrain', sol: 0, tours: 3, points: [
        [0, 0, 4, 2.6, { c: 6 }], [0, 60, 4, 2.4, { c: 7 }], [20, 95, 6, 2.4, { t: 5 }], [55, 105, 7, 2.4, { t: 5 }], [85, 85, 8, 2.4, { c: 8 }],
        [95, 50, 9, 2.4, { c: 9 }], [125, 30, 10, 2.4, { t: 5 }], [150, 55, 12, 2.4, { t: 5 }], [175, 30, 11, 2.4, { c: 8 }], [165, -10, 9, 2.4, { c: 8 }],
        [130, -30, 7, 2.4, { t: 5 }], [90, -40, 6, 2.4, { t: 5 }], [50, -25, 5, 2.4, { c: 7 }], [20, -40, 4, 2.4, { c: 7 }]] },
    { nom: 'RÉCIFS DU LAGON', lieu: 'ARCHIPEL · MIDI', theme: 'ile', sol: 0, tours: 3, points: [
        [0, 0, 2.5, 2.8], [0, 70, 2.5, 2.6], [10, 110, 3], [45, 135, 4], [90, 140, 5], [130, 120, 4], [150, 85, 3], [190, 70, 6], [220, 30, 8],
        [205, -15, 5], [165, -30, 3], [130, -60, 2.5, 2.4, { c: 5 }], [90, -75, 3], [50, -60, 3], [20, -40, 2.5, 2.6]] }
];
const HW = 2.3, PAS = 1.0, DEVERS_K = 7, DEVERS_MAX = 0.5;
// Plaques : à des parts du tour (0..1), écart en part de la demi-largeur, sorte
const PLAQUES = [
    [0.05, -0.45, 0], [0.13, 0.5, 1], [0.135, -0.5, 1], [0.24, 0.4, 0], [0.37, -0.4, 0], [0.47, 0, 1], [0.56, 0.45, 0],
    [0.66, -0.5, 1], [0.665, 0.5, 1], [0.74, 0, 0], [0.86, -0.4, 0], [0.93, 0.45, 1]
];

const v3 = (x, y, z) => ({ x, y, z });
const add = (a, b) => v3(a.x + b.x, a.y + b.y, a.z + b.z), sub = (a, b) => v3(a.x - b.x, a.y - b.y, a.z - b.z), mul = (a, k) => v3(a.x * k, a.y * k, a.z * k);
const dot = (a, b) => a.x * b.x + a.y * b.y + a.z * b.z, len = a => Math.sqrt(dot(a, a)), norm = a => mul(a, 1 / (len(a) || 1));
const cross = (a, b) => v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);

function construirePiste(P) {
    const pts = P.points.map(p => ({ c: v3(p[0], p[2], p[1]), hw: p[3] || HW, opt: p[4] || null })), n = pts.length;
    const fin = [];
    for (let i = 0; i < n; i++) {
        const p0 = pts[(i - 1 + n) % n].c, p1 = pts[i].c, p2 = pts[(i + 1) % n].c, p3 = pts[(i + 2) % n].c;
        const t01 = Math.pow(len(sub(p1, p0)), 0.5), t12 = Math.pow(len(sub(p2, p1)), 0.5), t23 = Math.pow(len(sub(p3, p2)), 0.5);
        const m1 = add(sub(p2, p1), mul(sub(mul(sub(p1, p0), 1 / t01), mul(sub(p2, p0), 1 / (t01 + t12))), t12));
        const m2 = add(sub(p2, p1), mul(sub(mul(sub(p3, p2), 1 / t23), mul(sub(p3, p1), 1 / (t12 + t23))), t12));
        for (let k = 0; k < 64; k++) {
            const t = k / 64, t2 = t * t, t3 = t2 * t;
            const a = 2 * t3 - 3 * t2 + 1, b = t3 - 2 * t2 + t, c = -2 * t3 + 3 * t2, d = t3 - t2;
            fin.push({ c: add(add(mul(p1, a), mul(m1, b)), add(mul(p2, c), mul(m2, d))), hw: pts[i].hw + (pts[(i + 1) % n].hw - pts[i].hw) * (t * t * (3 - 2 * t)), opt: pts[i].opt });
        }
    }
    const S = [0];
    for (let i = 1; i <= fin.length; i++) S.push(S[i - 1] + len(sub(fin[i % fin.length].c, fin[i - 1].c)));
    const L = S[fin.length], ns = Math.round(L / PAS), sec = [];
    for (let k = 0, j = 0; k < ns; k++) {
        const s = k * L / ns;
        while (S[j + 1] < s) j++;
        const t = (s - S[j]) / (S[j + 1] - S[j]), a = fin[j], b = fin[(j + 1) % fin.length];
        sec.push({ c: add(a.c, mul(sub(b.c, a.c), t)), hw: a.hw + (b.hw - a.hw) * t, opt: a.opt });
    }
    const m = sec.length;
    for (let k = 0; k < m; k++) sec[k].f = norm(sub(sec[(k + 1) % m].c, sec[(k - 1 + m) % m].c));
    const cap = k => Math.atan2(sec[k].f.x, sec[k].f.z);
    const courbe = [];
    for (let k = 0; k < m; k++) {
        let d = cap((k + 2) % m) - cap((k - 2 + m) % m);
        d = Math.atan2(Math.sin(d), Math.cos(d));
        courbe.push(d / (4 * PAS));   // > 0 : virage à droite (le cap croît vers la droite)
    }
    for (let k = 0; k < m; k++) {
        let s = 0, w = 0;
        for (let j = -6; j <= 6; j++) { const p = 7 - Math.abs(j); s += courbe[(k + j + m) % m] * p; w += p; }
        sec[k].devers = Math.max(-DEVERS_MAX, Math.min(DEVERS_MAX, -DEVERS_K * s / w));   // virage à droite : le bord gauche monte
        const f = sec[k].f, r0 = norm(v3(f.z, 0, -f.x)), u0 = cross(f, r0);
        sec[k].r = norm(add(mul(r0, Math.cos(sec[k].devers)), mul(u0, Math.sin(sec[k].devers))));
        const o = sec[k].opt, nat = !!(o && (o.c || o.t));
        sec[k].drap = (k === 0 ? 1 : 0) | (!nat && k % 7 === 3 && sec[k].c.y > P.sol + 1.5 ? 2 : 0) | (nat ? 4 : 0) | (o && o.t ? 8 : 0);
        sec[k].paroi = nat ? Math.min(31, (o.c || o.t) * (0.8 + 0.4 * (Math.sin(k * 0.37) * 0.5 + 0.5) * (0.6 + 0.4 * bruit(k, 0, 201)))) : 0;
    }
    return { sec, L };
}

// ---------------------------------------------------------------- Terrain autour de la piste
// (13/11/2026, d'après les circuits du genre montrés par l'utilisateur, la piste posée dans un relief : collines, mesas, montagnes)
// Grille de hauteurs tous les TER_G cases sur l'emprise de la piste plus TER_MARGE ; relief du thème (THEMES[].relief, au-dessus du
// sol) ; le long de la piste, un lit juste sous la chaussée (TER_SOUS) jusqu'à hw + TER_LIT (render.c dessine chaque cellule de la grille,
// la même précision à toute distance depuis le 14/11/2026 : le lit doit être plus large qu'une cellule pour qu'aucun triangle ne coupe la chaussée), puis une pente vers le relief sur
// TER_PENTE : tranchée dans une colline, remblai sur un creux de moins de TER_PONT, pont sur piliers au-dessus d'un creux plus
// profond ; jamais au-dessus d'une chaussée proche (piste qui passe sous une autre) ; l'île retombe au sol sur TER_BORD. Relief plat
// (la ville) : pas de terrain, le sol plat.
const TER_G = 8, TER_MARGE = 64, TER_BORD = 30, TER_LIT = 7, TER_PENTE = 14, TER_PONT = 2.5, TER_SOUS = 0.45;
function vbruit(x, z, s) {
    const i = Math.floor(x), j = Math.floor(z), fx = x - i, fz = z - j, sx = fx * fx * (3 - 2 * fx), sz = fz * fz * (3 - 2 * fz);
    const a = bruit(i & 0xffff, j & 0xffff, s), b = bruit((i + 1) & 0xffff, j & 0xffff, s), c = bruit(i & 0xffff, (j + 1) & 0xffff, s), d = bruit((i + 1) & 0xffff, (j + 1) & 0xffff, s);
    return a + (b - a) * sx + (c - a) * sz + (a - b - c + d) * sx * sz;
}
function fbm(x, z, s, oct) {
    let v = 0, a = 0.5, f = 1, t = 0;
    for (let o = 0; o < oct; o++) { v += vbruit(x * f + 50, z * f + 50, s + o * 17) * a; t += a; a *= 0.5; f *= 2.03; }
    return v / t;
}
const lisse = (a, b, x) => { const t = Math.max(0, Math.min(1, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
function reliefDe(R, x, z) {
    const e = R.echelle;
    if (R.style === 'collines') return lisse(0.32, 0.8, fbm(x / e, z / e, 301, 4)) * R.haut;
    if (R.style === 'mesas') {   // plateaux à flancs raides, sur deux étages, et un peu de relief sur les dessus
        const v = fbm(x / e, z / e, 311, 3);
        return (lisse(0.5, 0.54, v) * 0.6 + lisse(0.6, 0.63, v) * 0.4) * R.haut + fbm(x / 9, z / 9, 313, 2) * 1.2;
    }
    if (R.style === 'crateres') {   // la Lune : collines douces et cratères (bord relevé, fond creusé) sur une grille de cellules
        let h = (0.3 + fbm(x / e, z / e, 331, 3) * 0.35) * R.haut;
        const G = e * 0.9, ci = Math.floor(x / G), cj = Math.floor(z / G);
        for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
            const i = (ci + di) & 0xffff, j = (cj + dj) & 0xffff;
            if (bruit(i, j, 333) < 0.4) continue;
            const cx = (ci + di + 0.2 + 0.6 * bruit(i, j, 334)) * G, cz = (cj + dj + 0.2 + 0.6 * bruit(i, j, 335)) * G, rr = G * (0.18 + 0.22 * bruit(i, j, 336));
            const d = Math.hypot(x - cx, z - cz) / rr;
            if (d < 1) h -= R.haut * 0.45 * (1 - d * d);
            h += R.haut * 0.5 * Math.exp(-(((d - 1.05) / 0.2) ** 2));
        }
        return Math.max(0, h);
    }
    if (R.style === 'iles') {   // archipel : îles dans la mer (relief nul : la mer), collines dessus
        return lisse(0.5, 0.62, fbm(x / e, z / e, 341, 4)) * (0.45 + 0.55 * fbm(x / (e * 0.4), z / (e * 0.4), 343, 3)) * R.haut;
    }
    if (R.style === 'montagnes') {   // crêtes (bruit replié), sur un fond de collines
        const r = 1 - Math.abs(2 * fbm(x / e, z / e, 321, 4) - 1);
        return (Math.pow(r, 2.2) * 0.8 + fbm(x / (e * 0.5), z / (e * 0.5), 323, 3) * 0.3) * R.haut;
    }
    return 0;
}
// ---------------------------------------------------------------- Décors proches
// (14/11/2026, demande de l'utilisateur : « créer du décor beaucoup plus proche avec de la hauteur » pour masquer l'apparition des
// géométries) : de chaque côté de la piste, un volume toutes les DEC_PAS cases environ, à DEC_ECART cases du bord, de la hauteur du
// thème (DECORS : sorte 1 immeuble, en boîte ; 0 aiguille de roche, prisme effilé) ; jamais dans une portion naturelle (ses parois),
// ni à moins de DEC_LIBRE d'une autre partie de la piste (ni sur elle) ; tirages par bruit (reproductibles, sans toucher au hasard
// des textures). Bloc « DEC1 » d'une piste : nombre u32, puis 24 octets chacun : x, z, bas, haut, rayon f32, côtés u8, sorte u8,
// angle u8 (/ 256 tour), haut / bas du rayon u8 (/ 100)
const DEC_PAS = 7, DEC_ECART = [2.5, 6.0], DEC_LIBRE = 2.5;
const DECORS = {
    nuit: { sorte: 1, cotes: 4, r: [2.0, 3.4], h: [9, 24], eff: [1.0, 1.0] },
    crepuscule: { sorte: 0, cotes: 6, r: [1.5, 2.8], h: [5, 13], eff: [0.35, 0.65] },
    midi: { sorte: 0, cotes: 6, r: [1.8, 3.2], h: [6, 15], eff: [0.6, 0.85] },
    aube: { sorte: 0, cotes: 5, r: [1.5, 2.8], h: [7, 17], eff: [0.2, 0.45] },
    mars: { sorte: 0, cotes: 6, r: [1.8, 3.4], h: [6, 14], eff: [0.55, 0.85] },      // buttes
    lune: { sorte: 0, cotes: 5, r: [1.2, 2.4], h: [3, 8], eff: [0.3, 0.6] },         // rochers
    venus: { forme: 'champignon', cotes: 6, r: [0.5, 0.9], h: [6, 13], eff: [0.75, 0.9], chapeau: [2.2, 3.6], ep: [1.2, 2.0] },   // champignons géants : pied, chapeau
    souterrain: { sorte: 0, cotes: 5, r: [1.2, 2.6], h: [8, 22], eff: [0.12, 0.35] }, // stalagmites
    ile: { forme: 'palmier', cotes: 5, r: [0.2, 0.3], h: [5, 8.5], eff: [0.7, 0.85], chapeau: [1.8, 2.6], ep: [0.9, 1.3] },        // palmiers : tronc fin, couronne
};
function decorPiste(P, T, sec) {
    const D = DECORS[P.theme];
    if (!D) return [];
    const G = 8, grille = new Map(), cle = (i, j) => i * 100000 + j;
    sec.forEach((S, k) => { const c = cle(Math.floor(S.c.x / G), Math.floor(S.c.z / G)); if (!grille.has(c)) grille.set(c, []); grille.get(c).push(k); });
    const libre = (x, z, r, k0) => {
        const i0 = Math.floor((x - r - 12) / G), i1 = Math.floor((x + r + 12) / G), j0 = Math.floor((z - r - 12) / G), j1 = Math.floor((z + r + 12) / G);
        for (let i = i0; i <= i1; i++) for (let j = j0; j <= j1; j++) for (const k of grille.get(cle(i, j)) || []) {
            const S = sec[k], d = Math.hypot(S.c.x - x, S.c.z - z);
            if (d < S.hw + r + DEC_LIBRE) return false;
            if ((S.drap & 4) && d < S.hw + r + 10) return false;   // (parois et plateau d'une portion naturelle)
        }
        return true;
    };
    const tir = (a, b, k, s) => a + (b - a) * bruit(k, s, 9001);
    const out = [];
    for (const cote of [-1, 1]) {
        for (let k = 0; k < sec.length; ) {
            const S = sec[k];
            const pas = Math.max(3, Math.round(DEC_PAS * (0.75 + 0.5 * bruit(k, cote + 5, 9002))));
            if (!(S.drap & (4 | 8))) {
                const r = tir(D.r[0], D.r[1], k, cote + 11), e = S.hw + tir(DEC_ECART[0], DEC_ECART[1], k, cote + 13) + r;
                const n = Math.hypot(S.r.x, S.r.z) || 1, x = S.c.x + S.r.x / n * e * cote, z = S.c.z + S.r.z / n * e * cote;
                if (libre(x, z, D.forme ? Math.max(r, D.chapeau[1] * 0.6) : r, k)) {
                    const haut = Math.max(S.c.y, P.sol) + tir(D.h[0], D.h[1], k, cote + 17), angle = bruit(k, cote + 19, 9003);
                    out.push({ x, z, bas: P.sol - 1, haut, r, cotes: D.cotes, sorte: D.forme ? 0 : D.sorte, angle, eff: tir(D.eff[0], D.eff[1], k, cote + 23) });
                    if (D.forme) {   // chapeau (champignon : un dôme épais) ou couronne (palmier : un cône plat et pointu)
                        const rc = tir(D.chapeau[0], D.chapeau[1], k, cote + 29), ep = tir(D.ep[0], D.ep[1], k, cote + 31), champi = D.forme === 'champignon';
                        out.push({ x, z, bas: haut - ep * (champi ? 0.35 : 0.3), haut: haut + ep * (champi ? 0.65 : 0.7), r: rc, cotes: champi ? 7 : 6, sorte: 2,
                                   angle: bruit(k, cote + 37, 9003), eff: champi ? 0.42 : 0.12 });
                    }
                }
            }
            k += pas;
        }
    }
    return out;
}
// ---------------------------------------------------------------- Bâtiments du décor
// (14/11/2026, demande de l'utilisateur : « on peut aussi mettre des bâtiments dans les décors ») : par thème, quelques sortes de
// bâtiments (STRUCTURES : pièces empilées de bas en haut, chacune un prisme : rayon, hauteur, effilement, côtés, sorte ; k de 0 à 1
// tiré par bâtiment), un tous les STR_PAS sections environ d'un côté tiré, à STR_ECART cases du bord, posé sur le relief (hauteur du
// terrain à sa place), jamais sur la piste ni sur un autre décor ; sorte 3 : la texture des piliers (le béton du thème), 2 : le
// feuillage ; une pièce posée sur une autre a son dessous dessiné quand la caméra passe sous elle (render.c), et ne cache rien pour
// la visibilité précalculée. Ajoutés au bloc DEC1, après les décors proches.
const STR_PAS = [55, 85], STR_ECART = [6, 13];
const STRUCTURES = {
    crepuscule: [   // les Friches : tours en ruine, cheminées d'usine
        k => [{ r: 1.6 + 0.8 * k, h: 7 + 9 * k, eff: 0.92, cotes: 4, sorte: 3 }],
        k => [{ r: 1.4, h: 1.8, eff: 1, cotes: 4, sorte: 3 }, { r: 0.5 + 0.15 * k, h: 10 + 6 * k, eff: 0.75, cotes: 6, sorte: 3 }]],
    midi: [   // les Sables : châteaux d'eau
        k => [{ r: 0.3, h: 5 + 2 * k, eff: 1, cotes: 4, sorte: 3 }, { r: 1.3 + 0.3 * k, h: 1.8, eff: 1, cotes: 8, sorte: 3 }, { r: 1.3 + 0.3 * k, h: 0.7, eff: 0.25, cotes: 8, sorte: 3 }]],
    aube: [   // le Col : chalets (toit pointu)
        k => [{ r: 1.4 + 0.4 * k, h: 2.2, eff: 1, cotes: 4, sorte: 3 }, { r: 1.9 + 0.5 * k, h: 1.6, eff: 0.08, cotes: 4, sorte: 3 }]],
    mars: [   // Mars : dômes d'habitat, antennes
        k => [{ r: 2.4 + 1.2 * k, h: 1.0, eff: 0.9, cotes: 8, sorte: 3 }, { r: (2.4 + 1.2 * k) * 0.9, h: 1.7 + 0.6 * k, eff: 0.3, cotes: 8, sorte: 3 }],
        k => [{ r: 0.9, h: 1.2, eff: 1, cotes: 4, sorte: 3 }, { r: 0.12, h: 8 + 4 * k, eff: 0.5, cotes: 4, sorte: 3 }]],
    lune: [   // la Lune : modules de la base, dômes
        k => [{ r: 1.1 + 0.3 * k, h: 2.0 + 0.8 * k, eff: 1, cotes: 8, sorte: 3 }, { r: 1.1 + 0.3 * k, h: 0.6, eff: 0.45, cotes: 8, sorte: 3 }],
        k => [{ r: 2.2 + k, h: 0.9, eff: 0.9, cotes: 8, sorte: 3 }, { r: (2.2 + k) * 0.9, h: 1.5 + 0.5 * k, eff: 0.3, cotes: 8, sorte: 3 }]],
    ile: [   // l'île : cabanes au toit de palmes, phares
        k => [{ r: 1.2 + 0.3 * k, h: 1.6, eff: 1, cotes: 4, sorte: 3 }, { r: 1.8 + 0.4 * k, h: 1.3, eff: 0.1, cotes: 4, sorte: 2 }],
        k => [{ r: 0.9, h: 8 + 3 * k, eff: 0.7, cotes: 8, sorte: 3 }, { r: 0.8, h: 0.9, eff: 1, cotes: 8, sorte: 3 }, { r: 0.85, h: 0.7, eff: 0.2, cotes: 8, sorte: 3 }]],
};
// Hauteur du sol à (x, z) : le relief (triangles de la cellule, comme render.c), sinon le sol plat
function solEn(P, ter, x, z) {
    if (!ter) return P.sol;
    const fx = (x - ter.x0) / TER_G, fz = (z - ter.z0) / TER_G, I = Math.floor(fx), J = Math.floor(fz);
    if (I < 0 || J < 0 || I >= ter.nx - 1 || J >= ter.nz - 1) return P.sol;
    const u = fx - I, v = fz - J, h = (i, j) => P.sol + ter.h[j * ter.nx + i] / 16;
    const h00 = h(I, J), h10 = h(I + 1, J), h11 = h(I + 1, J + 1), h01 = h(I, J + 1);
    if (Math.abs(h00 - h11) <= Math.abs(h10 - h01)) return u >= v ? h00 + (h10 - h00) * u + (h11 - h10) * v : h00 + (h01 - h00) * v + (h11 - h01) * u;
    return u + v <= 1 ? h00 + (h10 - h00) * u + (h01 - h00) * v : h11 + (h01 - h11) * (1 - u) + (h10 - h11) * (1 - v);
}
function structuresPiste(P, sec, ter, dec) {
    const S0 = STRUCTURES[P.theme];
    if (!S0) return [];
    const tir = (a, b, k, s) => a + (b - a) * bruit(k, s, 9201), out = [];
    const libre = (x, z, r) => {
        for (const S of sec) { const d = Math.hypot(S.c.x - x, S.c.z - z); if (d < S.hw + r + 3 || ((S.drap & 4) && d < S.hw + r + 10)) return false; }
        for (const D of dec.concat(out)) if (Math.hypot(D.x - x, D.z - z) < D.r + r + 1) return false;
        return true;
    };
    for (let k = 20; k < sec.length; k += Math.round(tir(STR_PAS[0], STR_PAS[1], k, 1))) {
        const S = sec[k];
        if (S.drap & (4 | 8)) continue;
        const forme = S0[Math.floor(bruit(k, 2, 9202) * S0.length) % S0.length], q = bruit(k, 3, 9203), pieces = forme(q);
        const R = Math.max(...pieces.map(p => p.r)), cote = bruit(k, 4, 9204) < 0.5 ? -1 : 1, e = S.hw + tir(STR_ECART[0], STR_ECART[1], k, 5) + R;
        const n = Math.hypot(S.r.x, S.r.z) || 1, x = S.c.x + S.r.x / n * e * cote, z = S.c.z + S.r.z / n * e * cote;
        if (!libre(x, z, R)) continue;
        let base = solEn(P, ter, x, z);
        const angle = bruit(k, 6, 9205);
        pieces.forEach((p, i) => {
            const bas = i === 0 ? P.sol - 1 : base - 0.05;
            out.push({ x, z, bas, haut: base + p.h, r: p.r, cotes: p.cotes, sorte: p.sorte, angle, eff: p.eff });
            base += p.h;
        });
    }
    return out;
}
function terrainPiste(P, T, sec) {
    const R = T.relief;
    if (!R || R.style === 'plat') return null;
    let x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
    for (const S of sec) { x0 = Math.min(x0, S.c.x); x1 = Math.max(x1, S.c.x); z0 = Math.min(z0, S.c.z); z1 = Math.max(z1, S.c.z); }
    x0 = Math.floor((x0 - TER_MARGE) / TER_G) * TER_G; z0 = Math.floor((z0 - TER_MARGE) / TER_G) * TER_G;
    const nx = Math.ceil((x1 + TER_MARGE - x0) / TER_G) + 1, nz = Math.ceil((z1 + TER_MARGE - z0) / TER_G) + 1;
    const h = new Int16Array(nx * nz), portee = 2.6 + TER_LIT + TER_PENTE;
    let hmax = 0;
    for (let j = 0; j < nz; j++) for (let i = 0; i < nx; i++) {
        const x = x0 + i * TER_G, z = z0 + j * TER_G;
        const bord = Math.min(i, j, nx - 1 - i, nz - 1 - j) * TER_G;
        const nat = reliefDe(R, x, z) * lisse(0, TER_BORD, bord);
        let haut = nat, borne = 1e9;
        for (const S of sec) {
            const dx = S.c.x - x, dz = S.c.z - z;
            if (dx > portee || dx < -portee || dz > portee || dz < -portee) continue;
            const d = Math.hypot(dx, dz), lit = S.hw + TER_LIT;
            if (d >= lit + TER_PENTE) continue;
            const yk = S.c.y - Math.abs(S.r.y) * S.hw - P.sol - TER_SOUS, t = lisse(lit, lit + TER_PENTE, d);   // (sous le bord le plus bas : virage relevé)
            if (nat > yk) borne = Math.min(borne, yk + (nat - yk) * t);                     // tranchée
            else if (nat >= yk - TER_PONT) haut = Math.max(haut, yk + (nat - yk) * t);     // remblai
            if (d < lit) borne = Math.min(borne, yk);
        }
        const v = Math.max(0, Math.min(haut, borne));
        hmax = Math.max(hmax, v);
        h[j * nx + i] = Math.round(v * 16);
    }
    return { x0, z0, nx, nz, h, hmax };
}

// ---------------------------------------------------------------- Obstacles des canyons
// (14/11/2026, demande de l'utilisateur : « des obstacles dans les canyons ») : dans les portions naturelles (canyons et tunnels), un
// pilier de roche posé sur la chaussée tous les OBS_PAS sections environ, d'un côté puis de l'autre, à OBS_ECART de la demi-largeur
// de l'axe : un passage large d'un côté, un étroit de l'autre ; jamais près d'une plaque, du départ ni des bouts d'une portion.
// Bloc « OBS1 » : nombre u32, puis 8 octets chacun : section u16, écart i8 (1/16 de case), rayon u8 (1/32), hauteur u8 (1/16),
// côtés u8, angle u8 (/ 256 tour), 0
const OBS_PAS = [24, 40], OBS_R = [0.45, 0.72], OBS_H = [1.4, 3.0], OBS_ECART = [0.36, 0.5];
function obstaclesPiste(P, sec) {
    const m = sec.length, out = [], plaques = PLAQUES.map(([part]) => Math.round(part * m) % m);
    const tir = (a, b, k, s) => a + (b - a) * bruit(k, s, 9101);
    let suivant = 14, cote = 1;
    for (let k = 14; k < m - 8; k++) {
        if (k < suivant || !(sec[k].drap & 4)) continue;
        let dedans = true;
        for (let j = -5; j <= 5; j++) if (!(sec[(k + j + m) % m].drap & 4)) dedans = false;
        if (!dedans || plaques.some(p => Math.abs(p - k) < 8)) continue;
        const S = sec[k];
        // virage : pas dans un virage serré (les adversaires y passaient par la corde, en plein dedans : 85 chocs en trois tours aux
        // galeries) ; dans une courbe douce, à l'extérieur
        const cap = j => Math.atan2(sec[(k + j + m) % m].f.x, sec[(k + j + m) % m].f.z);
        let dc = cap(10) - cap(-10);
        dc = Math.atan2(Math.sin(dc), Math.cos(dc));
        if (Math.abs(dc) > 0.45) continue;
        if (bruit(k, 3, 9102) < 0.5) cote = -cote;
        if (Math.abs(dc) > 0.12) cote = dc > 0 ? -1 : 1;   // (cap qui croît : virage à droite, l'extérieur à gauche)
        out.push({ sec: k, d: cote * S.hw * tir(OBS_ECART[0], OBS_ECART[1], k, 1), r: tir(OBS_R[0], OBS_R[1], k, 2), h: tir(OBS_H[0], OBS_H[1], k, 4),
                   cotes: 5 + Math.floor(bruit(k, 5, 9103) * 3), angle: bruit(k, 6, 9104) });
        suivant = k + Math.round(tir(OBS_PAS[0], OBS_PAS[1], k, 7));
        cote = -cote;
    }
    return out;
}
// ---------------------------------------------------------------- Visibilité précalculée
// (14/11/2026, demande de l'utilisateur : « pour la géométrie de terrain, la logique est de n'avoir que des faces dans le champ de
// vision, inutile de faire de la géométrie qui ne se voit jamais ; les immeubles et montagnes permettent de bloquer la vue pour limiter
// les besoins en triangles derrière ») : pour chaque groupe de VIS_G sections, ce qui peut se voir depuis la caméra au-dessus d'elles
// (cellules du relief, décors, sections de la piste) ; render.c ne parcourt que la liste du groupe de la section la plus proche de la
// caméra (ag_vis_maj). Occultants : le relief (ses triangles, comme ag_terrain), les décors à moins de VIS_DEC_OPAQUE (prismes : dessus et flancs, comme
// ag_decors), les masses des portions naturelles (paroi et flanc, comme ag_naturel ; pas la voûte ni le dessus des tunnels : la caméra
// est dessous) ; hauteurs aux sommets d'une grille de VIS_OP, chaque cellule prise au plus bas de ses coins (prudent : elle ne cache que
// si elle est pleine). Points de vue : VIS_LAT en travers de chaque section, à VIS_H au-dessus de la chaussée (sur un relief, une caméra
// plus basse ne voit rien de plus ; render.c revient au dessin sans liste quand la caméra sort de ce couloir), ceux dans un occultant
// écartés ; de chacun, VIS_NA rayons, sur chacun la pente la plus haute des occultants déjà passés ; un point est vu s'il est au-dessus
// de cette pente sur l'un des deux rayons qui l'entourent (hors de ses VIS_FIN dernières cases : sa propre surface). Un groupe : ce
// qu'ont vu ses sections et VIS_DEBORD de part et d'autre. Bloc « VIS1 » : octets u32 (après ce champ), G, groupes u16, positions u32
// (groupes + 1, depuis le début des données), puis par groupe : rangées du relief, décors, plages de sections u16 (et 0), rangées (J, I0,
// I1 u16 : cellules I0 à I1 de la rangée J), décors u16, plages de sections (s0, s1 u16, comprises)
const VIS_DEC_OPAQUE = 40;   // décors : au-delà, en fondu translucide sur la LDK (render.c : AG_DEC_FONDU × la vue de 52), ils ne cachent rien
const VIS_LOIN = [40, 140];   // pas des rayons doublé au-delà de 40 cases, quadruplé au-delà de 140 (relief lointain)
const VIS_G = 4, VIS_OP = 0.5, VIS_PAS = 0.5, VIS_NA = 720, VIS_H = 3.0, VIS_COTE = 1.6, VIS_PRES = 1.0, VIS_FIN = 0.75, VIS_TPAS = 2, VIS_DEBORD = 2;
const VIS_LAT = hw => [-(hw + VIS_COTE), -0.5 * hw, 0, 0.5 * hw, hw + VIS_COTE];   // (render.c : AG_VIS_COTE, AG_VIS_HAUT = VIS_COTE, VIS_H)
const AGN_PLATEAU = 6, AGN_VOUTE = 3.2, AGN_TUNNEL_PENTE = 0.6, AGN_TOIT = 2, AGN_MUR_H = 0.5;   // (render.c : AG_PLATEAU, AG_VOUTE…)
// Cache de la visibilité (14/11/2026 : 5 à 7 min par génération) : build/cache_vis/<empreinte>.bin, empreinte de la piste (sections,
// terrain, décors, sol) et du calcul (source de visibilite, valeurs et sources des noms en capitales qu'elle cite, un niveau)
function visibiliteCache(P, sec, ter, dec) {
    const crypto = require('crypto'), h = crypto.createHash('sha1');
    const src = visibilite.toString(), vus = new Set();
    h.update(src);
    for (const nom of src.match(/\b[A-Za-z_][A-Za-z0-9_]*\b/g) || []) {
        if (vus.has(nom) || !/[A-Z]/.test(nom[0]) && !/^[a-z]+[A-Z]/.test(nom)) continue;
        vus.add(nom);
        let v; try { v = eval(nom); } catch (e) { continue; }
        h.update(nom + '=' + (typeof v === 'function' ? v.toString() : JSON.stringify(v)));
    }
    h.update(JSON.stringify({ sol: P.sol, sec, ter, dec }));
    const dossier = path.join(__dirname, '..', '..', 'build', 'cache_vis'), fichier = path.join(dossier, h.digest('hex') + '.bin');
    if (!process.env.SANS_CACHE && fs.existsSync(fichier)) {
        const b = fs.readFileSync(fichier), n = b.readUInt32LE(0);
        return { bloc: b.subarray(4 + n), bilan: b.subarray(4, 4 + n).toString('utf8') + ' (cache)' };
    }
    const r = visibilite(P, sec, ter, dec);
    fs.mkdirSync(dossier, { recursive: true });
    const t = Buffer.from(r.bilan, 'utf8'), l = Buffer.alloc(4); l.writeUInt32LE(t.length, 0);
    fs.writeFileSync(fichier, Buffer.concat([l, t, r.bloc]));
    return r;
}
function visibilite(P, sec, ter, dec) {
    const t0 = Date.now(), m = sec.length, sol = P.sol;
    // repère de chaque section comme antigrav.c : f de la précédente à la suivante, u = f × r, r = u × f
    const RP = sec.map((S, k) => {
        const f = norm(sub(sec[(k + 1) % m].c, sec[(k - 1 + m) % m].c)), u = norm(cross(f, S.r));
        return { f, u, r: norm(cross(u, f)) };
    });
    // 1. Occultants
    let x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
    for (const S of sec) { x0 = Math.min(x0, S.c.x - 30); x1 = Math.max(x1, S.c.x + 30); z0 = Math.min(z0, S.c.z - 30); z1 = Math.max(z1, S.c.z + 30); }
    if (ter) { x0 = Math.min(x0, ter.x0); z0 = Math.min(z0, ter.z0); x1 = Math.max(x1, ter.x0 + TER_G * (ter.nx - 1)); z1 = Math.max(z1, ter.z0 + TER_G * (ter.nz - 1)); }
    x0 = Math.floor(x0); z0 = Math.floor(z0);
    const nvx = Math.ceil((x1 - x0) / VIS_OP) + 2, nvz = Math.ceil((z1 - z0) / VIS_OP) + 2;
    const Hv = new Float32Array(nvx * nvz).fill(sol);
    const tri = (A, B, C) => {
        const d = (B[0] - A[0]) * (C[2] - A[2]) - (C[0] - A[0]) * (B[2] - A[2]);
        if (Math.abs(d) < 1e-6) return;
        const i0 = Math.max(0, Math.ceil((Math.min(A[0], B[0], C[0]) - x0) / VIS_OP)), i1 = Math.min(nvx - 1, Math.floor((Math.max(A[0], B[0], C[0]) - x0) / VIS_OP));
        const j0 = Math.max(0, Math.ceil((Math.min(A[2], B[2], C[2]) - z0) / VIS_OP)), j1 = Math.min(nvz - 1, Math.floor((Math.max(A[2], B[2], C[2]) - z0) / VIS_OP));
        for (let j = j0; j <= j1; j++) {
            const pz = z0 + j * VIS_OP;
            for (let i = i0; i <= i1; i++) {
                const px = x0 + i * VIS_OP;
                const w1 = ((px - A[0]) * (C[2] - A[2]) - (C[0] - A[0]) * (pz - A[2])) / d, w2 = ((B[0] - A[0]) * (pz - A[2]) - (px - A[0]) * (B[2] - A[2])) / d;
                if (w1 < -1e-4 || w2 < -1e-4 || w1 + w2 > 1 + 1e-4) continue;
                const h = A[1] + (B[1] - A[1]) * w1 + (C[1] - A[1]) * w2, k = j * nvx + i;
                if (h > Hv[k]) Hv[k] = h;
            }
        }
    };
    const quad = (a, b, c, d) => { tri(a, b, c); tri(a, c, d); };
    const th = (i, j) => sol + ter.h[j * ter.nx + i] / 16;
    if (ter) for (let j = 0; j + 1 < ter.nz; j++) for (let i = 0; i + 1 < ter.nx; i++) {
        const p = (a, b) => [ter.x0 + TER_G * a, th(a, b), ter.z0 + TER_G * b];
        const p00 = p(i, j), p10 = p(i + 1, j), p11 = p(i + 1, j + 1), p01 = p(i, j + 1);
        if (Math.abs(p00[1] - p11[1]) <= Math.abs(p10[1] - p01[1])) { tri(p00, p11, p10); tri(p00, p01, p11); } else { tri(p10, p00, p01); tri(p10, p01, p11); }
    }
    const prisme = D => {   // sommets du prisme comme ag_decors (angle et effilement tels que le paquet les garde)
        const n = Math.max(3, Math.min(8, D.cotes)), a0 = (Math.round(D.angle * 256) & 255) * (2 * Math.PI / 256), eff = Math.round(D.eff * 100) / 100;
        const Vb = [], Vt = [];
        for (let j = 0; j < n; j++) {
            const a = a0 + j * 2 * Math.PI / n;
            Vb.push([D.x + D.r * Math.cos(a), D.bas, D.z + D.r * Math.sin(a)]); Vt.push([D.x + D.r * eff * Math.cos(a), D.haut, D.z + D.r * eff * Math.sin(a)]);
        }
        return { n, Vb, Vt, eff };
    };
    const paroi = sec.map(S => Math.round(S.paroi * 8) / 8);   // (u8 du paquet, en huitièmes)
    const masse = [];   // par section naturelle : hauts des parois (cible) de chaque côté
    for (let k = 0; k < m; k++) {
        const a = sec[k], b = sec[(k + 1) % m];
        if (!(a.drap & 4)) continue;
        const ra = RP[k], rb = RP[(k + 1) % m], tun = !!(a.drap & 8), sortie = !(b.drap & 4);
        const pa = paroi[k], pb = sortie ? paroi[k] : paroi[(k + 1) % m];
        const ha = tun ? Math.min(pa, AGN_VOUTE) : pa, hb = tun ? Math.min(pb, AGN_VOUTE) : pb, pen = tun ? 0 : 0.28;
        const ta = tun ? Math.max(pa, ha + AGN_TOIT) : ha, tb = tun ? Math.max(pb, hb + AGN_TOIT) : hb;
        const hauts = [];
        for (const sg of [-1, 1]) {
            const Pa = add(a.c, mul(ra.r, a.hw * sg)), Pb = add(b.c, mul(rb.r, b.hw * sg)), oa = mul(ra.r, sg), ob = mul(rb.r, sg);
            const Ta = add(add(Pa, mul(ra.u, ha)), mul(oa, ha * pen)), Tb = add(add(Pb, mul(rb.u, hb)), mul(ob, hb * pen));
            const Ua = add(add(Pa, mul(ra.u, ta)), mul(oa, ta * pen)), Ub = add(add(Pb, mul(rb.u, tb)), mul(ob, tb * pen));
            const exa = tun ? AGN_TUNNEL_PENTE * (Ua.y - sol) + 1 : AGN_PLATEAU + Ua.y - sol, exb = tun ? AGN_TUNNEL_PENTE * (Ub.y - sol) + 1 : AGN_PLATEAU + Ub.y - sol;
            const Ga = add(Ua, mul(oa, exa)), Gb = add(Ub, mul(ob, exb));
            Ga.y = sol; Gb.y = sol;
            const A = q => [q.x, q.y, q.z];
            if (Ua.y > sol || Ub.y > sol) {
                quad(A(sub(Pa, mul(ra.u, 0.3))), A(sub(Pb, mul(rb.u, 0.3))), A(Tb), A(Ta));
                quad(A(Ua), A(Ub), A(Gb), A(Ga));
            }
            hauts.push(Ta, Ua);   // paroi et flanc jusqu'à son pied (le flanc, loin de la piste, se voit de loin)
            for (const t of [0.2, 0.4, 0.6, 0.8, 0.97]) hauts.push(add(mul(Ua, 1 - t), mul(Ga, t)), add(mul(add(Ua, Ub), 0.5 * (1 - t)), mul(add(Ga, Gb), 0.5 * t)));
        }
        masse[k] = hauts;
    }
    const cellules = () => {
        const H = new Float32Array((nvx - 1) * (nvz - 1));
        for (let j = 0; j + 1 < nvz; j++) for (let i = 0; i + 1 < nvx; i++) {
            const k = j * nvx + i;
            H[j * (nvx - 1) + i] = Math.min(Hv[k], Hv[k + 1], Hv[k + nvx], Hv[k + nvx + 1]) - 0.02;
        }
        return H;
    };
    const Hs = cellules();   // relief et masses (au-delà de VIS_DEC_OPAQUE)
    for (const D0 of dec) {
        if (D0.bas > sol + 0.5) continue;   /* (suspendu : chapeau, couronne ; la vue passe dessous) */
        const D = D0.eff > 1 ? Object.assign({}, D0, { eff: 1 }) : D0;   /* (plus large en haut : le pied seul, du rayon du bas, cache) */
        const { n, Vb, Vt } = prisme(D);
        for (let j = 1; j + 1 < n; j++) tri(Vt[0], Vt[j], Vt[j + 1]);
        for (let j = 0; j < n; j++) quad(Vb[j], Vb[(j + 1) % n], Vt[(j + 1) % n], Vt[j]);
    }
    const Hc = cellules();   // avec les décors
    const ncx = nvx - 1, ncz = nvz - 1;
    const Hat = (x, z) => {
        const i = Math.floor((x - x0) / VIS_OP), j = Math.floor((z - z0) / VIS_OP);
        return i < 0 || j < 0 || i >= ncx || j >= ncz ? sol : Hc[j * ncx + i];
    };
    // 2. Points à voir : relief (tous les VIS_TPAS, sur ses triangles), décors (dessus, et anneaux de leurs flancs), sections (axe,
    //    hauts des bords et des parois)
    const PX = [], PY = [], PZ = [], PQ = [], PK = [];   // position, sorte (0 relief, 1 décor, 2 section), rang (cellule, décor, section)
    const point = (x, y, z, q, k) => { PX.push(x); PY.push(y); PZ.push(z); PQ.push(q); PK.push(k); };
    const ncel = ter ? (ter.nx - 1) * (ter.nz - 1) : 0, NI = ter ? ter.nx - 1 : 0;
    if (ter) {
        const q = TER_G / VIS_TPAS;
        const hT = (I, J, fx, fz) => {   // hauteur sur les triangles de la cellule (comme ag_terrain)
            const h00 = th(I, J), h10 = th(I + 1, J), h11 = th(I + 1, J + 1), h01 = th(I, J + 1);
            if (Math.abs(h00 - h11) <= Math.abs(h10 - h01)) return fx >= fz ? h00 + (h10 - h00) * fx + (h11 - h10) * fz : h00 + (h01 - h00) * fz + (h11 - h01) * fx;
            return fx + fz <= 1 ? h00 + (h10 - h00) * fx + (h01 - h00) * fz : h11 + (h01 - h11) * (1 - fx) + (h10 - h11) * (1 - fz);
        };
        for (let J = 0; J + 1 < ter.nz; J++) for (let I = 0; I + 1 < ter.nx; I++) for (let b = 0; b <= q; b++) for (let a = 0; a <= q; a++) {
            const fx = a / q, fz = b / q;
            point(ter.x0 + TER_G * (I + fx), hT(I, J, fx, fz) + 0.1, ter.z0 + TER_G * (J + fz), 0, J * NI + I);
        }
    }
    dec.forEach((D, k) => {
        const { n, Vb, Vt } = prisme(D);
        point(D.x, D.haut + 0.05, D.z, 1, k);
        for (const t of [0.35, 0.65, 1]) for (let j = 0; j < n; j++) {
            const x = Vb[j][0] + (Vt[j][0] - Vb[j][0]) * t, z = Vb[j][2] + (Vt[j][2] - Vb[j][2]) * t, ox = x - D.x, oz = z - D.z, l = Math.hypot(ox, oz) || 1;
            point(x + ox / l * 0.08, D.bas + (D.haut - D.bas) * t - (t === 1 ? 0.05 : 0), z + oz / l * 0.08, 1, k);
        }
    });
    sec.forEach((S, k) => {
        const { u, r } = RP[k];
        point(S.c.x + u.x * 0.3, S.c.y + u.y * 0.3, S.c.z + u.z * 0.3, 2, k);
        for (const sg of [-1, 1]) { const e = add(add(S.c, mul(r, S.hw * sg)), mul(u, AGN_MUR_H + 0.05)); point(e.x, e.y, e.z, 2, k); }
        if (masse[k]) for (const h of masse[k]) point(h.x, h.y + 0.15, h.z, 2, k);
    });
    const NP = PX.length;
    // 3. Vu de chaque section
    // distances des pas le long d'un rayon : VIS_PAS jusqu'à VIS_LOIN[0], puis plus longs (le relief lointain, jusqu'au bord du terrain)
    const RMAX = Math.hypot(x1 - x0, z1 - z0) + 2, DS = [];
    for (let d = VIS_PAS; d <= RMAX; d += d < VIS_LOIN[0] ? VIS_PAS : d < VIS_LOIN[1] ? 2 * VIS_PAS : 4 * VIS_PAS) DS.push(d);
    const NS = DS.length, DSI = new Int32Array(Math.ceil(RMAX * 4) + 2);   // DSI[floor(d × 4)] : dernier pas à une distance <= d
    for (let q = 0, s = -1; q < DSI.length; q++) { while (s + 1 < NS && DS[s + 1] <= q / 4) s++; DSI[q] = s; }
    const M = new Float32Array(VIS_NA * NS), CA = new Float32Array(VIS_NA), SA = new Float32Array(VIS_NA);
    for (let a = 0; a < VIS_NA; a++) { CA[a] = Math.cos(a * 2 * Math.PI / VIS_NA); SA[a] = Math.sin(a * 2 * Math.PI / VIS_NA); }
    const vuPt = new Uint8Array(NP), vueSec = [];
    const voir = (Vx, Vy, Vz) => {   // marque dans vuPt les points vus de (Vx, Vy, Vz)
        for (let a = 0; a < VIS_NA; a++) {   // horizon de chaque rayon
            let mx = -1e9;
            const ca = CA[a], sa = SA[a], base = a * NS;
            for (let s = 0; s < NS; s++) {
                const d = DS[s];
                if (d >= VIS_PRES) {
                    const i = Math.floor((Vx + ca * d - x0) / VIS_OP), j = Math.floor((Vz + sa * d - z0) / VIS_OP);
                    const h = i < 0 || j < 0 || i >= ncx || j >= ncz ? sol : (d < VIS_DEC_OPAQUE ? Hc : Hs)[j * ncx + i], sl = (h - Vy) / d;
                    if (sl > mx) mx = sl;
                }
                M[base + s] = mx;
            }
        }
        for (let p = 0; p < NP; p++) {
            if (vuPt[p]) continue;
            const dx = PX[p] - Vx, dz = PZ[p] - Vz, d = Math.sqrt(dx * dx + dz * dz);
            if (d > RMAX) continue;
            const s = d - VIS_FIN > 0 ? DSI[Math.floor((d - VIS_FIN) * 4)] : -1;   // horizon des occultants jusqu'à d - VIS_FIN
            if (s < 0) { vuPt[p] = 1; continue; }
            let af = Math.atan2(dz, dx) / (2 * Math.PI) * VIS_NA;
            if (af < 0) af += VIS_NA;
            const a0 = Math.floor(af) % VIS_NA, a1 = (a0 + 1) % VIS_NA, sl = (PY[p] - Vy) / d;
            if (sl > M[a0 * NS + s] - 1e-4 || sl > M[a1 * NS + s] - 1e-4) vuPt[p] = 1;
        }
    };
    const marques = k => {
        const cel = new Uint8Array(ncel), de = new Uint8Array(dec.length), se = new Uint8Array(m);
        for (let p = 0; p < NP; p++) {
            if (!vuPt[p]) continue;
            if (PQ[p] === 0) cel[PK[p]] = 1; else if (PQ[p] === 1) de[PK[p]] = 1; else se[PK[p]] = 1;
        }
        if (k >= 0) se[k] = 1;   // (la sienne)
        return { cel, de, se };
    };
    const pointsDeVue = k => {
        const S = sec[k], { u, r } = RP[k], out = [];
        for (const o of VIS_LAT(S.hw)) {
            const Vx = S.c.x + r.x * o + u.x * VIS_H, Vy = S.c.y + r.y * o + u.y * VIS_H, Vz = S.c.z + r.z * o + u.z * VIS_H;
            if (Vy > Hat(Vx, Vz) + 0.05) out.push([Vx, Vy, Vz]);   // (pas dans un occultant : paroi d'un canyon, flanc d'un tunnel)
        }
        return out;
    };
    let nvue = 0;
    for (let k = 0; k < m; k++) {
        vuPt.fill(0);
        for (const V of pointsDeVue(k)) { voir(...V); nvue++; }
        vueSec.push(marques(k));
    }
    if (process.env.VIS_DIAG && process.env.VIS_DIAG.split(',')[0] === P.nom) {   // VIS_DIAG="NOM,x,y,z" : vu de ce point et pas du groupe de sa section
        const [, cx, cy, cz] = process.env.VIS_DIAG.split(',').map(Number);
        let best = 0, bd = 1e30;
        sec.forEach((S, k) => { const d = (S.c.x - cx) ** 2 + (S.c.y - cy) ** 2 + (S.c.z - cz) ** 2; if (d < bd) { bd = d; best = k; } });
        const g = Math.floor(best / VIS_G);
        vuPt.fill(0); voir(cx, cy, cz);
        const ici = marques(-1);
        const ks = [];
        for (let k = g * VIS_G - VIS_DEBORD; k < Math.min(m, (g + 1) * VIS_G) + VIS_DEBORD; k++) ks.push((k % m + m) % m);
        const cel = new Uint8Array(ncel);
        for (const k of ks) for (let i = 0; i < ncel; i++) cel[i] |= vueSec[k].cel[i];
        const manque = [], seG = new Uint8Array(m), msec = [];
        for (let i = 0; i < ncel; i++) if (ici.cel[i] && !cel[i]) manque.push(i);
        for (const k of ks) for (let i = 0; i < m; i++) seG[i] |= vueSec[k].se[i];
        for (let i = 0; i < m; i++) if (ici.se[i] && !seG[i]) msec.push(i);
        console.log(`  diagnostic : sections vues d'ici ${ici.se.reduce((a, b) => a + b, 0)}, du groupe ${seG.reduce((a, b) => a + b, 0)}, manquantes : ${msec.join(' ')}`);
        console.log(`  diagnostic : section ${best}, groupe ${g} (sections ${ks.join(' ')}), points de vue ${ks.map(k => pointsDeVue(k).length).join(' ')} ; cellules vues d'ici ${ici.cel.reduce((a, b) => a + b, 0)}, du groupe ${cel.reduce((a, b) => a + b, 0)}, manquantes : ${manque.map(i => (i % NI) + ':' + Math.floor(i / NI)).join(' ')}`);
        if (process.env.VIS_RAYON) for (const sg of [1, -1]) {   // VIS_RAYON="dx,dy" (pente à droite, en haut) : le rayon de l'écran, ce qu'il touche et ce qui le cache
            const [rx, ry] = process.env.VIS_RAYON.split(',').map(Number), l = Math.hypot(rx, 1);
            for (let D = 5; D < RMAX; D += 0.25) {
                const x = cx + sg * rx * D / l, z = cz + D / l, y = cy + ry * D / l;
                const I = Math.floor((x - ter.x0) / TER_G), J = Math.floor((z - ter.z0) / TER_G);
                if (I < 0 || J < 0 || I >= NI || J >= ter.nz - 1) continue;
                const h = Hat(x, z);
                if (h < y) continue;
                // premier point du rayon sous un occultant : ce qui le cache d'après le modèle
                let pire = null;
                for (let d = VIS_PRES; d < D - VIS_FIN; d += VIS_PAS) {
                    const xx = cx + (x - cx) * d / D, zz = cz + (z - cz) * d / D, hh = Hat(xx, zz), sl = (hh - cy) / d;
                    if (!pire || sl > pire.sl) pire = { d, sl, xx, zz, hh };
                }
                console.log(`    côté ${sg} : touche à ${D.toFixed(1)} (${x.toFixed(1)} ${z.toFixed(1)}, sol ${h.toFixed(2)}, rayon ${y.toFixed(2)}), cellule ${I}:${J}, vue d'ici ${ici.cel[J * NI + I]}, du groupe ${cel[J * NI + I]} ; pente du rayon ${((h - cy) / D).toFixed(3)}, pire occultant avant à ${pire && pire.d.toFixed(1)} (${pire && pire.xx.toFixed(1)} ${pire && pire.zz.toFixed(1)}, haut ${pire && pire.hh.toFixed(2)}, pente ${pire && pire.sl.toFixed(3)})`);
                break;
            }
        }
        for (const i of manque.slice(0, 3)) {
            const I = i % NI, J = Math.floor(i / NI), mx = ter.x0 + TER_G * (I + 0.5), mz = ter.z0 + TER_G * (J + 0.5);
            console.log(`    cellule ${I}:${J} au centre ${mx.toFixed(1)} ${mz.toFixed(1)}, hauteurs ${[th(I, J), th(I + 1, J), th(I + 1, J + 1), th(I, J + 1)].map(v => v.toFixed(1)).join(' ')}`);
        }
    }
    // 4. Groupes : union de leurs sections et de VIS_DEBORD de part et d'autre
    const ng = Math.ceil(m / VIS_G), donnees = [], pos = [0];
    let tc = 0, td = 0, ts = 0;
    for (let g = 0; g < ng; g++) {
        const cel = new Uint8Array(ncel), de = new Uint8Array(dec.length), se = new Uint8Array(m);
        for (let k = g * VIS_G - VIS_DEBORD; k < Math.min(m, (g + 1) * VIS_G) + VIS_DEBORD; k++) {
            const v = vueSec[(k % m + m) % m];
            for (let i = 0; i < ncel; i++) cel[i] |= v.cel[i];
            for (let i = 0; i < dec.length; i++) de[i] |= v.de[i];
            for (let i = 0; i < m; i++) se[i] |= v.se[i];
        }
        const rangs = [], decs = [], plages = [];
        for (let J = 0; ter && J + 1 < ter.nz; J++) for (let I = 0; I < NI; I++) {
            if (!cel[J * NI + I]) continue;
            let I1 = I;
            while (I1 + 1 < NI && cel[J * NI + I1 + 1]) I1++;
            rangs.push([J, I, I1]); tc += I1 - I + 1; I = I1;
        }
        for (let i = 0; i < dec.length; i++) if (de[i]) { decs.push(i); td++; }
        for (let i = 0; i < m; i++) {
            if (!se[i]) continue;
            let i1 = i;
            while (i1 + 1 < m && se[i1 + 1]) i1++;
            plages.push([i, i1]); ts += i1 - i + 1; i = i1;
        }
        const b = Buffer.alloc(8 + rangs.length * 6 + decs.length * 2 + plages.length * 4);
        let o = 0;
        for (const v of [rangs.length, decs.length, plages.length, 0]) { b.writeUInt16LE(v, o); o += 2; }
        for (const [J, I0, I1] of rangs) { b.writeUInt16LE(J, o); b.writeUInt16LE(I0, o + 2); b.writeUInt16LE(I1, o + 4); o += 6; }
        for (const d of decs) { b.writeUInt16LE(d, o); o += 2; }
        for (const [s0, s1] of plages) { b.writeUInt16LE(s0, o); b.writeUInt16LE(s1, o + 2); o += 4; }
        donnees.push(b); pos.push(pos[pos.length - 1] + b.length);
    }
    const corps = Buffer.concat([u16(VIS_G), u16(ng), ...pos.map(u32), ...donnees]);
    const bilan = `visibilité ${((Date.now() - t0) / 1000).toFixed(1)} s, ${nvue} points de vue : par groupe ${ncel ? Math.round(tc / ng / ncel * 100) : 0} % du relief, ` +
        `${dec.length ? Math.round(td / ng / dec.length * 100) : 0} % des décors, ${Math.round(ts / ng / m * 100)} % des sections ; ${Math.round(corps.length / 1024)} Ko`;
    return { bloc: Buffer.concat([Buffer.from('VIS1'), u32(corps.length), corps]), bilan };
}

// ---------------------------------------------------------------- Engins
// Places de couleur (AGC_* de antigrav.c) : 0 coque, 1 bande, 2 sombre, 3 verrière, 4 réacteur, 5 métal, 6 noir, 7 bord
const AGC = { coque: 0, bande: 1, sombre: 2, verriere: 3, reacteur: 4, metal: 5, noir: 6, bord: 7 };
// FLÈCHE (engin de secours, si un modèle importé manque) : sections lissées (loft), faces orientées vers l'extérieur
function loft(cx, profils, couleurs) {
    const V = [], F = [];
    for (const p of profils) {
        const [z, w, top, bas] = p, mid = (top + bas) / 2 + 0.02 * (top - bas);
        for (const [x, y] of [[-0.62 * w, top], [0.62 * w, top], [w, mid], [0.7 * w, bas], [-0.7 * w, bas], [-w, mid]]) V.push([cx + x, y, z]);
    }
    for (let s = 0; s + 1 < profils.length; s++) for (let i = 0; i < 6; i++)
        F.push({ v: [s * 6 + i, s * 6 + (i + 1) % 6, (s + 1) * 6 + (i + 1) % 6, (s + 1) * 6 + i], k: couleurs(i, s) });
    F.push({ v: [5, 4, 3, 2, 1, 0], k: 'avant' });
    const o = (profils.length - 1) * 6;
    F.push({ v: [0, 1, 2, 3, 4, 5].map(i => o + i), k: 'arriere' });
    return { V, F };
}
function assembler(pieces, places) {
    const V = [], F = [];
    for (const p of pieces) {
        const o = V.length, centre = p.V.reduce((a, v) => [a[0] + v[0] / p.V.length, a[1] + v[1] / p.V.length, a[2] + v[2] / p.V.length], [0, 0, 0]);
        V.push(...p.V);
        for (const f of p.F) {
            let vs = f.v.map(i => i + o);
            const P = vs.map(i => V[i]);
            let nx = 0, ny = 0, nz = 0, gx = 0, gy = 0, gz = 0;
            for (let i = 0; i < P.length; i++) {
                const a = P[i], b = P[(i + 1) % P.length];
                nx += (a[1] - b[1]) * (a[2] + b[2]); ny += (a[2] - b[2]) * (a[0] + b[0]); nz += (a[0] - b[0]) * (a[1] + b[1]);
                gx += a[0] / P.length; gy += a[1] / P.length; gz += a[2] / P.length;
            }
            const dir = p.double ? 1 : (gx - centre[0]) * nx + (gy - centre[1]) * ny + (gz - centre[2]) * nz;
            if (dir < 0) vs = vs.slice().reverse();
            const k = places(f.k);
            for (let i = 1; i + 1 < vs.length; i += 2) F.push({ v: i + 2 < vs.length ? [vs[0], vs[i], vs[i + 1], vs[i + 2]] : [vs[0], vs[i], vs[i + 1]], k });
            if (p.double) { const r = vs.slice().reverse(); for (let i = 1; i + 1 < r.length; i += 2) F.push({ v: i + 2 < r.length ? [r[0], r[i], r[i + 1], r[i + 2]] : [r[0], r[i], r[i + 1]], k }); }
        }
    }
    return { V, F };
}
function fleche() {
    const caisse = loft(0, [[0.80, 0.03, 0.05, 0.02], [0.45, 0.13, 0.12, -0.01], [0.05, 0.19, 0.17, -0.03], [-0.40, 0.21, 0.19, -0.03], [-0.62, 0.18, 0.16, -0.01]],
        (i, s) => i === 0 ? 'dessus' : i === 1 || i === 5 ? (s === 1 ? 'bande' : 'dessus') : i === 2 || i === 4 ? 'flanc' : 'dessous');
    const nacelle = cx => loft(cx, [[0.48, 0.03, 0.06, 0.01], [0.15, 0.07, 0.10, -0.02], [-0.45, 0.08, 0.10, -0.02], [-0.62, 0.07, 0.09, -0.01]],
        (i, s) => i === 0 ? (s === 0 ? 'bande' : 'dessus') : i === 3 ? 'dessous' : 'flanc');
    const verriere = loft(0, [[0.42, 0.04, 0.14, 0.10], [0.20, 0.10, 0.22, 0.15], [-0.05, 0.10, 0.24, 0.16], [-0.20, 0.06, 0.20, 0.16]], () => 'verriere');
    const aile = s => ({ V: [[s * 0.17, 0.09, 0.10], [s * 0.34, 0.07, 0.02], [s * 0.34, 0.09, -0.44], [s * 0.19, 0.14, -0.44], [s * 0.17, 0.05, 0.10], [s * 0.34, 0.03, 0.02], [s * 0.34, 0.05, -0.44], [s * 0.19, 0.06, -0.44]],
        F: [{ v: [0, 1, 2, 3], k: 'dessus' }, { v: [7, 6, 5, 4], k: 'dessous' }, { v: [4, 5, 1, 0], k: 'bord' }] });
    const derive = { V: [[0, 0.18, -0.22], [0, 0.46, -0.52], [0, 0.44, -0.62], [0, 0.17, -0.60]], F: [{ v: [0, 1, 2, 3], k: 'bande' }], double: true };
    const places = k => ({ arriere: AGC.reacteur, avant: AGC.noir, verriere: AGC.verriere, bande: AGC.bande, bord: AGC.bord, dessous: AGC.sombre, flanc: AGC.metal })[k] ?? AGC.coque;
    return assembler([caisse, nacelle(0.42), nacelle(-0.42), verriere, aile(1), aile(-1), derive], places);
}
// Coordonnées de texture d'un triangle dans la livrée (64 × 128) : projection selon sa normale (dessus : x, z ; flancs : z, y ;
// avant et arrière : x, y) ; dessus dans la moitié haute (nez en haut), flancs dans la moitié basse (nez à gauche)
function uvLivree(P, n) {
    const ax = Math.abs(n[0]), ay = Math.abs(n[1]), az = Math.abs(n[2]), c = (v, a, b) => Math.max(a, Math.min(b, v));
    return P.map(p => {
        if (ay >= ax && ay >= az) return [c((p[0] / 1.1 + 0.5) * 64, 0, 63), c((0.7 - p[2]) / 1.4 * 64, 0, 63)];
        if (ax >= az) return [c((0.7 - p[2]) / 1.4 * 64, 0, 63), c(64 + (0.45 - p[1]) / 0.6 * 64, 64, 127)];
        return [c((p[0] / 1.1 + 0.5) * 64, 0, 63), c(64 + (0.45 - p[1]) / 0.6 * 64, 64, 127)];
    }).map(([u, v]) => [Math.round(u), Math.round(v)]);
}
const VAISSEAUX = path.join(root, '..', 'assets', 'modeles', 'vaisseaux', 'antigrav');
const LOD_DIST = [5, 12];   // cases : au-delà, le niveau suivant (× VUE_K dans render.c) ; engins de 700, 320 et 140 triangles
const lum = h => (0.299 * (h >> 16 & 255) + 0.587 * (h >> 8 & 255) + 0.114 * (h & 255)) / 255;
const ecartRvb = (a, b) => Math.hypot((a >> 16 & 255) - (b >> 16 & 255), (a >> 8 & 255) - (b >> 8 & 255), (a & 255) - (b & 255));
// Engin importé (tools/antigrav/engins_importer.py) : sommets fusionnés. Avec sa texture (vaisseaux de Quaternius, 13/11/2026) :
// toutes les faces dans la livrée, aux coordonnées de texture du modèle (l'atlas recoloré : livreeTexture). Sans : la couleur la
// plus étendue du modèle et la suivante (assez différente, pas trop sombre) prennent la livrée de l'écurie (projection
// dessus / flancs), les autres restent celles du modèle (place 255)
// Noms montrés des engins (écran de l'écurie : « MODÈLE 2/2 · VESPA ») ; sinon le nom du pack
const NOMS_ENGINS = { mj_m1_ship_3: 'Vespa', mj_m3_ship_3: 'Nimbus', mj_m4_ship_2: 'Bastion' };
function enginImporte(id) {
    const f = path.join(VAISSEAUX, id + '.json');
    if (!fs.existsSync(f)) return null;
    const x = JSON.parse(fs.readFileSync(f, 'utf8'));
    const cols = x.couleurs.map(h => parseInt(h.slice(1), 16)), aire = cols.map(() => 0), T0 = x.lods[0].tris;
    const tri = (T, i) => [0, 1, 2].map(k => T.slice(i * 9 + k * 3, i * 9 + k * 3 + 3));
    const normale = P => { const u = [P[1][0] - P[0][0], P[1][1] - P[0][1], P[1][2] - P[0][2]], v = [P[2][0] - P[0][0], P[2][1] - P[0][1], P[2][2] - P[0][2]]; return [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]; };
    x.lods[0].coul.forEach((c, i) => { aire[c] += Math.hypot(...normale(tri(T0, i))) / 2; });
    const rang = cols.map((_, i) => i).sort((a, b) => aire[b] - aire[a]);
    const coque = rang.find(i => lum(cols[i]) > 0.3);
    const bande = rang.find(i => i !== coque && lum(cols[i]) > 0.25 && ecartRvb(cols[i], cols[coque]) > 60);
    let texture = null;
    if (x.texture) {
        const ft = path.join(VAISSEAUX, x.texture.fichier);
        if (fs.existsSync(ft)) texture = { w: x.texture.w, h: x.texture.h, rgb: fs.readFileSync(ft) };
    }
    const fixe = texture ? cols.map(() => 0) : cols.map(h => hex(h));   // (engin texturé : ses couleurs ne prennent pas de place)
    const uvModele = (l, i) => {   // coordonnées de texture d'un triangle, en texels de l'atlas (un seul tour : décalage commun)
        const U = [0, 1, 2].map(k => [l.uv[(i * 3 + k) * 2], l.uv[(i * 3 + k) * 2 + 1]]);
        const ou = Math.floor(Math.min(...U.map(p => p[0]))), ov = Math.floor(Math.min(...U.map(p => p[1])));
        return U.map(([u, v]) => [Math.max(0, Math.min(texture.w - 1, Math.floor((u - ou) * texture.w))),
            Math.max(0, Math.min(texture.h - 1, Math.floor((1 - (v - ov)) * texture.h)))]);
    };
    const lods = x.lods.map((l, li) => {
        const V = [], cle = new Map(), F = [];
        const sommet = p => { const k = p.map(v => Math.round(v * 1e4)).join(','); if (!cle.has(k)) { cle.set(k, V.length); V.push(p); } return cle.get(k); };
        l.coul.forEach((c, i) => {
            const P = tri(l.tris, i), v = P.map(sommet);
            if (v[0] === v[1] || v[1] === v[2] || v[0] === v[2]) return;
            if (texture && l.uv) { F.push({ v, k: AGC.coque, c: fixe[c], livree: true, uv: uvModele(l, i) }); return; }
            const livree = c === coque || c === bande;
            F.push({ v, k: livree ? AGC.coque : 255, c: fixe[c], livree, uv: livree ? uvLivree(P, normale(P)) : null });
        });
        return { V, F, dist: li + 1 < x.lods.length ? LOD_DIST[Math.min(li, LOD_DIST.length - 1)] : 0 };
    });
    return { id, nom: NOMS_ENGINS[id] || x.nom, lods, texture };
}

// Écuries : engin, couleurs des huit places (hex ; pleine lumière pour le réacteur et le bord), motif de la livrée (engin sans
// texture), accent (la couleur qui remplace l'orange de l'atlas d'un vaisseau texturé) et métal (éclat de ses gris), numéro,
// vitesse, accélération, maniabilité, devise. Vaisseaux de Quaternius depuis le 13/11/2026 (retour de l'utilisateur : ceux de
// Kenney, trop simples) : Challenger, Spitfire (gros réacteurs : la poussée), Zenith, Imperial (massif), Omen (flèche), Striker
// Deux engins par écurie depuis le 14/11/2026 (engin2 : les autres vaisseaux de Quaternius et trois de majadroid, CC0 ; section
// AGE2) ; couleurs néon le même jour (« soyons plus cyberpunk ») : accents néon, coques plus sombres (métal), réacteur (place 4) et
// bord (place 7) d'une couleur néon qui tranche avec l'accent
const ECURIES = [
    { nom: 'HALCYON', engin: 'q_challenger', engin2: 'mj_m1_ship_3', accent: 0xff2448, metal: 0.95, c: [0xe8ecf0, 0xff2448, 0x14161e, 0x30f0ff, 0x30f0ff, 0x7880a0, 0x08080c, 0xff2448], motif: 'bande', num: 1, v: 1.00, a: 1.00, m: 1.00, devise: "L'ÉCURIE HISTORIQUE" },
    { nom: 'ORBITA', engin: 'q_spitfire', engin2: 'mj_m3_ship_3', accent: 0x1e8cff, metal: 0.8, c: [0x1e8cff, 0xff8a1c, 0x101828, 0xffe070, 0xff8a1c, 0x6878a8, 0x080a14, 0x40c8ff], motif: 'chevrons', num: 7, v: 0.97, a: 1.08, m: 1.06, devise: 'TOUTE LA POUSSÉE' },
    { nom: 'IONIS', engin: 'q_zenith', engin2: 'q_executioner', accent: 0x2dff8a, metal: 0.65, c: [0x2dff8a, 0x0c1418, 0x08221a, 0xc8ff30, 0xc8ff30, 0x608878, 0x040806, 0x2dff8a], motif: 'eclairs', num: 3, v: 1.03, a: 0.94, m: 0.95, devise: 'LA FOUDRE EN BOUTEILLE' },
    { nom: 'KRAGG', engin: 'q_imperial', engin2: 'mj_m4_ship_2', accent: 0xffd60a, metal: 0.7, c: [0xffd60a, 0x202020, 0x302810, 0xff4a1a, 0xff4a1a, 0x887860, 0x0c0a08, 0xffd60a], motif: 'hachures', num: 9, v: 1.05, a: 0.92, m: 0.90, devise: 'LOURD, MAIS IMPLACABLE' },
    { nom: 'ASTRÉE', engin: 'q_omen', engin2: 'q_dispatcher', accent: 0xb44cff, metal: 0.85, c: [0xb44cff, 0xf0f0ff, 0x1e1636, 0xff4fd8, 0xff4fd8, 0x7870a8, 0x0a0814, 0xb44cff], motif: 'degrade', num: 5, v: 0.99, a: 1.02, m: 1.10, devise: 'LA GRÂCE EN VIRAGE' },
    { nom: 'VANTEX', engin: 'q_striker', engin2: 'q_insurgent', accent: 0xff1fa8, metal: 0.55, c: [0xff1fa8, 0x101012, 0x200810, 0xff3030, 0xff3030, 0x787880, 0x060406, 0xff1fa8], motif: 'damier', num: 4, v: 1.04, a: 0.98, m: 0.96, devise: 'NÉS POUR ATTAQUER' }
];
// Chiffres 3 × 5 (numéros de course)
const CHIFFRES = ['111101101101111', '010110010010111', '111001111100111', '111001111001111', '101101111001001', '111100111001111', '111100111101111', '111001001010010', '111101111101111', '111101111001111'];
// Livrée 64 × 128 : en haut le dessus (x en travers, nez en haut), en bas les flancs (nez à gauche, haut en haut)
function livree(e, ix) {
    const coque = ix[AGC.coque], bande = ix[AGC.bande], sombre = ix[AGC.sombre], bord = ix[AGC.bord], blanc = hex(0xf4f6fa);
    const coqueF = hex(melange(e.c[AGC.coque], 0x000000, 0.22)), bandeF = hex(melange(e.c[AGC.bande], 0x000000, 0.25));
    const t = new Tex(64, 128, coque);
    for (let y = 0; y < 128; y++) for (let x = 0; x < 64; x++) {
        const dessus = y < 64, yy = dessus ? y : y - 64;
        let c = (x % 16 === 0 || yy % 16 === 0) ? coqueF : coque;   // lignes de panneaux
        if (e.motif === 'bande') { if (dessus ? Math.abs(x - 31.5) < 6 : Math.abs(yy - 30) < 5) c = bande; if (dessus ? Math.abs(Math.abs(x - 31.5) - 8) < 1 : Math.abs(Math.abs(yy - 30) - 7) < 1) c = bord; }
        else if (e.motif === 'chevrons') { const p = ((dessus ? yy - Math.abs(x - 31.5) * 0.8 : x + Math.abs(yy - 30) * 0.8) % 16 + 16) % 16; if (p < 5) c = p < 1 ? bord : bande; }
        else if (e.motif === 'eclairs') { const p = (((dessus ? x + yy * 0.6 : yy + x * 0.6) + (((dessus ? yy : x) >> 3) & 1) * 6) % 20 + 20) % 20; if (p < 4) c = bande; else if (p < 5) c = bord; }
        else if (e.motif === 'hachures') { const p = ((x + yy) % 12 + 12) % 12; if ((dessus ? yy > 40 : x > 36) && p < 6) c = bande; if ((dessus ? yy === 40 : x === 36)) c = bord; }
        else if (e.motif === 'degrade') { const k = dessus ? yy / 64 : x / 64; if (bruit(x, y, 7) < k * 0.9 - 0.1) c = bande; if (bruit(x, y, 9) < 0.012) c = bord; }
        else if (e.motif === 'damier') { if ((dessus ? Math.abs(x - 31.5) < 10 : Math.abs(yy - 30) < 8) && (((x >> 2) ^ (yy >> 2)) & 1)) c = bande; if (dessus ? Math.abs(Math.abs(x - 31.5) - 11) < 1 : Math.abs(Math.abs(yy - 30) - 9) < 1) c = bord; }
        if (dessus && (x < 2 || x > 61)) c = sombre;
        t.set(x, y, c);
    }
    // Numéro : sur le dos vers l'arrière, et sur chaque flanc
    const chiffre = (x0, y0, d, k, c) => { for (let j = 0; j < 5; j++) for (let i = 0; i < 3; i++) if (CHIFFRES[d][j * 3 + i] === '1') t.rect(x0 + i * k, y0 + j * k, k, k, c); };
    t.rect(25, 40, 15, 13, sombre); chiffre(28, 41, e.num, 2, blanc);
    t.rect(36, 64 + 18, 11, 15, bandeF); chiffre(38, 64 + 20, e.num, 2, blanc);
    return t;
}

// Livrée d'un engin texturé : son atlas, l'orange (pixels saturés) remplacé par l'accent de l'écurie en cinq nuances selon leur
// éclat, le reste sur une rampe de dix gris commune à toutes les écuries (éclaircie ou assombrie par son métal) : 10 + 5 × 6
// couleurs de la palette commune
const GRIS = 10, ACCENT_K = [0.42, 0.62, 0.82, 1.02, 1.22];   // (néon, 14/11/2026 : plus clair, jusqu'à saturer ; 0,34 à 1,06 avant)
function livreeTexture(e, T) {
    const gris = Array.from({ length: GRIS }, (_, k) => { const g = Math.round((0.06 + 0.88 * k / (GRIS - 1)) * 255); return hex(g << 16 | g << 8 | g); });
    const acc = ACCENT_K.map(k => hex([16, 8, 0].reduce((h, sh) => h | Math.min(255, Math.round((e.accent >> sh & 255) * k)) << sh, 0)));
    const t = new Tex(T.w, T.h, gris[0]);
    for (let i = 0; i < T.w * T.h; i++) {
        const r = T.rgb[i * 3] / 255, g = T.rgb[i * 3 + 1] / 255, b = T.rgb[i * 3 + 2] / 255;
        const mx = Math.max(r, g, b), mn = Math.min(r, g, b), sat = mx > 0 ? (mx - mn) / mx : 0;
        if (sat > 0.28 && mx > 0.12) {
            const f = mx / 0.8;
            let j = 0;
            for (let k = 1; k < ACCENT_K.length; k++) if (Math.abs(ACCENT_K[k] - f) < Math.abs(ACCENT_K[j] - f)) j = k;
            t.p[i] = acc[j];
        } else {
            const L = Math.min(1, (0.299 * r + 0.587 * g + 0.114 * b) * e.metal);
            t.p[i] = gris[Math.max(0, Math.min(GRIS - 1, Math.round((L - 0.06) / 0.88 * (GRIS - 1))))];
        }
    }
    return t;
}

// Images de Forge (tools/antigrav/forge_images.py, 13/11/2026 : « créer des images avec Forge, pour les fonds mais aussi pour les
// écuries ») préparées dans assets/antigrav/ : horizon_<thème>.json (panorama à 360°, à la place de l'horizon dessiné par le code)
// et embleme_<écurie>.json (AGEM) ; largeur, hauteur, ligne d'horizon, couleurs (« #rrggbb »), pixels en base64 (0 : vide)
const IMAGES_FORGE = path.join(root, '..', 'assets', 'antigrav');
function imageForge(nom) {
    const fi = path.join(IMAGES_FORGE, nom + '.json');
    if (!fs.existsSync(fi)) return null;
    const d = JSON.parse(fs.readFileSync(fi, 'utf8'));
    return { w: d.w, h: d.h, base: d.base, cols: d.couleurs.map(c => parseInt(c.slice(1), 16)), px: Buffer.from(d.px, 'base64'), forge: true };
}

// ---------------------------------------------------------------- Écriture du paquet
const sections = [];
const section = (tag, buf) => sections.push([tag, buf]);
const u8 = v => Buffer.from([v & 255]);
const u16 = v => { const b = Buffer.alloc(2); b.writeUInt16LE(v & 0xFFFF); return b; };
const u32 = v => { const b = Buffer.alloc(4); b.writeUInt32LE(v >>> 0); return b; };
const f32 = v => { const b = Buffer.alloc(4); b.writeFloatLE(v); return b; };
const chaine = (s, n) => { const t = Buffer.from(s, 'utf8'); if (t.length > n - 1) throw new Error('texte trop long pour ' + (n - 1) + ' octets : ' + s); const b = Buffer.alloc(n); t.copy(b); return b; };
const rvb = h => Buffer.from([h >> 16 & 255, h >> 8 & 255, h & 255]);
const pad4 = b => Buffer.concat([b, Buffer.alloc((4 - b.length % 4) % 4)]);

// 1. Palette commune : couleurs des écuries, livrées, engins
const TEXTURES = [];   // grandes textures (DECO k = rang)
const ecuries = ECURIES.map(e => ({ ...e, ix: e.c.map((h, k) => hex(h, k === AGC.reacteur || k === AGC.bord ? 1 : 0)) }));
const MODELES = [];
for (const e of ecuries) {
    let k = MODELES.findIndex(m => m.id === e.engin);
    if (k < 0) {
        const m = enginImporte(e.engin);
        if (!m) {
            console.warn('engin ' + e.engin + ' absent de ' + path.relative(root, VAISSEAUX) + ' : FLÈCHE à la place');
            k = MODELES.findIndex(x => x.id === 'fleche');
            if (k < 0) {
                const f = fleche();
                MODELES.push({ id: 'fleche', nom: 'FLÈCHE', lods: [{ V: f.V, F: f.F.map(x => {
                    const P = x.v.map(i => f.V[i]), livre = x.k === AGC.coque || x.k === AGC.bande;
                    const n = [(P[1][1] - P[0][1]) * (P[2][2] - P[0][2]) - (P[1][2] - P[0][2]) * (P[2][1] - P[0][1]), (P[1][2] - P[0][2]) * (P[2][0] - P[0][0]) - (P[1][0] - P[0][0]) * (P[2][2] - P[0][2]), (P[1][0] - P[0][0]) * (P[2][1] - P[0][1]) - (P[1][1] - P[0][1]) * (P[2][0] - P[0][0])];
                    return { v: x.v, k: x.k, c: 0, livree: livre, uv: livre ? uvLivree(P, n) : null };
                }), dist: 0 }] });
                k = MODELES.length - 1;
            }
        } else { MODELES.push(m); k = MODELES.length - 1; }
    }
    e.modele = k;
    const T = MODELES[k].texture;
    e.livree = TEXTURES.length;
    TEXTURES.push(T ? livreeTexture(e, T) : livree(e, e.ix));
    // second engin (AGE2) : importé, sa livrée à la suite
    e.modele2 = -1; e.livree2 = -1;
    if (e.engin2) {
        let k2 = MODELES.findIndex(m => m.id === e.engin2);
        if (k2 < 0) { const m2 = enginImporte(e.engin2); if (m2) { MODELES.push(m2); k2 = MODELES.length - 1; } else console.warn('engin ' + e.engin2 + ' absent : ' + e.nom + ' n\'a qu\'un engin'); }
        if (k2 >= 0) { e.modele2 = k2; e.livree2 = TEXTURES.length; const T2 = MODELES[k2].texture; TEXTURES.push(T2 ? livreeTexture(e, T2) : livree(e, e.ix)); }
    }
}
const nCommun = COMMUN.length;

// 2. Pistes : chaque thème remplit sa palette, ses textures, son horizon
const HORIZONS = [], pistesBlocs = [];
let palPremiere = null;
for (const P of PISTES) {
    const T = THEMES[P.theme];
    THEME = [];
    const R = rampesTheme(T);
    const k0 = TEXTURES.length;
    TEXTURES.push(texPiste(R, false), texMur(R), texDessous(R), texSol(R, T.sol.style), texPilier(R), texPiste(R, true), texVitesse(R), texArme(R), texTerre(R), texRoche(R, T.roche.style), texFeuille(R));
    const tex = Array.from({ length: 11 }, (_, i) => k0 + i);   // ordre AGT_* : piste, mur, dessous, sol, pilier, départ, vitesse, arme, terre, roche, feuillage
    const texD = tex.map(k => { TEXTURES.push(texReduite(TEXTURES[k], 2)); return TEXTURES.length - 1; });   // la moitié de chacune
    const texQ = tex.map(k => { TEXTURES.push(texReduite(TEXTURES[k], 4)); return TEXTURES.length - 1; });   // son quart
    const hz = HORIZONS.length;
    HORIZONS.push(imageForge('horizon_' + P.theme) || horizon(T));
    const { sec, L } = construirePiste(P);
    const parts = [chaine(P.nom, 24), chaine(P.lieu, 24), f32(P.sol), u8(P.tours), u8(hz), u16(0), ...tex.map(u16), ...texD.map(u16), ...texQ.map(u16),
        rvb(T.ciel[0]), rvb(T.ciel[1]), rvb(T.ciel[2]), u8(Math.round(T.lum * 100)), ...T.teinte.map(t => u8(Math.round(t * 100))), u8(T.eau ? 1 : 0), u8(0),
        u8(THEME.length), u8(0), u8(0), u8(0)];
    for (const c of THEME) parts.push(Buffer.from(c));
    parts.push(u32(sec.length));
    for (const s of sec) parts.push(f32(s.c.x), f32(s.c.y), f32(s.c.z), f32(s.r.x), f32(s.r.y), f32(s.r.z), f32(s.hw), u8(s.drap), u8(Math.round(s.paroi * 8)), u8(0), u8(0));
    parts.push(u32(PLAQUES.length));
    for (const [part, ecart, sorte] of PLAQUES) {
        const k = Math.round(part * sec.length) % sec.length, hw = sec[k].hw;
        parts.push(u16(k), Buffer.from([Math.round(ecart * hw * 16) & 255]), u8(Math.round((sorte ? 0.55 : 0.7) * 16)), u8(sorte ? 2 : 3), u8(sorte), u16(0));
    }
    // Terrain (« TER1 », facultatif) : x0, z0, pas f32, nx, nz u16, hauteurs i16 (1/16 de case au-dessus du sol)
    const ter = terrainPiste(P, T, sec);
    if (ter) {
        const hb = Buffer.alloc(ter.h.length * 2);
        ter.h.forEach((v, i) => hb.writeInt16LE(v, i * 2));
        parts.push(Buffer.from('TER1'), f32(ter.x0), f32(ter.z0), f32(TER_G), u16(ter.nx), u16(ter.nz), hb);
    }
    const dec = decorPiste(P, T, sec);
    dec.push(...structuresPiste(P, sec, ter, dec));
    if (dec.length) {
        parts.push(Buffer.from('DEC1'), u32(dec.length));
        for (const d of dec) parts.push(f32(d.x), f32(d.z), f32(d.bas), f32(d.haut), f32(d.r), u8(d.cotes), u8(d.sorte), u8(Math.round(d.angle * 256) & 255), u8(Math.round(d.eff * 100)));
    } else parts.push(Buffer.from('DEC1'), u32(0));   // (vide : le bloc suivant reste à sa place)
    const vis = visibiliteCache(P, sec, ter, dec);
    parts.push(vis.bloc);
    const obs = obstaclesPiste(P, sec);
    parts.push(Buffer.from('OBS1'), u32(obs.length));
    for (const o of obs) parts.push(u16(o.sec), Buffer.from([Math.round(o.d * 16) & 255]), u8(Math.round(o.r * 32)), u8(Math.round(o.h * 16)), u8(o.cotes), u8(Math.round(o.angle * 256) & 255), u8(0));
    const corps = Buffer.concat(parts);
    pistesBlocs.push(Buffer.concat([u32(corps.length), corps]));
    if (!palPremiere) palPremiere = THEME.slice();
    console.log(`  piste « ${P.nom} » : ${sec.length} sections, ${L.toFixed(0)} cases ; ${THEME.length} couleurs de thème ; horizon ${HORIZONS[hz].forge ? 'de Forge, ' : ''}${HORIZONS[hz].cols.length} couleurs` + (ter ? ` ; terrain ${ter.nx} × ${ter.nz}, ${ter.hmax.toFixed(1)} au plus` : '') + ` ; ${dec.length} décors ; ${obs.length} obstacles ; ${vis.bilan}`);
}
THEME = null;

// AGJE
section('AGJE', chaine(JEU, 24));
// ---------------------------------------------------------------- Carrière
// (14/11/2026, demande de l'utilisateur : « étoffer les compétitions en créant une vraie carrière, avec de la variété ; un jeu où l'on
// ne découvre pas tout d'un coup et où les courses changent » ; conception : docs/course-anti-gravite.md, « 3 bis. Carrière ») :
// cinq ligues, une par catégorie ; chacune ouvre ses pistes et ses armes, a son calendrier d'épreuves (sortes EP, conditions CO,
// objectifs des médailles or, argent, bronze : la place, le temps en % du tour de référence, les coups portés, les tours tenus) et
// sa coupe. Section « AGCR » : version u8 (1), ligues u8, 0 u16 ; chaque ligue : nom (24), catégorie, épreuves, points de médailles
// pour la coupe u8, 0 u8, pistes ouvertes u16 (bits), armes u8 (bits des ARME_* d'antigrav.c), 0 u8 ; puis ses épreuves, 16 octets
// chacune : sorte, piste, tours, adversaires, conditions, écurie du duel (255 : aucune), objectifs × 3 ; coupe : pistes des
// courses 2 à 4 × 3 (255 : aucune) et leurs conditions × 3 ; 0
{
const EP = { COURSE: 0, CLM: 1, ELIM: 2, DUEL: 3, COMBAT: 4, ASCENSION: 5, COUPE: 6 };
const CO = { INVERSE: 1, NUIT: 2, BRUME: 4, SANS_ARMES: 8 };
const AR = { SEISME: 2, MISSILE: 4, MINES: 8, RAYON: 16, BOUCLIER: 32, TURBO: 64 };
const OBJ = { COURSE: [1, 2, 3], CLM: [99, 105, 113], ELIM: [1, 2, 3], DUEL: [1, 1, 1], COMBAT: [12, 8, 4], ASCENSION: [16, 11, 6], COUPE: [1, 2, 3] };   /* (ascension : paliers d'un quart de tour) */
const NE = 'NÉONS DE LUCERNE', CR = 'CRÊTE DE CENDRE', CA = 'CANYON DES SABLES', CL = 'COL DES CENDRES', TH = 'CANYONS DE THARSIS',
      LU = 'CRATÈRES DE LA LUNE', VE = 'JUNGLE DE VÉNUS', GA = 'GALERIES PROFONDES', LA = 'RÉCIFS DU LAGON';
const CARRIERE = [
    { nom: 'LIGUE DE CENDRE', classe: 0, seuil: 7, pistes: [NE, CR], armes: AR.SEISME | AR.BOUCLIER | AR.TURBO, epreuves: [
        { type: 'COURSE', piste: NE, tours: 3, adv: 3, cond: CO.SANS_ARMES },   // (tours : la base, × la catégorie dans antigrav.c : tours_classe)
        { type: 'CLM', piste: CR, tours: 3 },
        { type: 'COURSE', piste: CR, tours: 3, adv: 3 },
        { type: 'DUEL', piste: NE, tours: 3, rival: 'ASTRÉE' },
        { type: 'ELIM', piste: CR, adv: 3 },
        { type: 'COURSE', piste: NE, tours: 3, adv: 3, cond: CO.INVERSE },
        { type: 'COUPE', courses: [[NE], [CR], [NE, CO.INVERSE]], tours: 3, adv: 3 }] },
    { nom: 'LIGUE DE BRAISE', classe: 1, seuil: 9, pistes: [CA, CL], armes: AR.SEISME | AR.BOUCLIER | AR.TURBO | AR.MINES, epreuves: [
        { type: 'COURSE', piste: CA, tours: 3, adv: 3 },
        { type: 'COMBAT', piste: CR, tours: 3, adv: 3 },
        { type: 'CLM', piste: CL, tours: 3 },
        { type: 'DUEL', piste: CA, tours: 3, rival: 'IONIS' },
        { type: 'COURSE', piste: CL, tours: 3, adv: 3, cond: CO.BRUME },
        { type: 'ASCENSION', piste: CA },
        { type: 'ELIM', piste: CL, adv: 3, cond: CO.SANS_ARMES },
        { type: 'COUPE', courses: [[CA], [CL], [CR, CO.INVERSE]], tours: 3, adv: 3 }] },
    { nom: 'LIGUE DE FLAMME', classe: 2, seuil: 10, pistes: [LA, LU], armes: AR.SEISME | AR.BOUCLIER | AR.TURBO | AR.MINES | AR.MISSILE, epreuves: [
        { type: 'COURSE', piste: LA, tours: 3, adv: 3 },
        { type: 'CLM', piste: LU, tours: 3 },
        { type: 'COMBAT', piste: LU, tours: 3, adv: 3 },
        { type: 'DUEL', piste: LA, tours: 3, rival: 'KRAGG' },
        { type: 'ELIM', piste: CA, adv: 3, cond: CO.NUIT },
        { type: 'COURSE', piste: CL, tours: 3, adv: 3, cond: CO.INVERSE },
        { type: 'ASCENSION', piste: LA },
        { type: 'COUPE', courses: [[LA], [LU], [CA, CO.NUIT]], tours: 3, adv: 3 }] },
    { nom: 'LIGUE DE FOUDRE', classe: 3, seuil: 10, pistes: [TH, VE], armes: AR.SEISME | AR.BOUCLIER | AR.TURBO | AR.MINES | AR.MISSILE | AR.RAYON, epreuves: [
        { type: 'COURSE', piste: TH, tours: 3, adv: 3 },
        { type: 'COMBAT', piste: VE, tours: 3, adv: 3 },
        { type: 'CLM', piste: TH, tours: 3, cond: CO.INVERSE },
        { type: 'DUEL', piste: VE, tours: 3, rival: 'VANTEX' },
        { type: 'ELIM', piste: LU, adv: 3, cond: CO.BRUME },
        { type: 'COURSE', piste: LA, tours: 3, adv: 3, cond: CO.NUIT },
        { type: 'ASCENSION', piste: CL },
        { type: 'COUPE', courses: [[TH], [VE], [LU, CO.INVERSE]], tours: 3, adv: 3 }] },
    { nom: 'LIGUE NOVA', classe: 4, seuil: 11, pistes: [GA], armes: AR.SEISME | AR.BOUCLIER | AR.TURBO | AR.MINES | AR.MISSILE | AR.RAYON, epreuves: [
        { type: 'COURSE', piste: GA, tours: 3, adv: 3 },
        { type: 'ELIM', piste: TH, adv: 3, cond: CO.NUIT },
        { type: 'COMBAT', piste: GA, tours: 3, adv: 3 },
        { type: 'CLM', piste: VE, tours: 3, cond: CO.BRUME },
        { type: 'COURSE', piste: CR, tours: 3, adv: 3, cond: CO.INVERSE | CO.NUIT },
        { type: 'ASCENSION', piste: GA },
        { type: 'DUEL', piste: LU, tours: 3, rival: 'HALCYON' },
        { type: 'COUPE', courses: [[NE], [LU], [TH], [GA]], tours: 3, adv: 3 }] },
];
{
    const piste = nom => { const k = PISTES.findIndex(p => p.nom === nom); if (k < 0) throw new Error('carrière : piste inconnue ' + nom); return k; };
    const ecurie = nom => { const k = ECURIES.findIndex(e => e.nom === nom); if (k < 0) throw new Error('carrière : écurie inconnue ' + nom); return k; };
    const parts = [u8(1), u8(CARRIERE.length), u16(0)];
    for (const L of CARRIERE) {
        parts.push(chaine(L.nom, 24), u8(L.classe), u8(L.epreuves.length), u8(L.seuil), u8(0), u16(L.pistes.reduce((m, n) => m | 1 << piste(n), 0)), u8(L.armes), u8(0));
        for (const E of L.epreuves) {
            const c = E.courses || [[E.piste, E.cond || 0]], o = OBJ[E.type];
            if (o === undefined || EP[E.type] === undefined) throw new Error('carrière : épreuve inconnue ' + E.type);
            const b = Buffer.alloc(16, 0);
            b[0] = EP[E.type]; b[1] = piste(c[0][0]); b[2] = E.tours || (E.type === 'ELIM' ? (E.adv || 3) : 3); b[3] = E.type === 'DUEL' ? 1 : E.adv || 0;
            b[4] = c[0][1] || 0; b[5] = E.rival ? ecurie(E.rival) : 255; b[6] = o[0]; b[7] = o[1]; b[8] = o[2];
            for (let i = 0; i < 3; i++) { b[9 + i] = c[i + 1] ? piste(c[i + 1][0]) : 255; b[12 + i] = c[i + 1] ? c[i + 1][1] || 0 : 0; }
            parts.push(b);
        }
    }
    section('AGCR', Buffer.concat(parts));
    console.log('  carrière : ' + CARRIERE.map(L => L.nom + ' (' + L.epreuves.length + ')').join(', '));
}
}
// PAL : la commune et le thème de la première piste
{
    const b = Buffer.alloc(4 + 1024 + 8);
    b.writeUInt32LE(256, 0);
    COMMUN.forEach((c, i) => { b[4 + i * 4] = c[0]; b[5 + i * 4] = c[1]; b[6 + i * 4] = c[2]; b[7 + i * 4] = c[3]; });
    palPremiere.forEach((c, i) => { const j = PAL_COMMUN + i; b[4 + j * 4] = c[0]; b[5 + j * 4] = c[1]; b[6 + j * 4] = c[2]; b[7 + j * 4] = c[3]; });
    section('PAL ', b);
}
// TEX : tuile 0 = la palette, puis les grandes textures ; DECO : leur place
{
    const tuile = Buffer.alloc(256);
    for (let i = 0; i < 256; i++) tuile[i] = i;
    const morceaux = [tuile], deco = [u32(TEXTURES.length)];
    let off = 0;
    for (const t of TEXTURES) {
        const sh = Math.log2(t.w), shv = Math.log2(t.h);
        if (!Number.isInteger(sh) || !Number.isInteger(shv)) throw new Error('texture hors puissance de 2');
        deco.push(u32(off), u16(t.w), u16(t.h), u8(sh), u8(shv), u16(0));
        morceaux.push(Buffer.from(t.p));
        off += t.p.length;
    }
    section('TEX ', Buffer.concat(morceaux));
    section('DECO', Buffer.concat(deco));
}
// AGPS
section('AGPS', Buffer.concat([u8(1), u8(pistesBlocs.length), u16(0), ...pistesBlocs]));
// AGHZ
{
    const parts = [u8(HORIZONS.length), u8(0), u8(0), u8(0)];
    for (const h of HORIZONS) {
        parts.push(u16(h.w), u16(h.h), u16(h.base), u16(h.cols.length), pad4(Buffer.concat(h.cols.map(rvb))), pad4(Buffer.from(h.px)));
    }
    section('AGHZ', Buffer.concat(parts));
}
// AGEM : emblèmes des écuries, dans l'ordre d'AGEC (tous, sinon aucun : l'hexagone numéroté de antigrav.c)
{
    const em = ECURIES.map(e => imageForge('embleme_' + e.nom));
    if (em.every(Boolean)) {
        const parts = [u8(em.length), u8(0), u8(0), u8(0)];
        for (const h of em) parts.push(u16(h.w), u16(h.h), u16(h.base), u16(h.cols.length), pad4(Buffer.concat(h.cols.map(rvb))), pad4(Buffer.from(h.px)));
        section('AGEM', Buffer.concat(parts));
    } else if (em.some(Boolean)) console.warn('emblèmes incomplets : ' + ECURIES.filter((e, i) => !em[i]).map(e => e.nom).join(', ') + ' (AGEM non écrite)');
}
// AGIM : images du jeu (14/11/2026, demande de l'utilisateur : « appelle le jeu juste AntiGraV et créons un logo et des images de
// scène avec Forge ») : le logo (deux tailles) et les scènes de forge_images.py (logo.json, logo_petit.json, scene_<nom>.json), par
// nom : images u8, 0 × 3 ; chaque image : nom (16), puis comme AGEM
{
    const NOMS = ['logo', 'logo_petit', 'titre', 'ligue_cendre', 'ligue_braise', 'ligue_flamme', 'ligue_foudre', 'ligue_nova', 'garage', 'podium'];
    for (let m = 0; m < 3; m++) { for (let k = 0; k < 16; k++) NOMS.push('tro' + m + '_' + String(k).padStart(2, '0')); NOMS.push('trp' + m); }   // trophées : bronze, argent, or
    const ims = NOMS.map(n => [n, imageForge(/^(logo|tro|trp)/.test(n) ? n : 'scene_' + n)]).filter(([, im]) => im);
    if (ims.length) {
        const parts = [u8(ims.length), u8(0), u8(0), u8(0)];
        for (const [n, h] of ims) parts.push(chaine(n, 16), u16(h.w), u16(h.h), u16(h.base || 0), u16(h.cols.length), pad4(Buffer.concat(h.cols.map(rvb))), pad4(Buffer.from(h.px)));
        section('AGIM', Buffer.concat(parts));
        console.log('  images : ' + ims.filter(([n]) => !/^tr[op]/.test(n)).map(([n]) => n).join(', ') + ' ; ' + ims.filter(([n]) => /^tr[op]/.test(n)).length + ' images de trophées');
    }
}
// AGCK : cockpits des écuries (vue cockpit, 14/11/2026), dans l'ordre d'AGEC : cockpit_<écurie>.json de forge_images.py (tailles :
// la plus petite d'abord) ; une écurie sans cockpit a 0 image (la vue cockpit sans cadre)
{
    const ck = ECURIES.map(e => {
        const fi = path.join(IMAGES_FORGE, 'cockpit_' + e.nom + '.json');
        if (!fs.existsSync(fi)) return [];
        return JSON.parse(fs.readFileSync(fi, 'utf8')).tailles.map(d => ({ w: d.w, h: d.h, cols: d.couleurs.map(c => parseInt(c.slice(1), 16)), px: Buffer.from(d.px, 'base64') }));
    });
    if (ck.some(t => t.length)) {
        const parts = [u8(ck.length), u8(0), u8(0), u8(0)];
        for (const t of ck) {
            parts.push(u8(t.length), u8(0), u8(0), u8(0));
            for (const h of t) parts.push(u16(h.w), u16(h.h), u16(0), u16(h.cols.length), pad4(Buffer.concat(h.cols.map(rvb))), pad4(Buffer.from(h.px)));
        }
        section('AGCK', Buffer.concat(parts));
        console.log('  cockpits : ' + ECURIES.filter((e, i) => ck[i].length).map(e => e.nom).join(', '));
    }
}
// AGNA (version 3 : livrées texturées)
{
    const parts = [u8(3), u8(MODELES.length), u16(0)];
    for (const m of MODELES) {
        parts.push(chaine(m.nom, 16), u8(m.lods.length), u8(0), u8(0), u8(0));
        for (const l of m.lods) {
            parts.push(u16(l.V.length), u16(l.F.length), f32(l.dist));
            for (const v of l.V) parts.push(f32(v[0]), f32(v[1]), f32(v[2]));
            for (const f of l.F) {
                const vs = [0, 1, 2, 3].map(i => f.v[i] !== undefined ? f.v[i] : f.v[f.v.length - 1]);
                const uv = [0, 1, 2, 3].map(i => f.uv ? (f.uv[i] || f.uv[f.uv.length - 1]) : [0, 0]);
                parts.push(u8(f.v.length), u8(f.k), u8(f.c || 0), u8(f.livree ? 1 : 0), ...vs.map(u16), Buffer.from(uv.flat()));
            }
        }
    }
    section('AGNA', Buffer.concat(parts));
}
// AGEC (version 2)
{
    const parts = [u32(ecuries.length)];
    for (const e of ecuries) parts.push(chaine(e.nom, 16), u8(e.modele), u8(e.livree), u8(e.num), u8(0), Buffer.from(e.ix), f32(e.v), f32(e.a), f32(e.m), chaine(e.devise, 24));
    section('AGEC', Buffer.concat(parts));
}
// AGE2 (14/11/2026) : second engin de chaque écurie (ordre d'AGEC) : écuries u8, 0 × 3 ; chacune : modèle u8 (255 : aucun), 0, livrée u16
{
    const parts = [u8(ecuries.length), u8(0), u16(0)];
    for (const e of ecuries) parts.push(u8(e.modele2 >= 0 ? e.modele2 : 255), u8(0), u16(e.livree2 >= 0 ? e.livree2 : 0xFFFF));
    section('AGE2', Buffer.concat(parts));
    console.log('  engins : ' + ecuries.map(e => e.nom + ' ' + e.engin + (e.modele2 >= 0 ? ' + ' + e.engin2 : '')).join(', '));
}
// Polices, icônes et sons d'Echoes
const { audiCourse } = require('./sons_course.js');
const SON_N_ECHOES = 43;   // SON_N d'Echoes avant les sons de la course (core/audio.h : SON_AG_SEISME en est le rang)
{
    const b = fs.readFileSync(SOURCE);
    if (b.readUInt32LE(0) !== 0x4F484345) throw new Error(SOURCE + ' : pas un paquet');
    let o = 8;
    const voulues = new Set(['FONT', 'ICON', 'AUDI']);
    while (o + 8 <= b.length) {
        const tag = b.toString('latin1', o, o + 4), l = b.readUInt32LE(o + 4);
        if (voulues.has(tag)) {
            if (tag === 'AUDI') {   // les sons d'Echoes, puis ceux de la course (sons_course.js, SON_AG_* de core/audio.h, après les SON_N_ECHOES d'Echoes)
                const a = audiCourse(b.subarray(o + 8, o + 8 + l), SON_N_ECHOES);
                section(tag, a.audi);
                console.log('  sons de la course : ' + a.sons.map(x => x.nom).join(', '));
            } else section(tag, b.subarray(o + 8, o + 8 + l));
            voulues.delete(tag);
        }
        o += 8 + l;
    }
    if (voulues.size) console.warn('absentes de ' + path.basename(SOURCE) + ' : ' + [...voulues].join(', '));
}

const morceaux = [u32(0x4F484345), u32(5)];
for (const [tag, buf] of sections) {
    const pad = (4 - buf.length % 4) % 4;
    morceaux.push(Buffer.from(tag, 'latin1'), u32(buf.length + pad), buf, Buffer.alloc(pad));
}
morceaux.push(Buffer.from('END ', 'latin1'), u32(0));
fs.mkdirSync(path.dirname(SORTIE), { recursive: true });
const tout = Buffer.concat(morceaux);
fs.writeFileSync(SORTIE, tout);
console.log(`${path.relative(root, SORTIE)} : ${Math.round(tout.length / 1024)} Ko ; ${PISTES.length} pistes, ${HORIZONS.length} horizons, ${TEXTURES.length} textures ; ` +
    `engins ${MODELES.map(m => m.id + ' ' + m.lods.map(l => l.F.length).join('/')).join(', ')} ; palette commune ${nCommun}/${PAL_COMMUN}`);
// Aperçus (contrôle) : horizons et livrées en PNG à côté du paquet, si le Python des outils les convertit (facultatif)
if (process.env.AG_APERCUS) {
    const dir = path.join(path.dirname(SORTIE), 'apercus');
    fs.mkdirSync(dir, { recursive: true });
    HORIZONS.forEach((h, i) => fs.writeFileSync(path.join(dir, `horizon_${i}.json`), JSON.stringify({ w: h.w, h: h.h, cols: h.cols, px: Array.from(h.px) })));
    ecuries.forEach((e, i) => fs.writeFileSync(path.join(dir, `livree_${i}.json`), JSON.stringify({ w: 64, h: 128, px: Array.from(TEXTURES[e.livree].p), pal: COMMUN })));
}
