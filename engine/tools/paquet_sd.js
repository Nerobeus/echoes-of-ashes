/*
 * Paquet léger de la flash (ESP32) tiré de monde.pak, qui va sur la carte SD (/sd/echoes/monde.pak) :
 *   - toutes les sections qui précèdent CDAT, sauf les pixels des planches et les textures du sol ;
 *   - SPRS devient SPRX : les mêmes entrées (16 octets), suivies du décalage u32 de leurs pixels dans monde.pak ;
 *   - TEX ne garde que les tuiles du décor (avant GROUND_OFF) et les grands décors (après BIG_OFF, qui devient
 *     GROUND_OFF dans HEAD ; la taille de TEX aussi) ; TEXS : décalage des textures du sol dans monde.pak u32, nombre u32 ;
 *   - CHNK et CART restent tels quels (leurs décalages sont ceux de monde.pak), CDAT, INTR et CRTD restent sur la carte ;
 *   - SDPK : taille de monde.pak u32 (le firmware vérifie que la carte porte le bon fichier).
 * Le moteur lit le reste par eng_config_t.pak_read dans monde.pak (format : engine/core/pak.h).
 *
 *   node tools/paquet_sd.js data/monde.pak data/monde_esp32.pak
 */
const fs = require('fs');

function sections(buf) {
    const out = [];
    let o = 8;
    while (o + 8 <= buf.length) {
        const tag = buf.toString('latin1', o, o + 4), len = buf.readUInt32LE(o + 4);
        out.push({ tag, off: o, data: o + 8, len });
        if (tag === 'END ') break;
        o += 8 + len;
    }
    return out;
}

function section(tag, data) {
    const pad = (4 - data.length % 4) % 4, h = Buffer.alloc(8);
    h.write(tag, 0, 'latin1'); h.writeUInt32LE(data.length + pad, 4);
    return Buffer.concat([h, data, Buffer.alloc(pad)]);
}

function paquetLeger(full) {
    if (full.toString('latin1', 0, 4) !== 'ECHO') throw new Error('pas un paquet monde (ECHO)');
    const S = sections(full), get = t => S.find(s => s.tag === t);
    const H = get('HEAD'), T = get('TEX '), P = get('SPRS');
    if (!H || !T) throw new Error('HEAD ou TEX absente');
    const head = Buffer.from(full.subarray(H.data, H.data + H.len));
    const groundOff = head.readUInt32LE(8), bigOff = H.len >= 88 ? head.readUInt32LE(84) : 0;
    const groundEnd = bigOff || T.len;
    const decor = full.subarray(T.data, T.data + groundOff), big = bigOff ? full.subarray(T.data + bigOff, T.data + T.len) : Buffer.alloc(0);
    const tex = Buffer.concat([decor, big]);
    head.writeUInt32LE(tex.length, 20);
    if (bigOff) head.writeUInt32LE(groundOff, 84);
    const texs = Buffer.alloc(8);
    texs.writeUInt32LE(T.data + groundOff, 0); texs.writeUInt32LE((groundEnd - groundOff) / 1024, 4);
    const parts = [full.subarray(0, 8)];
    let sprxN = 0, retire = 0;
    for (const s of S) {
        if (s.tag === 'CDAT' || s.tag === 'INTR' || s.tag === 'CRTD' || s.tag === 'END ') continue;   // CRTD : cartes secondaires, sur la carte SD
        if (s.tag === 'HEAD') { parts.push(section('HEAD', head)); continue; }
        if (s.tag === 'TEX ') { parts.push(section('TEX ', tex), section('TEXS', texs)); retire += T.len - tex.length; continue; }
        if (s.tag === 'SPRS') {
            const ns = full.readUInt32LE(s.data), idx = [Buffer.alloc(4)];
            idx[0].writeUInt32LE(ns, 0);
            let q = s.data + 4;
            for (let i = 0; i < ns; i++) {
                const dirs = full[q + 1], frames = full[q + 2], fw = full.readUInt16LE(q + 4), fh = full.readUInt16LE(q + 6);
                const e = Buffer.alloc(20);
                full.copy(e, 0, q, q + 16); e.writeUInt32LE(q + 16, 16);
                idx.push(e);
                q += 16 + dirs * fh * frames * fw;
            }
            sprxN = ns;
            const sprx = Buffer.concat(idx);
            parts.push(section('SPRX', sprx)); retire += s.len - sprx.length;
            continue;
        }
        if (s.tag === 'IMAG') {   // images (intro, cut-ins) : la table seule, décalages absolus dans monde.pak
            const n = full.readUInt16LE(s.data), tab = Buffer.from(full.subarray(s.data, s.data + 4 + n * 12));
            for (let k = 0; k < n; k++) tab.writeUInt32LE(s.data + tab.readUInt32LE(4 + k * 12 + 8), 4 + k * 12 + 8);
            parts.push(section('IMGX', tab)); retire += s.len - tab.length;
            continue;
        }
        parts.push(full.subarray(s.off, s.data + s.len));
    }
    const sdpk = Buffer.alloc(4); sdpk.writeUInt32LE(full.length, 0);
    parts.push(section('SDPK', sdpk), section('END ', Buffer.alloc(0)));
    if (!P) throw new Error('SPRS absente');
    return { buf: Buffer.concat(parts), planches: sprxN, textures: (groundEnd - groundOff) / 1024, retire };
}

module.exports = { paquetLeger };

if (require.main === module) {
    const [src, dst] = process.argv.slice(2);
    if (!src || !dst) { console.error('node tools/paquet_sd.js monde.pak monde_esp32.pak'); process.exit(1); }
    const r = paquetLeger(fs.readFileSync(src));
    fs.writeFileSync(dst, r.buf);
    console.log(`${dst} : ${(r.buf.length / 1048576).toFixed(2)} Mo (${r.planches} planches et ${r.textures} textures du sol lues sur la carte SD : ${(r.retire / 1048576).toFixed(1)} Mo)`);
}
