// Sons propres à la course anti-gravité (14/11/2026, demandes de l'utilisateur : « il faut aussi l'effet sonore » du SÉISME, puis les
// autres armes et la destruction des vaisseaux) : synthétisés ici, ajoutés après ceux d'Echoes dans la section AUDI du paquet
// (generer.js), dans l'ordre des SON_AG_* de core/audio.h (à garder identiques ; un paquet d'Echoes ne les a pas : REPLI de audio.c).
// Mono 24 kHz 16 bits comme la banque d'Echoes ; l'essentiel entre 150 Hz et 4 kHz (haut-parleur d'une console portable) ; niveau
// ramené à une même énergie (RMS_CIBLE), crête bornée par un limiteur doux. Tirages par un générateur à graine : le même paquet à
// chaque fois.
'use strict';
const F = 24000;
let graine = 0x2468ace1;
const hasard = () => { graine ^= graine << 13; graine >>>= 0; graine ^= graine >>> 17; graine ^= graine << 5; graine >>>= 0; return graine / 4294967296; };
const bruit = () => hasard() * 2 - 1;
const tableau = s => new Float64Array(Math.round(s * F));
// Filtres simples (une voie) et passe-bande du second ordre (biquad, d'après le livre de recettes de R. Bristow-Johnson)
function passeBas(x, fc) { const a = 1 - Math.exp(-2 * Math.PI * fc / F); let y = 0; for (let i = 0; i < x.length; i++) { y += a * (x[i] - y); x[i] = y; } return x; }
function passeHaut(x, fc) { const a = Math.exp(-2 * Math.PI * fc / F); let y = 0, p = 0; for (let i = 0; i < x.length; i++) { y = a * (y + x[i] - p); p = x[i]; x[i] = y; } return x; }
function passeBande(x, fc, q, fcDe) {   // fcDe(i) : fréquence qui varie (balayage), sinon fc
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (let i = 0; i < x.length; i++) {
        const f = fcDe ? fcDe(i) : fc, w = 2 * Math.PI * f / F, al = Math.sin(w) / (2 * q), a0 = 1 + al;
        const b0 = al / a0, b2 = -al / a0, a1 = -2 * Math.cos(w) / a0, a2 = (1 - al) / a0;
        const y = b0 * x[i] + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x[i]; y2 = y1; y1 = y; x[i] = y;
    }
    return x;
}
const enveloppe = (i, n, att, dec) => Math.min(1, i / Math.max(1, att * F)) * Math.exp(-i / F / dec);
// Boucle sans claquement : les tf dernières secondes fondues dans les tf premières, puis coupées
function boucler(x, tf) {
    const m = Math.round(tf * F), n = x.length - m, y = new Float64Array(n);
    for (let i = 0; i < n; i++) y[i] = x[i];
    for (let i = 0; i < m; i++) { const k = i / m; y[i] = x[i] * k + x[n + i] * (1 - k); }
    return y;
}
const RMS_CIBLE = 0.16;
function finir(x, boucle, rmsK = 1) {
    let s = 0; for (const v of x) s += v * v;
    const rms = Math.sqrt(s / x.length) || 1, g = RMS_CIBLE * rmsK / rms;
    const pcm = new Int16Array(x.length);
    for (let i = 0; i < x.length; i++) { const v = Math.tanh(x[i] * g * 1.3) / Math.tanh(1.3); pcm[i] = Math.round(Math.max(-1, Math.min(1, v)) * 30000); }
    return { pcm, rate: F, boucle: boucle ? 1 : 0 };
}

// 1. SÉISME : grondement en boucle (1,2 s) : bruit brun grave, bande de 120 à 400 Hz, tremblement à 7 Hz, éboulis (craquements
//    de 800 à 2 000 Hz à des instants tirés)
function seisme() {
    const x = tableau(1.4);
    let b = 0;
    for (let i = 0; i < x.length; i++) { b = (b + bruit() * 0.08) * 0.995; x[i] = b; }
    passeBande(x, 220, 0.7);
    for (let i = 0; i < x.length; i++) x[i] *= 0.75 + 0.25 * Math.sin(2 * Math.PI * 7 * i / F) + 0.12 * Math.sin(2 * Math.PI * 2.3 * i / F);
    const eboulis = tableau(1.4);
    for (let k = 0; k < 26; k++) {
        const t0 = Math.floor(hasard() * (eboulis.length - 2000)), l = 300 + Math.floor(hasard() * 900), a = 0.3 + hasard() * 0.7;
        for (let i = 0; i < l; i++) eboulis[t0 + i] += bruit() * a * Math.exp(-i / (l * 0.3));
    }
    passeBande(eboulis, 1300, 1.2);
    for (let i = 0; i < x.length; i++) x[i] = x[i] * 3.2 + eboulis[i] * 0.9;
    return finir(boucler(x, 0.2), true, 1.1);
}
// 2. MISSILE : départ (0,7 s) : coup sourd, souffle qui balaie de 3 kHz à 600 Hz, sifflement qui descend
function missile() {
    const x = tableau(0.7), n = x.length;
    for (let i = 0; i < n; i++) x[i] = bruit() * enveloppe(i, n, 0.004, 0.22);
    passeBande(x, 0, 1.4, i => 3000 * Math.pow(600 / 3000, i / n));
    let ph = 0;
    for (let i = 0; i < n; i++) {
        const f = 900 * Math.pow(300 / 900, i / n); ph += f / F;
        x[i] += 0.35 * (2 * (ph % 1) - 1) * enveloppe(i, n, 0.01, 0.3) + 0.6 * Math.sin(2 * Math.PI * 140 * i / F) * Math.exp(-i / F / 0.05);
    }
    return finir(x, false);
}
// 3. MINES : pose (0,3 s) : choc métallique (partiels inharmoniques), puis un bip d'armement
function mine() {
    const x = tableau(0.3), n = x.length;
    for (let i = 0; i < n; i++) {
        const t = i / F;
        x[i] = (Math.sin(2 * Math.PI * 520 * t) + 0.7 * Math.sin(2 * Math.PI * 1340 * t) + 0.5 * Math.sin(2 * Math.PI * 2210 * t)) * Math.exp(-t / 0.05) + bruit() * 0.4 * Math.exp(-t / 0.01);
        if (t > 0.13 && t < 0.21) x[i] += 0.5 * Math.sign(Math.sin(2 * Math.PI * 1800 * t)) * Math.min(1, (t - 0.13) / 0.005, (0.21 - t) / 0.005);
    }
    return finir(x, false);
}
// 4. RAYON, charge : sifflement en boucle (0,5 s ; 90 périodes à 180 Hz, vibrato de 12 Hz : la boucle tombe juste), dont le moteur
//    monte la hauteur avec la charge
function charge() {
    const x = tableau(0.5), n = x.length;
    let ph = 0, ph2 = 0;
    for (let i = 0; i < n; i++) {
        const v = 1 + 0.03 * Math.sin(2 * Math.PI * 12 * i / F);
        ph += 180 * v / F; ph2 += 360 * v / F;
        x[i] = 0.6 * (2 * (ph % 1) - 1) + 0.4 * Math.sin(2 * Math.PI * ph2) + 0.25 * Math.sin(2 * Math.PI * ph * 4.02);
    }
    passeBas(x, 3000);
    return finir(x, true, 0.8);
}
// 5. RAYON, tir (0,9 s) : décharge qui tombe de 1 600 à 200 Hz (scie et carré désaccordé, saturés), éclat de bruit au départ
function rayon() {
    const x = tableau(0.9), n = x.length;
    let a = 0, b = 0;
    for (let i = 0; i < n; i++) {
        const f = 1600 * Math.pow(200 / 1600, Math.pow(i / n, 0.6));
        a += f / F; b += f * 1.51 / F;
        const e = enveloppe(i, n, 0.003, 0.3);
        x[i] = Math.tanh(2.5 * ((2 * (a % 1) - 1) * 0.6 + Math.sign(Math.sin(2 * Math.PI * b)) * 0.4)) * e + bruit() * Math.exp(-i / F / 0.03) * 0.8;
    }
    passeBas(x, 6000);
    return finir(x, false, 1.1);
}
// 6. BOUCLIER (0,7 s) : chatoiement qui monte (600 à 1 500 Hz, tremolo de 20 Hz)
function bouclier() {
    const x = tableau(0.7), n = x.length;
    const ph = [0, 0, 0, 0];
    for (let i = 0; i < n; i++) {
        const k = 1 + 0.5 * i / n, e = Math.sin(Math.PI * i / n);
        let s = 0;
        [600, 900, 1200, 1500].forEach((f, j) => { ph[j] += f * k / F; s += Math.sin(2 * Math.PI * ph[j]) / (j + 1); });
        x[i] = s * e * (0.7 + 0.3 * Math.sin(2 * Math.PI * 20 * i / F));
    }
    return finir(x, false, 0.8);
}
// 7. Destruction d'un vaisseau (1,6 s) : bruit dont la coupure tombe de 4 kHz à 300 Hz, coup grave (150 puis 70 Hz), crépitements
function explosion() {
    const x = tableau(1.6), n = x.length;
    for (let i = 0; i < n; i++) x[i] = bruit() * enveloppe(i, n, 0.005, 0.45);
    let y = 0;
    for (let i = 0; i < n; i++) { const fc = 300 + 3700 * Math.exp(-i / F / 0.25), a = 1 - Math.exp(-2 * Math.PI * fc / F); y += a * (x[i] - y); x[i] = y; }
    for (let i = 0; i < n; i++) {
        const t = i / F;
        x[i] = x[i] * 1.6 + 0.9 * Math.sin(2 * Math.PI * (150 - 80 * Math.min(1, t / 0.3)) * t) * Math.exp(-t / 0.18);
    }
    for (let k = 0; k < 40; k++) {
        const t0 = Math.floor((0.1 + Math.pow(hasard(), 1.5) * 1.2) * F), l = 80 + Math.floor(hasard() * 300), a = 0.5 * Math.exp(-t0 / F / 0.5);
        for (let i = 0; i < l && t0 + i < n; i++) x[t0 + i] += bruit() * a * Math.exp(-i / (l * 0.3));
    }
    return finir(x, false, 1.5);
}
// 8. Absorption de l'arme en énergie (0,5 s) : arpège qui monte, un écho
function absorbe() {
    const x = tableau(0.5), n = x.length, notes = [440, 554, 659, 880];
    for (let i = 0; i < n; i++) {
        const t = i / F, k = Math.min(3, Math.floor(t / 0.09)), tl = t - k * 0.09, f = notes[k];
        x[i] = (Math.sin(2 * Math.PI * f * t) * 0.7 + Math.sin(2 * Math.PI * f * 2 * t) * 0.3) * Math.exp(-tl / 0.12) * (t < 0.42 ? 1 : (0.5 - t) / 0.08);
    }
    const d = Math.round(0.06 * F);
    for (let i = n - 1; i >= d; i--) x[i] += 0.35 * x[i - d];
    return finir(x, false, 0.8);
}
// 9. Alarme : un missile vise le joueur (0,15 s) : deux tons
function alerte() {
    const x = tableau(0.15), n = x.length;
    for (let i = 0; i < n; i++) { const t = i / F, f = t < 0.07 ? 1200 : 1600; x[i] = Math.sign(Math.sin(2 * Math.PI * f * t)) * 0.6 * Math.min(1, i / 60, (n - i) / 60); }
    passeBas(x, 5000);
    return finir(x, false, 0.7);
}
// 10. Impact d'une arme sur un vaisseau (0,5 s) : éclat de bruit vers 1,5 kHz, coup à 120 Hz, tôle qui sonne (700 et 1 900 Hz)
function impact() {
    const x = tableau(0.5), n = x.length;
    for (let i = 0; i < n; i++) x[i] = bruit() * enveloppe(i, n, 0.002, 0.08);
    passeBande(x, 1500, 0.9);
    for (let i = 0; i < n; i++) {
        const t = i / F;
        x[i] = x[i] * 2 + 0.8 * Math.sin(2 * Math.PI * 120 * t) * Math.exp(-t / 0.08) + 0.3 * (Math.sin(2 * Math.PI * 700 * t) + 0.6 * Math.sin(2 * Math.PI * 1900 * t)) * Math.exp(-t / 0.15);
    }
    return finir(x, false, 1.2);
}

// Dans l'ordre des SON_AG_* de core/audio.h
const SONS = [['seisme', seisme], ['missile', missile], ['mine', mine], ['charge', charge], ['rayon', rayon], ['bouclier', bouclier],
              ['explosion', explosion], ['absorbe', absorbe], ['alerte', alerte], ['impact', impact]];
function sonsCourse() { graine = 0x2468ace1; return SONS.map(([nom, f]) => Object.assign({ nom }, f())); }
// Section AUDI : celle d'Echoes (sons 0 à n - 1), complétée jusqu'à SON_N (silences s'il en manque), puis ceux de la course
function audiCourse(audi, sonN) {
    const n = audi.readUInt32LE(0), parts = [], ajout = sonsCourse();
    let o = 8;
    for (let i = 0; i < n && i < sonN; i++) {
        const ne = audi.readUInt32LE(o), l = 8 + ((ne * 2 + 3) & ~3);
        parts.push(audi.subarray(o, o + l)); o += l;
    }
    for (let i = n; i < sonN; i++) { const b = Buffer.alloc(8); b.writeUInt16LE(24000, 4); parts.push(b); }   // (sons d'Echoes absents : vides)
    for (const s of ajout) {
        const b = Buffer.alloc(8 + ((s.pcm.length * 2 + 3) & ~3));
        b.writeUInt32LE(s.pcm.length, 0); b.writeUInt16LE(s.rate, 4); b[6] = s.boucle;
        for (let i = 0; i < s.pcm.length; i++) b.writeInt16LE(s.pcm[i], 8 + 2 * i);
        parts.push(b);
    }
    const tete = Buffer.from(audi.subarray(0, 8));
    tete.writeUInt32LE(sonN + ajout.length, 0);
    return { audi: Buffer.concat([tete, ...parts]), sons: ajout };
}
// Écoute : `node tools/antigrav/sons_course.js dossier` écrit chaque son en WAV
if (require.main === module) {
    const fs = require('fs'), path = require('path'), dos = process.argv[2] || '.';
    fs.mkdirSync(dos, { recursive: true });
    for (const s of sonsCourse()) {
        const d = Buffer.alloc(44 + s.pcm.length * 2);
        d.write('RIFF', 0); d.writeUInt32LE(36 + s.pcm.length * 2, 4); d.write('WAVEfmt ', 8); d.writeUInt32LE(16, 16); d.writeUInt16LE(1, 20); d.writeUInt16LE(1, 22);
        d.writeUInt32LE(s.rate, 24); d.writeUInt32LE(s.rate * 2, 28); d.writeUInt16LE(2, 32); d.writeUInt16LE(16, 34); d.write('data', 36); d.writeUInt32LE(s.pcm.length * 2, 40);
        for (let i = 0; i < s.pcm.length; i++) d.writeInt16LE(s.pcm[i], 44 + 2 * i);
        fs.writeFileSync(path.join(dos, s.nom + '.wav'), d);
        console.log(s.nom, (s.pcm.length / s.rate).toFixed(2) + ' s', s.boucle ? 'boucle' : '');
    }
}
module.exports = { sonsCourse, audiCourse };
