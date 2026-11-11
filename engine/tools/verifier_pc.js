// Contrôle sans fenêtre de la version PC (build/choeur_pc*.html, tools/build_web.js --pc) : Chrome muet sans fenêtre (profil
// réutilisé dans build/chrome_essai, sur D:), la page chargée, puis un parcours au clavier (écran titre, nouvelle partie,
// déplacements, menu) avec une capture à chaque étape, les touches de la fenêtre Touches changées puis gardées, et les
// erreurs de la page.
//
//   node tools/verifier_pc.js [build/choeur_pc_enfant.html] [dossier des captures] [largeur hauteur]
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe', PORT = 9363;
const page = path.resolve(process.argv[2] || path.join(__dirname, '..', 'build', 'choeur_pc_enfant.html'));
const sortie = path.resolve(process.argv[3] || path.join(__dirname, '..', 'build', 'verif_pc'));
const L = +process.argv[4] || 1280, H = +process.argv[5] || 720;
fs.mkdirSync(sortie, { recursive: true });
const profil = path.join(__dirname, '..', 'build', 'chrome_essai');
const attendre = ms => new Promise(r => setTimeout(r, ms));

(async () => {
    const ch = spawn(CHROME, ['--headless=new', '--mute-audio', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
        `--remote-debugging-port=${PORT}`, `--user-data-dir=${profil}`, `--window-size=${L},${H}`, 'about:blank'], { stdio: 'ignore' });
    let cibles;
    for (let k = 0; k < 50; k++) { try { cibles = await (await fetch(`http://127.0.0.1:${PORT}/json`)).json(); break; } catch (e) { await attendre(200); } }
    const cible = cibles.find(c => c.type === 'page');
    const ws = new WebSocket(cible.webSocketDebuggerUrl);
    await new Promise(r => ws.addEventListener('open', r));
    let id = 0;
    const attentes = new Map(), erreurs = [];
    ws.addEventListener('message', m => {
        const d = JSON.parse(m.data);
        if (d.id && attentes.has(d.id)) { attentes.get(d.id)(d); attentes.delete(d.id); }
        if (d.method === 'Runtime.exceptionThrown') erreurs.push(d.params.exceptionDetails.exception ? d.params.exceptionDetails.exception.description : d.params.exceptionDetails.text);
        if (d.method === 'Runtime.consoleAPICalled' && d.params.type === 'error') erreurs.push(d.params.args.map(a => a.value || a.description).join(' '));
    });
    const cmd = (method, params = {}) => new Promise(r => { const i = ++id; attentes.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
    const evaluer = async e => (await cmd('Runtime.evaluate', { expression: e, returnByValue: true })).result.result.value;
    await cmd('Runtime.enable'); await cmd('Page.enable');
    await cmd('Emulation.setDeviceMetricsOverride', { width: L, height: H, deviceScaleFactor: 1, mobile: false });
    await cmd('Page.navigate', { url: 'file:///' + page.replace(/\\/g, '/') });   // une première fois : le stockage de la page vidé (partie neuve, touches d'origine, aide ouverte)
    await attendre(1500);
    await cmd('Runtime.evaluate', { expression: "localStorage.clear()" });
    await cmd('Page.reload', { ignoreCache: true });
    const t0 = Date.now();
    for (;;) {
        await attendre(1000);
        const e = await evaluer("document.getElementById('etat') ? document.getElementById('etat').textContent : 'pas de page'");
        if (e === '' || Date.now() - t0 > 180000) { console.log('chargé en ' + Math.round((Date.now() - t0) / 1000) + ' s' + (e ? ' (état : ' + e + ')' : '')); break; }
    }
    let n = 0;
    const capture = async nom => {
        const r = await cmd('Page.captureScreenshot', { format: 'png' });
        fs.writeFileSync(path.join(sortie, String(++n).padStart(2, '0') + '_' + nom + '.png'), Buffer.from(r.result.data, 'base64'));
    };
    const touche = async (code, key, ms = 120) => {
        await cmd('Input.dispatchKeyEvent', { type: 'keyDown', code, key, windowsVirtualKeyCode: 0 });
        await attendre(ms);
        await cmd('Input.dispatchKeyEvent', { type: 'keyUp', code, key });
        await attendre(150);
    };
    await capture('aide');   // ouverte au premier lancement
    console.log('canevas : ' + await evaluer("(() => { const c = document.getElementById('ecran'); return c.width + ' x ' + c.height + ', affiché ' + c.style.width + ' x ' + c.style.height; })()"));
    await touche('Escape', 'Escape');
    await attendre(2000); await capture('titre');
    await touche('Enter', 'Enter'); await attendre(2500); await capture('apres_entree');
    for (let k = 0; k < 12; k++) { await touche('Enter', 'Enter'); await attendre(700); }
    await capture('prologue');
    if (process.env.MESURE) {   // temps par image à chaque définition, avec et sans flou (MESURE=1)
        await touche('KeyW', 'z', 1500);
        for (const flou of [true, false]) for (const def of ['854x480', '960x540', '1280x720', '1920x1080', 'auto']) {
            await evaluer(`(() => { const s = document.getElementById('rdef'), f = document.getElementById('rflou'); s.value = '${def}'; s.dispatchEvent(new Event('change')); f.checked = ${flou}; f.dispatchEvent(new Event('change')); })()`);
            await attendre(3500);
            const m = await evaluer('window.__ips');
            console.log(`${def} flou ${flou ? 'oui' : 'non'} : ${m ? m.ips.toFixed(1) + ' i/s, ' + m.ms.toFixed(1) + ' ms par image (' + m.w + ' x ' + m.h + ')' : '?'}`);
        }
    }
    await touche('KeyW', 'z', 1500); await capture('avance');
    await touche('KeyE', 'e', 800); await capture('camera');
    await touche('Tab', 'Tab'); await attendre(800); await capture('menu');   // (Menu : Tab, et la touche M du clavier, où qu'elle soit)
    await touche('Escape', 'Escape'); await attendre(800);
    // Fenêtre Touches : « Valider » sur K, gardé
    await evaluer("document.getElementById('btouches').click()");
    await attendre(300); await capture('touches');
    await evaluer("[...document.querySelectorAll('#liste-touches .touche')][10].click()");   // Valider, première case (ligne 6 : 2 cases par ligne)
    await touche('KeyK', 'k'); await attendre(300); await capture('touches_k');
    console.log('touches gardées : ' + await evaluer("localStorage.getItem('echoes-pc-touches') ? JSON.parse(localStorage.getItem('echoes-pc-touches')).t.ok.map(t => t.code).join(', ') : 'rien'"));
    await evaluer("document.getElementById('bfermer').click()");
    console.log(erreurs.length ? 'ERREURS :\n' + erreurs.join('\n') : 'aucune erreur dans la page');
    console.log('captures : ' + sortie);
    ws.close(); ch.kill();
    process.exit(0);
})().catch(e => { console.error(e); process.exit(1); });
