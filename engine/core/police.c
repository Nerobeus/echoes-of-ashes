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
 * Arrestation : un agent qui la touche (à pied, ou son véhicule presque arrêté), une voiture de police contre son
 * véhicule presque arrêté (POL_PRISE_S s), ou la fin d'une alarme d'effraction non semée (interieur.c). Scène
 * (police_arreter, arr_maj) : Sera s'arrête (sortie de son véhicule, confisqué s'il est volé), deux agents arrivent en
 * courant et l'encadrent, regards, caméra rapprochée (MISE), secousse, carton « ARRÊTÉE », répliques de l'agent (amende :
 * celle de l'alarme pour une effraction, POL_AMENDE_VOL + POL_AMENDE_NIV × le niveau pour un vol), fondu au noir. Pose
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
static const float POL_SEMER_S[4] = { 0, 8, 12, 16 };   /* s à tenir hors de vue, par niveau */
#define POL_PIED0 12.0f         /* cases : agents à pied apparus entre POL_PIED0 et POL_PIED1, hors de vue */
#define POL_PIED1 18.0f
#define POL_AG_V 4.4f           /* m/s : agents à la course (Sera court à 5,2) */
#define POL_AG_LOIN 24.0f       /* cases : un agent plus loin abandonne */
#define POL_AG_PERDU 8.0f       /* s au dernier point connu sans la voir : il s'en va */
#define POL_PRISE 0.7f          /* cases : un agent la touche */
#define POL_PRISE_VEH 1.9f      /* cases : une voiture de police contre son véhicule… */
#define POL_PRISE_V 0.8f        /* … presque arrêté (cases/s)… */
#define POL_PRISE_S 0.6f        /* … tant de temps */
#define POL_CLAN_R 40.0f        /* cases : bâtiment visitable cherché pour savoir si l'on est dans un village */
#define POL_FLANC 0.9f          /* cases : les deux agents se tiennent de part et d'autre de Sera */
#define POL_ZOOM 1.7f           /* caméra rapprochée : zoom du moment × POL_ZOOM (ZOOM_MAX au plus) */
#define POL_AMENDE_VOL 200      /* amende d'un vol : POL_AMENDE_VOL + POL_AMENDE_NIV × le niveau */
#define POL_AMENDE_NIV 100

float police_semer_s(void) { return POL_SEMER_S[POL.niveau < 0 ? 0 : POL.niveau > 3 ? 3 : POL.niveau]; }
int police_bloque(void) { return POL.arr != ARR_AUCUNE; }
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
    int c[MAX_PERSOS], n = 0;
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
    int c[MAX_PERSOS], n = 0, best = 0;
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
void police_vol(int npc) {
    if (E.w.carte || CR.on || npc < 0 || npc >= E.nnpc) return;
    const int sv = E.w.veh_service[E.npcs[npc].perso], base = sv == 1 || sv == 4 ? 2 : 1;   /* police, armée : 2 */
    E.npcs[npc].vole = (uint8_t)base;
    POL.clan = pol_clan(E.sera.x, E.sera.z);
    signaler(base, POL_VOL);
}
void police_effraction(int amende, int clan) {
    if (E.w.carte) return;
    POL.amende_eff += amende; POL.clan = clan;
    signaler(1, POL_EFFRACTION);
    hud_toast(clan ? "Effraction signalée : la milice vous cherche" : "Effraction signalée : la police vous cherche");
}
/* Recherche close : semée, ou après l'arrestation ; les poursuivants redeviennent du trafic (game.c), les agents s'en vont */
static void cloture(void) {
    POL.niveau = 0; POL.motif = 0; POL.amende_eff = 0; POL.semer = 0; POL.vue = 0; POL.prise_t = 0;
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
    /* Son véhicule : elle en sort ; volé, il sera confisqué (au noir) */
    POL.conf = E.ride >= 0 && E.npcs[E.ride].vole ? E.ride + 1 : 0;
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
    POL.arr = ARR_HALTE; POL.arr_t = POL.arr_tt = 0;
    hud_toast(POL.clan ? "Halte ! Milice !" : "Halte ! Police !");
    son_jouer(SON_ANNULER, 0.5f, 0.8f);
}
/* Répliques de l'agent : motif, confiscation, amende (prise sur la bourse ; elle n'a pas assez : tout) */
static void arr_dialogue(void) {
    const int vol = POL.motif & POL_VOL, eff = POL.motif & POL_EFFRACTION;
    POL.amende = POL.amende_eff + (vol ? POL_AMENDE_VOL + POL_AMENDE_NIV * (POL.niveau > 0 ? POL.niveau : 1) : 0);
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
    case ARR_FONDU:
        if (POL.arr_t >= 0.7f) { arr_fin(); fondu(1, 0, 0.6f); POL.arr = ARR_RETOUR; POL.arr_t = 0; }
        break;
    default:   /* ARR_RETOUR : le noir se lève */
        if (POL.arr_t >= 0.6f) POL.arr = ARR_AUCUNE;
        break;
    }
}

/* ---------- À chaque image ---------- */
void police_reset(void) {
    for (int k = 0; k < TRAFFIC_MAX; k++) if (E.traffic[k].poursuite) E.traffic[k].on = 0;
    if (POL.arr) {
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
        if (POL.niveau || POL.arr || agents) police_reset();
        return;
    }
    if (POL.arr) { agents_maj(dt, 1); arr_maj(dt); sirene(); return; }
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
        if (nvoit < POL.niveau && !POL.clan) { t->poursuite = 1; t->urgence = 1; t->mode = POUR_RESEAU; t->demi = 1; nvoit++; }
    }
    if (!POL.niveau) { agents_maj(dt, 0); POL.vue = 0; return; }   /* pas recherchée : des agents qui s'en vont, peut-être */
    for (int k = 0; k < POL_AGENTS && !E.in.on; k++) {
        const agent_t *a = &POL.ag[k];
        if (!a->on || a->etat != AG_CHASSE) continue;
        const float d = hypotf(a->x - E.sera.x, a->z - E.sera.z);
        if (d < POL_PROCHE / E.w.ech) proche = 1;
        if (d < POL_VUE_PIED / E.w.ech && vue_libre(a->x, a->z, E.sera.x, E.sera.z)) vue = 1;
    }
    /* Vue : la cible des poursuivants ; personne ne la voit ni n'est proche : le temps à tenir */
    POL.vue = vue;
    if (vue) { POL.vu_t = POL.t; POL.lx = E.sera.x; POL.lz = E.sera.z; }
    const int partie = E.in.on || hypotf(E.sera.x - POL.lx, E.sera.z - POL.lz) > POL_PROCHE / E.w.ech;   /* loin du dernier point connu */
    POL.semer = vue || proche || !partie ? 0 : POL.semer + dt;
    if (POL.semer >= police_semer_s()) {
        hud_toast(POL.clan ? "Vous avez semé la milice" : "Vous avez semé la police");
        cloture();
        return;
    }
    /* Renforts : une voiture de police tant qu'il en manque, sinon (milice, aucune dans la bibliothèque) un agent à pied */
    POL.spawn_t -= dt;
    if (POL.spawn_t <= 0) {
        POL.spawn_t = POL_SPAWN_S;
        const int v = POL.clan ? -1 : pol_voiture();
        if (v >= 0) { if (nvoit < POL.niveau) traffic_poursuivant(v); }
        else if (agents_chasse() < POL.niveau) renfort_pied();
    }
    agents_maj(dt, POL.t - POL.vu_t < POL_SUIVI);
    /* Prise : un agent qui la touche (à pied, ou son véhicule presque arrêté), une voiture contre son véhicule presque
       arrêté (pas un vaisseau en vol) ; jamais pendant une scène, un menu ou un dialogue */
    if (!script_running() && !E.ui_block && !hud_in_dialog() && !E.in.on) {
        const int lent = E.ride < 0 || (fabsf(E.ride_v) < POL_PRISE_V && !(ride_type() == VEH_VAISSEAU && E.ride_alt > 0.1f));
        int prise = 0, contact = 0;
        for (int k = 0; k < POL_AGENTS && lent; k++)
            if (POL.ag[k].on && POL.ag[k].etat == AG_CHASSE && hypotf(POL.ag[k].x - E.sera.x, POL.ag[k].z - E.sera.z) < POL_PRISE / E.w.ech) prise = 1;
        for (int k = 0; k < TRAFFIC_MAX && E.ride >= 0 && lent; k++)
            if (E.traffic[k].on && E.traffic[k].poursuite && hypotf(E.traffic[k].x - E.sera.x, E.traffic[k].z - E.sera.z) < POL_PRISE_VEH / E.w.ech) contact = 1;
        POL.prise_t = contact ? POL.prise_t + dt : 0;
        if (prise || POL.prise_t > POL_PRISE_S) { police_arreter(0, 0, POL.clan); return; }
    }
    sirene();
}
