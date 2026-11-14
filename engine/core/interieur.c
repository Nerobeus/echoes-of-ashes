/*
 * Intérieurs des bâtiments (comme l'émulateur : interieurEntrer, forcer, alarmeStep…).
 *
 * PORT (paquet) : bâtiments qu'on visite et leurs portes. INTR : un intérieur par niveau (rez-de-chaussée puis étages,
 * à la suite ; on change de niveau par les escaliers), précalculé par l'émulateur
 * (polygones, arbre des pièces, grille de marche), compressé en LZ4 ; lu dans le paquet, ou sur la carte SD
 * (interieurs.bin, eng_config_t.intr_read) qui l'emporte. Un seul intérieur en mémoire : celui où est Sera, chargé à
 * l'entrée. Ses coordonnées sont celles de son plan décalées de IO : loin du monde, que rien ne parcourt pendant ce
 * temps (fenêtre de chunks, personnages, trafic, rencontres).
 *
 * Accès : libre, horaires (ouvert le jour), fermé (crocheter ou pirater selon la serrure, chance selon le niveau de
 * l'équipe), scellé. Une effraction peut déclencher l'alarme : sirène et halo rouge, compte à rebours, puis la police
 * (ou la milice d'un village) : sur la planète, l'arrestation (police.c, police_arreter) si Sera est encore là, sinon elle
 * est recherchée (police_effraction) ; sur une autre carte, une amende, sauf si Sera s'est éloignée.
 */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "internal.h"
#include "audio.h"

static uint32_t rng_i = 0x5BD1E995u;
static float irand(void) { rng_i ^= rng_i << 13; rng_i ^= rng_i >> 17; rng_i ^= rng_i << 5; return (float)(rng_i >> 8) * (1.0f / 16777216.0f); }

static inline uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }
static inline float rdf(const uint8_t *p) { float f; uint32_t u = rd32(p); memcpy(&f, &u, 4); return f; }

/* Normale vers la rue de chaque côté d'un plan (0 : z0, 1 : x1, 2 : z1, 3 : x0) */
static const float SIDE_N[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };

/* ---------- Chargement ---------- */
static const uint8_t *intr_pak;   /* section INTR du paquet (sans carte SD) */
static uint32_t intr_pak_len;
static int intr_v;                 /* format : 1 (INT1, sans étages) ou 2 (INT2) */
static int intr_read(uint32_t off, void *dst, uint32_t len) {
    if (E.cfg.intr_read && E.cfg.intr_size) return off + len <= E.cfg.intr_size ? E.cfg.intr_read(off, dst, len) : -1;
    if (!intr_pak || off + len > intr_pak_len) return -1;
    memcpy(dst, intr_pak + off, len);
    return 0;
}

void int_load(void) {
    world_t *w = &E.w;
    memset(&E.in, 0, sizeof E.in); memset(&E.alarme, 0, sizeof E.alarme);
    E.porte = -1;
    w->nbats = w->nportes = 0; w->intr_n = 0;
    uint32_t sz = 0;
    const uint8_t *p = pak_section("PORT", &sz);
    if (p && sz >= 8) {
        uint32_t nb = rd32(p), np = rd32(p + 4);
        if (8 + nb * 16 + np * 20 <= sz && nb && (w->bats = E.cfg.alloc(sizeof(int_bat_t) * nb, 0)) && (!np || (w->portes = E.cfg.alloc(sizeof(int_porte_t) * np, 0)))) {
            const uint8_t *q = p + 8;
            for (uint32_t i = 0; i < nb; i++, q += 16) {
                int_bat_t *b = &w->bats[i];
                b->cx = rdf(q); b->cz = rdf(q + 4); b->mode = q[8] & 0x1F; b->eclaire = q[8] >> 7; b->noir = (q[8] >> 6) & 1; b->club = (q[8] >> 5) & 1; b->serrure = q[9]; b->niveau = q[10]; b->alarme = q[11];
                b->autorite = q[12]; b->force = 0; b->intr = rd16(q + 14);
            }
            for (uint32_t i = 0; i < nb; i++) { w->bats[i].comm = p[8 + i * 16 + 13]; w->bats[i].nom[0] = 0; }
            for (uint32_t i = 0; i < np; i++, q += 20) {
                int_porte_t *d = &w->portes[i];
                d->x = rdf(q); d->z = rdf(q + 4); d->nx = rdf(q + 8); d->nz = rdf(q + 12); d->bat = rd16(q + 16); d->di = q[18];
                d->w = q[19] ? q[19] / 10.0f : 1.2f;   /* paquet plus ancien : 0 */
                if (d->bat >= nb) d->bat = 0;
            }
            /* Noms des bâtiments, 24 octets chacun (paquet plus ancien : absents) */
            if ((uint32_t)(q - p) + nb * 24 <= sz)
                for (uint32_t i = 0; i < nb; i++, q += 24) { memcpy(w->bats[i].nom, q, 23); w->bats[i].nom[23] = 0; }
            w->nbats = (int)nb; w->nportes = (int)np;
        }
    }
    /* Carte de chaque bâtiment (PCAR : nombre u32, puis u8 chacun ; absente : tous sur la planète) */
    uint32_t pcsz = 0;
    const uint8_t *pc = pak_section("PCAR", &pcsz);
    for (int i = 0; i < w->nbats; i++) w->bats[i].carte = pc && pcsz >= 4 + (uint32_t)w->nbats && rd32(pc) == (uint32_t)w->nbats ? pc[4 + i] : 0;
    /* Commerces (COMM) : métiers u16, soins u16, verre u16, types rachetés u8, n u8, objets vendus u8 × n */
    w->ncomms = 0;
    uint32_t csz = 0;
    const uint8_t *c = pak_section("COMM", &csz);
    if (c && csz >= 4) {
        const uint32_t n = rd32(c); const uint8_t *q = c + 4, *end = c + csz;
        for (uint32_t i = 0; i < n && w->ncomms < COMM_MAX && q + 8 <= end; i++) {
            comm_t *m = &w->comms[w->ncomms++];
            m->qui = rd16(q); m->soins = rd16(q + 2); m->verre = rd16(q + 4); m->rachete = q[6]; const int nv = q[7];
            q += 8;
            if (q + nv > end) { w->ncomms--; break; }
            m->n = (uint8_t)(nv < COMM_VEND ? nv : COMM_VEND);
            memcpy(m->vend, q, m->n); q += nv;
        }
    }
    /* Paliers de l'équipement (EQPL, voir pak.h ; atelier : js/paliers.js) : niveau des paliers, palier et drapeaux de
       chaque objet du sac, types d'équipement vendus par chaque commerce, gamme de chaque bâtiment ; sans elle, aucune
       boutique ne vend d'équipement et les ennemis n'en lâchent pas */
    w->npal = w->pal_avance = 0;
    memset(w->eq_pal, 0, sizeof w->eq_pal); memset(w->eq_fl, 0, sizeof w->eq_fl);
    for (int i = 0; i < w->ncomms; i++) w->comms[i].eq = 0;
    for (int i = 0; i < w->nbats; i++) w->bats[i].gmin = w->bats[i].gmax = 0;
    const uint8_t *e = pak_section("EQPL", &csz);
    if (e && csz >= 8 && e[0] == 1) {
        const int np = e[1] < PAL_MAX ? e[1] : PAL_MAX - 1;
        uint32_t o = 4 + ((e[1] + 3u) & ~3u);
        if (o + 4 <= csz) {
            w->npal = np; w->pal_avance = e[2];
            for (int k = 0; k < np; k++) w->pal_niv[k + 1] = e[4 + k];
            const uint32_t no = rd32(e + o); o += 4;
            for (uint32_t k = 0; k < no && k < 256 && o + 2 * k + 2 <= csz; k++) {
                w->eq_pal[k] = e[o + 2 * k] <= np ? e[o + 2 * k] : 0; w->eq_fl[k] = e[o + 2 * k + 1];
            }
            o += (2 * no + 3) & ~3u;
            if (o + 4 <= csz) {
                const uint32_t nc = rd32(e + o); o += 4;
                for (uint32_t k = 0; k < nc && o + 4 <= csz; k++, o += 4) if (k < (uint32_t)w->ncomms) w->comms[k].eq = rd32(e + o);
                if (o + 4 <= csz) {
                    const uint32_t nb = rd32(e + o); o += 4;
                    for (uint32_t k = 0; k < nb && o + 2 <= csz; k++, o += 2)
                        if (k < (uint32_t)w->nbats) { w->bats[k].gmin = e[o]; w->bats[k].gmax = e[o + 1]; }
                }
            }
        }
    }
    /* Répliques génériques (REPL) : listes de lignes (longueur u8, texte UTF-8) */
    w->nrepls = 0;
    const uint8_t *r = pak_section("REPL", &csz);
    if (r && csz >= 4) {
        const uint32_t n = rd32(r); const uint8_t *q = r + 4, *end = r + csz;
        for (uint32_t i = 0; i < n && w->nrepls < REPL_MAX && q + 2 <= end; i++) {
            const int nl = rd16(q); q += 2;
            w->repl[w->nrepls] = q; w->nrepl[w->nrepls] = (uint16_t)nl;
            for (int l = 0; l < nl && q < end; l++) q += 1 + q[0];
            if (q > end) break;
            w->nrepls++;
        }
    }
    /* Répertoire des intérieurs : carte SD, sinon section INTR (après CDAT : le paquet doit être entier en mémoire) */
    intr_pak = pak_section("INTR", &intr_pak_len);
    uint8_t h[16];
    if (!w->nbats || intr_read(0, h, 16) || memcmp(h, "INT", 3) || (h[3] != '1' && h[3] != '2')) return;
    intr_v = h[3] - '0';
    const uint32_t n = rd32(h + 4), raw = rd32(h + 8), comp = rd32(h + 12);
    if (!n || n > 65535 || !raw) return;
    w->intr_dir = E.cfg.alloc(n * 12, 0);
    if (!w->intr_dir || intr_read(16, w->intr_dir, n * 12)) { w->intr_n = 0; return; }
    /* Tampons de lecture et de décompression, place de l'intérieur : dans le bloc de la fenêtre de chunks, réservé
       ensuite par eng_init à au moins intr_need octets (int_fenetre) */
    w->intr_cap = 2 * raw + 256;
    w->intr_need = ((comp + 16 + 15) & ~15u) + ((raw + 16 + 15) & ~15u) + w->intr_cap;   /* le plus gros (int_besoin : chacun) */
    w->intr_n = (int)n; w->intr_raw_max = raw; w->intr_comp_max = comp;
}

/* Le bloc de la fenêtre de chunks pour l'intérieur : ses places sont vidées (le monde ne bouge pas pendant la visite ;
   world_stream les recharge ensuite) */
static void int_fenetre(void) {
    world_t *w = &E.w;
    if (w->fen_int) return;
    world_vider_fenetre();
    w->fen_int = 1;
}
/* Place que demande l'intérieur k (données lues, décompressées, puis développées : deux fois les décompressées) ; le
   bloc est taillé pour lui à chaque lecture (int_parse), plus pour le plus gros de tous */
static uint32_t int_besoin(int k) {
    const uint8_t *d = E.w.intr_dir + k * 12;
    const uint32_t clen = rd32(d + 4), rlen = rd32(d + 8);
    return ((clen + 16 + 15) & ~15u) + ((rlen + 16 + 15) & ~15u) + 2 * rlen + 256;
}
int int_trop_grand(int k) { const world_t *w = &E.w; return k >= 0 && k < w->intr_n && w->fen_mem && int_besoin(k) > w->fen_cap; }

/* Intérieur k du répertoire : décompressé et rangé dans E.in (polygones au format pak_poly_t, décalés de IO) */
static int int_parse(int k) {
    world_t *w = &E.w;
    if (k < 0 || k >= w->intr_n || !w->fen_mem || int_besoin(k) > w->fen_cap) return 0;
    int_fenetre();
    const uint8_t *d = w->intr_dir + k * 12;
    const uint32_t off = rd32(d), clen = rd32(d + 4), rlen = rd32(d + 8);
    w->intr_comp = w->fen_mem; w->intr_raw = w->intr_comp + ((clen + 16 + 15) & ~15u);
    w->intr_mem = w->intr_raw + ((rlen + 16 + 15) & ~15u); w->intr_cap = w->fen_cap - (uint32_t)(w->intr_mem - w->fen_mem);
    if (clen > w->intr_comp_max || rlen > w->intr_raw_max || intr_read(off, w->intr_comp, clen)) return 0;
    const uint32_t hl = intr_v >= 2 ? 40 : 36;
    if (lz4_decode(w->intr_comp, (int)clen, w->intr_raw, (int)rlen) != (int)rlen || rlen < hl) return 0;
    const uint8_t *p = w->intr_raw, *end = p + rlen;
    interieur_t *I = &E.in;
    I->w = rdf(p); I->d = rdf(p + 4); I->res = rdf(p + 8); I->h = rdf(p + 12); I->f = rdf(p + 16);
    I->gw = rd16(p + 20); I->gh = rd16(p + 22); I->npoly = rd16(p + 24); I->nsol = rd16(p + 26);
    I->nobj = rd16(p + 28); I->nnode = rd16(p + 30); I->nsort = rd16(p + 32); I->npnj = rd16(p + 34);
    I->etage = hl > 36 ? rd16(p + 36) : 0;
    const int nesc = hl > 36 ? rd16(p + 38) : 0;
    p += hl;
    uint8_t *m = w->intr_mem, *mend = m + w->intr_cap;
    I->poly_off = (uint32_t *)m; m += I->npoly * 4;
    I->poly_base = m;
    for (int i = 0; i < I->npoly; i++) {
        if (p + 16 > end) return 0;
        const int nv = p[3];
        if (p + 16 + nv * 12 > end || m + 16 + nv * 24 > mend) return 0;
        I->poly_off[i] = (uint32_t)(m - I->poly_base);
        pak_poly_t *pp = (pak_poly_t *)m;
        pp->tile = rd16(p); pp->flags = p[2]; pp->nv = (uint8_t)nv;
        for (int c = 0; c < 3; c++) pp->n[c] = rdf(p + 4 + c * 4);
        p += 16;
        for (int v = 0; v < nv; v++, p += 12) {
            pak_vert_t *q = &pp->v[v];
            q->x = IO + rds16(p) / 256.0f; q->y = rds16(p + 2) / 256.0f; q->z = IO + rds16(p + 4) / 256.0f;
            q->u = rds16(p + 6) / 4.0f; q->v = rds16(p + 8) / 4.0f; q->l = p[10] / 16.0f;
        }
        m += 16 + nv * 24;
    }
    m = (uint8_t *)(((uintptr_t)m + 3) & ~(uintptr_t)3);
    const size_t need = I->nobj * sizeof(int_obj_t) + I->nnode * sizeof(int_node_t) + I->nsort * sizeof(int_sortie_t) + (size_t)I->gw * I->gh + I->npoly + 16;
    if (m + need > mend || p + I->nobj * 16 + I->nnode * 12 + I->nsort * 12 + I->npnj * 8 + nesc * 16 + (size_t)I->gw * I->gh > end) return 0;
    I->obj = (int_obj_t *)m; m += I->nobj * sizeof(int_obj_t);
    for (int i = 0; i < I->nobj; i++, p += 16) {
        int_obj_t *o = &I->obj[i];
        o->x0 = rds16(p) / 256.0f; o->z0 = rds16(p + 2) / 256.0f; o->x1 = rds16(p + 4) / 256.0f; o->z1 = rds16(p + 6) / 256.0f;
        o->p0 = rd16(p + 8); o->np = rd16(p + 10); o->mesh = rd16(p + 12) & 1; o->mur = rd16(p + 14);   /* (mur : 0 dans un paquet d'avant le 03/11/2026) */
        if (o->p0 + o->np > I->npoly) return 0;
    }
    I->node = (int_node_t *)m; m += I->nnode * sizeof(int_node_t);
    for (int i = 0; i < I->nnode; i++, p += 12) {
        int_node_t *nd = &I->node[i];
        nd->kind = p[0]; nd->c = rds16(p + 2) / 256.0f; nd->lo = rd16(p + 4); nd->hi = rd16(p + 6); nd->first = rd16(p + 8); nd->count = rd16(p + 10);
        if (nd->first + nd->count > I->nobj || (nd->kind && (nd->lo >= I->nnode || nd->hi >= I->nnode))) return 0;
    }
    for (int i = 0; i < I->nobj; i++) if (I->obj[i].mur > I->nobj) I->obj[i].mur = 0;
    I->sort = (int_sortie_t *)m; m += I->nsort * sizeof(int_sortie_t);
    for (int i = 0; i < I->nsort; i++, p += 12) {
        int_sortie_t *s = &I->sort[i];
        s->x0 = rds16(p) / 256.0f; s->z0 = rds16(p + 2) / 256.0f; s->x1 = rds16(p + 4) / 256.0f; s->z1 = rds16(p + 6) / 256.0f;
        s->side = p[8] & 3; s->porte = p[9];
    }
    /* Personnages à métier (8 octets : place x, z i16 1/256, personnage u8, regard u8 en 1/256 de tour, drapeaux u8 : 1
       qui danse (danseuse, client sur la piste), 2 nageur, 4 scène de pole dance (barre), 8 serveuse (service, 03/11/2026),
       bit 7 : neuvième bit du personnage (au-delà de 255 : MAX_PERSOS), métier u8) ; INT_PNJ_MAX au plus (8 avant le
       03/11/2026) */
    const int np = I->npnj;
    I->npnj = 0;
    for (int i = 0; i < np; i++, p += 8) {
        const int pk = p[4] | (p[6] >> 7) << 8;
        if (I->npnj < INT_PNJ_MAX && pk < E.w.npersos && !E.w.persos[pk].masque && !((p[6] & 2) && !tenue_vue(tenue_nage(pk)))   /* 18+ verrouillé : ni masqué, ni nageur en tenue 18+ */
            && !(E.w.persos[pk].membre > 0 && in_band(E.w.persos[pk].membre))) {   /* membre recruté : dans la bande (interieurPnj de l'émulateur) */
            int_pnj_t *q = &I->pnj[I->npnj++];
            q->x = rds16(p) / 256.0f; q->z = rds16(p + 2) / 256.0f; q->perso = pk; q->dir = p[5] * (2 * PI_F / 256);
            q->danse = p[6] & 1; q->nage = (p[6] >> 1) & 1; q->barre = (p[6] >> 2) & 1; q->service = (p[6] >> 3) & 1; q->metier = p[7];
        }
    }
    /* Escaliers (16 octets : zone x0, z0, x1, z1 i16 1/256, vers i8, regard u8 1/256 de tour, arrivée x, z i16 1/256) */
    I->nesc = 0;
    for (int i = 0; i < nesc; i++, p += 16)
        if (I->nesc < INT_ESC_MAX) {
            int_esc_t *e = &I->esc[I->nesc++];
            e->x0 = rds16(p) / 256.0f; e->z0 = rds16(p + 2) / 256.0f; e->x1 = rds16(p + 4) / 256.0f; e->z1 = rds16(p + 6) / 256.0f;
            e->vers = (int8_t)p[8]; e->dir = p[9] * (2 * PI_F / 256); e->x = rds16(p + 10) / 256.0f; e->z = rds16(p + 12) / 256.0f;
        }
    I->grid = m; memcpy(m, p, (size_t)I->gw * I->gh); m += (size_t)I->gw * I->gh;
    p += (size_t)I->gw * I->gh;
    /* Cellules (pièces cellule, cellules : la prison, police.c), après la grille depuis le 01/11/2026 : nombre u16, puis
       x0, z0, x1, z1 i16 1/256 chacune ; un paquet plus ancien n'en a pas (int_cellule devine alors la pièce) */
    I->ncel = I->ncof = 0;
    if (p + 2 <= end) {
        const int nc = rd16(p);
        if (nc && p + 10 <= end) { for (int c = 0; c < 4; c++) I->cel[c] = rds16(p + 2 + c * 2) / 256.0f; I->ncel = 1; }
        p += 2 + (size_t)nc * 8;
    }
    /* Coffre à butin (coffres.c ; choisi par l'émulateur, coffreDe), après les cellules : nombre u16 (0 ou 1), puis le point
       d'action x, z i16 1/256, sorte u8 (0 coffre, 1 caisse, 2 armoire), 0 u8 */
    if (p + 2 <= end && rd16(p) && p + 8 <= end) { I->cof[0] = rds16(p + 2) / 256.0f; I->cof[1] = rds16(p + 4) / 256.0f; I->cof_sorte = p[6] < 3 ? p[6] : 0; I->ncof = 1; }
    if (p + 2 <= end) p += 2 + (rd16(p) ? 6 : 0);
    I->fade = m; memset(m, 0, I->npoly); m += I->npoly;
    /* Sols surélevés (03/11/2026, après le coffre ; un paquet plus ancien n'en a pas : tout à 0) : nombre u16, puis 16 octets
       chacun : x0, z0, x1, z1 i16 1/256, hauteurs y0, y1 i16 1/256, axe u8 (0 plat, 1 +x, 2 +z, 3 -x, 4 -z), 0 u8, 0 u16 */
    I->nhaut = 0; I->sy_ok = 0;
    m = (uint8_t *)(((uintptr_t)m + 3) & ~(uintptr_t)3);
    I->haut = (int_haut_t *)m;
    if (p + 2 <= end) {
        const int nh = rd16(p);
        p += 2;
        for (int i = 0; i < nh && p + 16 <= end && (uint8_t *)(I->haut + I->nhaut + 1) <= mend; i++, p += 16) {
            int_haut_t *h = &I->haut[I->nhaut++];
            h->x0 = rds16(p) / 256.0f; h->z0 = rds16(p + 2) / 256.0f; h->x1 = rds16(p + 4) / 256.0f; h->z1 = rds16(p + 6) / 256.0f;
            h->y0 = rds16(p + 8) / 256.0f; h->y1 = rds16(p + 10) / 256.0f; h->axe = p[12] <= 4 ? p[12] : 0;
        }
    }
    m = (uint8_t *)(((uintptr_t)(I->haut + I->nhaut) + 3) & ~(uintptr_t)3);
    I->libre = m; I->nlibre = m < mend ? (uint32_t)(mend - m) : 0;
    I->cur = k; I->zone = 1;
    return I->nnode > 0 && (I->nsort > 0 || I->etage > 0);
}

/* Personnages à métier du niveau chargé : des exemplaires de plus le temps de la visite (immobiles ; on leur parle) ; graine
   du nom : 2048 + (bâtiment visité × 4 + étage) × 8 + place, au-delà de la 8e place (foule d'un club, 03/11/2026) 30000 +
   bâtiment × 12 + place - 8 (interieurPnj de l'émulateur) */
static float serv_bloque[INT_PNJ_MAX], serv_attente[INT_PNJ_MAX]; static int16_t serv_vers[INT_PNJ_MAX]; static uint8_t serv_chez[INT_PNJ_MAX];
static void int_pnj(void) {
    interieur_t *I = &E.in;
    if (I->npcn) { E.nnpc = I->npc0; E.talk = -2; }
    I->npc0 = E.nnpc; I->npcn = 0;
    for (int i = 0; i < I->npnj && E.nnpc < E.npc_cap; i++) {
        npc_t *pn = &E.npcs[E.nnpc++];
        memset(pn, 0, sizeof *pn);
        pn->x = pn->hx = pn->tx = IO + I->pnj[i].x; pn->z = pn->hz = pn->tz = IO + I->pnj[i].z;
        pn->dir = I->pnj[i].dir; pn->perso = (uint16_t)I->pnj[i].perso; pn->wait = 1e9f;
        pn->graine = (uint16_t)(i < 8 ? 2048 + (I->bat * 4 + I->etage) * 8 + i : 30000 + I->bat * 12 + i - 8);   /* nom tiré : comme interieurPnj de l'émulateur */
        serv_bloque[i] = 0; serv_attente[i] = 2 + irand() * 4; serv_vers[i] = -1; serv_chez[i] = 1;
        pn->var = (uint8_t)pvar_choix(pn->perso, pn->graine);
        I->npcn++;
    }
}

/* Escaliers : Sera entre dans la zone d'un escalier (grille 3 : il monte, 4 : la trémie, on descend) ; au niveau voisin
   (intérieur suivant ou précédent de INTR), à l'arrivée de cet escalier (interieurEscalier de l'émulateur) */
static void int_escalier(void) {
    interieur_t *I = &E.in;
    const int i = (int)floorf((E.sera.x - IO + 2) / I->res), j = (int)floorf((E.sera.z - IO + 2) / I->res);
    const int v = i >= 0 && j >= 0 && i < I->gw && j < I->gh ? I->grid[j * I->gw + i] : 0, avant = I->zone;
    I->zone = v;
    if (v < 3 || v > 4 || v == avant) return;
    const int vers = v == 3 ? 1 : -1;
    const float lx = E.sera.x - IO, lz = E.sera.z - IO;
    const int_esc_t *e = NULL;
    for (int k = 0; k < I->nesc; k++) {
        const int_esc_t *q = &I->esc[k];
        if (q->vers != vers) continue;
        if (!e || (lx >= q->x0 - 0.3f && lx <= q->x1 + 0.3f && lz >= q->z0 - 0.3f && lz <= q->z1 + 0.3f)) e = q;
    }
    if (!e) return;
    const int_esc_t es = *e;   /* E.in est réécrit par int_parse */
    const int cible = I->cur + vers, etage = I->etage;
    if (!int_parse(cible) || I->etage != etage + vers) {   /* niveau illisible : on reste (ou, faute de mieux, dans la rue) */
        if (int_parse(cible - vers)) { I->zone = v; return; }
        E.sera.x = I->rx; E.sera.z = I->rz; E.yaw = I->ryaw; int_reset(); return;
    }
    int_pnj();
    E.sera.x = IO + es.x; E.sera.z = IO + es.z; E.sera.dir = es.dir; I->zone = 1;
    E.has_dest = 0; E.talk = -2;
    son_jouer(SON_PAS_2, 0.35f, 0.9f);   /* une marche */
}

/* ---------- Accès ---------- */
static int est_jour(void) { const float h = E.cycle == 1 ? 0.5f : E.cycle == 2 ? 0.0f : E.heure; return h > 0.29f && h < 0.83f; }
static int competence(void) { const int c = 1 + E.level / 20; return c > 5 ? 5 : c; }
static float chance_forcer(int niv) { const float c = 0.5f + 0.15f * (competence() - (niv ? niv : 1)); return c < 0.05f ? 0.05f : c > 0.95f ? 0.95f : c; }
static int porte_ouverte(const int_bat_t *b) { return b->mode == ACC_LIBRE || b->force || (b->mode == ACC_HORAIRES && est_jour()); }
int int_porte_ouverte(int porte) { return porte >= 0 && porte < E.w.nportes && porte_ouverte(&E.w.bats[E.w.portes[porte].bat]); }

/* Porte devant Sera : dans la bande devant l'ouverture (sa largeur, 0,4 de plus de chaque côté), de la façade à 1,6
   case d'elle (le point de la porte est à 0,6 de la façade) ; la plus proche, -1 : aucune (porteDevant de l'émulateur) */
static int porte_devant(void) {
    float bd = 1e9f;
    int best = -1;
    for (int k = 0; k < E.w.nportes; k++) {
        const int_porte_t *d = &E.w.portes[k];
        if (E.w.bats[d->bat].carte != E.w.carte) continue;   /* les portes de sa carte seulement (PCAR) */
        const float dx = E.sera.x - d->x, dz = E.sera.z - d->z;
        if (fabsf(dx) > 3 || fabsf(dz) > 3) continue;
        const float prof = dx * d->nx + dz * d->nz, lat = fabsf(dz * d->nx - dx * d->nz);
        if (prof < -0.7f || prof > 1 || lat > d->w / 2 + 0.4f) continue;
        const float l = hypotf(dx, dz);
        if (l < bd) { bd = l; best = k; }
    }
    return best;
}

/* Entrée par la porte k : Sera dans l'intérieur, devant la sortie de cette porte, tournée vers le fond */
static int entrer_dedans(int k) {
    const int_porte_t *d = &E.w.portes[k];
    const int_bat_t *b = &E.w.bats[d->bat];
    if (!int_parse(b->intr)) { hud_toast(int_trop_grand(b->intr) ? "Intérieur trop grand pour la mémoire" : E.w.intr_n ? "Intérieur illisible" : "Intérieurs absents : echoes/interieurs.bin (carte SD)"); return 0; }
    interieur_t *I = &E.in;
    const int_sortie_t *s = &I->sort[0];
    for (int i = 0; i < I->nsort; i++) if (I->sort[i].porte == d->di) { s = &I->sort[i]; break; }
    I->bat = d->bat; I->rx = E.sera.x; I->rz = E.sera.z; I->ryaw = E.yaw;
    const float *n = SIDE_N[s->side], e = 0.9f * (I->f > 0 ? I->f : 1);
    E.sera.x = IO + (s->x0 + s->x1) / 2 - n[0] * e; E.sera.z = IO + (s->z0 + s->z1) / 2 - n[1] * e;
    E.sera.dir = atan2f(-n[0], -n[1]); E.yaw = E.sera.dir;   /* caméra côté entrée, regard vers l'intérieur */
    E.has_dest = 0; E.talk = -2; E.porte = -1; E.sera_y_ok = 0;
    I->on = 1; I->npcn = 0;
    /* Éteint : fermé à clé, ou à horaires la nuit (on est entré de force), sauf un type toujours éclairé */
    I->sombre = b->noir || (!b->eclaire && !(b->mode == ACC_LIBRE || (b->mode == ACC_HORAIRES && est_jour())));   /* noir : cinéma */
    I->club = b->club && !I->sombre;   /* boîte de nuit ouverte : sa lumière, ses éclairs, sa musique (club_maj de game.c) */
    int_pnj();
    son_jouer(SON_PORTE, 0.5f, 1.05f);
    return 1;
}

/* Point de sortie devant la porte d (03/11/2026, demande de l'utilisateur : « en sortant d'un bâtiment, Sera se retrouve
   souvent coincée dans un mur ») : le premier point libre pour son corps (sera_libre : marges MARGE_MUR et MARGE_DECOR
   comprises) devant l'ouverture, de SORTIE_A0 à SORTIE_A1 cases le long de sa normale, au milieu puis décalé de côté,
   puis plus près de la porte (ruelle d'une case entre deux façades : la bande libre n'a que 0,46 case) ; sinon la place
   libre la plus proche (sera_placer). Avant : le point à 0,4 case de celui de la porte, contrôlé sans la marge du corps ;
   posée dans la marge d'un mur (le sien ou celui d'en face), elle ne pouvait plus faire un pas (interieurSortir de
   l'émulateur). La fenêtre de chunks doit couvrir la porte (world_stream). */
#define SORTIE_A0 0.4f
#define SORTIE_A1 2.2f
int int_point_sortie(const int_porte_t *d, float *ox, float *oz) {
    static const float LAT[] = { 0, 0.25f, -0.25f, 0.5f, -0.5f, 0.8f, -0.8f };
    const int na = (int)lroundf((SORTIE_A1 - SORTIE_A0) / 0.15f) + 1;
    for (int t = 0; t < na + 3; t++) {
        const float a = t < na ? SORTIE_A0 + 0.15f * t : SORTIE_A0 - 0.15f * (t - na + 1);   /* puis 0,25, 0,1, -0,05 */
        for (int l = 0; l < (int)(sizeof LAT / sizeof LAT[0]); l++) {
            const float x = d->x + d->nx * a - d->nz * LAT[l], z = d->z + d->nz * a + d->nx * LAT[l];
            if (sera_libre(x, z)) { *ox = x; *oz = z; return 1; }
        }
    }
    return 0;
}
/* Sortie par l'ouverture s : devant la porte du même rang dans la rue (sinon là où Sera est entrée) */
static void sortir(const int_sortie_t *s) {
    interieur_t *I = &E.in;
    I->on = 0;
    if (I->npcn) { E.nnpc = I->npc0; I->npcn = 0; E.talk = -2; }   /* ses personnages restent dedans */
    int pk = -1;
    for (int k = 0; k < E.w.nportes; k++) if (E.w.portes[k].bat == I->bat && E.w.portes[k].di == s->porte) { pk = k; break; }
    const int_porte_t *d = pk >= 0 ? &E.w.portes[pk] : NULL;
    const float x0 = d ? d->x + d->nx * SORTIE_A0 : I->rx, z0 = d ? d->z + d->nz * SORTIE_A0 : I->rz;
    world_stream(x0, z0, 1);   /* fenêtre de chunks rechargée (l'intérieur en tenait le bloc ; s n'est plus lu ensuite) */
    float x, z;
    if (d && int_point_sortie(d, &x, &z)) { E.sera.x = x; E.sera.z = z; }
    else if (sera_libre(I->rx, I->rz)) { E.sera.x = I->rx; E.sera.z = I->rz; }
    else sera_placer(x0, z0, 3);
    if (d) E.sera.dir = atan2f(d->nx, d->nz);
    E.yaw = I->ryaw; E.has_dest = 0; E.sera_y_ok = 0;
    son_jouer(SON_PORTE, 0.5f, 0.95f);
}

int int_case(float x, float z) {
    const interieur_t *I = &E.in;
    const int i = (int)floorf((x - IO + 2) / I->res), j = (int)floorf((z - IO + 2) / I->res);
    return i >= 0 && j >= 0 && i < I->gw && j < I->gh ? I->grid[j * I->gw + i] : 0;
}
/* Case de la grille de marche libre (1), seuil d'une sortie (2), zone d'un escalier (3 il monte, 4 trémie) ou eau d'un
   bassin (5) */
int int_libre(float x, float z) {
    const interieur_t *I = &E.in;
    const int i = (int)floorf((x - IO + 2) / I->res), j = (int)floorf((z - IO + 2) / I->res);
    if (!(i >= 0 && j >= 0 && i < I->gw && j < I->gh && I->grid[j * I->gw + i] > 0)) return 0;
    /* On ne traverse pas ses personnages (0,3 m à l'échelle humaine) */
    const float r = 0.3f * (I->f > 0 ? I->f : 1);
    for (int k = I->npc0; k < I->npc0 + I->npcn; k++) if (hypotf(E.npcs[k].x - x, E.npcs[k].z - z) < r) return 0;
    return 1;
}

/* Hauteur du sol en (x, z) dans l'intérieur (03/11/2026) : le dernier sol surélevé qui contient le point (escalier : sa
   pente ; gradins d'un cinéma, estrade d'une danseuse : à plat), sinon 0 (interieurSolY de l'émulateur) */
static const int_haut_t *int_haut(float x, float z) {
    const interieur_t *I = &E.in;
    const float lx = x - IO, lz = z - IO;
    for (int k = I->nhaut - 1; k >= 0; k--) {
        const int_haut_t *h = &I->haut[k];
        if (lx >= h->x0 && lx <= h->x1 && lz >= h->z0 && lz <= h->z1) return h;
    }
    return NULL;
}
float int_sol_y(float x, float z) {
    const int_haut_t *h = int_haut(x, z);
    if (!h) return 0;
    const float lx = x - IO, lz = z - IO, dx = h->x1 - h->x0 > 1e-4f ? h->x1 - h->x0 : 1e-4f, dz = h->z1 - h->z0 > 1e-4f ? h->z1 - h->z0 : 1e-4f;
    const float t = h->axe == 1 ? (lx - h->x0) / dx : h->axe == 2 ? (lz - h->z0) / dz : h->axe == 3 ? (h->x1 - lx) / dx : h->axe == 4 ? (h->z1 - lz) / dz : 0;
    return h->y0 + (h->y1 - h->y0) * t;
}
int int_pente(float x, float z) { const int_haut_t *h = int_haut(x, z); return h && h->axe; }

/* ---------- Alarme ---------- */
/* Amende d'une effraction : (1 + le niveau de la serrure) × les crédits d'un combat au niveau de l'équipe, ALARME_MIN au
   moins (01/11/2026 : 50 + 50 × le niveau avant, moins d'un combat passé le niveau 10 ; alarmeDeclencher de l'émulateur) */
#define ALARME_MIN 50
static void alarme_declencher(const int_bat_t *b) {
    if (E.alarme.on) return;
    const int niv = b->niveau ? b->niveau : 1, cr = credits_combat(0) > ALARME_MIN ? credits_combat(0) : ALARME_MIN;
    E.alarme.on = 1; E.alarme.t = 0; E.alarme.duree = 30 - 3 * niv < 15 ? 15 : (float)(30 - 3 * niv);
    E.alarme.x = b->cx; E.alarme.z = b->cz; E.alarme.amende = (1 + niv) * cr; E.alarme.clan = b->autorite == AUT_CLAN;
    hud_toast("ALARME !");
}
static void forcer(int k) {
    int_bat_t *b = &E.w.bats[E.w.portes[k].bat];
    const float ch = chance_forcer(b->niveau), a = b->alarme / 100.0f;
    const int ok = irand() < ch;
    const float risque = ok ? a * 0.5f : (a + 0.3f > 1 ? 1 : a + 0.3f);
    son_jouer(ok ? SON_VALIDER : SON_ANNULER, 0.5f, 1);
    if (ok) { b->force = 1; if (entrer_dedans(k)) hud_toast(b->serrure ? "Serrure piratée" : "Serrure crochetée"); }
    else hud_toast("La serrure résiste");
    if (irand() < risque) alarme_declencher(b);
}

/* Devant une porte (bouton, touche OK) : entrer si elle est ouverte, sinon proposer de la forcer. Confirmation (01/11/2026) :
   ce qui est en jeu sous le titre, la chance de réussite et le risque d'alarme d'ensemble (forcer : réussite × alarme / 2
   + échec × (alarme + 30 %)) ; ANNULER (RETOUR à la manette) ne fait rien (porteFermee de l'émulateur) */
static int choix_porte = -1;
void int_interagir(void) {
    const int k = E.porte;
    if (k < 0 || k >= E.w.nportes) return;
    const int_bat_t *b = &E.w.bats[E.w.portes[k].bat];
    if (b->mode == ACC_SCELLE) { hud_toast("Porte scellée"); return; }
    if (porte_ouverte(b)) { entrer_dedans(k); return; }
    char opts[2][48], l2[72];
    const float ch = chance_forcer(b->niveau), a = b->alarme / 100.0f, risque = ch * a * 0.5f + (1 - ch) * (a + 0.3f > 1 ? 1 : a + 0.3f);
    snprintf(opts[0], sizeof opts[0], "%s · NIV. %d", b->serrure ? "PIRATER" : "CROCHETER", b->niveau);
    snprintf(opts[1], sizeof opts[1], "ANNULER");
    choix_porte = k;
    hud_choice_porte(opts, 2, b->mode == ACC_HORAIRES ? "FERMÉ LA NUIT" : "PORTE FERMÉE");
    snprintf(l2, sizeof l2, "RÉUSSITE %d %% · ALARME %d %%", (int)lroundf(ch * 100), (int)lroundf(risque * 100));
    hud_choice_texte("Forcer la porte ? L'alarme peut sonner.", l2);
}
void int_choix(int k) { if (k == 0 && choix_porte >= 0) forcer(choix_porte); choix_porte = -1; }

/* Banc d'essai : entrer par la porte k (forcée s'il le faut) */
int int_entrer_porte(int k, int force) {
    if (k < 0 || k >= E.w.nportes) return 0;
    if (force) E.w.bats[E.w.portes[k].bat].force = 1;
    return porte_ouverte(&E.w.bats[E.w.portes[k].bat]) && entrer_dedans(k);
}

/* ---------- Prison (police.c, comme la prison de l'émulateur) ---------- */
/* Les bâtiments qui ont des cellules (catalogue de js/batiments.js, par leur nom : Caserne de l'Ordre, pièce « cellules » ;
   Poste de garde, pièce « cellule », aussi dans les villages : la milice) */
static int a_cellules(const int_bat_t *b) { return strstr(b->nom, "CASERNE") || strstr(b->nom, "POSTE DE GARDE") || strstr(b->nom, "Caserne") || strstr(b->nom, "Poste de garde"); }   /* noms de PORT en capitales */
int int_prison(float x, float z, int clan) {
    int best = -1, bmeme = 0;
    float bd = 1e9f;
    for (int i = 0; i < E.w.nbats; i++) {
        const int_bat_t *b = &E.w.bats[i];
        if (b->carte || !a_cellules(b) || b->intr >= E.w.intr_n || int_trop_grand(b->intr)) continue;
        const int meme = b->autorite != AUT_AUCUNE && (b->autorite == AUT_CLAN) == (clan != 0);   /* la milice d'un village, ou l'Ordre */
        const float d = hypotf(b->cx - x, b->cz - z);
        if (best < 0 || meme > bmeme || (meme == bmeme && d < bd)) { best = i; bd = d; bmeme = meme; }
    }
    return best;
}
/* Feuille de l'arbre des pièces la plus éloignée de (ex, ez), son rectangle dans r (repli sans le bloc des cellules : la
   pièce du fond, la cellule d'un poste de garde) */
static void feuille_loin(int nd, float x0, float z0, float x1, float z1, float ex, float ez, float *bd, float *r) {
    const int_node_t *n = &E.in.node[nd];
    if (!n->kind) {
        const float d = hypotf((x0 + x1) / 2 - ex, (z0 + z1) / 2 - ez);
        if (d > *bd) { *bd = d; r[0] = x0; r[1] = z0; r[2] = x1; r[3] = z1; }
    } else if (n->kind == 1) { feuille_loin(n->lo, x0, z0, n->c, z1, ex, ez, bd, r); feuille_loin(n->hi, n->c, z0, x1, z1, ex, ez, bd, r); }
    else { feuille_loin(n->lo, x0, z0, x1, n->c, ex, ez, bd, r); feuille_loin(n->hi, x0, n->c, x1, z1, ex, ez, bd, r); }
}
int int_cellule(int bat, float *cel) {
    const int_bat_t *B = &E.w.bats[bat];
    interieur_t *I = &E.in;
    int ok = 0;
    for (int k = 0; k < 4 && !ok; k++) {   /* le niveau qui a des cellules (bloc des cellules), sinon le rez-de-chaussée */
        if (!int_parse(B->intr + k) || I->etage != k) break;
        ok = I->ncel;
    }
    if (!ok && !int_parse(B->intr)) return 0;
    /* L'entrée : la sortie de la porte principale (un étage sans sortie : son milieu) */
    const int_sortie_t *s = I->nsort ? &I->sort[0] : NULL;
    for (int i = 0; i < I->nsort; i++) if (I->sort[i].porte == 0) { s = &I->sort[i]; break; }
    const float ex = s ? (s->x0 + s->x1) / 2 : I->w / 2, ez = s ? (s->z0 + s->z1) / 2 : I->d / 2, f = I->f > 0 ? I->f : 1;
    float r[4] = { 0, 0, I->w, I->d }, bd = -1;
    if (ok) memcpy(r, I->cel, sizeof r);
    else feuille_loin(0, 0, 0, I->w, I->d, ex, ez, &bd, r);
    /* Retour dans la rue : devant la porte principale, tournée vers la rue */
    I->bat = bat; I->rx = B->cx; I->rz = B->cz; I->ryaw = E.yaw;
    for (int k = 0; k < E.w.nportes; k++) {
        const int_porte_t *d = &E.w.portes[k];
        if (d->bat == bat && d->di == 0) { I->rx = d->x + d->nx * 0.4f; I->rz = d->z + d->nz * 0.4f; I->ryaw = atan2f(d->nx, d->nz); break; }
    }
    I->on = 1; I->npcn = 0; I->sombre = 0;
    int_pnj();
    /* Sera dans la cellule : la place libre la plus proche de son milieu, dans la pièce ; tournée vers l'entrée, la caméra de
       ce côté */
    const float cx = (r[0] + r[2]) / 2, cz = (r[1] + r[3]) / 2;
    float px = cx, pz = cz;
    for (float rr = 0; rr <= 3 * f; rr += 0.1f * f) {
        int trouve = 0;
        for (int a = 0; a < (rr > 0 ? 16 : 1) && !trouve; a++) {
            const float qx = cx + cosf(a * PI_F / 8) * rr, qz = cz + sinf(a * PI_F / 8) * rr;
            if (qx > r[0] && qx < r[2] && qz > r[1] && qz < r[3] && int_libre(IO + qx, IO + qz)) { px = qx; pz = qz; trouve = 1; }
        }
        if (trouve) break;
    }
    E.sera.x = IO + px; E.sera.z = IO + pz;
    E.sera.dir = atan2f(ex - px, ez - pz); E.yaw = atan2f(px - ex, pz - ez);
    E.has_dest = 0; E.talk = -2; E.porte = -1; E.sera_y_ok = 0; E.nage = 0;
    for (int c = 0; c < 4; c++) cel[c] = IO + r[c];
    cel[4] = IO + ex; cel[5] = IO + ez;
    return 1;
}
/* Un exemplaire de plus dans l'intérieur où est Sera (le garde qui vient ouvrir la cellule) */
int int_pnj_ajouter(int perso, float x, float z, float dir) {
    interieur_t *I = &E.in;
    if (!I->on || perso < 0 || perso >= E.w.npersos || E.nnpc >= E.npc_cap || E.nnpc != I->npc0 + I->npcn) return -1;
    npc_t *pn = &E.npcs[E.nnpc];
    memset(pn, 0, sizeof *pn);
    pn->x = pn->hx = pn->tx = x; pn->z = pn->hz = pn->tz = z; pn->dir = dir; pn->perso = (uint16_t)perso; pn->wait = 1e9f;
    pn->graine = (uint16_t)(2048 + (I->bat * 4 + I->etage) * 8 + 7);
    I->npcn++;
    return E.nnpc++;
}
/* Sortie de l'intérieur par la porte principale (au rez-de-chaussée), dans la rue devant elle */
void int_liberer(void) {
    interieur_t *I = &E.in;
    if (!I->on) return;
    if (I->etage && !int_parse(E.w.bats[I->bat].intr)) { E.sera.x = I->rx; E.sera.z = I->rz; E.yaw = I->ryaw; int_reset(); E.stream_hold = 1; return; }
    const int_sortie_t *s = &I->sort[0];
    for (int i = 0; i < I->nsort; i++) if (I->sort[i].porte == 0) { s = &I->sort[i]; break; }
    sortir(s);
}

/* Clients et habitants d'un intérieur (métiers 8 et 9 de METIERS_ORDRE + 1) : d'une place libre à une autre, 0,8 à 2,5 m
   plus loin (à l'échelle humaine), en ligne droite sur le sol de marche (grille 1 ou 2 : ni escalier ni bassin), puis une
   pause de 3 à 9 s ; le personnel reste à son poste ; arrêtés quand Sera leur parle (interieurPromenade de l'émulateur) */
static int int_sol_libre(float x, float z, int self) {
    const interieur_t *I = &E.in;
    const int i = (int)floorf((x - IO + 2) / I->res), j = (int)floorf((z - IO + 2) / I->res);
    if (!(i >= 0 && j >= 0 && i < I->gw && j < I->gh)) return 0;
    const int g = I->grid[j * I->gw + i];
    if ((g != 1 && g != 2) || int_pente(x, z)) return 0;   /* ni escalier */
    const float r = 0.3f * (I->f > 0 ? I->f : 1);
    if (hypotf(E.sera.x - x, E.sera.z - z) < r) return 0;
    for (int k = I->npc0; k < I->npc0 + I->npcn; k++) if (k != self && hypotf(E.npcs[k].x - x, E.npcs[k].z - z) < r) return 0;
    return 1;
}
static void int_promenade(float dt) {
    interieur_t *I = &E.in;
    const float f = I->f > 0 ? I->f : 1;
    for (int i = 0; i < I->npcn && i < I->npnj; i++) {
        const int m = I->pnj[i].metier, k = I->npc0 + i;
        if ((m != 8 && m != 9) || I->pnj[i].danse || k == E.talk) continue;   /* (qui danse reste sur la piste) */
        npc_t *n = &E.npcs[k];
        if (!n->walking) {
            if (n->wait > 1e8f) n->wait = 2 + irand() * 6;
            n->wait -= dt; n->anim += dt;
            if (n->anim > 0.22f) { n->anim = 0; n->frame++; }
            if (n->wait > 0) continue;
            for (int t = 0; t < 8 && !n->walking; t++) {
                const float a = irand() * 2 * PI_F, r = (0.8f + irand() * 1.7f) * f, tx = n->x + cosf(a) * r, tz = n->z + sinf(a) * r;
                int ok = 1;
                for (float s = 0.25f * f; s <= r && ok; s += 0.25f * f) ok = int_sol_libre(n->x + cosf(a) * s, n->z + sinf(a) * s, k);
                if (ok) { n->tx = tx; n->tz = tz; n->walking = 1; }
            }
            if (!n->walking) n->wait = 1 + irand() * 2;
            continue;
        }
        const float dx = n->tx - n->x, dz = n->tz - n->z, d = hypotf(dx, dz);
        if (d < 0.05f) { n->walking = 0; n->wait = 3 + irand() * 6; continue; }
        float sp = 0.9f * f * dt;
        if (sp > d) sp = d;
        const float nx = n->x + dx / d * sp, nz = n->z + dz / d * sp;
        if (!int_sol_libre(nx, nz, k)) { n->walking = 0; n->wait = 1 + irand(); continue; }
        n->x = nx; n->z = nz; n->dir = atan2f(dx, dz); n->anim += dt;
        if (n->anim > 0.16f) { n->anim = 0; n->frame++; }
    }
}

/* Serveuse (drapeau 8 de la place dans INTR ; 03/11/2026, demande de l'utilisateur : elles restaient plantées au comptoir) :
   de sa place devant le comptoir vers un client (un autre client ou habitant de l'intérieur, au sol : pas un danseur, ni sur
   une estrade) qu'elle atteint en ligne droite à plus de SERV_LOIN, s'arrête SERV_ARRET0 à 1 s à SERV_ECART de lui, tournée
   vers lui, revient à sa place, y reste SERV_COMPTOIR0 à 1 s, et ainsi de suite ; sans client à portée, elle fait quelques
   pas dans la salle (SERV_TOUR0 à 1) ; gênée en chemin plus de SERV_BLOQUE s, elle revient (interieurService de
   l'émulateur ; serv_vers : client visé, -1 retour, -2 quelques pas) */
#define SERV_ECART 0.55f
#define SERV_ARRET0 3.0f
#define SERV_ARRET1 6.0f
#define SERV_COMPTOIR0 4.0f
#define SERV_COMPTOIR1 8.0f
#define SERV_BLOQUE 2.0f
#define SERV_LOIN 0.9f
#define SERV_TOUR0 1.2f
#define SERV_TOUR1 3.0f
static int serv_ligne(float x0, float z0, float x1, float z1, int self, float f) {   /* segment praticable, pas de 0,15 m */
    const float d = hypotf(x1 - x0, z1 - z0);
    int n = (int)ceilf(d / (0.15f * f)); if (n < 1) n = 1;
    for (int k = 1; k <= n; k++) if (!int_sol_libre(x0 + (x1 - x0) * k / n, z0 + (z1 - z0) * k / n, self)) return 0;
    return 1;
}
static void int_service(float dt) {
    interieur_t *I = &E.in;
    const float f = I->f > 0 ? I->f : 1;
    for (int i = 0; i < I->npcn && i < I->npnj; i++) {
        const int k = I->npc0 + i;
        if (!I->pnj[i].service || k == E.talk) continue;
        npc_t *n = &E.npcs[k];
        if (!n->walking) {
            n->anim += dt; if (n->anim > 0.22f) { n->anim = 0; n->frame++; }
            if ((serv_attente[i] -= dt) > 0) continue;
            if (!serv_chez[i]) { n->tx = n->hx; n->tz = n->hz; serv_vers[i] = -1; serv_bloque[i] = 0; n->walking = 1; continue; }
            int cl[INT_PNJ_MAX], nc = 0;
            for (int j = 0; j < I->npcn && j < I->npnj; j++) {
                const npc_t *c = &E.npcs[I->npc0 + j];
                if (j != i && (I->pnj[j].metier == 8 || I->pnj[j].metier == 9) && !I->pnj[j].danse && int_sol_y(c->x, c->z) < 0.05f) cl[nc++] = j;
            }
            /* un client qu'elle atteint en ligne droite (point à SERV_ECART de lui, du côté d'où elle vient, à 45° près) */
            for (int e = 0; e < 4 && !n->walking && nc; e++) {
                const int j = cl[(int)(irand() * nc) % nc];
                const npc_t *c = &E.npcs[I->npc0 + j];
                const float a0 = atan2f(n->x - c->x, n->z - c->z);
                for (int t = 0; t < 3 && !n->walking; t++) {
                    const float a = a0 + ((t & 1) ? 1 : -1) * (float)((t + 1) / 2) * (PI_F / 4), x = c->x + sinf(a) * SERV_ECART * f, z = c->z + cosf(a) * SERV_ECART * f;
                    if (hypotf(x - n->x, z - n->z) > SERV_LOIN * f && serv_ligne(n->x, n->z, x, z, k, f)) { n->tx = x; n->tz = z; serv_vers[i] = (int16_t)j; n->walking = 1; }
                }
            }
            /* personne à portée : quelques pas dans la salle */
            for (int t = 0; t < 8 && !n->walking; t++) {
                const float a = irand() * 2 * PI_F, r = (SERV_TOUR0 + irand() * (SERV_TOUR1 - SERV_TOUR0)) * f, x = n->x + sinf(a) * r, z = n->z + cosf(a) * r;
                if (serv_ligne(n->x, n->z, x, z, k, f)) { n->tx = x; n->tz = z; serv_vers[i] = -2; n->walking = 1; }
            }
            if (!n->walking) serv_attente[i] = 1 + irand() * 2; else serv_bloque[i] = 0;
            continue;
        }
        const float dx = n->tx - n->x, dz = n->tz - n->z, d = hypotf(dx, dz);
        const int pres = d < 0.25f * f && !int_sol_libre(n->x + dx / d * fminf(d, 0.9f * f * dt), n->z + dz / d * fminf(d, 0.9f * f * dt), k);   /* tout près, le dernier pas pris (sa place au bord d'un meuble) */
        if (d < 0.06f * f || pres) {   /* arrivée : à côté du client, tournée vers lui ; à sa place, tournée vers la salle */
            n->walking = 0;
            if (serv_vers[i] != -1) {   /* chez un client, ou au bout de ses pas */
                serv_chez[i] = 0;
                if (serv_vers[i] >= 0) { const npc_t *c = &E.npcs[I->npc0 + serv_vers[i]]; n->dir = atan2f(c->x - n->x, c->z - n->z); serv_attente[i] = SERV_ARRET0 + irand() * (SERV_ARRET1 - SERV_ARRET0); }
                else serv_attente[i] = 1 + irand();
            } else { serv_chez[i] = 1; n->dir = I->pnj[i].dir; serv_attente[i] = SERV_COMPTOIR0 + irand() * (SERV_COMPTOIR1 - SERV_COMPTOIR0); }
            continue;
        }
        const float sp = fminf(d, 0.9f * f * dt), nx = n->x + dx / d * sp, nz = n->z + dz / d * sp;
        if (int_sol_libre(nx, nz, k) || !int_sol_libre(n->x, n->z, k)) { n->x = nx;   /* (partie d'une case prise : elle en sort) */
            n->z = nz; n->dir = atan2f(dx, dz); serv_bloque[i] = 0; n->anim += dt; if (n->anim > 0.16f) { n->anim = 0; n->frame++; } continue; }
        if ((serv_bloque[i] += dt) > SERV_BLOQUE) {   /* gênée : revient à sa place (le retour gêné : elle attend et repart) */
            serv_bloque[i] = 0;
            if (serv_vers[i] != -1) { serv_vers[i] = -1; n->tx = n->hx; n->tz = n->hz; }
            else { n->walking = 0; serv_attente[i] = 1 + irand(); }
        }
    }
}

/* Danse de foule (DANSEUR_* d'internal.h) : un client qui danse sans animation de danse à lui, au sol, fait de temps en
   temps quelques pas autour de sa place sans se retourner (il reste tourné vers la scène : draw_int_npc) ; gêné, il
   s'arrête là (interieurDanseurs de l'émulateur) */
static void int_danseurs(float dt) {
    interieur_t *I = &E.in;
    const float f = I->f > 0 ? I->f : 1;
    for (int i = 0; i < I->npcn && i < I->npnj; i++) {
        const int k = I->npc0 + i;
        npc_t *n = &E.npcs[k];
        if (!I->pnj[i].danse || I->pnj[i].metier != 8 || k == E.talk || has_anim(perso_vue(n->perso, n->var).sheet, ANIM_DANSE) || int_sol_y(n->hx, n->hz) > 0.05f) continue;
        if (!n->walking) {
            if (n->wait > 1e8f) n->wait = DANSEUR_PAUSE0 + irand() * (DANSEUR_PAUSE1 - DANSEUR_PAUSE0);
            if ((n->wait -= dt) > 0) continue;
            for (int t = 0; t < 6 && !n->walking; t++) {
                const float a = irand() * 2 * PI_F, r = (0.2f + irand() * (DANSEUR_ERRE - 0.2f)) * f, x = n->hx + sinf(a) * r, z = n->hz + cosf(a) * r;
                if (serv_ligne(n->x, n->z, x, z, k, f)) { n->tx = x; n->tz = z; n->walking = 1; }
            }
            n->wait = DANSEUR_PAUSE0 + irand() * (DANSEUR_PAUSE1 - DANSEUR_PAUSE0);
            continue;
        }
        const float dx = n->tx - n->x, dz = n->tz - n->z, d = hypotf(dx, dz);
        if (d < 0.04f * f) { n->walking = 0; continue; }
        const float sp = fminf(d, DANSEUR_V * f * dt), nx = n->x + dx / d * sp, nz = n->z + dz / d * sp;
        if (!int_sol_libre(nx, nz, k)) { n->walking = 0; continue; }
        n->x = nx; n->z = nz;
    }
}

/* À chaque image : porte devant Sera dehors, sortie par un seuil dedans ; compte à rebours de l'alarme (à zéro : la
   police et l'amende, sauf si Sera s'est éloignée ; la position de sa porte si elle est dedans) */
void int_update(float dt) {
    interieur_t *I = &E.in;
    if (I->on) {
        E.porte = -1;
        int_escalier();
        if (I->on) { int_promenade(dt); int_service(dt); int_danseurs(dt); }
        /* Hauteur de Sera (sols surélevés : escaliers, gradins), lissée (interieurSolY, inside.sy de l'émulateur) */
        if (I->on) {
            const float t = int_sol_y(E.sera.x, E.sera.z);
            if (!I->sy_ok) { I->sy = t; I->sy_ok = 1; } else I->sy += (t - I->sy) * fminf(1, dt * 14);
        }
        const float lx = E.sera.x - IO, lz = E.sera.z - IO;
        if (I->on && I->nsort && (lx <= -0.05f || lz <= -0.05f || lx >= I->w + 0.05f || lz >= I->d + 0.05f)) {
            const int_sortie_t *best = &I->sort[0]; float bd = 1e9f;
            for (int i = 0; i < I->nsort; i++) {
                const float d = hypotf((I->sort[i].x0 + I->sort[i].x1) / 2 - lx, (I->sort[i].z0 + I->sort[i].z1) / 2 - lz);
                if (d < bd) { bd = d; best = &I->sort[i]; }
            }
            sortir(best);
        }
    } else E.porte = E.ride < 0 && !CR.on && !hud_in_dialog() ? porte_devant() : -1;
    alarme_t *A = &E.alarme;
    if (!A->on) return;
    A->t += dt;
    son_boucle(CLE_SIRENE, SON_SIRENE, 0.35f, (650 + 350 * (0.5f + 0.5f * sinf(A->t * PI_F * 1.4f))) / 700.0f);
    if (A->t < A->duree || hud_in_dialog()) return;
    A->on = 0;
    const float px = I->on ? I->rx : E.sera.x, pz = I->on ? I->rz : E.sera.z;
    /* Semée : sur la planète, recherchée (niveau 1, police.c) ; ailleurs, rien */
    if (hypotf(px - A->x, pz - A->z) > 22 / E.w.ech) { if (E.w.carte) hud_toast("Vous avez semé la police"); else police_effraction(A->amende, A->clan); return; }
    if (I->on) {
        if (I->etage) { const int b0 = E.w.bats[I->bat].intr; if (!int_parse(b0)) { int_reset(); return; } }
        const int_sortie_t *s = &I->sort[0];
        for (int i = 0; i < I->nsort; i++) if (I->sort[i].porte == 0) { s = &I->sort[i]; break; }
        sortir(s);
    }
    /* Non semée, sur la planète : l'arrestation (sa scène : police_arreter) ; ailleurs, l'amende seule */
    if (!E.w.carte) { police_arreter(POL_EFFRACTION, A->amende, A->clan); return; }
    const int paye = E.credits < A->amende ? E.credits : A->amende;
    E.credits -= paye;
    static char l1[64], l2[64], l3[80];
    const char *nom = A->clan ? "MILICE" : "POLICE";
    snprintf(l1, sizeof l1, "Halte ! Effraction signalée.");
    snprintf(l2, sizeof l2, "Amende : %d crédits.", A->amende);
    snprintf(l3, sizeof l3, "Tu n'as que %d crédits ? On prend tout.", paye);
    const char *lines[3] = { l1, l2, l3 };
    hud_lines(nom, lines, paye < A->amende ? 3 : 2, "PATROUILLE");
}

/* Position à enregistrer : la porte d'entrée, dans la rue, si Sera est dedans */
void int_position(float *x, float *z, float *yaw) {
    if (E.in.on) { *x = E.in.rx; *z = E.in.rz; *yaw = E.in.ryaw; }
    else { *x = E.sera.x; *z = E.sera.z; *yaw = E.yaw; }
}
/* Métier + 1 de l'exemplaire npc s'il est un personnage de l'intérieur où est Sera (0 sinon) */
int int_metier_pnj(int npc) {
    const interieur_t *I = &E.in;
    return I->on && npc >= I->npc0 && npc < I->npc0 + I->npcn && npc - I->npc0 < I->npnj ? I->pnj[npc - I->npc0].metier : 0;
}
/* Nom du bâtiment d'une porte (bulle de la porte) */
const char *int_porte_nom(int porte) { return porte >= 0 && porte < E.w.nportes ? E.w.bats[E.w.portes[porte].bat].nom : ""; }
/* Une réplique générique au hasard de la liste (métier, ou lieu : REPL_VILLE + 0 ville, 1 village, 2 campement) */
int repl_ligne(int liste, char *out, int cap) {
    const world_t *w = &E.w;
    if (liste < 0 || liste >= w->nrepls || !w->nrepl[liste] || cap < 2) return 0;
    /* Réplique 18+ (premier octet 1, édition unique) : sautée tant que le 18+ est verrouillé, sa marque ôtée */
    #define REPL_VUE(q) (!((q)[0] && (q)[1] == 1 && verrou18()))
    int vues = 0;
    const uint8_t *q = w->repl[liste];
    for (int i = 0; i < w->nrepl[liste]; i++, q += 1 + q[0]) vues += REPL_VUE(q);
    if (!vues) return 0;
    int k = (int)(irand() * vues);
    q = w->repl[liste];
    while (!REPL_VUE(q) || k-- > 0) q += 1 + q[0];
    #undef REPL_VUE
    const int m = q[0] && q[1] == 1, n = q[0] - m < cap - 1 ? q[0] - m : cap - 1;
    memcpy(out, q + 1 + m, (size_t)n); out[n] = 0;
    return 1;
}
/* Intérieur visité relu s'il a rendu le bloc de la fenêtre (combat pendant la visite : world_stream l'a repris pour
   l'arène) ; illisible : Sera dans la rue, devant la porte d'entrée */
int int_pret(void) {
    interieur_t *I = &E.in;
    if (!I->on || E.w.fen_int) return 1;
    const int npc0 = I->npc0, npcn = I->npcn;   /* ses exemplaires restent tels quels (E.npcs) */
    if (int_parse(I->cur)) { I->npc0 = npc0; I->npcn = npcn; return 1; }
    E.sera.x = I->rx; E.sera.z = I->rz; E.yaw = I->ryaw;
    int_reset();
    E.stream_hold = 1;
    return 0;
}

/* Chargement d'une partie : dans la rue, sans alarme */
void int_reset(void) {
    if (E.in.on && E.in.npcn) E.nnpc = E.in.npc0;
    E.in.on = 0; E.in.npcn = 0; E.alarme.on = 0; E.porte = -1; choix_porte = -1;
}
