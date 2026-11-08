/*
 * Le Chœur des Cendres - répliques des PNJ par zone (03/11/2026, demande de l'utilisateur : « les PNJ répètent tous les
 * mêmes phrases »).
 *
 * Un personnage sans répliques à lui en dit une du lot de son métier dans un intérieur, sinon de la zone où est Sera :
 * grille des zones de la planète (quartiers de Lucerne, rempart, villages, camps, régions, circuit ; un lot par bloc),
 * lot de chaque autre carte ; dans un intérieur, la zone de sa porte. Atelier : js/repliques.js (TilesetGen.Repliques),
 * émulateur : replChoisir, replZone, replPhase, replPaquet ; format des sections REPZ, REPT et REPX dans pak.h.
 *
 * Mémoire : l'index et la grille (REPZ) restent dans le paquet (projeté depuis la flash sur l'ESP32 : rien en PSRAM) ;
 * les textes (REPT, après CDAT : sur la carte SD de la montre) sont lus un lot à la fois dans un tampon réservé au
 * démarrage à la taille du plus gros lot. Dehors, le lot de la zone est lu quand Sera change de zone (repl_maj) ; dans un
 * intérieur, le lot du métier est lu au moment où l'on parle. Rien n'est réservé pendant le jeu.
 *
 * Pas de redites : chaque exemplaire (graine g) parcourt le lot dans un ordre à lui (départ et pas tirés de g, le pas
 * premier avec le nombre de répliques possibles : tout le lot avant de se répéter), en sautant les REPL_RECENTS
 * dernières répliques dites par n'importe qui (deux voisins ne disent pas la même phrase ; toutes récentes : celle dite
 * il y a le plus longtemps) ; REPL_DITS exemplaires gardent
 * leur rang dans leur ordre (le plus anciennement servi cède sa place). Seules les répliques de la phase de l'histoire
 * (drapeaux de REPZ, le plus haut levé l'emporte), sans les 18+ tant que le 18+ est verrouillé. Même calcul que
 * replChoisir de l'émulateur : même réplique dans le même état.
 */
#include "internal.h"
#include <math.h>
#include <string.h>

#define REPL_RECENTS 64   /* puissance de 2 : âge d'une place sur 32 bits sans signe, comme replAge de l'émulateur */
#define REPL_DITS 32

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static struct {
    int ok;
    const uint8_t *ph, *mc, *dir, *grille;   /* REPZ : drapeaux des phases, lots des métiers puis des cartes, répertoire, grille */
    int nl, np, nm, nc, pas, n, defaut;
    uint32_t t_off, t_len, max;              /* REPT : décalage dans le paquet (ou monde.pak), taille ; plus gros lot */
    uint8_t *buf; int lot, nlignes; uint32_t taille;   /* lot dans le tampon (-1 : aucun) */
    int echec;                               /* lot illisible : pas d'autre essai avant une autre zone */
    uint32_t lus, lectures;                  /* octets lus, nombre de lectures (rendu_test) */
} Z ENG_FROID;

typedef struct { uint16_t g; uint8_t c, pris; uint32_t vu; } repl_dit_t;
static struct { uint32_t tour; uint16_t recents[REPL_RECENTS]; repl_dit_t dits[REPL_DITS]; } MEMO ENG_FROID;

/* Octets du paquet : par la plateforme (carte SD, flash), sinon dans le paquet en mémoire */
static int lire(uint32_t off, void *dst, uint32_t len) {
    if (E.cfg.pak_read) return E.cfg.pak_read(off, dst, len);
    if ((size_t)off + len > E.pak_len) return -1;
    memcpy(dst, E.pak + off, len);
    return 0;
}

/* Démarrage (eng_init, après int_load) : index et grille, tampon d'un lot ; sans REPZ, l'ancienne section REPL
   (repl_ligne de interieur.c) */
void repl_init(void) {
    memset(&Z, 0, sizeof Z); Z.lot = -1; Z.echec = -1;
    memset(&MEMO, 0, sizeof MEMO); memset(MEMO.recents, 0xFF, sizeof MEMO.recents);
    uint32_t sz = 0, ts = 0;
    const uint8_t *z = pak_section("REPZ", &sz);
    if (!z || sz < 16 || memcmp(z, "RPZ1", 4)) return;
    const int nl = rd16(z + 4), np = z[6], nm = z[12], nc = z[13], gn = rd16(z + 10);
    const uint32_t mo = 16 + (((uint32_t)np * 2 + 3) & ~3u), dof = mo + (((uint32_t)nm + nc + 3) & ~3u), go = dof + (uint32_t)nl * 32;
    if (!nl || go + (uint32_t)gn * gn > sz) return;
    const uint8_t *x = pak_section("REPX", &ts), *t;   /* paquet léger (tools/paquet_sd.js) : REPT dans monde.pak */
    if (x && ts >= 8) { Z.t_off = rd32(x); Z.t_len = rd32(x + 4); }
    else if ((t = pak_section("REPT", &ts))) { Z.t_off = (uint32_t)(t - E.pak); Z.t_len = ts; }
    else return;
    uint8_t h[4];
    if (Z.t_len < 4 || lire(Z.t_off, h, 4) || memcmp(h, "RPT1", 4)) return;
    Z.nl = nl; Z.np = np; Z.nm = nm; Z.nc = nc; Z.n = gn; Z.pas = rd16(z + 8); Z.defaut = z[7];
    Z.ph = z + 16; Z.mc = z + mo; Z.dir = z + dof; Z.grille = gn ? z + go : NULL;
    for (int k = 0; k < nl; k++) { const uint32_t s = rd32(Z.dir + 32 * k + 4); if (s > Z.max) Z.max = s; }
    if (!Z.max || Z.max > 65535) return;
    Z.buf = E.cfg.alloc(Z.max, 0);
    if (!Z.buf) return;
    Z.ok = 1;
}

/* Lot k dans le tampon (0 : vide ou illisible) */
static int charger(int k) {
    if (k == Z.lot) return 1;
    if (k < 0 || k >= Z.nl) return 0;
    const uint8_t *d = Z.dir + 32 * k;
    const uint32_t off = rd32(d), size = rd32(d + 4);
    if (!size || size > Z.max || 4 + off + size > Z.t_len) return 0;
    if (lire(Z.t_off + 4 + off, Z.buf, size)) { Z.lot = -1; Z.echec = k; return 0; }
    Z.lot = k; Z.nlignes = rd16(d + 8); Z.taille = size; Z.lus += size; Z.lectures++;
    return 1;
}

/* Phase de l'histoire : 0 (prologue), puis k + 1 pour le plus haut drapeau k des phases levé (replPhase de l'émulateur) */
int repl_phase(void) {
    int p = 0;
    for (int k = 0; k < Z.np; k++) {
        const int f = rd16(Z.ph + 2 * k);
        if (f < SC_MAX_FLAGS && (E.flags[f >> 3] >> (f & 7) & 1)) p = k + 1;
    }
    return p;
}

/* Lot de la zone où est Sera (replZone de l'émulateur ; -1 : pas de lots) */
int repl_zone(void) {
    if (!Z.ok) return -1;
    if (E.w.carte) {
        const int c = E.w.carte - 1, v = c < Z.nc ? Z.mc[Z.nm + c] : 255;
        return v == 255 ? Z.defaut : v;
    }
    if (!Z.grille || !Z.pas) return Z.defaut;
    const int fx = (int)floorf(E.in.on ? E.in.rx : E.sera.x), fz = (int)floorf(E.in.on ? E.in.rz : E.sera.z);
    const int i = fx < 0 ? -1 : fx / Z.pas, j = fz < 0 ? -1 : fz / Z.pas;
    const int v = i >= 0 && j >= 0 && i < Z.n && j < Z.n ? Z.grille[j * Z.n + i] : 255;
    return v == 255 ? Z.defaut : v;
}

/* Dehors, le lot de la zone de Sera lu dès qu'elle en change (chaque image : rien à faire tant qu'elle y reste) */
void repl_maj(void) {
    if (!Z.ok || E.in.on) return;
    const int z = repl_zone();
    if (z == Z.lot || z == Z.echec) return;
    Z.echec = -1;
    charger(z);
}

static int pgcd(int a, int b) { while (b) { const int r = a % b; a = b; b = r; } return a; }
/* Âge de la réplique id parmi les récentes (0 : la dernière dite ; -1 : pas récente) */
static int age(uint16_t id) {
    int a = -1;
    for (int i = 0; i < REPL_RECENTS; i++) if (MEMO.recents[i] == id) { const int v = (int)((MEMO.tour - 1u - (uint32_t)i) % REPL_RECENTS); if (a < 0 || v < a) a = v; }
    return a;
}

/* Rang de la réplique que dit l'exemplaire de graine g dans le lot k (dans le tampon) ; -1 : aucune possible */
static int choisir(int k, int phase, int v18, unsigned g) {
    uint8_t elig[255];
    int m = 0;
    const uint8_t *q = Z.buf, *fin = Z.buf + Z.taille;
    for (int i = 0; i < Z.nlignes && i < 255 && q + 2 <= fin; i++, q += 2 + q[0]) {
        const int fl = q[1], a = fl & 7, b = fl >> 3 & 7;
        if (a <= phase && phase <= b && !((fl & 64) && v18)) elig[m++] = (uint8_t)i;
    }
    if (!m) return -1;
    g &= 0xFFFFu;
    const uint32_t h1 = (g * 0x9E3779B1u) >> 16, h2 = ((g ^ 0x5BD1u) * 0x85EBCA6Bu) >> 16;
    const int s = (int)((h1 + (uint32_t)k * 7) % (uint32_t)m);
    int t = m > 1 ? 1 + (int)(h2 % (uint32_t)(m - 1)) : 1;
    while (m > 1 && pgcd(t, m) != 1) t = t % (m - 1) + 1;
    int d = -1;
    for (int i = 0; i < REPL_DITS; i++) if (MEMO.dits[i].vu && MEMO.dits[i].g == g) { d = i; break; }
    const int c = d >= 0 ? MEMO.dits[d].c % m : 0;
    int j = 0, e = 0, jv = 0, ev = 0, av = -1;
    for (; j < m; j++) {
        e = elig[(s + ((c + j) % m) * t) % m];
        const int a = age((uint16_t)(k << 8 | e));
        if (a < 0) break;
        if (a > av) { av = a; jv = j; ev = e; }
    }
    if (j == m) { j = jv; e = ev; }
    if (d < 0) { d = 0; for (int i = 1; i < REPL_DITS; i++) if (MEMO.dits[i].vu < MEMO.dits[d].vu) d = i; }
    MEMO.tour++;
    MEMO.dits[d] = (repl_dit_t){ (uint16_t)g, (uint8_t)((c + j + 1) % m), 1, MEMO.tour };
    MEMO.recents[(MEMO.tour - 1) % REPL_RECENTS] = (uint16_t)(k << 8 | e);
    return e;
}

/* Réplique générique de l'exemplaire npc (metier : métier + 1 dans l'intérieur où est Sera, 0 dehors), dans out ;
   0 : aucune. Lot du métier, sinon (vide, ou rien de possible à cette phase) celui de la zone ; paquet sans REPZ :
   l'ancienne liste au hasard (REPL) */
int repl_dire(int npc, int metier, char *out, int cap) {
    if (!Z.ok) return repl_ligne(metier ? metier - 1 : REPL_VILLE, out, cap);
    if (npc < 0 || npc >= E.nnpc || cap < 2) return 0;
    const int ph = repl_phase(), v18 = verrou18(), zone = repl_zone();
    const int essais[2] = { metier > 0 && metier <= Z.nm ? Z.mc[metier - 1] : -1, zone };
    for (int a = 0; a < 2; a++) {
        const int k = essais[a];
        if (k < 0 || k >= Z.nl || !charger(k)) continue;
        const int e = choisir(k, ph, v18, E.npcs[npc].graine);
        if (e < 0) continue;
        const uint8_t *q = Z.buf;
        for (int i = 0; i < e; i++) q += 2 + q[0];
        const int n = q[0] < cap - 1 ? q[0] : cap - 1;
        memcpy(out, q + 2, (size_t)n); out[n] = 0;
        return 1;
    }
    return 0;
}

/* État pour les essais (rendu_test) : nom du lot k (REPZ), lot dans le tampon, sa taille, octets lus, lectures, tampon */
const char *repl_lot_nom(int k) { return Z.ok && k >= 0 && k < Z.nl ? (const char *)Z.dir + 32 * k + 12 : "?"; }
int repl_phase_drapeau(int k) { return Z.ok && k >= 0 && k < Z.np ? rd16(Z.ph + 2 * k) : -1; }   /* -1 : pas de phase k */
int repl_lot_lignes(int k) { return Z.ok && k >= 0 && k < Z.nl ? rd16(Z.dir + 32 * k + 8) : 0; }
void repl_etat(int *lots, int *lot, uint32_t *tampon, uint32_t *lus, uint32_t *lectures) {
    *lots = Z.ok ? Z.nl : 0; *lot = Z.lot; *tampon = Z.ok ? Z.max : 0; *lus = Z.lus; *lectures = Z.lectures;
}
