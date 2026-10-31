// Exporte le paquet moteur (data/monde.pak) depuis l'émulateur : Edge sans fenêtre ouvre emulateur.html, piloté par
// son protocole de débogage, appelle window.echoesExportPak() (qui attend que le monde soit placé) et récupère le
// binaire. Pas de délai fixe : une grande carte peut mettre plusieurs secondes à se construire.
//
//   node tools/exporter_paquet.js [chemin/emulateur.html] [sortie.pak]
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const root = path.join(__dirname, '..');
const emu = path.resolve(process.argv[2] || path.join(root, '..', 'emulateur', 'emulateur.html'));
const out = path.resolve(process.argv[3] || path.join(root, 'data', 'monde.pak'));
const edge = 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const port = 9300 + Math.floor(Math.random() * 400);
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'echoes-pak-'));
const url = p => 'file:///' + p.replace(/\\/g, '/').split('/').map(encodeURIComponent).join('/').replace(/^([A-Za-z])%3A/, '$1:');
const sleep = ms => new Promise(r => setTimeout(r, ms));

const browser = spawn(edge, ['--headless=new', '--disable-gpu', '--allow-file-access-from-files', '--remote-debugging-port=' + port,
    '--user-data-dir=' + path.join(work, 'profil'), url(emu) + (process.env.ECHOES_HASH ? '#' + process.env.ECHOES_HASH : '')], { stdio: 'ignore' });   // ECHOES_HASH=carte-essai : la carte d'essai
const fail = msg => { console.error('échec : ' + msg); browser.kill(); process.exit(1); };

(async () => {
    let target = null;
    for (let i = 0; i < 80 && !target; i++) {
        try { target = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find(t => t.type === 'page'); } catch (e) {}
        if (!target) await sleep(250);
    }
    if (!target) fail("Edge ne répond pas");
    const ws = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((ok, ko) => { ws.onopen = ok; ws.onerror = ko; });
    const expr = `(async () => {
        for (let i = 0; i < 600 && !window.echoesExportPak; i++) await new Promise(r => setTimeout(r, 100));
        ${process.env.ECHOES_HASH === 'carte-essai' ? "await window.echoesEval('worldReady'); window.echoesEval('cartes.length < 2 && carteEssai()');" : ''}
        const a = await window.echoesExportPak();
        let s = '';
        for (let i = 0; i < a.length; i += 0x8000) s += String.fromCharCode.apply(null, a.subarray(i, i + 0x8000));
        return btoa(s);
    })()`;
    // La page peut encore naviguer (about:blank -> émulateur) : on réessaie si son contexte disparaît
    let msg, id = 0;
    for (let k = 0; k < 5; k++) {
        const n = ++id;
        ws.send(JSON.stringify({ id: n, method: 'Runtime.evaluate', params: { expression: expr, awaitPromise: true, returnByValue: true, timeout: 600000 } }));
        msg = await new Promise(r => { ws.onmessage = e => { const m = JSON.parse(e.data); if (m.id === n) r(m); }; });
        if (!(msg.error && /context/i.test(msg.error.message))) break;
        await sleep(1000);
    }
    ws.close();
    browser.kill();
    const res = msg.result || {};
    if (res.exceptionDetails || !res.result || typeof res.result.value !== 'string')
        fail("l'émulateur n'a pas exporté de paquet " + JSON.stringify(res.exceptionDetails || msg.error || {}).slice(0, 600));
    const pak = Buffer.from(res.result.value, 'base64');
    fs.mkdirSync(path.dirname(out), { recursive: true });
    fs.writeFileSync(out, pak);
    const parts = [];
    for (let o = 8; o < pak.length;) { const l = pak.readUInt32LE(o + 4); parts.push(pak.toString('latin1', o, o + 4).trim() + ' ' + Math.round(l / 1024) + ' Ko'); o += 8 + l; }
    console.log(path.relative(process.cwd(), out) + ' : ' + Math.round(pak.length / 1024) + ' Ko (' + parts.join(', ') + ')');
    await sleep(300);   // Edge libère son profil
    try { fs.rmSync(work, { recursive: true, force: true }); } catch (e) {}
})();
