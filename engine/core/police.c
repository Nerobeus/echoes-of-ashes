/*
 * Police : recherche, poursuite et arrestation (comme la section « Police » de l'émulateur : policeMaj, policeVol,
 * policeArreter…, à garder identiques).
 *
 * Recherche : niveau 0 à 3, sur la planète seulement (carte 0), hors course ; ni sauvegardée, ni gardée d'une carte à
 * l'autre. Un véhicule volé alerte toujours les autorités (niveau 1, 2 pour un véhicule de police ou de l'armée) et reste
 * signalé tant que Sera le conduit (npc_t.vole) : une voiture de police du trafic qui le voit relance la recherche. Une
 * alarme d'effraction semée donne le niveau 1. Un nouveau délit pendant une recherche la monte d'un niveau.
 *
 * Poursuite : jusqu'à « niveau » voitures de police de la bibliothèque (service police de VEH2), apparues sur une route
 * ou une rue hors de vue, de POL_LOIN0 à POL_LOIN1 cases de Sera ; elles suivent le réseau vers leur cible (game.c :
 * pour_*), Sera vue à l'instant (POL_SUIVI s après le dernier regard), sinon son dernier point connu. Sera à pied, la
 * voiture qui l'a rejointe se gare et des agents (GARDE, OFFICIÈRE… de la bibliothèque) en descendent et courent vers
 * elle. Dans un village (le bâtiment visitable le plus proche tenu par un clan, comme la milice de l'alarme) ou sans
 * voiture de police dans la bibliothèque : des agents à pied seulement, apparus hors de vue.
 *
 * Semer la police : aucun poursuivant ne la voit (à moins de POL_VUE cases, POL_VUE_PIED pour un agent, sans rien de
 * bâti entre eux) ni n'est à moins de POL_PROCHE cases, et Sera est à plus de POL_PROCHE cases de son dernier point connu
 * (rester là où on l'a vue ne la cache pas), pendant POL_SEMER_S secondes selon le niveau ; dans un bâtiment, Sera est
 * cachée. La jauge de hud.c (draw_recherche) montre le temps qu'il reste à tenir.
 *
 * Délai : après un premier délit, la première voiture n'arrive qu'au bout de POL_ALERTE_S s (le temps que la victime
 * prévienne), puis un renfort toutes les POL_SPAWN_S s ; avant le 01/11/2026, elle partait tout de suite de 15 à 25 cases
 * et prenait Sera 2 s après le vol, pendant qu'elle démarrait.
 *
 * Prise (01/11/2026) : un agent qui la touche (à pied, ou son véhicule presque arrêté), ou une voiture de police contre son
 * véhicule immobilisé (moins de POL_PRISE_V cases/s pendant POL_PRISE_S s) : un combat contre les agents (police_combat,
 * plus bas), plus l'arrestation tout de suite ; perdu, l'arrestation et la prison. Pendant le répit qui suit un combat
 * gagné (POL_REPIT s) : ni renfort ni prise.
 *
 * Arrestation : un combat perdu contre les agents, ou la fin d'une alarme d'effraction non semée (interieur.c). Scène
 * (police_arreter, arr_maj) : Sera s'arrête (sortie de son véhicule, confisqué s'il est volé), deux agents arrivent en
 * courant et l'encadrent, regards, caméra rapprochée (MISE), secousse, carton « ARRÊTÉE », répliques de l'agent (amende :
 * celle de l'alarme pour une effraction, (POL_AMENDE_K + le niveau) × les crédits d'un combat pour un vol), fondu au noir. Pose
 * de Sera : sa défense (ANIM_DEFEND) jouée une fois puis tenue, sinon l'attente. Le joueur ne la dirige pas
 * (police_bloque, mise_bloque).
 */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "internal.h"
#include "audio.h"

police_t POL ENG_FROID;

static uint32_t rng_p = 0x2545F491u;
static float prand(void) { rng_p ^= rng_p << 13; rng_p ^= rng_p >> 17; rng_p ^= rng_p << 5; return (float)(rng_p >> 8) * (1.0f / 16777216.0f); }

#define POL_SUIVI 2.0f          /* s : les poursuivants suivent Sera tant de temps après l'avoir vue */
#define POL_SPAWN_S 2.0f        /* s entre deux renforts */
#define POL_VUE 10.0f           /* cases : une voiture de police voit Sera à moins de tant, rien de bâti entre elles */
#define POL_VUE_PIED 8.0f       /* cases : un agent */
#define POL_PROCHE 12.0f        /* cases : un poursuivant plus près empêche de la semer */
static const float POL_SEMER_S[4] = { 0, 12, 16, 20 };   /* s à tenir hors de vue, par niveau (8, 12, 16 avant le 01/11/2026) */
#define POL_PIED0 12.0f         /* cases : agents à pied apparus entre POL_PIED0 et POL_PIED1, hors de vue */
#define POL_PIED1 18.0f
#define POL_AG_V 4.4f           /* m/s : agents à la course (Sera court à 5,2) */
#define POL_AG_LOIN 24.0f       /* cases : un agent plus loin abandonne */
#define POL_AG_PERDU 8.0f       /* s au dernier point connu sans la voir : il s'en va */
#define POL_PRISE 0.7f          /* cases : un agent la touche */
#define POL_PRISE_VEH 1.9f      /* cases : une voiture de police contre son véhicule… */
#define POL_PRISE_V 0.5f        /* … immobilisé (cases/s ; 0,8 avant le 01/11/2026)… */
#define POL_PRISE_S 2.0f        /* … tant de temps (0,6 s avant : un simple accrochage au démarrage suffisait) */
static const float POL_ALERTE_S[4] = { 0, 6, 4, 3 };   /* s avant la première voiture après un premier vol, par niveau */
#define POL_CLAN_R 40.0f        /* cases : bâtiment visitable cherché pour savoir si l'on est dans un village */
#define POL_FLANC 0.9f          /* cases : les deux agents se tiennent de part et d'autre de Sera */
#define POL_ZOOM 1.7f           /* caméra rapprochée : zoom du moment × POL_ZOOM (ZOOM_MAX au plus) */
/* Amende d'un vol : (POL_AMENDE_K + le niveau de recherche) × les crédits d'un combat au niveau de l'équipe, POL_AMENDE_MIN
   au moins (01/11/2026 : 200 + 100 × le niveau avant, moins d'un combat passé le niveau 15 ; trois à cinq combats désormais,
   comme avant au début du jeu ; arrDialogue de l'émulateur) */
#define POL_AMENDE_K 2
#define POL_AMENDE_MIN 100

float police_semer_s(void) { return POL_SEMER_S[POL.niveau < 0 ? 0 : POL.niveau > 3 ? 3 : POL.niveau]; }
int police_bloque(void) { return POL.arr != ARR_AUCUNE || POL.enc != 0; }   /* arrestation, ou interception à l'enceinte */
int police_cible(float *x, float *z) {
    if (POL.t - POL.vu_t < POL_SUIVI) { *x = E.sera.x; *z = E.sera.z; return 1; }
    *x = POL.lx; *z = POL.lz;
    return 0;
}
int police_hors_vue(float x, float z) {
    return (x - E.cam.x) * E.cam.sy + (z - E.cam.z) * E.cam.cy < 0 || hypotf(x - E.sera.x, z - E.sera.z) > POL_VUE_R / E.w.ech;
}

/* Rien de bâti entre (x0, z0) et (x1, z1) : aucune case sous un bâtiment (hidden des chunks) sur la ligne, tous les
   demi-cases (vueLibre de l'émulateur) */
static int vue_libre(float x0, float z0, float x1, float z1) {
    const float dx = x1 - x0, dz = z1 - z0;
    const int n = (int)(hypotf(dx, dz) * 2) + 1;
    for (int k = 1; k < n; k++) {
        const float t = (float)k / n;
        const int i = (int)floorf(x0 + dx * t), j = (int)floorf(z0 + dz * t);
        const chunk_t *c = chunk_of_cell(i, j);
        if (!c || c->hidden[cell_local(i, j)]) return 0;
    }
    return 1;
}
/* Village : le bâtiment visitable de la planète le plus proche (à moins de POL_CLAN_R cases) tenu par une autorité est
   celui d'un clan (la milice de l'alarme) ; ailleurs, l'Ordre (polClan de l'émulateur) */
static int pol_clan(float x, float z) {
    float bd = POL_CLAN_R / E.w.ech;
    int clan = 0;
    for (int i = 0; i < E.w.nbats; i++) {
        const int_bat_t *b = &E.w.bats[i];
        if (b->carte || b->autorite == AUT_AUCUNE) continue;
        const float d = hypotf(b->cx - x, b->cz - z);
        if (d < bd) { bd = d; clan = b->autorite == AUT_CLAN; }
    }
    return clan;
}
/* Voiture de police de la bibliothèque (service police de VEH2 : voiture, camion ou mech, présente), au hasard ; -1 : aucune */
static int pol_voiture(void) {
    static int c[MAX_PERSOS] ENG_FROID;   /* statique, en PSRAM : la pile du jeu fait 8 Ko sur l'ESP32 */
    int n = 0;
    for (int p = 0; p < E.w.npersos; p++) {
        const perso_t *pe = &E.w.persos[p];
        const int ty = VEH_TYPE(pe->flags);
        if ((pe->flags & PERS_VEHICULE) && E.w.veh_service[p] == 1 && ty >= VEH_VOITURE && ty <= VEH_MECH && pe->sheet >= 0 && perso_present(p)) c[n++] = p;
    }
    return n ? c[(int)(prand() * n) % n] : -1;
}
/* Agent à pied : un personnage de la bibliothèque (ni véhicule ni ennemi, présent) ; police : officier, garde de l'Ordre,
   police, patrouille, sinon un garde ou un soldat ; milice : milicien, vigie, factionnaire, portier, sinon un garde (pas
   de l'Ordre) ; à défaut un passant (générique ou métier) ; au hasard parmi les meilleurs (polAgentPerso de l'émulateur) */
static int pol_rang(const char *nom, int clan) {
    if (!nom) return 0;
    if (clan) return strstr(nom, "MILIC") || strstr(nom, "VIGIE") || strstr(nom, "FACTION") || strstr(nom, "PORTIER") ? 3 : strstr(nom, "GARDE") && !strstr(nom, "ORDRE") ? 2 : 0;
    return strstr(nom, "OFFICI") || strstr(nom, "ORDRE") || strstr(nom, "POLIC") || strstr(nom, "PATROUIL") ? 3 : strstr(nom, "GARDE") || strstr(nom, "SOLDAT") ? 2 : 0;
}
static int pol_agent_perso(int clan) {
    static int c[MAX_PERSOS] ENG_FROID;   /* statique, en PSRAM : la pile du jeu */
    int n = 0, best = 0;
    for (int p = 0; p < E.w.npersos; p++) {
        const perso_t *pe = &E.w.persos[p];
        if ((pe->flags & PERS_VEHICULE) || pe->sheet < 0 || !perso_present(p)) continue;
        int ennemi = 0;
        for (int e = 0; e < E.w.nenemies && !ennemi; e++) ennemi = E.w.enemies[e].perso == p;
        if (ennemi) continue;
        int r = pol_rang(pe->nom, clan);
        if (!r && (pe->flags & PERS_FLANE) && PERS_GENRE(pe->flags)) r = 1;
        if (!r || r < best) continue;
        if (r > best) { best = r; n = 0; }
        c[n++] = p;
    }
    return n ? c[(int)(prand() * n) % n] : -1;
}
/* Nouvel agent (place libre, sinon -1) */
static int agent_poser(int perso, float x, float z, float dir, int etat) {
    if (perso < 0) return -1;
    for (int k = 0; k < POL_AGENTS; k++) {
        agent_t *a = &POL.ag[k];
        if (a->on) continue;
        memset(a, 0, sizeof *a);
        a->on = 1; a->perso = (uint8_t)perso; a->x = a->tx = x; a->z = a->tz = z; a->dir = dir; a->etat = (uint8_t)etat;
        a->var = (uint8_t)pvar_choix(perso, (unsigned)(prand() * 2048));
        return k;
    }
    return -1;
}
/* Place praticable à pied la plus proche de (x, z), jusqu'à r cases ; 0 : aucune */
static int place_libre(float x, float z, float r, float *ox, float *oz) {
    for (float rr = 0; rr <= r; rr += 0.5f)
        for (int a = 0; a < (rr > 0 ? 16 : 1); a++) {
            const float qx = x + cosf(a * PI_F / 8) * rr, qz = z + sinf(a * PI_F / 8) * rr;
            if (sol_pieton(qx, qz)) { *ox = qx; *oz = qz; return 1; }
        }
    return 0;
}
/* Renfort à pied (milice, ou pas de voiture de police) : hors de vue, de POL_PIED0 à POL_PIED1 cases de Sera */
static void renfort_pied(void) {
    const int p = pol_agent_perso(POL.clan);
    for (int a = 0; a < 24 && p >= 0; a++) {
        const float ang = prand() * 2 * PI_F, d = (POL_PIED0 + prand() * (POL_PIED1 - POL_PIED0)) / E.w.ech;
        const float x = E.sera.x + sinf(ang) * d, z = E.sera.z + cosf(ang) * d;
        if (!police_hors_vue(x, z) || !sol_pieton(x, z)) continue;
        agent_poser(p, x, z, atan2f(E.sera.x - x, E.sera.z - z), AG_CHASSE);
        return;
    }
}
static int agents_chasse(void) {
    int n = 0;
    for (int k = 0; k < POL_AGENTS; k++) n += POL.ag[k].on && POL.ag[k].etat == AG_CHASSE;
    return n;
}
void police_descendre(int k) {
    const traffic_t *t = &E.traffic[k];
    int n = agents_chasse();
    const int voulu = POL.niveau >= 2 ? 2 : 1;
    const float lx = -cosf(t->dir), lz = sinf(t->dir);   /* à gauche du cap ; le second à droite */
    for (int i = 0; i < voulu && n < POL.niveau; i++) {
        const float s = i ? -1.0f : 1.0f;
        float x, z;
        if (!place_libre(t->x + lx * 0.9f * s, t->z + lz * 0.9f * s, 1.5f, &x, &z)) continue;
        if (agent_poser(pol_agent_perso(0), x, z, t->dir, AG_CHASSE) >= 0) n++;
    }
    son_jouer(SON_PORTIERE, 0.5f, 1.05f);
}
/* Agents qui s'en vont : chacun à 10 cases à l'opposé de Sera, en marchant (retirés hors de vue) */
static void agents_partir(void) {
    for (int k = 0; k < POL_AGENTS; k++) {
        agent_t *a = &POL.ag[k];
        if (!a->on) continue;
        float dx = a->x - E.sera.x, dz = a->z - E.sera.z, d = hypotf(dx, dz);
        if (d < 0.01f) { dx = 1; dz = 0; d = 1; }
        a->etat = AG_DEPART; a->tx = a->x + dx / d * 10 / E.w.ech; a->tz = a->z + dz / d * 10 / E.w.ech;
    }
}
/* Un pas de l'agent vers (tx, tz) à v m/s, en glissant le long des obstacles ; 1 : arrivé */
static int agent_pas(agent_t *a, float tx, float tz, float v, float dt) {
    const float dx = tx - a->x, dz = tz - a->z, d = hypotf(dx, dz), sp = v / E.w.ech * dt;
    if (v <= 0 || d < 0.03f) {
        if (a->allure) { a->allure = 0; a->frame = 0; }
        a->anim += dt;
        if (a->anim > 0.22f) { a->anim = 0; a->frame++; }
        return 1;
    }
    const float s = sp > d ? d : sp, nx = a->x + dx / d * s, nz = a->z + dz / d * s;
    if (a->etat == AG_PLACE || sol_pieton(nx, nz)) { a->x = nx; a->z = nz; }   /* sa place à côté de Sera : sans obstacle */
    else if (sol_pieton(nx, a->z)) a->x = nx;
    else if (sol_pieton(a->x, nz)) a->z = nz;
    a->dir = atan2f(dx, dz);
    const int al = v > 2 ? 2 : 1;
    if (a->allure != al) { a->allure = (uint8_t)al; a->frame = 0; }
    a->anim += dt;
    if (a->anim > (al == 2 ? 0.1f : 0.16f)) { a->anim = 0; a->frame++; }
    return d <= sp;
}
/* Agents : poursuite (Sera suivie, sinon son dernier point connu, où il finit par abandonner), place de l'arrestation
   (immobile, tourné vers elle), départ */
static void agents_maj(float dt, int suivi) {
    for (int k = 0; k < POL_AGENTS; k++) {
        agent_t *a = &POL.ag[k];
        if (!a->on) continue;
        const float ds = hypotf(E.sera.x - a->x, E.sera.z - a->z);
        if (a->etat == AG_CHASSE) {
            if (ds > POL_AG_LOIN / E.w.ech) { a->on = 0; continue; }
            const float tx = suivi ? E.sera.x : POL.lx, tz = suivi ? E.sera.z : POL.lz;
            if (!suivi && hypotf(tx - a->x, tz - a->z) < 0.4f / E.w.ech) {   /* au dernier point connu : il regarde autour */
                agent_pas(a, a->x, a->z, 0, dt);
                if ((a->perdu += dt) > POL_AG_PERDU) a->etat = AG_DEPART;
            } else { a->perdu = 0; agent_pas(a, tx, tz, POL_AG_V, dt); }
        } else if (a->etat == AG_PLACE) {
            if (agent_pas(a, a->tx, a->tz, POL_AG_V, dt)) a->dir = atan2f(E.sera.x - a->x, E.sera.z - a->z);
        } else {
            if (police_hors_vue(a->x, a->z) || ds > POL_AG_LOIN / E.w.ech || agent_pas(a, a->tx, a->tz, 1.3f, dt)) a->on = 0;
        }
    }
}
/* Sirène de la voiture poursuivante la plus proche (moins forte avec la distance, étouffée dans un bâtiment) */
static void sirene(void) {
    float bd = 1e9f;
    for (int k = 0; k < TRAFFIC_MAX; k++) {
        const traffic_t *t = &E.traffic[k];
        if (t->on && t->poursuite) { const float d = hypotf(t->x - E.sera.x, t->z - E.sera.z); if (d < bd) bd = d; }
    }
    const float d = bd * E.w.ech;
    if (d < 22) son_boucle(CLE_SIRENE, SON_SIRENE, 0.3f * (1 - d / 22) * (E.in.on ? 0.4f : 1), (650 + 350 * (0.5f + 0.5f * sinf(POL.t * PI_F * 1.4f))) / 700.0f);
}

/* ---------- Délits ---------- */
/* La recherche monte : base (premier délit), sinon un niveau de plus (3 au plus) ; Sera signalée là où elle est */
static void signaler(int base, int motif) {
    POL.niveau = POL.niveau ? (POL.niveau + 1 > base ? POL.niveau + 1 : base) : base;
    if (POL.niveau > 3) POL.niveau = 3;
    POL.motif |= motif; POL.semer = 0;
    POL.vu_t = POL.t; POL.lx = E.sera.x; POL.lz = E.sera.z;
    if (POL.spawn_t > 1) POL.spawn_t = 1;
}
/* Niveau de recherche qu'apporterait le vol d'un véhicule perso (confirmation du vol, hud.c) : comme signaler ; 0 hors de la
   planète ou en course (personne n'est prévenu) */
int police_niveau_vol(int perso) {
    if (E.w.carte || CR.on || perso < 0 || perso >= MAX_PERSOS) return 0;
    const int sv = E.w.veh_service[perso], base = sv == 1 || sv == 4 ? 2 : 1, n = POL.niveau ? (POL.niveau + 1 > base ? POL.niveau + 1 : base) : base;
    return n > 3 ? 3 : n;
}
void police_vol(int npc) {
    if (E.w.carte || CR.on || npc < 0 || npc >= E.nnpc) return;
    const int sv = E.w.veh_service[E.npcs[npc].perso], base = sv == 1 || sv == 4 ? 2 : 1;   /* police, armée : 2 */
    E.npcs[npc].vole = (uint8_t)base;
    POL.clan = pol_clan(E.sera.x, E.sera.z);
    const int premier = !POL.niveau;
    signaler(base, POL_VOL);
    if (premier) POL.alerte = POL.spawn_t = POL_ALERTE_S[POL.niveau];   /* le temps que la victime prévienne la police */
}
/* Le scénario signale Sera (instruction « police N » : les casses de Lucerne) : recherchée au moins au niveau n */
#define POL_AMENDE_SCENE 400   /* amende d'une casse, par niveau de recherche */
void police_scenario(int n) {
    if (E.w.carte || CR.on) return;
    const int avant = POL.niveau;
    POL.clan = pol_clan(E.sera.x, E.sera.z); POL.amende_eff += POL_AMENDE_SCENE * n;
    signaler(n, POL_EFFRACTION);
    if (POL.niveau < n) POL.niveau = n;
    if (!avant) { POL.alerte = POL.spawn_t = POL_ALERTE_S[POL.niveau]; hud_toast(POL.clan ? "La milice vous cherche" : "La police vous cherche"); }
}
void police_effraction(int amende, int clan) {
    if (E.w.carte) return;
    POL.amende_eff += amende; POL.clan = clan;
    signaler(1, POL_EFFRACTION);
    hud_toast(clan ? "Effraction signalée : la milice vous cherche" : "Effraction signalée : la police vous cherche");
}
/* Recherche close : semée, ou après l'arrestation ; les poursuivants redeviennent du trafic (game.c), les agents s'en vont */
static void cloture(void) {
    POL.niveau = 0; POL.motif = 0; POL.amende_eff = 0; POL.semer = 0; POL.vue = 0; POL.prise_t = 0; POL.alerte = 0; POL.repit = 0;
    agents_partir();
}

/* ---------- Arrestation ---------- */
static const agent_t *agent_arr(int i) { return POL.aa[i] >= 0 && POL.ag[POL.aa[i]].on ? &POL.ag[POL.aa[i]] : NULL; }
/* Place d'un agent à côté de Sera (côté s de la caméra : les deux à l'écran), tournée de 0,6 rad à chaque essai si un mur
   la prend */
static void place_flanc(float s, float *x, float *z) {
    const float r = POL_FLANC / E.w.ech;
    for (int e = 0; e < 5; e++) {
        const float a = E.yaw + (e & 1 ? 1 : -1) * 0.6f * ((e + 1) / 2);
        *x = E.sera.x + cosf(a) * s * r; *z = E.sera.z - sinf(a) * s * r;
        if (sol_pieton(*x, *z)) return;
    }
    *x = E.sera.x + cosf(E.yaw) * s * r; *z = E.sera.z - sinf(E.yaw) * s * r;
}
void police_arreter(int motif, int amende, int clan) {
    if (POL.arr || E.w.carte || CR.on || BT.on) return;
    POL.motif |= motif;
    if (motif & POL_EFFRACTION) { POL.amende_eff += amende; POL.clan = clan; }
    E.alarme.on = 0;
    /* Son véhicule : elle en sort ; volé, il sera confisqué (au noir). Après un combat perdu, elle en était sortie : celui
       qu'elle conduisait quand on l'a prise (police_combat) */
    if (E.ride >= 0) POL.conf = E.npcs[E.ride].vole ? E.ride + 1 : 0;
    else if (!POL.vaincue) POL.conf = 0;
    vehicule_sortir();
    E.has_dest = 0; E.dest_bloque = 0; E.talk = -2;
    /* Deux agents : les plus proches de ceux qui sont là, sinon des renforts qui arrivent en courant (près de la voiture de
       police la plus proche, sinon à 5 cases d'elle, de leur côté) */
    POL.aa[0] = POL.aa[1] = -1;
    for (int i = 0; i < 2; i++) {
        const float s = i ? -1.0f : 1.0f;
        int best = -1;
        float bd = 12 / E.w.ech;
        for (int k = 0; k < POL_AGENTS; k++) {
            const float d = hypotf(POL.ag[k].x - E.sera.x, POL.ag[k].z - E.sera.z);
            if (POL.ag[k].on && k != POL.aa[0] && d < bd) { bd = d; best = k; }
        }
        if (best < 0) {
            float x = E.sera.x + (cosf(E.yaw) * s * 5 - sinf(E.yaw) * 2) / E.w.ech, z = E.sera.z + (-sinf(E.yaw) * s * 5 - cosf(E.yaw) * 2) / E.w.ech, cd = 10 / E.w.ech;
            for (int k = 0; k < TRAFFIC_MAX; k++) {
                const traffic_t *t = &E.traffic[k];
                const float d = hypotf(t->x - E.sera.x, t->z - E.sera.z);
                if (t->on && t->poursuite && d < cd) { cd = d; x = t->x - cosf(t->dir) * 0.9f * s; z = t->z + sinf(t->dir) * 0.9f * s; }
            }
            float px, pz;
            if (!place_libre(x, z, 3, &px, &pz)) { px = x; pz = z; }
            best = agent_poser(pol_agent_perso(POL.clan), px, pz, atan2f(E.sera.x - px, E.sera.z - pz), AG_PLACE);
        }
        if (best < 0) continue;
        agent_t *a = &POL.ag[best];
        a->etat = AG_PLACE; a->perdu = 0;
        place_flanc(s, &a->tx, &a->tz);
        POL.aa[i] = (int8_t)best;
    }
    for (int k = 0; k < POL_AGENTS; k++)   /* les autres restent là où ils sont */
        if (POL.ag[k].on && k != POL.aa[0] && k != POL.aa[1]) { POL.ag[k].etat = AG_PLACE; POL.ag[k].tx = POL.ag[k].x; POL.ag[k].tz = POL.ag[k].z; }
    /* Caméra rapprochée, pose de Sera (défense, sinon l'attente) */
    mise_camera(0, E.sera.x, E.sera.z, -10, E.zoom * POL_ZOOM < ZOOM_MAX ? E.zoom * POL_ZOOM : ZOOM_MAX, 1.2f);
    E.pose_anim = has_anim(member_sheet(0), ANIM_DEFEND) ? ANIM_DEFEND : ANIM_IDLE;
    E.pose_t = 1e9f; E.sera.state = 3; E.sera.frame = 0; E.sera.anim = 0;
    POL.arr = ARR_HALTE; POL.arr_t = POL.arr_tt = 0; POL.dehors = 0;
    hud_toast(POL.vaincue ? (POL.clan ? "La milice vous maîtrise" : "Les agents vous maîtrisent") : POL.clan ? "Halte ! Milice !" : "Halte ! Police !");
    son_jouer(SON_ANNULER, 0.5f, 0.8f);
}
/* Répliques de l'agent : motif, confiscation, amende (prise sur la bourse ; elle n'a pas assez : tout) */
static void arr_dialogue(void) {
    const int vol = POL.motif & POL_VOL, eff = POL.motif & POL_EFFRACTION;
    const int cr = credits_combat(0) > POL_AMENDE_MIN ? credits_combat(0) : POL_AMENDE_MIN;
    POL.amende = POL.amende_eff + (vol ? (POL_AMENDE_K + (POL.niveau > 0 ? POL.niveau : 1)) * cr : 0);
    if (POL.amende <= 0) POL.amende = 50;
    POL.paye = E.credits < POL.amende ? E.credits : POL.amende;
    E.credits -= POL.paye;
    int n = 0;
    snprintf(POL.l[n++], sizeof POL.l[0], "%s", vol && eff ? "Halte ! Vol et effraction." : vol ? "Halte ! Ce véhicule est volé." : "Halte ! Effraction signalée.");
    if (POL.conf) snprintf(POL.l[n++], sizeof POL.l[0], "Le véhicule est confisqué.");
    snprintf(POL.l[n++], sizeof POL.l[0], "Amende : %d crédits.", POL.amende);
    if (POL.paye < POL.amende && POL.paye) snprintf(POL.l[n++], sizeof POL.l[0], "Tu n'as que %d crédits ? On prend tout.", POL.paye);
    else if (POL.paye < POL.amende) snprintf(POL.l[n++], sizeof POL.l[0], "Pas un crédit ? Ce sera noté au registre.");
    const char *lines[4] = { POL.l[0], POL.l[1], POL.l[2], POL.l[3] };
    const agent_t *a = agent_arr(0);
    hud_lines(POL.clan ? "MILICE" : "POLICE", lines, n, a ? E.w.persos[a->perso].nom : "PATROUILLE");
}
/* Au noir : véhicule confisqué, agents et poursuivants partis, caméra et Sera rendues */
static void arr_fin(void) {
    if (POL.conf && POL.conf - 1 < E.nnpc && E.npcs[POL.conf - 1].vole) vehicule_retirer(POL.conf - 1);
    POL.conf = 0;
    for (int k = 0; k < POL_AGENTS; k++) POL.ag[k].on = 0;
    for (int k = 0; k < TRAFFIC_MAX; k++) if (E.traffic[k].poursuite) E.traffic[k].on = 0;
    if (MISE.cam_on) { MISE.cam_on = 0; E.yaw = MISE.yaw0; eng_set_zoom(MISE.zoom0); }
    E.pose_t = 0; E.sera.state = 0; E.sera.frame = 0; E.sera.anim = 0;
    cloture();
}
static void fondu(float a, float b, float d) { MISE.fondu_a = a; MISE.fondu_b = b; MISE.fondu_t = 0; MISE.fondu_d = d; }

/* ---------- Enceinte des villes (01/11/2026 : Lucerne) ----------
   ENCE (pak.h ; enceintes de l'émulateur, tirées de towns[].anneau et portes de js/settlements.js) : centre, rayon du mur
   par secteur (Settlements.anneau : le secteur k à l'angle k / A × 2π, de +x vers +z, interpolé entre deux secteurs),
   portes (milieu, direction de l'extérieur), drapeau du passage (passage_halo). Sans ce drapeau, Sera n'a rien à faire
   dedans : à plus de ENC_MARGE case en deçà du mur, hors d'une scène, d'un intérieur, d'un combat, d'un dialogue et d'une
   recherche de la police, les gardes l'interceptent (enceinte_maj) : leurs répliques, fondu au noir, véhicule volé
   confisqué, et elle est posée devant la porte la plus proche, dehors (ENC_DEHORS cases au-delà du mur, comme les gardes
   des zones porte_bloc), tournée vers elle, la caméra derrière ; son propre véhicule la suit. Une sortie de prison dans
   l'enceinte la pose là aussi (arr_maj : ARR_SORTIE). Les zones porte_bloc du scénario gardent les portes ; ceci ferme
   les trous : bords d'une porte (6 cases d'ouverture pour une zone de 2,2 cases de rayon), lac ou mer où le mur s'arrête,
   vaisseau, prison, téléport (enceinteMaj de l'émulateur) */
#define ENC_MARGE 1.0f          /* cases en deçà du mur */
#define ENC_DEHORS 3.5f         /* cases au-delà de la porte (comme les lieux « porte » de js/histoire.js) */
#define ENC_OCT 40              /* en-tête d'une enceinte (pak.h) */
enum { ENC_AUCUNE, ENC_HALTE, ENC_FONDU, ENC_RETOUR };
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { const uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }
typedef struct { float cx, cz; int flag, na, np; const uint8_t *nom, *rad, *portes; } ence_t;
/* ENCE : nombre u32, puis chaque enceinte (ENC_OCT octets, rayons f32 × A, portes 16 octets × np) ; mal formée : ignorée */
void police_load(void) {
    uint32_t sz = 0, o = 4;
    const uint8_t *p = pak_section("ENCE", &sz);
    E.w.ence = NULL; E.w.nence = 0;
    if (!p || sz < 4) return;
    for (uint32_t k = 0, n = rd32(p); k < n && k < 8 && o + ENC_OCT <= sz; k++) {
        const uint32_t a = rd16(p + o + 10), np = rd16(p + o + 12), t = ENC_OCT + a * 4 + np * 16;
        if (a < 3 || o + t > sz) break;
        o += t; E.w.nence = (int)k + 1;
    }
    if (E.w.nence) E.w.ence = p + 4;
}
static int ence_lire(int k, ence_t *e) {
    const uint8_t *p = E.w.ence;
    for (int i = 0; p && i < E.w.nence; i++) {
        e->cx = rdf(p); e->cz = rdf(p + 4); e->flag = rd16(p + 8); e->na = rd16(p + 10); e->np = rd16(p + 12);
        e->nom = p + 16; e->rad = p + ENC_OCT; e->portes = e->rad + e->na * 4;
        if (i == k) return 1;
        p = e->portes + e->np * 16;
    }
    return 0;
}
/* Rayon du mur dans la direction de (x, z) vu du centre (Settlements.anneauR) */
static float ence_rayon(const ence_t *e, float x, float z) {
    float th = atan2f(z - e->cz, x - e->cx);
    if (th < 0) th += 2 * PI_F;
    const float u = th / (2 * PI_F) * e->na, f = u - floorf(u);
    const int k0 = (int)floorf(u) % e->na, k1 = (k0 + 1) % e->na;
    return rdf(e->rad + k0 * 4) * (1 - f) + rdf(e->rad + k1 * 4) * f;
}
int enceinte_dedans(float x, float z) {
    ence_t e;
    if (E.w.carte) return 0;
    for (int k = 0; k < E.w.nence && ence_lire(k, &e); k++) if (hypotf(x - e.cx, z - e.cz) < ence_rayon(&e, x, z)) return 1;
    return 0;
}
int enceinte_interdite(float x, float z) {
    ence_t e;
    if (E.w.carte) return -1;
    for (int k = 0; k < E.w.nence && ence_lire(k, &e); k++) {
        if (e.flag >= SC_MAX_FLAGS || ((E.flags[e.flag >> 3] >> (e.flag & 7)) & 1)) continue;   /* ouverte, ou passage payé */
        if (hypotf(x - e.cx, z - e.cz) < ence_rayon(&e, x, z) - ENC_MARGE) return k;
    }
    return -1;
}
/* Sera (et son véhicule) devant la porte de l'enceinte k la plus proche, dehors, tournée vers elle */
static void enceinte_poser(int k) {
    ence_t e;
    if (!ence_lire(k, &e) || !e.np) return;
    int best = 0;
    float bd = 1e9f;
    for (int i = 0; i < e.np; i++) {
        const float d = hypotf(rdf(e.portes + i * 16) - E.sera.x, rdf(e.portes + i * 16 + 4) - E.sera.z);
        if (d < bd) { bd = d; best = i; }
    }
    const uint8_t *g = e.portes + best * 16;
    const float ox = rdf(g + 8), oz = rdf(g + 12);
    E.sera_y_ok = 0;
    sera_placer(rdf(g) + ox * ENC_DEHORS, rdf(g + 4) + oz * ENC_DEHORS, 6);
    E.sera.dir = E.yaw = atan2f(-ox, -oz);
    if (E.ride >= 0) { E.npcs[E.ride].x = E.sera.x; E.npcs[E.ride].z = E.sera.z; E.npcs[E.ride].dir = E.sera.dir; E.ride_v = 0; }
    E.has_dest = 0; E.stream_hold = 1;
}
/* Les gardes l'interceptent : elle s'arrête (sort d'un véhicule volé), ils parlent */
static void enceinte_intercepter(int k) {
    POL.enc = ENC_HALTE; POL.enc_t = 0; POL.enc_k = k;
    POL.enc_conf = E.ride >= 0 && E.npcs[E.ride].vole ? E.ride + 1 : 0;
    pilote_arreter(NULL);   /* sinon il la ramènerait au mur */
    if (POL.enc_conf) vehicule_sortir();
    else if (E.ride >= 0) E.ride_v = 0;
    E.has_dest = 0; E.dest_bloque = 0; E.talk = -2;
    static const char *const L0 = "Halte ! On n'entre pas dans Lucerne sans avoir payé le passage à la grande porte.",
        *const L1 = "Et ce véhicule est signalé volé. On le garde, il retourne à son propriétaire.",
        *const L2 = "On te raccompagne dehors. La prochaine fois, passe par la grande porte avec tes deux cristaux.";
    const char *lines[3] = { L0, L1, L2 };
    if (!POL.enc_conf) lines[1] = L2;
    const int p = pol_agent_perso(0);
    hud_lines("GARDE", lines, POL.enc_conf ? 3 : 2, p >= 0 ? E.w.persos[p].nom : "PATROUILLE");
    son_jouer(SON_ANNULER, 0.5f, 0.8f);
}
/* Interception : les répliques, le noir, la porte ; le noir se lève */
static void enceinte_maj(float dt) {
    POL.enc_t += dt;
    E.has_dest = 0; E.talk = -2; E.porte = -1;
    if (E.ride >= 0) E.ride_v = 0;
    switch (POL.enc) {
    case ENC_HALTE:
        if (POL.enc_t > 0.3f && !hud_in_dialog()) { fondu(MISE.fondu, 1, 0.6f); POL.enc = ENC_FONDU; POL.enc_t = 0; }
        break;
    case ENC_FONDU:
        if (POL.enc_t < 0.7f) break;
        if (POL.enc_conf && POL.enc_conf - 1 < E.nnpc && E.npcs[POL.enc_conf - 1].vole) vehicule_retirer(POL.enc_conf - 1);
        POL.enc_conf = 0;
        enceinte_poser(POL.enc_k);
        fondu(1, 0, 0.6f); POL.enc = ENC_RETOUR; POL.enc_t = 0;
        hud_toast("Reconduite devant la porte de la ville");
        break;
    default:   /* ENC_RETOUR */
        if (POL.enc_t >= 0.6f) POL.enc = ENC_AUCUNE;
        break;
    }
}

/* ---------- Combat contre les forces de l'ordre (01/11/2026) ----------
   Quand la police prend Sera, un combat de groupe remplace l'arrestation immédiate : le niveau de recherche + 1 agents
   (2 à 4), au niveau de l'équipe + POL_COMBAT_NIV × le niveau de recherche (2, 4 ou 6 au-dessus), sur la fiche de la
   Patrouille de l'Ordre (PATROUILLE de ENNE, un garde de l'histoire ; sinon l'ennemi ordinaire du niveau le plus proche),
   PV et ATQ × POL_FORCE selon le niveau de l'équipe (tools/equilibrage/police.js : environ deux victoires sur trois en
   recherche 2 avec l'équipement du palier du moment, presque aucune sans équipement), dessinés en agents (ceux qui l'ont
   prise d'abord, puis d'autres : GARDE, OFFICIÈRE… de l'Ordre ; milice d'un village : VIGIE, FACTIONNAIRE…). Elle sort de
   son véhicule avant le combat. Pas de fuite. Perdu (battle.c : ni écran de défaite ni RÉESSAYER) : la scène de
   l'arrestation, puis la prison, le véhicule volé confisqué. Gagné : la récompense d'un combat de ce niveau ; la
   recherche baisse d'un niveau (à 0 : « Vous leur avez échappé ») ; sinon les agents et les voitures de police se
   retirent et elle a POL_REPIT s sans renfort ni prise pour semer les autres (policeCombat de l'émulateur) */
#define POL_COMBAT_NIV 2
#define POL_REPIT 20.0f
static const uint8_t POL_FORCE_N[13] = { 1, 8, 12, 16, 20, 25, 30, 40, 50, 60, 75, 90, 100 };   /* niveau de l'équipe */
static const float POL_FORCE[13] = { 1, 1, 1, 1, 1.02f, 1.05f, 1.07f, 1.08f, 1.12f, 1.12f, 1.18f, 1.2f, 1.22f };
static float pol_force(int L) {
    for (int i = 1; i < 13; i++)
        if (L <= POL_FORCE_N[i]) { const float t = (float)(L - POL_FORCE_N[i - 1]) / (POL_FORCE_N[i] - POL_FORCE_N[i - 1]); return POL_FORCE[i - 1] + (POL_FORCE[i] - POL_FORCE[i - 1]) * (t < 0 ? 0 : t); }
    return POL_FORCE[12];
}
/* Fiche de combat des agents : la Patrouille de l'Ordre, sinon l'ennemi ordinaire (ni sorts ni PV d'élite : enc_ok de game.c)
   du niveau le plus proche ; -1 : aucun (polFiche de l'émulateur) */
static int pol_fiche(int lvl) {
    int best = -1, bd = 1 << 30;
    for (int k = 0; k < E.w.nenemies; k++) {
        const enemy_t *en = &E.w.enemies[k];
        const char *nm = E.w.persos[en->perso].nom;
        if (nm && !strcmp(nm, "PATROUILLE")) return k;
        if (en->sorts || en->hp > 600 * (en->lvl + 6)) continue;
        const int d = en->lvl > lvl ? en->lvl - lvl : lvl - en->lvl;
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}
static void police_combat(void) {
    const int n = POL.niveau + 1 < 2 ? 2 : POL.niveau + 1 > BT_EMAX ? BT_EMAX : POL.niveau + 1;
    const int L = E.level + POL_COMBAT_NIV * POL.niveau > NIV_MAX ? NIV_MAX : E.level + POL_COMBAT_NIV * POL.niveau;
    const int f = pol_fiche(L);
    if (f < 0) { police_arreter(0, 0, POL.clan); return; }   /* aucun ennemi dans le paquet : l'arrestation, comme avant */
    /* Les agents du combat : ceux qui l'ont prise (les plus proches), puis d'autres de la bibliothèque */
    int ks[BT_EMAX], pers[BT_EMAX], var[BT_EMAX], pris[POL_AGENTS] = { 0 };
    for (int i = 0; i < n; i++) {
        int best = -1;
        float bd = POL_PROCHE / E.w.ech;
        for (int k = 0; k < POL_AGENTS; k++) {
            const float d = hypotf(POL.ag[k].x - E.sera.x, POL.ag[k].z - E.sera.z);
            if (POL.ag[k].on && !pris[k] && d < bd) { bd = d; best = k; }
        }
        ks[i] = f;
        if (best >= 0) { pris[best] = 1; pers[i] = POL.ag[best].perso; var[i] = POL.ag[best].var; }
        else { pers[i] = pol_agent_perso(POL.clan); var[i] = pers[i] >= 0 ? pvar_choix(pers[i], (unsigned)(prand() * 2048)) : 0; }
    }
    /* Son véhicule : elle en sort ; volé, il sera confisqué si elle perd */
    POL.conf = E.ride >= 0 && E.npcs[E.ride].vole ? E.ride + 1 : 0;
    vehicule_sortir();
    E.has_dest = 0; E.dest_bloque = 0; E.talk = -2; POL.prise_t = 0;
    battle_start_groupe(ks, n, L, 0, GROUPE_PARTS, NULL);
    if (!BT.on) { police_arreter(0, 0, POL.clan); return; }
    const float k = pol_force(E.level);
    for (int i = 0; i < BT.nen; i++) {
        bt_en_t *e = &BT.en[i];
        if (pers[i] >= 0) { e->perso = pers[i]; e->var = var[i]; snprintf(e->name, sizeof e->name, "%s", E.w.persos[pers[i]].nom); }
        e->hp = e->max = (int)lroundf(e->hp * k); e->atk = (int)lroundf(e->atk * k);
    }
    BT.police = 1;
    hud_toast(POL.clan ? "La milice vous encercle !" : "Les agents vous encerclent !");
}
void police_apres_combat(int won) {
    if (!won) {   /* maîtrisée : la scène de l'arrestation, puis la prison */
        POL.vaincue = 1;
        police_arreter(0, 0, POL.clan);
        POL.vaincue = 0;
        return;
    }
    POL.conf = 0;
    for (int k = 0; k < POL_AGENTS; k++) POL.ag[k].on = 0;   /* à terre, ou ils se replient */
    for (int k = 0; k < TRAFFIC_MAX; k++) if (E.traffic[k].on && E.traffic[k].poursuite) E.traffic[k].on = 0;
    if (--POL.niveau <= 0) { cloture(); hud_toast(POL.clan ? "Vous avez échappé à la milice" : "Vous avez échappé à la police"); return; }
    POL.repit = POL_REPIT; POL.semer = 0; POL.prise_t = 0;
    POL.vu_t = POL.t; POL.lx = E.sera.x; POL.lz = E.sera.z;
    hud_toast("Agents à terre : filez avant les renforts !");
}

/* ---------- Prison (01/11/2026) ----------
   Au noir, après les répliques de l'arrestation : Sera est emmenée dans la prison la plus proche (int_prison : Caserne de
   l'Ordre ou Poste de garde, de l'autorité de la milice ou de l'Ordre d'abord), dans sa cellule (int_cellule). Le noir se
   lève sur la cellule, carton du temps (le lendemain matin, 7 h 30 au cycle ; le surlendemain si l'amende n'a pas été
   payée en entier : le reste en corvées), puis un garde entre, vient jusqu'à elle et parle (motif, niveau, amende), fondu
   au noir, et Sera ressort devant la porte de la prison, libre, sans recherche ; le zoom du lieu reprend. Sans prison
   (aucune sur la carte, ou trop grande pour la mémoire de la montre), elle est relâchée sur place comme avant. Comme la
   prison de l'émulateur (prisonEntrer, prisonGarde…). */
#define POL_MATIN (7.5f / 24)   /* heure de la sortie de cellule */
/* Répliques du garde : la nuit passée, le motif (et le niveau), l'amende, la sortie */
static void prison_lignes(int motif, int niveau) {
    const int vol = motif & POL_VOL, eff = motif & POL_EFFRACTION;
    snprintf(POL.lp[0], sizeof POL.lp[0], "%s", POL.nuits > 1 ? "Debout. Deux nuits au frais, ça calme." : "Debout. La nuit est finie.");
    snprintf(POL.lp[1], sizeof POL.lp[1], "%s", vol && eff ? "Vol et effraction : la prochaine fois, ce sera le tribunal."
             : vol && niveau >= 2 ? "Plusieurs patrouilles pour te coincer. On se souviendra de toi."
             : vol ? "Le véhicule retourne à son propriétaire."
             : POL.clan ? "Les portes du village ne sont pas à toi." : "Les serrures de la ville ne sont pas à toi.");
    snprintf(POL.lp[2], sizeof POL.lp[2], "%s", POL.paye >= POL.amende ? "L'amende est réglée, ton nom est rayé du registre."
             : "Le reste de l'amende, tu l'as payé en corvées. La cour n'a jamais été si propre.");
    snprintf(POL.lp[3], sizeof POL.lp[3], "%s", POL.dehors ? "On te raccompagne jusqu'à la porte de la ville. Sans passage payé, tu n'as rien à faire ici."
             : "La sortie est par là. Ne reviens pas.");
}
/* Le garde entre : à l'endroit de la cellule le plus proche de l'entrée du niveau (sa porte, le plus souvent) */
static void prison_garde(void) {
    const float f = E.in.f > 0 ? E.in.f : 1;
    float bx = E.sera.x, bz = E.sera.z, bd = 1e9f;
    for (float x = POL.cel[0] + 0.15f * f; x < POL.cel[2]; x += 0.2f * f)
        for (float z = POL.cel[1] + 0.15f * f; z < POL.cel[3]; z += 0.2f * f) {
            const float d = hypotf(x - POL.cel[4], z - POL.cel[5]);
            if (d < bd && hypotf(x - E.sera.x, z - E.sera.z) > 0.9f * f && int_libre(x, z)) { bd = d; bx = x; bz = z; }
        }
    POL.garde = int_pnj_ajouter(pol_agent_perso(POL.clan), bx, bz, atan2f(E.sera.x - bx, E.sera.z - bz));
}
/* Un pas du garde vers Sera (1,1 m/s à l'échelle humaine), arrêté à 0,8 m d'elle ; 1 : arrivé */
static int garde_pas(float dt) {
    if (POL.garde < 0 || POL.garde >= E.nnpc) return 1;
    npc_t *n = &E.npcs[POL.garde];
    const float f = E.in.f > 0 ? E.in.f : 1, dx = E.sera.x - n->x, dz = E.sera.z - n->z, d = hypotf(dx, dz), reste = d - 0.8f * f;
    n->dir = atan2f(dx, dz);
    if (reste <= 0.02f) { n->walking = 0; return 1; }
    const float sp = fminf(reste, 1.1f * f * dt);
    n->x += dx / d * sp; n->z += dz / d * sp; n->walking = 1; n->anim += dt;
    if (n->anim > 0.16f) { n->anim = 0; n->frame++; }
    return 0;
}

static void arr_maj(float dt) {
    POL.arr_t += dt; POL.arr_tt += dt;
    E.has_dest = 0; E.talk = -2; E.porte = -1;
    const agent_t *a0 = agent_arr(0), *a1 = agent_arr(1);
    /* Sera regarde l'agent qui lui parle ; sa défense jouée en 0,5 s, puis tenue */
    if (POL.arr < ARR_FONDU) {
        if (a0) E.sera.dir = atan2f(a0->x - E.sera.x, a0->z - E.sera.z);
        E.pose_t = 1e9f;
        const int ps = member_sheet(0);
        if (ps >= 0 && E.pose_anim == ANIM_DEFEND) {
            const int n = E.w.sheets[anim_sheet(ps, ANIM_DEFEND)].frames, f = (int)(POL.arr_tt / 0.5f * n);
            E.sera.frame = f < n - 1 ? f : n - 1; E.sera.anim = 0;
        }
    }
    switch (POL.arr) {
    case ARR_HALTE: {   /* les agents arrivent ; au bout de 4 s, ceux qui traînent sont à leur place */
        const int ok0 = !a0 || hypotf(a0->x - a0->tx, a0->z - a0->tz) < 0.05f, ok1 = !a1 || hypotf(a1->x - a1->tx, a1->z - a1->tz) < 0.05f;
        if (!(ok0 && ok1) && POL.arr_t < 4) break;
        for (int i = 0; i < 2; i++) if (POL.aa[i] >= 0) { agent_t *a = &POL.ag[POL.aa[i]]; a->x = a->tx; a->z = a->tz; a->dir = atan2f(E.sera.x - a->x, E.sera.z - a->z); }
        POL.arr = ARR_ENCADREE; POL.arr_t = 0;
        MISE.sec_f = 1.5f; MISE.sec_t = 0.35f;
        son_jouer(SON_CHOC, 0.45f, 0.7f);
        const int vol = POL.motif & POL_VOL, eff = POL.motif & POL_EFFRACTION;
        hud_title("ARRÊTÉE", vol && eff ? "Vol et effraction" : vol ? "Vol de véhicule" : "Effraction");
        break;
    }
    case ARR_ENCADREE:   /* le carton, puis l'agent parle */
        if (POL.arr_t >= HUD_TITLE_S - 0.4f) { arr_dialogue(); POL.arr = ARR_DIALOGUE; POL.arr_t = 0; }
        break;
    case ARR_DIALOGUE:
        if (!hud_in_dialog()) { fondu(MISE.fondu, 1, 0.6f); POL.arr = ARR_FONDU; POL.arr_t = 0; }
        break;
    case ARR_FONDU: {   /* au noir : la prison, sinon relâchée sur place */
        if (POL.arr_t < 0.7f) break;
        const int motif = POL.motif, niveau = POL.niveau;
        arr_fin();
        POL.prison = int_prison(E.sera.x, E.sera.z, POL.clan);
        if (POL.prison >= 0 && int_cellule(POL.prison, POL.cel)) {
            POL.nuits = POL.paye < POL.amende ? 2 : 1;
            /* prison dans une enceinte où elle n'a pas le passage (Lucerne avant passage_halo) : relâchée devant sa porte */
            POL.dehors = enceinte_interdite(E.w.bats[POL.prison].cx, E.w.bats[POL.prison].cz) >= 0;
            prison_lignes(motif, niveau);
            if (E.cycle != 3) E.heure = POL_MATIN;   /* l'horloge du cycle, comme l'instruction « heure » (pas l'heure réelle) */
            hud_title(POL.nuits > 1 ? "LE SURLENDEMAIN MATIN" : "LE LENDEMAIN MATIN", E.w.bats[POL.prison].nom);
            /* une nuit en cellule (02/11/2026) : toute la bande se réveille PV et PM pleins, ceux qui étaient à terre debout
               (la section « Police » de l'émulateur, à l'identique) */
            for (int i = 0; i < NMEMB; i++) { PARTY[i].hp = PARTY[i].max; PARTY[i].mp = PARTY[i].maxmp; }
            POL.garde = -1;
            fondu(1, 0, 0.8f); POL.arr = ARR_CELLULE; POL.arr_t = 0;
            break;
        }
        POL.prison = -1;
        fondu(1, 0, 0.6f); POL.arr = ARR_RETOUR; POL.arr_t = 0;
        break;
    }
    case ARR_CELLULE:   /* seule dans la cellule le temps du carton, puis le garde entre */
        if (POL.arr_t < HUD_TITLE_S) break;
        prison_garde();
        POL.arr = ARR_GARDE; POL.arr_t = 0;
        break;
    case ARR_GARDE:   /* il vient jusqu'à elle, elle le regarde ; puis il parle */
        if (POL.garde >= 0 && POL.garde < E.nnpc) E.sera.dir = atan2f(E.npcs[POL.garde].x - E.sera.x, E.npcs[POL.garde].z - E.sera.z);
        if (garde_pas(dt) || POL.arr_t > 4) {
            const char *lines[4] = { POL.lp[0], POL.lp[1], POL.lp[2], POL.lp[3] };
            hud_lines(POL.clan ? "MILICE" : "POLICE", lines, 4, POL.garde >= 0 && POL.garde < E.nnpc ? E.w.persos[E.npcs[POL.garde].perso].nom : "PATROUILLE");
            POL.arr = ARR_PAROLES; POL.arr_t = 0;
        }
        break;
    case ARR_PAROLES:
        if (!hud_in_dialog()) { fondu(MISE.fondu, 1, 0.6f); POL.arr = ARR_SORTIE; POL.arr_t = 0; }
        break;
    case ARR_SORTIE: {   /* au noir : dans la rue, devant la porte de la prison (sans passage : devant celle de la ville) */
        if (POL.arr_t < 0.7f) break;
        int_liberer();
        const int k = enceinte_interdite(E.sera.x, E.sera.z);
        if (k >= 0) enceinte_poser(k);
        POL.garde = -1; E.zoom_lieu = 0; E.has_dest = 0;   /* le zoom du lieu reprend */
        fondu(1, 0, 0.6f); POL.arr = ARR_RETOUR; POL.arr_t = 0;
        hud_toast(k >= 0 ? "Libre, devant la porte de la ville" : "Libre");
        break;
    }
    default:   /* ARR_RETOUR : le noir se lève */
        if (POL.arr_t >= 0.6f) POL.arr = ARR_AUCUNE;
        break;
    }
}

/* ---------- À chaque image ---------- */
void police_reset(void) {
    for (int k = 0; k < TRAFFIC_MAX; k++) if (E.traffic[k].poursuite) E.traffic[k].on = 0;
    if (POL.arr || POL.enc) {
        if (MISE.cam_on) { MISE.cam_on = 0; E.yaw = MISE.yaw0; eng_set_zoom(MISE.zoom0); }
        E.pose_t = 0; MISE.fondu = 0; MISE.fondu_t = MISE.fondu_d = 0;
    }
    memset(&POL, 0, sizeof POL);
    POL.aa[0] = POL.aa[1] = -1;
}
void police_update(float dt) {
    POL.t += dt;
    if (E.w.carte || CR.on) {   /* planète seulement, hors course */
        int agents = 0;
        for (int k = 0; k < POL_AGENTS; k++) agents |= POL.ag[k].on;
        if (POL.niveau || POL.arr || POL.enc || agents) police_reset();
        return;
    }
    if (POL.arr) { agents_maj(dt, 1); arr_maj(dt); sirene(); return; }
    if (POL.enc) { enceinte_maj(dt); return; }   /* interception à l'enceinte en cours */
    if (POL.repit > 0) POL.repit -= dt;          /* combat gagné : ni renfort ni prise */
    const int signale = E.ride >= 0 ? E.npcs[E.ride].vole : 0;   /* véhicule volé conduit : niveau qu'il donne */
    int vue = 0, proche = 0, nvoit = 0;
    /* Voitures de police du trafic : les poursuivantes, et les autres qui repèrent le véhicule volé (ou Sera recherchée) ;
       une voiture qui la voit la poursuit s'il en manque */
    for (int k = 0; k < TRAFFIC_MAX; k++) {
        traffic_t *t = &E.traffic[k];
        if (!t->on || E.w.veh_service[t->perso] != 1) continue;
        nvoit += t->poursuite;
        if (E.in.on || (!t->poursuite && !signale && !POL.niveau)) continue;
        const float d = hypotf(t->x - E.sera.x, t->z - E.sera.z);
        if (t->poursuite && d < POL_PROCHE / E.w.ech) proche = 1;
        if (d >= POL_VUE / E.w.ech || !vue_libre(t->x, t->z, E.sera.x, E.sera.z)) continue;
        vue = 1;
        if (t->poursuite) continue;
        if (!POL.niveau) { POL.niveau = signale; POL.motif |= POL_VOL; POL.clan = 0; POL.semer = 0; hud_toast("Véhicule volé repéré !"); }
        if (nvoit < POL.niveau && !POL.clan && POL.repit <= 0) {   /* en liaison d'un réseau à l'autre (POUR_RALLIE) : il la finit en poursuivant */
            t->poursuite = 1; t->urgence = 1; if (t->mode != POUR_RALLIE) t->mode = POUR_RESEAU; t->demi = 1; nvoit++;
        }
    }
    if (!POL.niveau) {   /* pas recherchée : des agents qui s'en vont, peut-être ; dans une enceinte sans le passage : les gardes */
        agents_maj(dt, 0); POL.vue = 0;
        if (!script_running() && !E.ui_block && !hud_in_dialog() && !hud_in_choice() && !E.in.on && !mise_bloque()) {
            const int k = enceinte_interdite(E.sera.x, E.sera.z);
            if (k >= 0) enceinte_intercepter(k);
        }
        return;
    }
    for (int k = 0; k < POL_AGENTS && !E.in.on; k++) {
        const agent_t *a = &POL.ag[k];
        if (!a->on || a->etat != AG_CHASSE) continue;
        const float d = hypotf(a->x - E.sera.x, a->z - E.sera.z);
        if (d < POL_PROCHE / E.w.ech) proche = 1;
        if (d < POL_VUE_PIED / E.w.ech && vue_libre(a->x, a->z, E.sera.x, E.sera.z)) vue = 1;
    }
    /* Vol signalé : la police ne cherche pas encore ; au bout du délai, elle sait où est le véhicule (la victime l'a signalé) */
    if (POL.alerte > 0 && (POL.alerte -= dt) <= 0) { POL.alerte = 0; POL.vu_t = POL.t; POL.lx = E.sera.x; POL.lz = E.sera.z; }
    /* Vue : la cible des poursuivants ; personne ne la voit ni n'est proche : le temps à tenir */
    POL.vue = vue;
    if (vue) { POL.vu_t = POL.t; POL.lx = E.sera.x; POL.lz = E.sera.z; }
    const int partie = E.in.on || hypotf(E.sera.x - POL.lx, E.sera.z - POL.lz) > POL_PROCHE / E.w.ech;   /* loin du dernier point connu */
    POL.semer = vue || proche || !partie || POL.alerte > 0 ? 0 : POL.semer + dt;
    if (POL.semer >= police_semer_s()) {
        hud_toast(POL.clan ? "Vous avez semé la milice" : "Vous avez semé la police");
        cloture();
        return;
    }
    /* Renforts : une voiture de police tant qu'il en manque, sinon (milice, aucune dans la bibliothèque) un agent à pied */
    POL.spawn_t -= dt;
    if (POL.spawn_t <= 0 && POL.repit <= 0) {
        POL.spawn_t = POL_SPAWN_S;
        const int v = POL.clan ? -1 : pol_voiture();
        if (v >= 0) { if (nvoit < POL.niveau) traffic_poursuivant(v); }
        else if (agents_chasse() < POL.niveau) renfort_pied();
    }
    agents_maj(dt, POL.t - POL.vu_t < POL_SUIVI);
    /* Prise : un agent qui la touche (à pied, ou son véhicule presque arrêté), une voiture contre son véhicule immobilisé
       (pas un vaisseau en vol) : le combat contre les agents ; jamais pendant une scène, un menu, un dialogue ni le répit */
    if (!script_running() && !E.ui_block && !hud_in_dialog() && !E.in.on && POL.repit <= 0) {
        const int lent = E.ride < 0 || (fabsf(E.ride_v) < POL_PRISE_V && !(ride_type() == VEH_VAISSEAU && E.ride_alt > 0.1f));
        int prise = 0, contact = 0;
        for (int k = 0; k < POL_AGENTS && lent; k++)
            if (POL.ag[k].on && POL.ag[k].etat == AG_CHASSE && hypotf(POL.ag[k].x - E.sera.x, POL.ag[k].z - E.sera.z) < POL_PRISE / E.w.ech) prise = 1;
        for (int k = 0; k < TRAFFIC_MAX && E.ride >= 0 && lent; k++)
            if (E.traffic[k].on && E.traffic[k].poursuite && hypotf(E.traffic[k].x - E.sera.x, E.traffic[k].z - E.sera.z) < POL_PRISE_VEH / E.w.ech) contact = 1;
        POL.prise_t = contact ? POL.prise_t + dt : 0;
        if (prise || POL.prise_t > POL_PRISE_S) { police_combat(); return; }
    }
    sirene();
}
