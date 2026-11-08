/*
 * Le Chœur des Cendres — le véhicule de Sera (01/11/2026 ; section « Garage » de l'émulateur, à garder identiques).
 *
 * Offert (code CODE_VOITURE du menu SYSTÈME › CODE) ou acheté (boutique de véhicules : hud_vehicules de hud.c), il est à
 * elle : ni volé ni recherché (npc_t.vole reste à 0), ni confisqué. Un seul à la fois : le nouveau remplace l'ancien, qui
 * repart (npc_t.mien = 2, retiré dès que son bloc d'exemplaires est celui de la carte en cours, hors d'un intérieur). C'est
 * un exemplaire ajouté (comme un véhicule volé, vehicle_spawn) que vehicle_spawn ne reprend jamais pour un autre.
 *
 * Il apparaît sur la place libre la plus proche de Sera où il tient (terre ferme, à GARAGE_MARGE de tout obstacle et de
 * tout personnage), Sera au volant. Acheté dans un intérieur (concession) : livré devant la porte, dans la rue, à la sortie.
 * Il reste là où elle le laisse, sur sa carte : la planète garde ses exemplaires ajoutés, une autre carte les perd quand
 * Sera la quitte (world_carte), et il y revient à sa dernière place quand elle y retourne. La sauvegarde (ESVG, hud.c) garde
 * son personnage, sa palette, sa carte, sa place et si Sera était au volant ; au chargement, il revient là, Sera au volant
 * si elle y était.
 */
#include "internal.h"
#include <string.h>

#define GARAGE_MARGE 0.9f   /* m : emprise du véhicule autour de son centre (marge de conduite 0,45 m en plus) */
#define GARAGE_GENS 1.4f    /* m : personne à moins de tant de son centre (trafic : × 1,6) */
#define GARAGE_RMAX 5.0f    /* cases : place cherchée jusqu'à cette distance */
#define GARAGE_SEUIL 1.5f   /* cases : livré devant une porte, pas plus près du seuil (ni de Sera qui en sort) */
/* Prix d'un véhicule hors de la carrière (js/carriere.js de l'Atelier : PRIX_K × (note - PRIX_0)², arrondi à 100 ; note =
   vitesse + accélération / 4 + 3 × (adhérence - 1)), au moins VEH_PRIX_MIN */
#define VEH_PRIX_K 260.0
#define VEH_PRIX_0 7.5
#define VEH_PRIX_MIN 1000
#define VEH_PRIX_VOL 4   /* ce qui vole : bien plus cher (06/11/2026 : la concession) */
#define VEH_VOL_MIN 20000   /* et jamais à moins de tant : voler vaut plus que les flotteurs */

/* on : Sera a un véhicule ; attente : à poser quand Sera est sur sa carte, dehors, hors combat ; placer : chercher une
   place libre autour de (x, z) (livraison devant une porte), sinon à sa place exacte ; abord : Sera au volant à la pose */
static struct { int on, perso, var, carte, attente, placer, abord; float x, z, dir; } G;

int garage_perso(void) { return G.on ? G.perso : -1; }

/* Pièces du véhicule de Sera (06/11/2026) : VK_* d'internal.h ; sectionPieces de l'émulateur */
static const struct { const char *nom, *desc; int prix[3]; } VK[VK_N] = {
    { "GUÉ RENFORCÉ", "Prises d'air en hauteur : il passe bien plus profond", { 6000, 0, 0 } },
    { "CHENILLES", "Hors des routes, il garde presque toute sa vitesse", { 4000, 0, 0 } },
    { "FLOTTEURS", "Il ne coule plus : lacs et mer, lentement, jusqu'aux îles", { 15000, 0, 0 } },
    { "MOTEUR", "Vitesse et accélération : 8 % de plus à chaque niveau", { 2500, 8000, 24000 } } };
int vk_monte(int p, int k) { return G.on && G.perso == p && k >= 0 && k < VK_N ? E.veh_up[k] : 0; }
int vk_max(int k) { return k == VK_MOTEUR ? 3 : 1; }
int vk_prix(int k) { return k >= 0 && k < VK_N && E.veh_up[k] < vk_max(k) ? VK[k].prix[E.veh_up[k]] : 0; }
const char *vk_nom(int k) { return VK[k].nom; }
const char *vk_desc(int k) { return VK[k].desc; }
int concession_ici(void) {
    if (!E.in.on || E.in.bat < 0 || E.in.bat >= E.w.nbats) return 0;
    return !strncmp(E.w.bats[E.in.bat].nom, "Concession", 10) || !strncmp(E.w.bats[E.in.bat].nom, "CONCESSION", 10);   /* (PORT : en capitales) */
}
/* Ce qu'elle vend : les modèles proposés, sans ceux des services (police, armée, secours, voirie, transport), ni la navette de l'histoire */
void concession_ouvrir(void) {
    static int L[MAX_PERSOS] ENG_FROID;
    int n = 0;
    for (int p = 0; p < E.w.npersos && p < MAX_PERSOS; p++) if (veh_proposable(p) && !E.w.veh_service[p] && strcmp(E.w.persos[p].nom, "NAVETTE")) L[n++] = p;
    hud_vehicules(1, L, n);
}

/* Proposé (menu des véhicules) : un véhicule avec sa planche, présent (drapeau de présence : une livrée se débloque par le
   scénario), qui roule, plane ou vole (pas un bateau : il n'irait que sur l'eau), et pas un véhicule unique de l'histoire
   (voiture d'un rival, navette nommée) : un modèle de la course, du trafic, à palettes ou d'un service */
int veh_proposable(int p) {
    if (p < 0 || p >= E.w.npersos || p >= MAX_PERSOS) return 0;
    const perso_t *pe = &E.w.persos[p];
    const int t = VEH_TYPE(pe->flags);
    if (!(pe->flags & PERS_VEHICULE) || pe->sheet < 0 || t < VEH_VOITURE || t > VEH_VAISSEAU || t == VEH_BATEAU || !perso_present(p)) return 0;
    return E.w.veh_course[p] || (pe->flags & PERS_TRAFIC) || pe->nvar > 0 || E.w.veh_service[p];
}

int veh_prix(int p) {
    const int kvol = p >= 0 && p < E.w.npersos && VEH_TYPE(E.w.persos[p].flags) == VEH_VAISSEAU ? VEH_PRIX_VOL : 1;
    for (int i = 0; i < CARRD.nveh; i++) if (CARRD.veh[i].perso == p && CARRD.veh[i].prix > 0) return kvol > 1 && (int)CARRD.veh[i].prix * kvol < VEH_VOL_MIN ? VEH_VOL_MIN : (int)CARRD.veh[i].prix * kvol;
    float v, a;
    veh_params(p, &v, &a);
    const double g = p >= 0 && p < MAX_PERSOS && E.w.veh_grip[p] > 0 ? E.w.veh_grip[p] : 1, note = v + a / 4.0 + 3 * (g - 1), d = note > VEH_PRIX_0 ? note - VEH_PRIX_0 : 0;
    const int prix = (int)lround(VEH_PRIX_K * d * d / 100) * 100;
    const int total = (prix > VEH_PRIX_MIN ? prix : VEH_PRIX_MIN) * kvol;
    return kvol > 1 && total < VEH_VOL_MIN ? VEH_VOL_MIN : total;
}

/* Place pour le véhicule p en (x, z) : dans la carte, chunks chargés, la terre ferme (pont compris), loin des obstacles
   (cinq points de son emprise, chacun à 0,45 m au moins de tout rectangle de collision), personne autour (Sera exceptée :
   elle monte dedans). -1 : chunk pas chargé (attendre), 0 : pas ici, 1 : libre */
static int place_ok(int p, float x, float z) {
    const world_t *w = &E.w;
    const float m = GARAGE_MARGE / w->ech, mm = 0.45f / w->ech, rg = GARAGE_GENS / w->ech;
    if (x < 3 || z < 3 || x > w->n - 3 || z > w->n - 3) return 0;
    static const float D[5][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int q = 0; q < 5; q++) {
        const float px = x + D[q][0] * m, pz = z + D[q][1] * m;
        const int i = (int)floorf(px), j = (int)floorf(pz);
        const chunk_t *c = chunk_of_cell(i, j);
        if (!c) return -1;
        const uint8_t f = c->flags[cell_local(i, j)];
        if (!(f & F_ROAD) && (f & (F_WATER | F_RIVER))) return 0;
        for (uint32_t k = 0; k < c->nsolid; k++) {
            const float *r = &c->solids[k * 4];
            if (px > r[0] - mm && px < r[2] + mm && pz > r[1] - mm && pz < r[3] + mm) return 0;
        }
    }
    for (int k = 0; k < E.nnpc; k++) {
        const npc_t *n = &E.npcs[k];
        if (k != E.ride && npc_present(n) && fabsf(n->x - x) < rg && fabsf(n->z - z) < rg && hypotf(n->x - x, n->z - z) < rg) return 0;
    }
    for (int k = 0; k < TRAFFIC_MAX; k++)
        if (E.traffic[k].on && hypotf(E.traffic[k].x - x, E.traffic[k].z - z) < rg * 1.6f) return 0;
    return 1;
}
/* Place libre la plus proche de (x0, z0), de rmin à GARAGE_RMAX cases (anneaux de 0,5 case, 16 directions) ; -1 : chunks
   pas encore chargés autour, 0 : aucune */
static int chercher(int p, float x0, float z0, float rmin, float *ox, float *oz) {
    int attendre = !chunk_of_cell((int)floorf(x0), (int)floorf(z0));   /* le point même : son chunk n'est pas encore là */
    for (float r = rmin; r <= GARAGE_RMAX && !attendre; r += 0.5f)
        for (int a = 0; a < (r > 0 ? 16 : 1); a++) {
            const float x = x0 + cosf(a * PI_F / 8) * r, z = z0 + sinf(a * PI_F / 8) * r;
            const int ok = place_ok(p, x, z);
            if (ok > 0) { *ox = x; *oz = z; return 1; }
            if (ok < 0 && r <= rmin + 1) attendre = 1;   /* tout près : le chunk n'est pas encore là */
        }
    return attendre ? -1 : 0;
}
/* Exemplaire du véhicule posé en (x, z) (-1 : pas de place parmi les exemplaires ajoutés) */
static int poser(float x, float z, float dir) {
    const int s = vehicle_spawn(G.perso, x, z, dir);
    if (s < 0) return -1;
    npc_t *n = &E.npcs[s];
    n->var = (uint8_t)G.var; n->mien = 1; n->place_ok = 1;
    return s;
}
/* L'ancien véhicule repart : marqué (mien = 2) dans le bloc de la carte en cours et dans celui de la planète (gardé quand
   Sera est ailleurs) ; garage_maj le retire */
static void oublier(void) {
    for (int k = E.nnpc_base; k < E.nnpc; k++) if (E.npcs[k].mien) E.npcs[k].mien = 2;
    if (E.w.carte && E.npc_tous) for (int k = E.npc_cnt[0]; k < E.nnpc_c0; k++) if (E.npc_tous[k].mien) E.npc_tous[k].mien = 2;
    if (E.ride >= 0 && E.npcs[E.ride].mien == 2) E.npcs[E.ride].mien = 0;   /* (conduit : il reste un véhicule ordinaire) */
    G.on = 0;
}

int garage_donner(int perso, int var) {
    if (perso < 0 || perso >= E.w.npersos || !(E.w.persos[perso].flags & PERS_VEHICULE)) return 0;
    if (var < 0 || var > E.w.persos[perso].nvar) var = 0;
    if (E.in.on) {   /* concession : livré devant la porte, dans la rue, à la sortie (garage_maj) */
        float x, z, yaw;
        int_position(&x, &z, &yaw);
        oublier();
        G.on = 1; G.perso = perso; G.var = var; G.carte = E.w.carte; G.x = x; G.z = z; G.dir = yaw;
        G.attente = G.placer = 1; G.abord = 0;
        return 2;
    }
    float x, z;
    if (chercher(perso, E.sera.x, E.sera.z, 0, &x, &z) <= 0) return 0;
    oublier();
    G.on = 1; G.perso = perso; G.var = var; G.carte = E.w.carte; G.x = x; G.z = z; G.dir = E.yaw;
    G.attente = G.placer = G.abord = 0;
    const int s = poser(x, z, E.yaw);   /* tourné comme la caméra : la vue ne pivote pas */
    if (s < 0) { G.on = 0; return 0; }
    vehicle_board(s);
    return 1;
}

void garage_maj(void) {
    if (E.in.on || BT.on || !E.npcs) return;
    for (int k = E.nnpc - 1; k >= E.nnpc_base; k--) if (E.npcs[k].mien == 2 && k != E.ride) vehicule_retirer(k);   /* l'ancien */
    if (!G.on || G.carte != E.w.carte) return;
    int s = -1;
    for (int k = E.nnpc_base; k < E.nnpc && s < 0; k++) if (E.npcs[k].mien == 1) s = k;
    if (s >= 0) { G.x = E.npcs[s].x; G.z = E.npcs[s].z; G.dir = E.npcs[s].dir; G.attente = 0; return; }   /* suivi */
    /* Plus là (carte quittée puis retrouvée, partie chargée, livraison) : reposé à sa place */
    float x = G.x, z = G.z;
    if (G.placer) {
        const int r = chercher(G.perso, G.x, G.z, GARAGE_SEUIL, &x, &z);
        if (r < 0) return;   /* chunks pas encore là */
        if (r == 0) { x = G.x; z = G.z; }
    }
    if ((s = poser(x, z, G.dir)) < 0) return;   /* pas de place parmi les exemplaires : plus tard */
    G.x = x; G.z = z; G.attente = G.placer = 0;
    if (G.abord && E.ride < 0 && !CR.on) { const float d = G.dir; vehicle_board(s); E.sera.dir = E.npcs[s].dir = d; }
    G.abord = 0;
}

void garage_reset(void) { oublier(); memset(&G, 0, sizeof G); }

void garage_ecrire(garage_sv_t *s) {
    memset(s, 0, sizeof *s);
    if (!G.on) return;
    s->on = (uint8_t)(1 | (G.attente && G.placer ? 2 : 0));
    s->perso = (uint8_t)G.perso; s->var = (uint8_t)G.var; s->carte = (uint8_t)G.carte;
    s->abord = (uint8_t)(E.ride >= 0 && E.npcs[E.ride].mien == 1);
    s->x = G.x; s->z = G.z; s->dir = G.dir;
}

void garage_lire(const garage_sv_t *s) {
    oublier();
    memset(&G, 0, sizeof G);
    if (!(s->on & 1) || s->perso >= E.w.npersos || !(E.w.persos[s->perso].flags & PERS_VEHICULE) || s->carte >= E.w.ncartes) return;
    G.on = 1; G.perso = s->perso; G.var = s->var <= E.w.persos[s->perso].nvar ? s->var : 0; G.carte = s->carte;
    G.x = s->x; G.z = s->z; G.dir = s->dir;
    G.attente = 1; G.placer = (s->on & 2) != 0; G.abord = s->abord != 0;
}
