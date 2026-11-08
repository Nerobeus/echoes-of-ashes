/*
 * Essai des répliques par zone sans régénérer le monde : ajoute les sections REPZ et REPT (format : core/pak.h) à une
 * copie d'un paquet existant, juste avant END (rien ne bouge : les décalages absolus de CHNK, CART, IMAG restent bons ; un
 * REPL déjà là reste, REPZ l'emporte). Les sections sont faites par replPaquet de l'émulateur de l'Atelier lui-même (son
 * texte est tiré de emulateur.html), sur le JSON de serveur/repliques-zones.js de l'Atelier (lots, grille, drapeaux du
 * scénario). Avec --choix, rejoue aussi le choix de l'émulateur (replChoisir) sur une suite de paroles pour la comparer à
 * celle du moteur (rendu_test, étape repl:N).
 *
 *   node tools/repliques_paquet.js data/monde.pak repliques.json build/monde_repl.pak [emulateur.html]
 *   node tools/repliques_paquet.js --choix repliques.json lot phase verrou18 graine1,graine2,… [tours] [emulateur.html]
 */
const fs = require('fs'), path = require('path');
const EMU = 'D:/Documents/Stellar Exodus/tileset-generator-v4/emulateur/emulateur.html';

// Texte d'une fonction de l'émulateur (de « function nom(» à son accolade fermante), évaluée ici
function fonctionEmu(html, nom, avant) {
    const i = html.indexOf('function ' + nom + '(');
    if (i < 0) throw new Error(nom + ' introuvable dans l\'émulateur');
    let k = html.indexOf('{', i), prof = 0;
    for (; k < html.length; k++) { if (html[k] === '{') prof++; else if (html[k] === '}' && --prof === 0) break; }
    return new Function('TextEncoder', (avant || '') + html.slice(i, k + 1) + '\nreturn ' + nom + ';')(TextEncoder);
}

const args = process.argv.slice(2);
if (args[0] === '--choix') {
    const [, json, lot, phase, v18, graines, tours = '1', emu = EMU] = args;
    const html = fs.readFileSync(emu, 'utf8');
    // replChoisir et ce qu'il emploie (constantes et PGCD de l'émulateur), recopiés tels quels
    const decl = ['REPL_RECENTS', 'replMemoNeuve', 'replPgcd', 'replAge'].map(n => new RegExp('const ' + n + ' = [^\\n]*\\n').exec(html)[0]).join('');
    const choisir = fonctionEmu(html, 'replChoisir', decl), memoNeuve = new Function(decl + 'return replMemoNeuve;')();
    const R = JSON.parse(fs.readFileSync(json, 'utf8')).repliques, k = +lot, memo = memoNeuve(), gs = graines.split(',').map(Number);
    let n = 0;
    for (let t = 0; t < +tours; t++) for (const g of gs) {
        const e = choisir(R.lots[k].l, k, +phase, +v18, g, memo);
        console.log(String(++n).padStart(4) + '. ' + (e >= 0 ? R.lots[k].l[e][0] : '…'));
    }
    process.exit(0);
}
const [src, json, dst, emu = EMU] = args;
if (!dst) { console.error('node tools/repliques_paquet.js monde.pak repliques.json sortie.pak [emulateur.html]'); process.exit(1); }
const html = fs.readFileSync(emu, 'utf8'), replPaquet = fonctionEmu(html, 'replPaquet');
const J = JSON.parse(fs.readFileSync(json, 'utf8'));
const rp = replPaquet(J.repliques, n => { const f = J.flags.indexOf(n); return f >= 0 ? f : 65535; });
const buf = fs.readFileSync(src);
if (buf.toString('latin1', 0, 4) !== 'ECHO') throw new Error('pas un paquet monde (ECHO)');
const section = (tag, data) => {
    const pad = (4 - data.length % 4) % 4, h = Buffer.alloc(8);
    h.write(tag, 0, 'latin1'); h.writeUInt32LE(data.length + pad, 4);
    return Buffer.concat([h, Buffer.from(data), Buffer.alloc(pad)]);
};
let fin = -1;
for (let o = 8; o + 8 <= buf.length; o += 8 + buf.readUInt32LE(o + 4)) {
    const t = buf.toString('latin1', o, o + 4);
    if (t === 'REPZ' || t === 'REPT') throw new Error('le paquet a déjà ' + t);
    if (t === 'END ') { fin = o; break; }
}
if (fin < 0) throw new Error('pas de section END');
const out = fs.openSync(dst, 'w');
for (let o = 0; o < fin; o += 64 << 20) fs.writeSync(out, buf, o, Math.min(64 << 20, fin - o));
fs.writeSync(out, section('REPZ', rp.z)); fs.writeSync(out, section('REPT', rp.t)); fs.writeSync(out, buf, fin, buf.length - fin);
fs.closeSync(out);
const G = J.repliques.grille;
console.log(`${dst} : REPZ ${rp.z.length} octets (grille ${G ? G.n + ' × ' + G.n : 'aucune'}), REPT ${(rp.t.length / 1024).toFixed(1)} Ko, ` +
    `plus gros lot ${new DataView(rp.z.buffer).getUint16(14, true)} octets, ${J.repliques.lots.length} lots`);
