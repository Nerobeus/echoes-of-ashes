/*
 * Combat à timeline (CTB), portage de l'émulateur (startBattle, timeline, advance, perform, updateBattle, endBattle)
 *
 * - Chaque unité (les 4 membres de l'équipe, puis les ennemis du groupe) a un compteur ct : la plus petite valeur joue,
 *   puis avance de 100 / VIT × rang (rang 1 pour une action, 0,6 pour la défense). La timeline affiche les 7 prochains tours.
 * - Tour de l'équipe : ATTAQUE, TECHNIQ., OBJETS (potion du sac), DÉFENSE (dégâts divisés par deux jusqu'au tour
 *   suivant), FUITE (rencontres seulement : fuite_chance). L'action part au premier toucher, sur l'ennemi visé (BT.cible :
 *   toucher un ennemi le vise ; abattu, le plus faible) ; une action de zone touche tout le groupe (act_zone).
 * - Tour d'un ennemi : 0,8 s d'attente, puis un de ses sorts ou une attaque de son répertoire (ENAT : morsure, tir,
 *   grenade…), sur un membre ou sur toute l'équipe (enemy_act).
 * - Groupe de 1 à BT_EMAX ennemis (battle_start_groupe) : chacun a ses PV, son tour, sa place et sa chute ; règle du
 *   partage plus bas (GROUPE_PV, GROUPE_ATQ, SBIRE_PART). Patrouilles hostiles : en fin de fichier.
 * - Mêmes valeurs et mêmes durées que l'émulateur (en secondes ici, E.clock) ; le rendu est dans render.c (caméra
 *   fixe sur l'arène, équipe de dos, ennemi de face ou Sentinelle en boîtes), l'interface dans hud.c.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "internal.h"
#include "audio.h"

battle_t BT ENG_FROID;

#define RED 0xFF3B4Eu
#define AMBER 0xFFB020u
#define GREEN 0x7DFF9Au

/* Générateur du combat (Math.random de l'émulateur) */
static uint32_t rng_b = 0x2545F491u;
static float brand(void) { rng_b ^= rng_b << 13; rng_b ^= rng_b >> 17; rng_b ^= rng_b << 5; return (float)(rng_b >> 8) * (1.0f / 16777216.0f); }
static float rnd(float a, float b) { return a + brand() * (b - a); }

/* Unité en jeu : membre de l'équipe (in_team) debout, ou un ennemi du groupe debout */
static int alive(int u) { return u < BT_ENEMY ? in_team(u) && PARTY[u].hp > 0 : u - BT_ENEMY < BT.nen && BT.en[u - BT_ENEMY].hp > 0; }
static int spd_of(int u) { return u < BT_ENEMY ? PARTY[u].spd : BT.en[u - BT_ENEMY].spd; }
int battle_vivants(void) { int n = 0; for (int k = 0; k < BT.nen; k++) n += BT.en[k].hp > 0; return n; }
int battle_enemy_vu(int k) {
    const bt_en_t *e = &BT.en[k];
    return BT.visible && k < BT.nen && (e->hp > 0 || e->mort_t <= 0 || E.clock - e->mort_t < ENNEMI_CHUTE);
}
/* Ennemi visé : celui que l'équipe a touché tant qu'il est debout, sinon le plus faible (le moins de PV) */
static void cible_maj(void) {
    if (BT.cible >= 0 && BT.cible < BT.nen && BT.en[BT.cible].hp > 0) return;
    BT.cible = 0;
    for (int k = 0, best = -1; k < BT.nen; k++)
        if (BT.en[k].hp > 0 && (best < 0 || BT.en[k].hp < best)) { best = BT.en[k].hp; BT.cible = k; }
}
/* Place de l'ennemi k dans l'arène : celle d'un ennemi seul (epos), décalée selon la taille du groupe, à droite (r) et plus
   loin (f) de la caméra (mètres, ÷ ech ; ePos de l'émulateur) : en arc autour de la place du chef, le chef au milieu */
static const float BAT_GRP[BT_EMAX + 1][BT_EMAX][2] = {   /* loin des boutons de l'éventail (à gauche) : en diagonale vers le haut à droite */
    { { 0 } }, { { 0, 0 } },
    { { -0.1f, 0 }, { 0.7f, 0.4f } },
    { { 0.2f, 0.05f }, { -0.35f, -0.4f }, { 0.95f, 0.55f } },
    { { 0.05f, 0 }, { 0.75f, 0.45f }, { -0.45f, -0.45f }, { 1.4f, 0.9f } },
};
/* Groupe de grands ennemis (taille à l'écran au-delà de 1 : mammouth, tricératops, char…) : places écartées d'autant
   (au plus BAT_ECART_MAX fois) et reculées de BAT_RECUL par unité d'écart, pour qu'ils ne couvrent ni l'équipe ni
   leurs voisins (01/11/2026, tailles des sprites rendues cohérentes ; groupeEcart de l'émulateur). Le plus grand du
   groupe, morts compris : les places ne bougent pas pendant le combat */
#define BAT_ECART_MAX 1.8f
#define BAT_RECUL 0.6f
static float groupe_ecart(void) {
    float t = 1;
    for (int k = 0; k < BT.nen && k < BT_EMAX; k++) {
        const int p = BT.en[k].perso;
        if (p >= 0 && p < E.w.npersos && E.w.persos[p].taille > t) t = E.w.persos[p].taille;
    }
    return t > BAT_ECART_MAX ? BAT_ECART_MAX : t;
}
void battle_enemy_pos(int k, float *x, float *z) {
    const int n = BT.nen < 1 ? 1 : BT.nen > BT_EMAX ? BT_EMAX : BT.nen;
    const float g = n > 1 ? groupe_ecart() : 1;
    const float r = BAT_GRP[n][k % BT_EMAX][0] * g, f = BAT_GRP[n][k % BT_EMAX][1] * g + (g - 1) * BAT_RECUL;
    *x = E.w.epos_x + (cosf(BAT_YAW) * r + sinf(BAT_YAW) * f) / E.w.ech;
    *z = E.w.epos_z + (-sinf(BAT_YAW) * r + cosf(BAT_YAW) * f) / E.w.ech;
}
/* Point vers lequel le membre i avance (élan, pas de celui qui choisit) et se tourne : l'ennemi que touche son action,
   sinon l'ennemi visé (vise de l'émulateur) */
int battle_vise_en(int i) {
    return BT.phase == BP_ANIM && BT.an.who == i && BT.an.ntarget && BT.an.targets[0] >= BT_ENEMY ? BT.an.targets[0] - BT_ENEMY : BT.cible > 0 ? BT.cible : 0;
}
void battle_vise(int i, float *x, float *z) { battle_enemy_pos(battle_vise_en(i), x, z); }
/* Nom du groupe : le chef seul, sinon chaque ennemi différent dans l'ordre, avec son nombre (« GLANEUR × 3 »,
   « GLANEUR × 2, COLOSSE ») ; groupeNom de l'émulateur */
void bt_groupe_nom(char *buf, int cap) {
    int n = 0;
    buf[0] = 0;
    for (int k = 0; k < BT.nen && n < cap; k++) {
        int dbl = 0, c = 0;
        for (int j = 0; j < k; j++) dbl |= BT.en[j].perso == BT.en[k].perso;
        for (int j = k; j < BT.nen; j++) c += BT.en[j].perso == BT.en[k].perso;
        if (dbl) continue;
        n += c > 1 ? snprintf(buf + n, (size_t)(cap - n), "%s%s × %d", n ? ", " : "", BT.en[k].name, c) : snprintf(buf + n, (size_t)(cap - n), "%s%s", n ? ", " : "", BT.en[k].name);
    }
}

int battle_timeline(int n, int *out) {
    float ct[BT_UNITS];
    memcpy(ct, BT.ct, sizeof ct);
    int k = 0;
    for (; k < n; k++) {
        int bi = -1;
        for (int i = 0; i < BT_UNITS; i++) if (alive(i) && (bi < 0 || ct[i] < ct[bi])) bi = i;
        if (bi < 0) break;
        out[k] = bi; ct[bi] += 100.0f / spd_of(bi);
    }
    return k;
}

/* Bannière (hold : jusqu'au prochain toucher, sinon 1,3 s) */
static void banner(const char *text, const char *sub, uint32_t color, int hold) {
    snprintf(BT.ban.text, sizeof BT.ban.text, "%s", text);
    snprintf(BT.ban.sub, sizeof BT.ban.sub, "%s", sub ? sub : "");
    BT.ban.color = color; BT.ban.hold = hold; BT.ban.t0 = E.clock; BT.ban.on = 1;
}

/* Nombre flottant au-dessus d'une unité : la place libre, sinon la plus ancienne */
static void popup(int unit, const char *text, uint32_t color) {
    int k = 0;
    for (int i = 0; i < BT_POPUPS; i++) {
        if (!BT.pop[i].on || E.clock - BT.pop[i].t0 > 1.3f) { k = i; break; }
        if (BT.pop[i].t0 < BT.pop[k].t0) k = i;
    }
    snprintf(BT.pop[k].text, sizeof BT.pop[k].text, "%s", text);
    BT.pop[k].color = color; BT.pop[k].t0 = E.clock; BT.pop[k].unit = unit;
    BT.pop[k].dx = (int)(brand() * 20 - 10); BT.pop[k].on = 1;
}

/* Palette de l'ennemi d'un combat du scénario : celle de ce qu'on voit dans le monde (paletteVue de l'émulateur) :
   l'interlocuteur s'il est ce personnage ; sinon son acteur (sa palette, acteur_poser), ou une cible de chasse active à
   moins de PAL_VUE cases (couleurs de base) ; sinon l'exemplaire posé le plus proche à moins de PAL_VUE cases ; sinon la
   palette de base. Jamais au hasard : l'ennemi garde en combat les couleurs qu'il avait juste avant */
static int palette_vue(int perso) {
    const world_t *w = &E.w;
    if (E.talk >= 0 && E.talk < E.nnpc && E.npcs[E.talk].perso == perso) return E.npcs[E.talk].var;
    const int a = acteur_de(perso);
    if (a >= 0) return MISE.act[a].var;
    int best = -1;
    float bd = PAL_VUE;
    for (int j = 0; j < E.nnpc; j++) {
        const float d = hypotf(E.npcs[j].x - E.sera.x, E.npcs[j].z - E.sera.z);
        if (E.npcs[j].perso == perso && npc_present(&E.npcs[j]) && d < bd) { bd = d; best = j; }
    }
    for (int k = 0; k < w->ncibles; k++) {   /* cible active plus proche que l'exemplaire (et pas doublée par lui) */
        const cible_t *c = &w->cibles[k];
        if (c->perso != perso || c->carte != w->carte || !script_cible_active(c)) continue;
        if (hypotf(c->x - E.sera.x, c->z - E.sera.z) < PAL_VUE && (best < 0 || hypotf(E.npcs[best].x - c->x, E.npcs[best].z - c->z) >= CIBLE_DOUBLE)) return 0;
    }
    return best >= 0 ? E.npcs[best].var : 0;
}

/* Règle du partage (un groupe ne vaut pas plus qu'un ennemi seul de son niveau ; startBattle de l'émulateur,
   tools/equilibrage/combat.js) :
   - groupe à parts égales (rencontres, patrouilles, « combat A, B », « combat A x3 ») : chacun garde le profil de sa fiche
     (PV, ATQ, VIT et sorts, au niveau du combat et réglés sur la taille de l'équipe), PV × GROUPE_PV[n], ATQ × GROUPE_ATQ[n] ;
   - boss et sbires (« combat BOSS avec S x2 ») : chaque sbire a SBIRE_PART des PV et de l'ATQ du boss (sa vitesse, ses sorts
     et son allure restent les siens) ; le boss lui cède ces PV (il en garde BOSS_GARDE au moins) mais seulement BOSS_CEDE de
     son ATQ (les sbires, plus faibles, tombent vite et leurs coups avec eux) ;
   - récompense : GROUPE_XP[n] fois celle d'un ennemi seul, partagée selon les PV (part de chacun : son cœur de machine et
     son équipement tombent avec cette chance).
   Tables étalonnées au simulateur (tools/equilibrage/groupes.js, 33 ennemis ordinaires aux niveaux 8 à 60, équipe complète) :
   à parité, même victoire (99 %) et mêmes PV perdus (20 %) qu'un ennemi seul ; l'expérience suit la durée du combat (plus
   de tours ennemis : × 1,10, 1,23, 1,37) ; boss avec deux sbires : même victoire que seul (53 % sur 8 boss à leur niveau) */
static const float GROUPE_PV[BT_EMAX + 1] = { 1, 1, 0.5f, 0.34f, 0.26f };
static const float GROUPE_ATQ[BT_EMAX + 1] = { 1, 1, 0.63f, 0.43f, 0.32f };
static const float GROUPE_XP[BT_EMAX + 1] = { 1, 1, 1.1f, 1.25f, 1.35f };
#define SBIRE_PART 0.15f
#define BOSS_CEDE 0.04f
#define BOSS_GARDE 0.55f
#define POL_DEFAITE_S 2.2f   /* combat perdu contre la police : la bannière tant de temps, puis l'arrestation */
/* Copie du début du combat (en mémoire, jamais sur la carte SD ; reessai de l'émulateur) : ce que le combat change (l'équipe
   entière, PV, PM et le reste de chaque membre, par la taille réelle de PARTY ; objets du sac, crédits) et de quoi le
   relancer tel quel (ennemis du groupe, leurs palettes, niveau, boss, boss et sbires). RÉESSAYER LE COMBAT de l'écran de
   défaite la remet et relance le même combat (battle_reessayer) ; la transformation et la file repartent du début du
   combat, comme la première fois */
static struct { int ok, nk, lvl, boss, mode, ks[BT_EMAX], var[BT_EMAX]; member_t party[sizeof PARTY / sizeof PARTY[0]]; int items[MAX_ITEMS], credits; } RT ENG_FROID;
/* Arène du combat (11/11/2026, demande de l'utilisateur : « varier les scènes de combat, c'est toujours une scène de ville même
   quand on est loin des villes ») : la plus proche de Sera (de la porte de l'intérieur où elle est) parmi celles que l'émulateur
   a relevées sur sa carte, une par bloc de ARENE_BLOC cases (AREN : arenesCalc de l'émulateur) ; sinon celle de la carte (HEAD,
   CART), comme avant. Le combat se joue dans le paysage où l'on est : désert, forêt, neige, marais, ville. AREN : nombre de
   cartes u16, puis pour chacune son nombre u16 et ses arènes, x et z × 2 en u16 (areneChoisir de l'émulateur, à garder identique) */
static void arene_choisir(void) {
    world_t *w = &E.w;
    const uint8_t *p = w->aren, *fin = p ? p + w->aren_sz : NULL;
    if (!p) return;
    const int nc = p[0] | p[1] << 8;
    p += 2;
    for (int k = 0; k < nc && p + 2 <= fin; k++) {
        const int n = p[0] | p[1] << 8;
        p += 2;
        if (p + (size_t)n * 4 > fin) return;
        if (k == w->carte && n > 0) {
            const float x = E.in.on ? E.in.rx : E.sera.x, z = E.in.on ? E.in.rz : E.sera.z;
            float bd = 1e30f, bx = 0, bz = 0;
            for (int i = 0; i < n; i++) {
                const uint8_t *q = p + i * 4;
                const float ax = (q[0] | q[1] << 8) / 2.0f, az = (q[2] | q[3] << 8) / 2.0f, d = (ax - x) * (ax - x) + (az - z) * (az - z);
                if (d < bd) { bd = d; bx = ax; bz = az; }
            }
            w->arena_x = bx; w->arena_z = bz;
            w->epos_x = w->arena_x + (sinf(BAT_YAW) * BAT_EAV - cosf(BAT_YAW) * BAT_EG) / w->ech;   /* comme world_carte */
            w->epos_z = w->arena_z + 2.5f + (cosf(BAT_YAW) * BAT_EAV + sinf(BAT_YAW) * BAT_EG) / w->ech;
            return;
        }
        p += (size_t)n * 4;
    }
}
void battle_start(int k, int lvl, int boss) { battle_start_groupe(&k, 1, lvl, boss, GROUPE_PARTS, NULL); }
void battle_start_groupe(const int *ks, int nk, int lvl, int boss, int mode, const int *vars) {
    const world_t *w = &E.w;
    if (BT.on || nk < 1 || !ks || ks[0] < 0 || ks[0] >= w->nenemies) return;
    if (nk > BT_EMAX) nk = BT_EMAX;
    memcpy(RT.party, PARTY, sizeof PARTY);   /* copie du début du combat : l'équipe et le sac avant tout changement */
    for (int i = 0; i < n_items && i < MAX_ITEMS; i++) RT.items[i] = ITEMS[i].count;
    RT.credits = E.credits;
    effet_manette(ENG_EFFET_COMBAT);
    BT.ret_x = E.sera.x; BT.ret_z = E.sera.z;
    arene_choisir();   /* le combat se joue près d'où l'on est (11/11/2026) */
    E.has_dest = 0;
    /* palette : combat du scénario (boss) : celle de l'ennemi vu juste avant (palette_vue, avant hud_battle_start qui
       oublie l'interlocuteur) ; patrouille : celles qu'on voyait ; rencontre : une de ses palettes (PVAR), au hasard */
    int vue[BT_EMAX];
    for (int i = 0; i < nk && i < BT_EMAX; i++)
        vue[i] = vars ? vars[i] : boss && ks[i] >= 0 && ks[i] < w->nenemies ? palette_vue(w->enemies[ks[i]].perso) : -1;
    hud_battle_start();
    BT.boss = boss; BT.fuites = 0; BT.patr = -1; BT.police = 0; BT.arene = 0;   /* police_combat, arene.c le marquent après */
    /* Niveau du combat (celui du chef) : PV et ATQ de chaque fiche suivent l'équipe, × (niveau + 6) / (niveau de la fiche + 6) */
    BT.lvl = lvl > 0 ? (lvl > NIV_MAX ? NIV_MAX : lvl) : w->enemies[ks[0]].lvl;
    /* L'ennemi se règle sur la taille de l'équipe : PV et attaque à (n + 1) / 5, puis TAILLE_PV, TAILLE_ATQ (startBattle de
       l'émulateur, tools/equilibrage/combat.js) : à deux et à trois, une fiche vaut autant qu'à quatre (audit du 01/11/2026 :
       0,8 / 0,75 et 0,9 / 0,85 avant, un boss fait pour deux ou trois était 1,3 à 1,4 fois trop fort pour quatre) ; Sera seule
       au prologue, sans soins ni magie, un peu moins */
    static const float TAILLE_PV[5] = { 1, 0.75f, 1.05f, 1, 1 }, TAILLE_ATQ[5] = { 1, 0.65f, 1, 0.95f, 1 };
    int n = 0;
    for (int i = 0; i < NMEMB; i++) n += in_team(i);
    if (n > EQUIPE_MAX) n = EQUIPE_MAX;
    const float f = (n + 1) / 5.0f;
    BT.nen = 0;
    for (int i = 0; i < nk && BT.nen < BT_EMAX; i++) {
        if (ks[i] < 0 || ks[i] >= w->nenemies) continue;
        const enemy_t *en = &w->enemies[ks[i]];
        bt_en_t *e = &BT.en[BT.nen];
        memset(e, 0, sizeof *e);
        e->perso = en->perso; e->ek = ks[i]; e->spd = en->spd; e->part = 1; e->faible = en->faible; e->resiste = en->resiste;
        E.best_vu[ks[i] >> 3] |= (uint8_t)(1 << (ks[i] & 7));   /* bestiaire : vu */
        e->var = vue[i] >= 0 ? vue[i] : (int)(brand() * (w->persos[en->perso].nvar + 1));
        snprintf(e->name, sizeof e->name, "%s", w->persos[en->perso].nom);
        const float fl = (BT.lvl + 6.0f) / (en->lvl + 6.0f);
        e->hp = (int)lroundf(en->hp * fl); e->atk = (int)lroundf(en->atk * fl);
        e->hp = (int)lroundf(e->hp * f * TAILLE_PV[n]); e->atk = (int)lroundf(e->atk * f * TAILLE_ATQ[n]);
        BT.nen++;
    }
    BT.sbires = mode == GROUPE_SBIRES && BT.nen > 1;
    if (BT.sbires) {   /* boss et sbires : les sbires prennent leur part du boss (ses PV, un peu de son ATQ) */
        const float hb = (float)BT.en[0].hp, ab = (float)BT.en[0].atk, ns = (float)(BT.nen - 1);
        for (int k = 1; k < BT.nen; k++) { BT.en[k].hp = (int)lroundf(hb * SBIRE_PART); BT.en[k].atk = (int)lroundf(ab * SBIRE_PART); BT.en[k].sbire = 1; }
        BT.en[0].hp = (int)lroundf(hb * fmaxf(BOSS_GARDE, 1 - SBIRE_PART * ns)); BT.en[0].atk = (int)lroundf(ab * fmaxf(BOSS_GARDE, 1 - BOSS_CEDE * ns));
    } else if (BT.nen > 1)
        for (int k = 0; k < BT.nen; k++) { BT.en[k].hp = (int)lroundf(BT.en[k].hp * GROUPE_PV[BT.nen]); BT.en[k].atk = (int)lroundf(BT.en[k].atk * GROUPE_ATQ[BT.nen]); }
    float tot = 0;
    for (int k = 0; k < BT.nen; k++) { if (BT.en[k].hp < 1) BT.en[k].hp = 1; BT.en[k].max = BT.en[k].hp; tot += BT.en[k].hp; }
    for (int k = 0; BT.nen > 1 && k < BT.nen; k++) BT.en[k].part = GROUPE_XP[BT.nen] * BT.en[k].hp / tot;
    BT.cible = -1; cible_maj();
    BT.visible = 1;
    for (int i = 0; i < NMEMB; i++) {
        BT.off[i] = BT.anim[i] = 0; BT.frame[i] = 0; BT.defending[i] = 0;
        if (PARTY[i].hp < 0) PARTY[i].hp = 0;   /* à terre (02/11/2026) : il entre au combat à terre, ne joue pas (alive) ; toute l'équipe à terre : défaite */
    }
    for (int u = 0; u < BT_UNITS; u++) BT.ct[u] = u < BT_ENEMY + BT.nen ? 100.0f / spd_of(u) * (0.3f + brand() * 0.7f) : 0;
    BT.phase = BP_INTRO; BT.t0 = E.clock; BT.actor = -1; BT.locked = 0;
    BT.rw_loot = BT.rw_eq = BT.rw_eq2 = BT.rw_soin = BT.rw_noyau = -1; BT.rw_nloot = 0;
    memset(&BT.pending, 0, sizeof BT.pending); memset(&BT.an, 0, sizeof BT.an);
    BT.cut.img = BT.cut.last = -1; BT.cut.tech = NULL; BT.cut.plein = BT.cut.paire = BT.cut.reac = BT.cut.special = 0;
    memset(BT.transfo, 0, sizeof BT.transfo);
    BT.transfo[0] = eveil_hors_combat();   /* mode d'essai : Sera commence le combat transformée */
    memset(BT.degres_vus, 0, sizeof BT.degres_vus); BT.reac_img = -1;
    for (int i = 0; i < BT_POPUPS; i++) BT.pop[i].on = 0;
    BT.shake = 0; BT.shake_amp = 1; BT.fade_t = E.clock;
    BT.on = 1;
    E.stream_hold = 1;   /* fenêtre de chunks recentrée sur l'arène à la mise à jour suivante */
    /* Entrée : le cut-in de l'équipe face à l'ennemi (draw_intro_combat de hud.c), INTRO_S, ou jusqu'au toucher */
    /* Copie du début du combat : de quoi le relancer tel quel (mêmes ennemis, palettes et niveau) */
    RT.nk = BT.nen; RT.lvl = BT.lvl; RT.boss = boss; RT.mode = mode; RT.ok = BT.nen > 0;
    for (int k = 0; k < BT.nen; k++) { RT.ks[k] = BT.en[k].ek; RT.var[k] = BT.en[k].var; }
}
int battle_reessai_ok(void) { return RT.ok && BT.on && BT.phase == BP_LOSE && !BT.police && !BT.arene; }   /* pas contre la police, ni à l'arène */
/* RÉESSAYER LE COMBAT (écran de défaite) : la copie du début remise, le même combat relancé tout de suite, avec la même
   entrée ; sans battle_end : le script qui attendait ce combat attend toujours (une victoire continue sa scène), la
   patrouille qui l'avait engagé reste la sienne */
static void battle_reessayer(void) {
    int ks[BT_EMAX], var[BT_EMAX];
    const int nk = RT.nk, lvl = RT.lvl, boss = RT.boss, mode = RT.mode, patr = BT.patr;
    memcpy(ks, RT.ks, sizeof ks); memcpy(var, RT.var, sizeof var);
    memcpy(PARTY, RT.party, sizeof PARTY);
    for (int i = 0; i < n_items && i < MAX_ITEMS; i++) ITEMS[i].count = RT.items[i];
    E.credits = RT.credits;
    BT.on = 0; BT.ban.on = 0;
    battle_start_groupe(ks, nk, lvl, boss, mode, var);
    BT.patr = patr;
}

/* ---------- Niveau de l'équipe ---------- */
/* Expérience d'un ennemi du niveau lvl ; d'un niveau au suivant (02/11/2026, consigne de l'utilisateur) : 2 + 18 (L − 1) / 58
   ennemis seuls de ce niveau, en ligne droite : 2 du niveau 1 au 2, 20 du 59 au 60, 32 au 99 (avant : 15 + 0,1 L). Calcul
   entier, arrondi au plus proche : (116 + 18 (L − 1)) × exp_of(L) / 58 (expNext de l'émulateur, tools/equilibrage/combat.js) */
static int exp_of(int lvl) { return (int)lroundf(10 * powf((float)lvl, 1.45f) + 10); }
int exp_next(int L) { return ((116 + 18 * (L - 1)) * exp_of(L) + 29) / 58; }
/* Crédits d'un combat, tout compris (06/11/2026, demande de l'utilisateur : 30 % de moins) ; soins et amendes, qui en dépendent, suivent */
#define CR_X 0.7f
#define CRISTAL_RENC 0.5f   /* cristal d'une machine vaincue en rencontre : une fois sur deux (un combat de l'histoire le laisse toujours : il en faut pour passer la porte) */
static int credits_of(int lvl) { return (int)lroundf(CR_X * (5 * powf((float)lvl, 1.3f) + 10)); }
/* Crédits d'un combat ordinaire au niveau de l'équipe (lvl < 1 : celui-ci) : l'échelle des amendes (police.c, interieur.c) */
int credits_combat(int lvl) { return credits_of(lvl < 1 ? (E.level < 1 ? 1 : E.level) : lvl); }
/* Prix d'un soin au niveau de l'équipe (02/11/2026 ; prixSoins de l'émulateur, tools/equilibrage/progression.js) : le prix du
   commerce (COMM : clinique 40, hôpital 80, verre 15 ou 25) tant qu'un combat rapporte moins de 100 crédits, puis × crédits
   d'un combat / 100 : une clinique vaut toujours 0,4 combat, l'hôpital 0,8 (avant : 40 et 80 crédits à tout niveau) */
int prix_soins(int base) { const int p = (base * credits_combat(0) + 50) / 100; return p > base ? p : base; }

/* ---------- Jobs et équipement ---------- */
static const float AFF_F[5] = { 0, 0.7f, 0.85f, 1, 1.2f };
/* ---------- Magie : progression des jobs, cristaux du Chœur (03/11/2026 ; section « Magie » de l'émulateur) ----------
   Un job enseigne chacun de ses sorts à son niveau (JOBS version 3 : sort_niv_job ; avant, le niveau du sort) ; un objet
   porté donne les siens, un membre garde ceux qu'il a appris (E.appris : cristaux du Chœur), au niveau du sort. */
int sort_niv_job(int j, int s) {
    if (j < 0 || j >= E.w.njobs || s < 0 || s >= E.w.nsorts) return 0;
    return E.w.jobs[j].niv ? E.w.jobs[j].niv[s] : E.w.sorts[s].niv;
}
int sort_appris(int m, int s) { return m >= 0 && m < NMEMB && s >= 0 && s < 64 && (E.appris[m][s >> 5] >> (s & 31) & 1); }
uint64_t sorts_masque(int m, int L) {
    if (m < 0 || m >= NMEMB) return 0;
    const int j = E.job[m] < E.w.njobs ? E.job[m] : -1;
    uint64_t job = j >= 0 ? E.w.jobs[j].sorts : 0, autres = (uint64_t)E.appris[m][1] << 32 | E.appris[m][0], r = 0;
    for (int s = 0; s < 3; s++) { const eqitem_t *q = eq_item(E.eq[m][s]); if (q) autres |= q->sorts; }
    for (int k = 0; k < E.w.nsorts; k++)
        if (((job >> k & 1) && sort_niv_job(j, k) <= L) || ((autres >> k & 1) && E.w.sorts[k].niv <= L)) r |= 1ull << k;
    return r;
}
/* Les plus forts d'abord (03/11/2026 : un magicien en connaît une trentaine ; la première page du menu MAGIE montre les GIGA),
   puis dans l'ordre du catalogue (sortsOf de l'émulateur) */
int sorts_of(int m, int *out) {
    const uint64_t b = sorts_masque(m, E.level);
    int n = 0;
    for (int nv = 2; nv >= 0; nv--)
        for (int k = 0; k < E.w.nsorts; k++) if ((b >> k & 1) && (E.w.sorts[k].fx - FX_MAGIE) % 3 == nv) out[n++] = k;
    return n;
}
/* Cristal porté par le membre m (l'accessoire d'abord) qui lui apprend encore un sort : 1, *s ce sort, *pts ses points,
   *max ceux à réunir ; 0 sinon */
int cristal_de(int m, int *s, int *pts, int *max) {
    static const int ORDRE[3] = { EQ_ACCESS, EQ_ARME, EQ_ARMURE };
    if (m < 0 || m >= NMEMB) return 0;
    for (int o = 0; o < 3; o++) {
        const eqitem_t *q = eq_item(E.eq[m][ORDRE[o]]);
        if (!q || !q->pts) continue;
        for (int k = 0; k < E.w.nsorts && k < 64; k++)
            if ((q->sorts >> k & 1) && !sort_appris(m, k)) { *s = k; *pts = E.apts[m][k]; *max = q->pts; return 1; }
    }
    return 0;
}
/* Ce que la victoire apprend (tableau de victoire : BT.rw_app ; appliquer : à battle_end, les points comptés) : les niveaux
   gagnés (les sorts nouveaux de chaque membre de la bande au nouveau niveau), puis chaque membre au combat, debout, qui
   porte un cristal : CRISTAL_PT_BOSS (combat de l'histoire), CRISTAL_PT_RARE (ennemi rare ou élite) ou CRISTAL_PT_RENC
   points, le sort appris quand ils atteignent ceux du cristal (apprentissage de l'émulateur) */
static void apprentissage(int appliquer) {
    if (!appliquer) BT.rw_napp = 0;
    int L = E.level, x = E.exp + BT.rw_xp;
    while (L < NIV_MAX && x >= exp_next(L)) { x -= exp_next(L); L++; }
    for (int i = 0; !appliquer && L > E.level && i < NMEMB; i++) {
        if (!((E.recrues | 1) >> i & 1)) continue;
        const uint64_t nv = sorts_masque(i, L) & ~sorts_masque(i, E.level);
        for (int k = 0; k < 64 && BT.rw_napp < RW_APP_MAX; k++) if (nv >> k & 1) { BT.rw_app[BT.rw_napp].m = (uint8_t)i; BT.rw_app[BT.rw_napp].s = (uint8_t)k; BT.rw_app[BT.rw_napp].pts = BT.rw_app[BT.rw_napp].max = 0; BT.rw_napp++; }
    }
    int rare = 0;
    for (int k = 0; k < BT.nen; k++) { const enemy_t *en = BT.en[k].ek >= 0 && BT.en[k].ek < E.w.nenemies ? &E.w.enemies[BT.en[k].ek] : NULL; if (en && (en->sorts || en->poids <= RARE_POIDS)) rare = 1; }
    const int pt = BT.boss ? CRISTAL_PT_BOSS : rare ? CRISTAL_PT_RARE : CRISTAL_PT_RENC;
    for (int i = 0; i < NMEMB; i++) {
        int s, p, mx;
        if (!in_team(i) || PARTY[i].hp <= 0 || !cristal_de(i, &s, &p, &mx)) continue;
        const int np = p + pt > 255 ? 255 : p + pt;
        if (appliquer) { E.apts[i][s] = (uint8_t)np; if (np >= mx) E.appris[i][s >> 5] |= 1u << (s & 31); }
        else if (BT.rw_napp < RW_APP_MAX) { BT.rw_app[BT.rw_napp].m = (uint8_t)i; BT.rw_app[BT.rw_napp].s = (uint8_t)s; BT.rw_app[BT.rw_napp].pts = (uint8_t)np; BT.rw_app[BT.rw_napp].max = (uint8_t)mx; BT.rw_napp++; }
    }
}
/* Sorts nouveaux depuis le niveau L0 (gain d'expérience hors combat : « experience » du scénario), annoncés un par un
   (bandeau des annonces : « VARN apprend MÉGA SACRÉ ») */
void sorts_annoncer(int L0) {
    for (int i = 0; L0 < E.level && i < NMEMB; i++) {
        if (!((E.recrues | 1) >> i & 1)) continue;
        const uint64_t nv = sorts_masque(i, E.level) & ~sorts_masque(i, L0);
        for (int k = 0; k < 64; k++) if (nv >> k & 1) { char t[64]; snprintf(t, sizeof t, "%s apprend %s", PARTY[i].name, E.w.sorts[k].nom); hud_annonce("NOUVEAU SORT", t, ""); }
    }
}
/* Objets en combat (07/11/2026, demande de l'utilisateur : on n'avait que la POTION, sur le plus blessé) : tous les soins du sac ; à un camarade qu'on choisit, ou à toute l'équipe (MÉGA…). Ce qui
   relève un membre à terre (SERINGUE DE RAPPEL) reste hors combat. combatObjets de l'émulateur */
int battle_objets(uint8_t *l) {
    int n = 0;
    /* (une relève aussi, depuis le 08/11/2026 : la SERINGUE DE RAPPEL sur un camarade à terre ; avant, hors combat seulement) */
    for (int i = 0; i < n_items && n < MAX_ITEMS; i++) if (ITEMS[i].count > 0 && (ITEMS[i].releve || ITEMS[i].type == OBJ_LANCER || (ITEMS[i].type == 0 && (ITEMS[i].hp > 0 || ITEMS[i].mp > 0)))) l[n++] = (uint8_t)i;
    return n;
}
int sorts_page(int n, int *first, int *pages) {
    const int per = n <= 4 ? 4 : 3, np = n ? (n + per - 1) / per : 1, pg = BT.mpage % np;
    *first = pg * per; *pages = np;
    return n - *first < per ? n - *first : per;
}
const eqitem_t *eq_item(int i) { return E.w.eqit && i >= 0 && i < MAX_ITEMS && E.w.eqit[i].cat < E.w.neqtypes ? &E.w.eqit[i] : NULL; }
int eq_slot(int i) { const eqitem_t *r = eq_item(i); return r ? E.w.eqtypes[r->cat].slot : -1; }
int eq_aff(int m, int i) {
    const eqitem_t *r = eq_item(i);
    if (!r) return 0;
    if (i < n_items && ITEMS[i].tenue && tenue_named(m, ITEMS[i].tenue) < 0) return 0;   /* armure qui se voit (bikini) : il faut sa tenue */
    const int a = E.job[m] < E.w.njobs ? E.w.jobs[E.job[m]].aff[r->cat] : 3;
    return a > 4 ? 4 : a;
}
/* Statistiques du membre m : celles du niveau, son job, puis son équipement selon l'affinité */
int eq_up_niv(int i) { return i >= 0 && i < 256 ? E.eq_up[i >> 2] >> ((i & 3) * 2) & 3 : 0; }
void eq_up_poser(int i, int n) { if (i >= 0 && i < 256) E.eq_up[i >> 2] = (uint8_t)((E.eq_up[i >> 2] & ~(3 << ((i & 3) * 2))) | (n & 3) << ((i & 3) * 2)); }
void party_refresh(int k) {
    member_t *m = &PARTY[k];
    float hp = m->lhp, mp = m->lmp, atk = m->latk, mag = m->latk, def = (float)(GAIN_DEF * (E.level - 1)), spd = (float)m->bspd;
    if (E.job[k] < E.w.njobs) {
        const job_t *j = &E.w.jobs[E.job[k]];
        hp *= 1 + j->pv / 100.0f; mp *= 1 + j->pm / 100.0f; atk *= 1 + j->atq / 100.0f; mag *= 1 + j->mag / 100.0f; spd += (float)j->vit;
    }
    for (int s = 0; s < 3; s++) {
        const eqitem_t *r = eq_item(E.eq[k][s]);
        if (!r) continue;
        const float f = AFF_F[eq_aff(k, E.eq[k][s])] * (1 + EQ_UP_X * eq_up_niv(E.eq[k][s]));
        hp += r->pv * f; mp += r->pm * f; atk += r->atq * f; mag += r->mag * f; def += r->def * f; spd += r->vit * f;
    }
    m->mag = mag < 1 ? 1 : (int)lroundf(mag);
    m->max = hp < 1 ? 1 : (int)lroundf(hp); m->maxmp = mp < 0 ? 0 : (int)lroundf(mp); m->atk = atk < 1 ? 1 : (int)lroundf(atk);
    m->def = def < 0 ? 0 : (int)lroundf(def); m->spd = spd < 1 ? 1 : (int)lroundf(spd);
    if (m->hp > m->max) m->hp = m->max;
    if (m->mp > m->maxmp) m->mp = m->maxmp;
}
int party_set_job(int k, int j) {
    int off = -1;
    if (j < 0 || j >= E.w.njobs) return -1;
    E.job[k] = (uint8_t)j; E.jobs_known[k] |= (uint16_t)(1u << j);
    for (int s = 0; s < 3; s++) {
        const int i = E.eq[k][s];
        if (i == EQ_AUCUN || eq_aff(k, i)) continue;
        ITEMS[i].count++; E.eq[k][s] = EQ_AUCUN;
        if (off < 0) off = i;
    }
    party_refresh(k);
    return off;
}
void equip_reset(void) {
    memset(E.appris, 0, sizeof E.appris); memset(E.apts, 0, sizeof E.apts);   /* aucun sort appris (cristaux du Chœur) */
    for (int k = 0; k < NMEMB; k++) {
        E.job[k] = E.w.job0[k]; E.jobs_known[k] = E.w.jobs0[k];
        for (int s = 0; s < 3; s++) E.eq[k][s] = eq_item(E.w.eq0[k][s]) ? E.w.eq0[k][s] : EQ_AUCUN;
    }
}

void party_set_level(int L) {
    E.level = L < 1 ? 1 : L > NIV_MAX ? NIV_MAX : L;
    const float f = (E.level + 6) / 26.0f;
    for (int i = 0; i < NMEMB; i++) {   /* tous, au combat, en réserve ou pas encore recrutés : rien à rattraper */
        member_t *m = &PARTY[i];
        if (!m->bhp) {   /* première fois : les valeurs de PARTY sont celles du niveau 20 */
            m->bhp = m->max; m->bmp = m->maxmp; m->batk = m->atk; m->bspd = m->spd;
            for (int t = 0; t < m->ntech; t++) m->tech[t].bheal = m->tech[t].heal;
        }
        const float rh = m->max ? (float)m->hp / m->max : 1, rm = m->maxmp ? (float)m->mp / m->maxmp : 1;
        /* PM (02/11/2026) : la réserve ne suit plus le niveau (valeurs de PARTY, puis le job et l'équipement) ; une rencontre
           en prend environ un quart à tous les niveaux (avant : × f, 65 % au niveau 5 et 10 % au niveau 58) */
        m->lhp = m->bhp * f; m->lmp = (float)m->bmp; m->latk = m->batk * (7 + GAIN_ATQ * (E.level - 1)) / 26.0f;
        party_refresh(i);
        m->hp = (int)lroundf(m->max * rh); m->mp = (int)lroundf(m->maxmp * rm);
        if (rh > 0 && m->hp < 1) m->hp = 1;   /* à terre : un niveau gagné ne le relève pas */
        for (int t = 0; t < m->ntech; t++) if (m->tech[t].bheal) m->tech[t].heal = (int)lroundf(m->tech[t].bheal * f);
    }
}

int party_gain_exp(int n) {
    int up = 0;
    E.exp += n;
    while (E.level < NIV_MAX && E.exp >= exp_next(E.level)) { E.exp -= exp_next(E.level); party_set_level(E.level + 1); up++; }
    return up;
}

/* ---------- Équipe : la bande et ceux qui combattent (equipeScenario, equipeBasculer de l'émulateur, à garder identiques) ----------
   E.recrues : la bande (Sera et chaque membre recruté) ; E.team : au combat, Sera et EQUIPE_MAX - 1 compagnons au plus,
   choisis au menu ÉQUIPE (hud.c) ; les autres attendent en réserve, au niveau commun de l'équipe (party_set_level : rien
   à rattraper) et voyagent avec elle (leurs répliques : drapeaux recrue_<nom> du scénario) ; E.impose : les compagnons mis
   au combat par « equipe », que le menu ne retire pas. Instruction du scénario : 0 « equipe » (l'équipe exacte : la bande,
   au combat les EQUIPE_MAX premiers dans l'ordre de PARTY, imposés), 1 « rejoindre » (dans la bande ; au combat tant
   qu'il reste une place, sinon en réserve), 2 « quitter » (hors de la bande ; sans imposition, la place libre revient à
   la première recrue en réserve), 3 « equipe libre » (plus personne d'imposé). Renvoie les membres qui ont rejoint (1)
   ou quitté (2) la bande, pour l'annonce */
int equipe_nombre(void) { int n = 0; for (int i = 0; i < NMEMB; i++) n += in_team(i); return n; }
static void equipe_completer(void) {
    for (int i = 1; i < NMEMB && !E.impose && equipe_nombre() < EQUIPE_MAX; i++) if (in_band(i)) E.team |= 1 << i;
}
int equipe_scenario(int mode, int mask) {
    const int avant = E.recrues;
    mask &= (1 << NMEMB) - 1;
    if (mode == 0) {
        E.recrues = mask | 1; E.team = 1;
        for (int i = 1; i < NMEMB && equipe_nombre() < EQUIPE_MAX; i++) if (mask >> i & 1) E.team |= 1 << i;
        E.impose = E.team & ~1;
    } else if (mode == 1) {
        E.recrues |= mask;
        for (int i = 1; i < NMEMB && equipe_nombre() < EQUIPE_MAX; i++) if (mask >> i & 1) E.team |= 1 << i;
    } else if (mode == 2) {
        mask &= ~1;   /* Sera ne quitte pas l'équipe */
        E.recrues &= ~mask; E.team &= ~mask; E.impose &= ~mask;
        equipe_completer();
    } else if (mode == 3) E.impose = 0;
    E.recrues |= 1; E.team = (E.team & E.recrues) | 1;
    return mode == 1 ? E.recrues & ~avant : mode == 2 ? avant & ~E.recrues : 0;
}
/* Équipe lue d'une sauvegarde (ou d'avant les recrues) : au combat seulement des membres de la bande, EQUIPE_MAX au plus */
void equipe_valider(void) {
    E.recrues = (E.recrues | E.team | 1) & ((1 << NMEMB) - 1);
    E.team = (E.team & E.recrues) | 1; E.impose &= E.team & ~1;
    for (int i = NMEMB - 1; i > 0 && equipe_nombre() > EQUIPE_MAX; i--) if (in_team(i) && !(E.impose >> i & 1)) E.team &= ~(1 << i);
}
int equipe_basculer(int m) {
    if (m <= 0 || m >= NMEMB) return EQB_SERA;
    if (!in_band(m)) return EQB_HORS;
    if (in_team(m)) {
        if (E.impose >> m & 1) return EQB_IMPOSE;
        E.team &= ~(1 << m);
        return EQB_FAIT;
    }
    if (equipe_nombre() >= EQUIPE_MAX) return EQB_PLEIN;
    E.team |= 1 << m;
    return EQB_FAIT;
}
/* « JADE, ONYX » : les noms des membres du masque, dans l'ordre de PARTY */
void equipe_noms(int mask, char *buf, int cap) {
    int n = 0;
    buf[0] = 0;
    for (int i = 0; i < NMEMB && n < cap; i++) if (mask >> i & 1) n += snprintf(buf + n, (size_t)(cap - n), "%s%s", n ? ", " : "", PARTY[i].name);
}

/* Ennemi dont la fiche laisse un cœur (pas les humains) */
static int drops_heart(int perso) {
    for (int e = 0; e < E.w.nenemies; e++) if (E.w.enemies[e].perso == perso) return E.w.enemies[e].coeur;
    return 0;
}

/* ---------- Paliers de l'équipement (EQPL ; atelier : js/paliers.js) ---------- */
int palier_de(int lvl) {
    int p = 0;
    for (int k = 1; k <= E.w.npal; k++) if (E.w.pal_niv[k] <= lvl) p = k;
    return p;
}
/* Les boutiques vendent les paliers dont le niveau ne dépasse pas celui de l'équipe ou de l'histoire de plus de la marge */
int eq_limite_vente(void) { const int h = script_niveau_histoire(); return (E.level > h ? E.level : h) + E.w.pal_avance; }
void eq_resume(int i, char *buf, int cap) {
    const eqitem_t *r = eq_item(i);
    int n = 0;
    buf[0] = 0;
    if (!r) return;
    const struct { const char *nom; int v; } S[6] = { { "ATQ", r->atq }, { "DÉF", r->def }, { "PV", r->pv }, { "PM", r->pm }, { "MAG", r->mag }, { "VIT", r->vit } };
    for (int k = 0; k < 6 && n < cap; k++) if (S[k].v) n += snprintf(buf + n, (size_t)(cap - n), "%s%s %+d", n ? " · " : "", S[k].nom, S[k].v);
    for (int s = 0, vus = 0; s < E.w.nsorts && n < cap && vus < 2; s++)   /* ses sorts (deux au plus) */
        if (r->sorts >> s & 1) { n += snprintf(buf + n, (size_t)(cap - n), "%s%s", n ? " · " : "", E.w.sorts[s].nom); vus++; }
}
/* Ce qu'on achète dans le commerce comm (COMM) du bâtiment bat (PORT) : les objets du commerce, puis l'équipement de ses
   types (EQPL) dans la gamme du bâtiment : ses EQ_RECENTS derniers paliers ouverts (niveau au plus celui de l'équipe ou
   de l'histoire, plus la marge : eq_limite_vente), du plus haut au plus bas ; renvoie le nombre d'objets écrits dans
   out (shopListe de l'émulateur) */
int boutique_liste(int comm, int bat, uint8_t *out, int cap) {
    int n = 0;
    if (comm < 0 || comm >= E.w.ncomms) return 0;
    const comm_t *c = &E.w.comms[comm];
    for (int i = 0; i < c->n && n < cap; i++) if (c->vend[i] < n_items && item_vu(&ITEMS[c->vend[i]])) out[n++] = c->vend[i];
    const int_bat_t *b = bat >= 0 && bat < E.w.nbats ? &E.w.bats[bat] : NULL;
    if (!c->eq || !b || !b->gmin) return n;
    /* les EQ_RECENTS derniers paliers ouverts de la gamme : le plus haut que permet le niveau, et ceux d'avant */
    int haut = palier_de(eq_limite_vente());
    if (haut > b->gmax) haut = b->gmax;
    for (int p = haut; p >= b->gmin && p > haut - EQ_RECENTS; p--) {
        for (int k = 0; k < n_items && n < cap; k++) {
            const eqitem_t *r = eq_item(k);
            if (r && E.w.eq_pal[k] == p && (E.w.eq_fl[k] & EQ_VENDU) && (c->eq >> r->cat & 1) && ITEMS[k].prix > 0 && item_vu(&ITEMS[k])) out[n++] = (uint8_t)k;
        }
    }
    return n;
}
/* Équipement lâché : chance selon l'ennemi (combat de l'histoire DROP_BOSS, ennemi rare ou élite DROP_RARE, sinon
   DROP_BASE), puis un objet au hasard parmi ceux du palier du niveau du combat qui se lâchent (le palier d'en dessous
   s'il n'y en a pas) ; -1 : rien (dropEquip de l'émulateur). Ennemi k d'un groupe : chance × sa part de la récompense
   (un sbire n'est pas le boss : DROP_BASE) ; drop_equip : le chef. Un boss (pas un sbire), un ennemi rare ou une élite peut
   aussi lâcher un cristal du Chœur de ce palier (EQ_ELITE, 03/11/2026) */
/* Un équipement au hasard du palier p (le palier d'en dessous s'il n'y en a pas) aux drapeaux fl d'EQPL ; -1 : aucun (arène :
   l'équipement d'une série gagnée, arene.c ; equipHasard de l'émulateur) */
int equip_hasard(int palier, int fl) {
    if (!E.w.npal) return -1;
    if (palier > E.w.npal) palier = E.w.npal;
    for (int p = palier; p >= 1; p--) {
        int n = 0;
        for (int i = 0; i < n_items; i++) n += (E.w.eq_fl[i] & fl) && E.w.eq_pal[i] == p && eq_item(i) && item_vu(&ITEMS[i]);
        if (!n) continue;
        int r = (int)(brand() * n) % n;
        for (int i = 0; i < n_items; i++) if ((E.w.eq_fl[i] & fl) && E.w.eq_pal[i] == p && eq_item(i) && item_vu(&ITEMS[i]) && r-- == 0) return i;
    }
    return -1;
}
static int drop_equip_de(int k) {
    if (!E.w.npal || k < 0 || k >= BT.nen) return -1;
    const enemy_t *en = BT.en[k].ek >= 0 && BT.en[k].ek < E.w.nenemies ? &E.w.enemies[BT.en[k].ek] : NULL;
    const int rare = en && (en->sorts || en->poids <= RARE_POIDS), fl = EQ_BUTIN | ((BT.boss && !BT.en[k].sbire) || rare ? EQ_ELITE : 0);
    const float ch = BT.boss && !BT.en[k].sbire ? DROP_BOSS : rare ? DROP_RARE : DROP_BASE;
    if (brand() >= ch * fminf(1, BT.en[k].part)) return -1;
    for (int p = palier_de(BT.lvl); p >= 1; p--) {
        int n = 0;
        for (int i = 0; i < n_items; i++) n += (E.w.eq_fl[i] & fl) && E.w.eq_pal[i] == p && eq_item(i);
        if (!n) continue;
        int r = (int)(brand() * n) % n;
        for (int i = 0; i < n_items; i++) if ((E.w.eq_fl[i] & fl) && E.w.eq_pal[i] == p && eq_item(i) && r-- == 0) return i;
    }
    return -1;
}
int drop_equip(void) { return drop_equip_de(0); }
/* Noyau élémentaire lâché à la victoire (NOYAU_* d'internal.h, 08/11/2026) : un élément au hasard parmi ceux dont les
   ennemis du combat attaquent (sorts, attaques élémentaires de ENAT), avec la chance du plus fort d'entre eux (chef d'un
   combat de l'histoire NOYAU_BOSS, ennemi rare ou élite NOYAU_RARE, sinon NOYAU_RENC) ; -1 : rien (noyauButin de l'émulateur) */
static int noyau_butin(void) {
    int els = 0;
    float ch = 0;
    for (int k = 0; k < BT.nen; k++) {
        const enemy_t *en = BT.en[k].ek >= 0 && BT.en[k].ek < E.w.nenemies ? &E.w.enemies[BT.en[k].ek] : NULL;
        if (!en) continue;
        int e0 = 0;
        for (int s2 = 0; s2 < E.w.nsorts && s2 < 64; s2++) if ((en->sorts >> s2 & 1) && !E.w.sorts[s2].soin) e0 |= 1 << FX_ELEM(E.w.sorts[s2].fx);
        for (int i = 0; i < en->natq; i++) { const int fx = en->atq[i * ENAT_ATQ + 20]; if (fx >= FX_MAGIE && fx < FX_N) e0 |= 1 << FX_ELEM(fx); }
        e0 &= ~(1 << MG_SOIN);
        if (!e0) continue;
        els |= e0;
        const float c = BT.boss && !BT.en[k].sbire ? NOYAU_BOSS : en->sorts || en->poids <= RARE_POIDS ? NOYAU_RARE : NOYAU_RENC;
        if (c > ch) ch = c;
    }
    if (!els || brand() >= ch) return -1;
    int l[MAGIE_N], n = 0;
    for (int e = 0; e < MAGIE_N; e++) if ((els >> e & 1) && noyau_de(e) >= 0) l[n++] = e;
    return n ? noyau_de(l[(int)(brand() * n) % n]) : -1;
}
/* Soin lâché au niveau lvl (DROP_SOIN d'internal.h) : le soin de PV le plus cher dont le prix ne dépasse pas DROP_SOIN_VAL
   fois les crédits du combat, sinon le moins cher ; -1 : aucun soin dans le sac */
static int soin_butin(int lvl) {
    const float budget = credits_of(lvl) * DROP_SOIN_VAL;
    int best = -1, moins = -1;
    for (int i = 0; i < n_items; i++) {
        const item_t *it = &ITEMS[i];
        if (it->type != 0 || it->hp <= 0 || it->mp != 0 || it->releve || it->prix <= 0) continue;
        if (moins < 0 || it->prix < ITEMS[moins].prix) moins = i;
        if (it->prix <= budget && (best < 0 || it->prix > ITEMS[best].prix)) best = i;
    }
    return best >= 0 ? best : moins;
}

/* Fin du combat : une machine vaincue laisse son cœur ; PV et PM restent ceux de la fin du combat (02/11/2026 : avant,
   l'équipe était soignée) ; un membre à terre le reste (0 PV) jusqu'à ce qu'on le relève : objet du sac qui relève
   (`releve` de la table des objets), soins d'une clinique, de l'hôpital, d'une tente du troc ou d'une infirmerie, REPRENDRE,
   une nuit en prison, l'instruction « soigner ». Victoire : retour à la position d'avant ; défaite : le
   script du scénario qui avait lancé le combat s'arrête ; fuite (won 2) : retour à la position d'avant, sans récompense. */
static void battle_end(int won) {
    const int fui = won == 2;
    if (fui) won = 0;
    /* Combat du scénario gagné : la cible de chasse de cet ennemi, là où il était, n'est plus dessinée (endBattle de
       l'émulateur) ; sinon l'ennemi vaincu restait debout pendant les répliques qui suivent */
    if (won) for (int k = 0; k < BT.nen; k++) E.best_bat[BT.en[k].ek >> 3] |= (uint8_t)(1 << (BT.en[k].ek & 7));   /* bestiaire : vaincu */
    if (won && BT.boss)
        for (int k = 0; k < E.w.ncibles; k++) {
            cible_t *c = &E.w.cibles[k];
            for (int e = 0; e < BT.nen; e++)
                if (c->perso == BT.en[e].perso && c->carte == E.w.carte && hypotf(c->x - BT.ret_x, c->z - BT.ret_z) < PAL_VUE) c->vaincue = 1;
        }
    /* Patrouille hostile : vaincue (ou l'équipe défaite), elle s'en va ; fuie, elle reste un moment sur place sans chasser */
    if (BT.patr >= 0 && BT.patr < PATR_MAX) {
        patr_t *pt = &PATR[BT.patr];
        if (fui) { pt->etat = 2; pt->repos = PATR_REPOS; } else pt->on = 0;
        BT.patr = -1;
    }
    if (won) {   /* récompense */
        E.credits += BT.rw_cr;
        if (BT.rw_loot >= 0) ITEMS[BT.rw_loot].count = ITEMS[BT.rw_loot].count + BT.rw_nloot > 99 ? 99 : ITEMS[BT.rw_loot].count + BT.rw_nloot;
        if (BT.rw_eq >= 0 && ITEMS[BT.rw_eq].count < 99) ITEMS[BT.rw_eq].count++;
        if (BT.rw_eq2 >= 0 && ITEMS[BT.rw_eq2].count < 99) ITEMS[BT.rw_eq2].count++;
        if (BT.rw_soin >= 0 && ITEMS[BT.rw_soin].count < 99) ITEMS[BT.rw_soin].count++;
        if (BT.rw_noyau >= 0 && ITEMS[BT.rw_noyau].count < 99) ITEMS[BT.rw_noyau].count++;
        apprentissage(1);   /* points des cristaux portés, sorts appris (avant le niveau : les mêmes que le tableau) */
        if (party_gain_exp(BT.rw_xp)) { char t[48]; snprintf(t, sizeof t, "NIVEAU %d : PV et attaque en hausse", E.level); hud_toast(t); effet_manette(ENG_EFFET_NIVEAU); }
    }
    if (!won && !fui) script_abort();
    for (int i = 0; i < NMEMB; i++) { if (PARTY[i].hp < 0) PARTY[i].hp = 0; BT.off[i] = 0; }   /* à terre : le reste jusqu'aux soins */
    BT.on = 0; BT.ban.on = 0; BT.shake = 0; BT.fade_t = E.clock;
    for (int i = 0; i < BT_POPUPS; i++) BT.pop[i].on = 0;
    /* Victoire, fuite : la position d'avant ; défaite : là aussi, le choix de l'écran de défaite fait le reste (battle_defaite :
       le dernier point de sauvegarde, ou l'écran titre ; avant le 01/11/2026, le départ de la planète en gardant tout) */
    E.sera.x = BT.ret_x; E.sera.z = BT.ret_z;
    E.has_dest = 0;
    E.stream_hold = 1;   /* fenêtre recentrée sur Sera à la mise à jour suivante */
    /* Contre les forces de l'ordre (police.c) : gagné, elle s'échappe ; perdu, l'arrestation puis la prison */
    if (BT.police) { BT.police = 0; police_apres_combat(won); }
    if (BT.arene) { BT.arene = 0; arene_apres_combat(won); }   /* arène (arene.c) : la suite de la série */
}
/* Défaite (BP_LOSE, draw_defaite de hud.c) : RÉESSAYER LE COMBAT (DEF_REESSAYER) le relance depuis sa copie du début
   (battle_reessayer) ; REPRENDRE (DEF_REPRENDRE) recharge le dernier point de sauvegarde de la partie en cours
   (hud_reprendre : sauvegarde automatique ou du menu ; celle du début de la partie au pis) ; ÉCRAN TITRE (DEF_TITRE) */
void battle_defaite(int choix) {
    if (!BT.on || BT.phase != BP_LOSE || BT.police || BT.arene || E.clock - BT.t0 < 0.4f) return;   /* pas avant que les choix paraissent */
    son_jouer(SON_VALIDER, 0.45f, 1);
    if (choix == DEF_REESSAYER) { if (RT.ok) battle_reessayer(); return; }
    battle_end(0);
    if (choix == DEF_TITRE) accueil_debut(); else hud_reprendre();
}

/* Tour suivant : fin du combat, ou l'unité en tête de la timeline */
static void advance(void) {
    if (BT.ban.hold) BT.ban.on = 0;
    if (!battle_vivants()) { BT.phase = BP_DYING; BT.t0 = E.clock; son_jouer(SON_EXPLOSION, 0.9f, 1); return; }
    int debout = 0;
    for (int i = 0; i < NMEMB; i++) debout |= alive(i);
    if (!debout) {
        BT.phase = BP_LOSE; BT.t0 = E.clock;   /* l'écran de défaite : REPRENDRE ou ÉCRAN TITRE (draw_defaite de hud.c) */
        BT.ban.on = 0;
        effet_manette(ENG_EFFET_DEFAITE);
        /* contre la police : une bannière, puis l'arrestation (battle_update, POL_DEFAITE_S) */
        if (BT.police) { banner("MAÎTRISÉE", POL.clan ? "La milice vous passe les menottes" : "Les agents vous passent les menottes", RED, 1); son_jouer(SON_ANNULER, 0.5f, 0.7f); }
        else if (BT.arene) { banner("ÉLIMINÉE", "L'arène vous renvoie aux vestiaires", RED, 1); son_jouer(SON_ANNULER, 0.5f, 0.7f); }   /* arène : ni écran de défaite ni RÉESSAYER */
        return;
    }
    cible_maj();   /* ennemi visé abattu : le plus faible */
    int a = BT_ENEMY;
    battle_timeline(1, &a);
    BT.actor = a; BT.locked = 0; memset(&BT.pending, 0, sizeof BT.pending);
    if (a >= BT_ENEMY) { BT.phase = BP_ENEMY; BT.t0 = E.clock; }
    else { BT.defending[a] = 0; BT.phase = BP_CMD; }
}

static void end_turn(float rank) { BT.ct[BT.actor] += 100.0f / spd_of(BT.actor) * rank; advance(); }

/* Fuite d'une rencontre : chance selon la vitesse du plus rapide de l'équipe encore debout contre celle du plus rapide des
   ennemis debout, plus FUITE_ESSAI à chaque essai raté, entre FUITE_MIN et FUITE_MAX (fuiteChance de l'émulateur) ; un
   combat du scénario (BT.boss) ne se fuit pas */
#define FUITE_BASE 0.55f
#define FUITE_VIT 0.05f
#define FUITE_ESSAI 0.2f
#define FUITE_MIN 0.2f
#define FUITE_MAX 0.95f
static float fuite_chance(void) {
    int v = 0, ve = 1;
    for (int i = 0; i < NMEMB; i++) if (alive(i) && PARTY[i].spd > v) v = PARTY[i].spd;
    for (int k = 0; k < BT.nen; k++) if (BT.en[k].hp > 0 && BT.en[k].spd > ve) ve = BT.en[k].spd;
    const float c = FUITE_BASE + FUITE_VIT * (v - ve) + FUITE_ESSAI * BT.fuites;
    return c < FUITE_MIN ? FUITE_MIN : c > FUITE_MAX ? FUITE_MAX : c;
}

/* ---------- Effets des armes (FX de l'émulateur ; dessin : fx_draw de hud.c) ----------
   Instant du coup (s), élan vers l'ennemi (1 : au contact, 0 : à distance), sons de l'effet (instant, son, volume,
   hauteur ; vol 0 : aucun). Les techniques jouent l'effet de l'arme en plus grand (k = 1,4), ou leur effet propre.
   Sorts (FX_MAGIE et suivants) : données de l'élément à son niveau 2 (MAGIE) : coup, fin de l'action, secousses de
   l'écran (instant, durée, force), sons, couleur d'une technique qui le porte ; niveaux 1 et 3 : temps × MG_TS,
   secousses × MG_AS, sons du niveau 1 moins forts. */
static const float FX_HIT[FX_MAGIE] = { 0.33f, 0.36f, 0.36f, 0.32f, 0.5f, 0.42f, 0.45f, 0.3f, 0.4f, 0.45f };
static const float FX_DASH[FX_MAGIE] = { 1, 1, 0.5f, 0, 0, 0, 0, 1, 0, 0 };
typedef struct { float t, dur, amp; } fx_sec_t;
typedef struct { float t; int son; float vol, h; } fx_son_t;
typedef struct { float hit, fin; fx_sec_t sec[3]; fx_son_t son[3]; uint32_t coul; } magie_t;
static const magie_t MAGIE[MAGIE_N] = {
    [MG_FOUDRE] = { 0.75f, 1.5f, { { 0.35f, 0.1f, 1.5f }, { 0.55f, 0.1f, 1.5f }, { 0.75f, 0.35f, 3 } },
        { { 0.35f, SON_FOUDRE, 0.4f, 1.6f }, { 0.55f, SON_FOUDRE, 0.4f, 1.8f }, { 0.75f, SON_FOUDRE, 1, 1.0f } }, 0x9FD8FF },
    [MG_BRASIER] = { 0.7f, 1.7f, { { 0.7f, 0.5f, 3 }, { 1.0f, 0.5f, 0.6f } },
        { { 0, SON_MAGIE, 0.5f, 0.8f }, { 0.45f, SON_FEU, 0.7f, 1.0f }, { 0.7f, SON_FEU, 1, 0.75f } }, 0xFF7A2A },
    [MG_GIVRE] = { 0.8f, 1.45f, { { 0.6f, 0.1f, 1.2f }, { 0.8f, 0.25f, 2 } },
        { { 0.15f, SON_MAGIE, 0.45f, 1.4f }, { 0.6f, SON_GLACE, 0.7f, 1.1f }, { 0.8f, SON_GLACE, 1, 0.9f } }, 0x8FF0FF },
    [MG_SACRE] = { 0.75f, 1.65f, { { 0.75f, 0.45f, 2.2f } },
        { { 0.1f, SON_SACRE, 0.6f, 1.0f }, { 0.5f, SON_SACRE, 0.5f, 1.5f }, { 0.75f, SON_EXPLOSION, 0.6f, 1.6f } }, 0xFFE7A0 },
    [MG_METEORE] = { 0.9f, 1.7f, { { 0.6f, 0.15f, 2 }, { 0.75f, 0.15f, 2 }, { 0.9f, 0.45f, 3.5f } },
        { { 0.6f, SON_TERRE, 0.7f, 1.1f }, { 0.75f, SON_EXPLOSION, 0.7f, 0.9f }, { 0.9f, SON_EXPLOSION, 1, 0.6f } }, 0xFF5A2A },
    [MG_NEANT] = { 0.9f, 1.6f, { { 0.3f, 0.55f, 0.7f }, { 0.9f, 0.45f, 3 } },
        { { 0.1f, SON_TENEBRES, 0.7f, 1.0f }, { 0.5f, SON_TENEBRES, 0.6f, 0.7f }, { 0.9f, SON_EXPLOSION, 0.9f, 0.5f } }, 0xB05CFF },
    [MG_POISON] = { 0.75f, 1.5f, { { 0.75f, 0.2f, 1.2f } },
        { { 0.1f, SON_MAGIE, 0.4f, 0.7f }, { 0.45f, SON_POISON, 0.6f, 1.1f }, { 0.75f, SON_POISON, 0.9f, 0.85f } }, 0x8CFF3A },
    [MG_TERRE] = { 0.6f, 1.5f, { { 0.15f, 0.45f, 0.6f }, { 0.6f, 0.4f, 2.5f } },
        { { 0.15f, SON_MAGIE, 0.4f, 0.6f }, { 0.6f, SON_TERRE, 1, 0.9f }, { 0.62f, SON_COUP, 0.6f, 0.5f } }, 0xC8955A },
    [MG_EAU] = { 0.62f, 1.5f, { { 0.55f, 0.35f, 2 } },
        { { 0.1f, SON_MAGIE, 0.4f, 1.2f }, { 0.55f, SON_EAU, 0.9f, 1.0f }, { 0.75f, SON_EAU, 0.6f, 1.3f } }, 0x3AA8FF },
    [MG_VENT] = { 0.72f, 1.5f, { { 0.2f, 0.9f, 0.6f }, { 0.72f, 0.3f, 1.8f } },
        { { 0.1f, SON_VENT, 0.6f, 1.2f }, { 0.4f, SON_VENT, 0.7f, 0.9f }, { 0.72f, SON_LAME, 0.6f, 1.3f } }, 0xB8FFD8 },
    [MG_NEUTRE] = { 0.8f, 1.65f, { { 0.8f, 0.5f, 2.5f } },
        { { 0.1f, SON_MAGIE, 0.6f, 1.0f }, { 0.5f, SON_MAGIE, 0.6f, 1.5f }, { 0.8f, SON_EXPLOSION, 0.9f, 1.0f } }, 0xE6E0FF },
    [MG_SOIN] = { 0.55f, 1.35f, { { 0 } }, { { 0.05f, SON_SOIN, 0.5f, 1.2f }, { 0.55f, SON_SOIN, 0.7f, 1.0f } }, 0x7DFF9A },
};
static int is_sort(int fx) { return fx >= FX_MAGIE && fx < FX_N; }
static float fx_hit(int fx) { return fx < 0 ? 0.45f : is_sort(fx) ? MAGIE[FX_ELEM(fx)].hit * MG_TS(FX_NIV(fx)) : FX_HIT[fx]; }
static float fx_fin(int fx) { return is_sort(fx) ? MAGIE[FX_ELEM(fx)].fin * MG_TS(FX_NIV(fx)) : 1.05f; }
#define TIR_RECUL 0.12f
#define TIR_RETOUR 0.25f
static float fx_dash(int fx) { return fx >= 0 && fx < FX_MAGIE ? FX_DASH[fx] : 0; }
/* Distance de l'élan du membre i (mètres) : jusqu'au contact de l'ennemi visé, selon l'effet de son action */
static float elan_dist(int i) {
    float bx, bz, ex, ez; battle_member(team_place(i), &bx, &bz); battle_vise(i, &ex, &ez);
    return fmaxf(0, hypotf(ex - bx, ez - bz) * E.w.ech - BAT_CONTACT) * fx_dash(BT.an.fx);
}
uint32_t fx_couleur(int fx) { return is_sort(fx) ? MAGIE[FX_ELEM(fx)].coul : 0; }
static const fx_son_t FX_SONS[FX_MAGIE][3];
/* Son i de l'effet fx au niveau du sort (0 : aucun) */
static int fx_son(int fx, int i, fx_son_t *o) {
    if (fx < 0 || i >= 3) return 0;
    *o = is_sort(fx) ? MAGIE[FX_ELEM(fx)].son[i] : FX_SONS[fx][i];
    if (o->vol <= 0) return 0;
    if (is_sort(fx)) { o->t *= MG_TS(FX_NIV(fx)); if (FX_NIV(fx) == 1) o->vol *= 0.8f; }
    return 1;
}
/* Secousse i du sort fx à son niveau (0 : aucune) */
static int fx_sec(int fx, int i, fx_sec_t *o) {
    if (!is_sort(fx) || i >= 3 || MAGIE[FX_ELEM(fx)].sec[i].dur <= 0) return 0;
    const int n = FX_NIV(fx);
    *o = MAGIE[FX_ELEM(fx)].sec[i]; o->t *= MG_TS(n); o->dur *= MG_TS(n); o->amp *= MG_AS(n);
    return 1;
}
static const fx_son_t FX_SONS[FX_MAGIE][3] = {
    { { 0, SON_ATTAQUE, 0.7f, 1 } },
    { { 0.28f, SON_ATTAQUE, 0.6f, 1.15f }, { 0.35f, SON_LAME, 0.55f, 1.1f }, { 0.42f, SON_LAME, 0.5f, 1.3f } },
    { { 0.18f, SON_ATTAQUE, 0.6f, 1.5f }, { 0.36f, SON_TIR, 0.5f, 1.8f } },
    { { 0.25f, SON_TIR, 0.6f, 1.0f }, { 0.33f, SON_TIR, 0.6f, 1.08f }, { 0.41f, SON_TIR, 0.6f, 0.95f } },
    { { 0.2f, SON_TIR, 0.7f, 0.6f }, { 0.5f, SON_EXPLOSION, 0.9f, 0.8f } },
    { { 0.25f, SON_ATTAQUE, 0.6f, 1.7f } },
    { { 0, SON_MAGIE, 0.6f, 1.2f }, { 0.45f, SON_MAGIE, 0.6f, 0.8f } },
    { { 0.38f, SON_COUP, 0.6f, 1.1f }, { 0.46f, SON_COUP, 0.6f, 1.25f } },
    { { 0.2f, SON_LASER, 0.6f, 1.0f }, { 0.28f, SON_LASER, 0.55f, 1.06f }, { 0.36f, SON_LASER, 0.55f, 0.94f } },
    { { 0.2f, SON_LASER, 0.6f, 0.6f }, { 0.3f, SON_FEU, 0.6f, 1.2f } },
};
const char *const ELEM_NOM[MAGIE_N] = { "FOUDRE", "FEU", "GLACE", "SACRÉ", "MÉTÉORE", "NÉANT", "POISON", "TERRE", "EAU", "VENT", "NEUTRE", "SOIN" };
/* Élément de l'objet i (MG_*), -1 : aucun : celui que la forge d'un ferrailleur lui a donné (E.eq_el), sinon le sien (JOBS ;
   eqElDe de l'émulateur) */
int eq_el_de(int i) {
    const eqitem_t *r = eq_item(i);
    if (!r) return -1;
    const int f = i >= 0 && i < 256 ? E.eq_el[i >> 1] >> ((i & 1) * 4) & 15 : 0;
    if (f && f <= MAGIE_N) return f - 1;
    return r->el && r->el <= MAGIE_N ? r->el - 1 : -1;
}
void eq_el_poser(int i, int el) {
    if (i >= 0 && i < 256) E.eq_el[i >> 1] = (uint8_t)((E.eq_el[i >> 1] & ~(15 << ((i & 1) * 4))) | ((el + 1) & 15) << ((i & 1) * 4));
}
/* Élément de l'arme que porte le membre k (MG_*), -1 : aucun (armeElement de l'émulateur) */
int arme_element(int k) {
    const int i = k >= 0 && k < NMEMB ? E.eq[k][EQ_ARME] : EQ_AUCUN;
    const eqitem_t *r = eq_item(i);
    return r && E.w.eqtypes[r->cat].slot == EQ_ARME ? eq_el_de(i) : -1;
}
/* Le membre m porte une armure ou un accessoire de l'élément el (membreResiste de l'émulateur) */
int membre_resiste(int m, int el) {
    if (m < 0 || m >= NMEMB || el < 0) return 0;
    for (int sl = EQ_ARMURE; sl <= EQ_ACCESS; sl++) {
        const int i = E.eq[m][sl];
        const eqitem_t *r = eq_item(i);
        if (r && E.w.eqtypes[r->cat].slot != EQ_ARME && eq_el_de(i) == el) return 1;
    }
    return 0;
}
/* Noyau élémentaire de l'élément el dans la table des objets, -1 : aucun (noyauDe de l'émulateur) */
int noyau_de(int el) {
    for (int i = 0; i < n_items; i++) if (ITEMS[i].noyau == el + 1 && item_vu(&ITEMS[i])) return i;
    return -1;
}
/* Un ennemi du paquet est faible à l'élément el : une arme de cet élément sert (elementUtile de l'émulateur) */
int element_utile(int el) {
    for (int k = 0; k < E.w.nenemies; k++) if (E.w.enemies[k].faible >> el & 1) return 1;
    return 0;
}
/* Effet et couleur de l'arme du membre k (sans arme : IMPACT, couleur du thème) */
static void fx_of(int k, int *fx, uint32_t *col) {
    const eqitem_t *r = eq_item(E.eq[k][EQ_ARME]);
    const eqtype_t *t = r ? &E.w.eqtypes[r->cat] : NULL;
    if (t && t->slot == EQ_ARME) { *fx = t->effet; *col = t->couleur; } else { *fx = FX_IMPACT; *col = BT_C1; }
}

/* Cut-ins (CUTI) : personnage du membre m (le premier : l'héroïne choisie, 255 pour Sera), image tirée parmi les siennes */
/* (la recrue de l'héroïne choisie : ceux de Sera, 255) */
static int cutin_pers(int m) { return m == 0 ? (E.heroine >= 0 ? E.heroine : 255) : membre_en_sera(m) ? 255 : E.w.party_perso[m]; }
/* Genre voulu par un effet (FX_*, sort : son élément) ; soin : CG_SOIN ; aucun effet (-1) : CG_NEUTRE (cutGenre de
   l'émulateur) */
int cut_genre(int fx, int soin) {
    static const uint8_t ARME[FX_MAGIE] = { CG_CORPS, CG_LAME, CG_FOUET, CG_TIR, CG_CANON, CG_ARC, CG_MAGIE, CG_CORPS, CG_LASER, CG_LAME };
    static const uint8_t ELEM[MAGIE_N] = { CG_FOUDRE, CG_FEU, CG_GLACE, CG_SACRE, CG_FEU, CG_TENEBRES, CG_TENEBRES, CG_MAGIE, CG_GLACE, CG_MAGIE, CG_MAGIE, CG_SOIN };
    if (soin) return CG_SOIN;
    if (fx >= FX_MAGIE) return (fx - FX_MAGIE) / 3 < MAGIE_N ? ELEM[(fx - FX_MAGIE) / 3] : CG_MAGIE;
    return fx >= 0 ? ARME[fx] : CG_NEUTRE;
}
/* Genre d'après le nom d'une technique sans effet propre (« TIR LASER », « LAME-FOUDRE ») : le premier mot-clé trouvé
   (cutGenreNom de l'émulateur) ; CG_NEUTRE sans mot-clé (alors l'arme décide) */
int cut_genre_nom(const char *nom) {
    static const struct { const char *mot; uint8_t g; } MOTS[] = {
        { "SOIN", CG_SOIN }, { "GUÉRI", CG_SOIN }, { "FOUDRE", CG_FOUDRE }, { "ÉCLAIR", CG_FOUDRE }, { "TONNERRE", CG_FOUDRE },
        { "FEU", CG_FEU }, { "FLAMM", CG_FEU }, { "BRASIER", CG_FEU }, { "INCEND", CG_FEU }, { "GLACE", CG_GLACE }, { "GIVRE", CG_GLACE },
        { "SACRÉ", CG_SACRE }, { "LUMIÈRE", CG_SACRE }, { "TÉNÈBRE", CG_TENEBRES }, { "OMBRE", CG_TENEBRES },
        { "NÉANT", CG_TENEBRES }, { "LASER", CG_LASER }, { "CANON", CG_CANON }, { "OBUS", CG_CANON }, { "ROQUETTE", CG_CANON },
        { "ARCAN", CG_MAGIE }, { "FLÈCHE", CG_ARC }, { "ARC", CG_ARC }, { "FOUET", CG_FOUET }, { "TIR", CG_TIR }, { "BALLE", CG_TIR }, { "RAFALE", CG_TIR },
        { "LAME", CG_LAME }, { "TAILLE", CG_LAME }, { "ENTAILLE", CG_LAME }, { "ÉPÉE", CG_LAME }, { "COUPE", CG_LAME },
        { "POING", CG_CORPS }, { "COUP DE PIED", CG_CORPS }, { "FRAPPE", CG_CORPS }, { "CHARGE", CG_CORPS },
        { "MAGIE", CG_MAGIE }, { "SORT", CG_MAGIE }, { "PIRAT", CG_MAGIE }, { "HACK", CG_MAGIE },
    };
    for (size_t i = 0; nom && i < sizeof MOTS / sizeof MOTS[0]; i++) if (strstr(nom, MOTS[i].mot)) return MOTS[i].g;
    return CG_NEUTRE;
}
/* Proximité de l'image de genre g au genre voulu v : 3 le même, 2 la même famille, 1 une pose neutre (ou rien de voulu) */
static int cut_proche(int v, int g) {
    static const uint8_t CG_PARENT[CG_N] = { CG_NEUTRE, CG_LAME, CG_TIR, CG_TIR, CG_CORPS, CG_MAGIE, CG_MAGIE, CG_MAGIE, CG_MAGIE, CG_MAGIE,
                                             CG_MAGIE, CG_MAGIE, CG_LAME, CG_TIR, CG_TIR };
    if (!v) return 1;
    if (g == v) return 3;
    return g && g < CG_N && CG_PARENT[g] == CG_PARENT[v] ? 2 : !g;
}
static int img_genre(int k) { return k >= 0 && k < E.w.nimg ? E.w.img_tab[k * 12 + 5] : 0; }
/* Meilleure proximité des images d'un jeu (faces seulement pour une paire) */
static int jeu_proche(int i, int genre) {
    const int pas = E.w.cutins[i].paire ? 2 : 1;
    int b = 0;
    for (int k = 0; k + pas <= E.w.cutins[i].n; k += pas) { const int p = cut_proche(genre, img_genre(E.w.cutins[i].first + k)); if (p > b) b = p; }
    return b;
}
int cutin_tirer(int pers, int tenue, int avant, int plein, int degats, int special, int genre, int *est_plein, int *est_paire) {
    /* jeu de la tenue portée d'abord (sinon celui de la tenue de base), plein écran ensuite s'il est demandé, puis celui
       qui a une image du genre voulu ; à égalité (bandeaux et paires face / dos), l'un d'eux au hasard */
    int best = -1, nb = 0, j = -1;
    tenue = tenue_base(tenue);   /* variante de couleur : les cut-ins de sa tenue */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < E.w.ncutins; i++) {
            const int t = E.w.cutins[i].pers != pers ? 0 : tenue >= 0 && E.w.cutins[i].tenue == tenue ? 2 : E.w.cutins[i].tenue == 255;
            if (!t || !cut_vu(E.w.cutins[i].ed) || (E.w.cutins[i].plein && !plein) || E.w.cutins[i].degats != degats || E.w.cutins[i].special != special) continue;
            const int sc = (t * 2 + E.w.cutins[i].plein) * 4 + jeu_proche(i, genre);
            if (!pass) { if (sc > best) { best = sc; nb = 1; } else if (sc == best) nb++; }
            else if (sc == best && j < 0 && (int)(brand() * nb) == 0) j = i;
            else if (sc == best) nb--;
        }
    if (j < 0) return -1;
    *est_plein = E.w.cutins[j].plein; *est_paire = E.w.cutins[j].paire;
    const int pas = E.w.cutins[j].paire ? 2 : 1, n = E.w.cutins[j].n / pas, f = E.w.cutins[j].first;   /* une paire : la face (rang pair) */
    if (n <= 0) return -1;
    /* parmi les images les plus proches du genre voulu, une autre que la précédente si possible */
    const int pb = jeu_proche(j, genre);
    int nc = 0, na = 0;
    for (int k = 0; k < n; k++) if (cut_proche(genre, img_genre(f + k * pas)) == pb) { nc++; na += f + k * pas != avant; }
    int r = (int)(brand() * (na ? na : nc)) % (na ? na : nc);
    for (int k = 0; k < n; k++)
        if (cut_proche(genre, img_genre(f + k * pas)) == pb && (!na || f + k * pas != avant) && r-- == 0) return f + k * pas;
    return f;
}

/* Cut-in tiré et lu dès le choix de l'action (technique, sort de niveau III) : la lecture se cache dans le choix de la
   cible ; plein : plein écran voulu (coup de grâce, grand sort), sinon un bandeau */
static void cutin_preparer(const char *nom, int plein, int special, int genre) {
    int iw, ih;
    const uint16_t *pal;
    BT.cut.tech = nom; BT.cut.plein = BT.cut.paire = 0;
    BT.cut.reac = 0; BT.cut.special = special;
    BT.cut.img = cutin_tirer(cutin_pers(BT.actor), tenue_of(BT.actor, 0), BT.cut.last, plein, 0, special, genre, &BT.cut.plein, &BT.cut.paire);
    if (BT.cut.img >= 0 && BT.cut.paire) img_paire(BT.cut.img, 0);
    else if (BT.cut.img >= 0) img_ix(BT.cut.img, &iw, &ih, &pal, 0);
}

int eveil_dispo(int m) {
    if (m == 0 && E.essai) return 1;   /* mode d'essai : à tout niveau */
    if (m < 0 || m >= NMEMB || E.level < NIV_EVEIL) return 0;
    for (int j = 0; j < E.w.njobs && j < 16; j++) if (!strcmp(E.w.jobs[j].nom, "ÉVEIL") && ((E.jobs_known[m] >> j) & 1)) return 1;
    return 0;
}
uint32_t eveil_aura(int m) {
    static const struct { const char *nom; uint32_t c; } AURA[6] = {
        { "SERA", 0xFF7AD9 }, { "JADE", 0xA45CFF }, { "AMBRE", 0xFF8A2A }, { "AZUR", 0x46E4FF }, { "SAULE", 0x34E88E }, { "ONYX", 0xFF2E4A } };
    const char *nom = m == 0 && E.heroine >= 0 ? E.w.persos[E.heroine].nom : membre_en_sera(m) ? PARTY[0].name   /* la recrue de l'héroïne choisie : celle de Sera */
                    : m >= 0 && m < NMEMB ? PARTY[m].name : NULL;
    for (int k = 0; nom && k < 6; k++) if (!strcmp(nom, AURA[k].nom)) return AURA[k].c;
    return 0xFFD54A;   /* les autres : or */
}
/* Fin de la transformation (après son cut-in) : le membre est transformé jusqu'à la fin du combat, puis le tour suivant */
static void eveil_fin(void) {
    BT.transfo[BT.actor] = 1; BT.cut.special = 0;
    popup(BT.actor, "ÉVEIL", 0xFFD54Au); son_jouer(SON_MAGIE, 0.8f, 0.7f);
    BT.shake = 0.4f; BT.shake_amp = 2;
    end_turn(1);
}

/* Action de zone : elle touche tout le groupe (sort d'attaque de niveau III, technique à l'effet BALAYAGE, attaque ultime),
   chacun × ZONE_F quand ils sont plusieurs (comme les sorts de niveau III des ennemis sur l'équipe) ; ATTAQUE : jamais */
#define ZONE_F 0.6f
static int act_zone(const bt_act_t *a, int fx) {
    if (!a->name || !strcmp(a->name, "ATTAQUE") || a->heal) return 0;
    return (is_sort(fx) && FX_NIV(fx) == 3 && FX_ELEM(fx) != MG_SOIN) || a->fx == FX_BALAYAGE;
}
static void lancer(const bt_act_t *act, int annonce);
static void perform(const bt_act_t *act);
/* L'objet i est pris dans le sac et part sur l'unité u (membre, ou BT_ENEMY + k) ; u < 0 : soin de toute l'équipe, objet lancé sur tout le groupe */
static void objet_utiliser(int i, int u) {
    const item_t *it = &ITEMS[i];
    bt_act_t a;
    memset(&a, 0, sizeof a);
    a.name = it->name; a.item = 1; a.cible1 = u + 1;
    if (it->type == OBJ_LANCER) { a.degats = it->hp; a.fx = it->mp; a.delay = it->hp ? 0 : FUMI_RETARD; }
    else if (it->releve && u >= 0 && u < NMEMB) { a.heal = (int)ceilf(PARTY[u].max * RELEVE_PART); a.soin = SOIN_RELEVE; }   /* relève : il reprend sa place dans la frise (apply_hit), un quart de ses PV */
    else if (u >= BT_ENEMY) { a.heal = it->hp; a.reaction = strcmp(it->name, "EAU CLAIRE") ? 1 : 2; a.fx = FX_SORT(MG_SOIN, 1); }
    else { a.heal = it->hp; a.rend_pm = it->mp; a.soin = u < 0 ? 2 : 0; }
    ITEMS[i].count--;
    perform(&a);
}
/* La cible debout suivante de u pour l'objet i : les camarades, puis les ennemis. Un objet lancé ne va qu'aux ennemis, ce qui ne rend que des PM qu'aux camarades.
   i < 0 : l'action en attente (cible_choisir) : l'ennemi debout suivant, ou le camarade suivant (debout ; à terre pour une relève) */
static int cible_suivante(int u, int i) {
    if (i < 0) {
        if (BT.pend_sorte == CIBLE_ENNEMI) {
            for (int k = 1; k <= BT.nen; k++) { const int q = (u - BT_ENEMY + k) % BT.nen; if (BT.en[q].hp > 0) return BT_ENEMY + q; }
            return u;
        }
        for (int k = 1; k <= NMEMB; k++) { const int q = (u + k) % NMEMB; if (in_team(q) && (BT.pend_sorte == CIBLE_RELEVE ? PARTY[q].hp <= 0 : PARTY[q].hp > 0)) return q; }
        return u;
    }
    if (ITEMS[i].releve) {   /* relève : le camarade à terre suivant, de l'équipe au combat */
        for (int k = 1; k <= NMEMB; k++) { const int q = ((u < NMEMB ? u : 0) + k) % NMEMB; if (in_team(q) && PARTY[q].hp <= 0) return q; }
        return u;
    }
    const int lance = ITEMS[i].type == OBJ_LANCER, ennemis = lance || ITEMS[i].hp > 0, tot = NMEMB + BT.nen, p = u >= BT_ENEMY ? NMEMB + u - BT_ENEMY : u;
    for (int k = 1; k <= tot; k++) {
        const int q = (p + k) % tot;
        if (q < NMEMB ? !lance && in_team(q) && PARTY[q].hp > 0 : ennemis && BT.en[q - NMEMB].hp > 0) return q < NMEMB ? q : BT_ENEMY + q - NMEMB;
    }
    return u;
}
/* Effet de l'action (celui que lancer lui donnera) : le sort, sinon la technique, sinon l'arme ; soin et objet : aucun */
static int act_fx(const bt_act_t *act) {
    int fx = -1; uint32_t col;
    if (act->sort) fx = act->fx;
    else if (!(act->heal || act->item)) { fx_of(BT.actor, &fx, &col); if (act->fx > 0 && act->fx < FX_N) fx = act->fx; }
    return fx;
}
/* Cible d'une action (11/11/2026, retour de jeu : « pour les sorts de soin on ne peut pas choisir la cible ; pour tous les sorts et
   attaques il faut pouvoir choisir ») : un soin pour un seul (pas toute l'équipe) va au camarade choisi, une attaque, une technique
   ou un sort sur un seul ennemi (pas une action de zone) à l'ennemi choisi, comme un objet (BP_ALLIE : SUIVANT, la cible, RETOUR, ou
   toucher son sprite). Seulement s'il y a plus d'une cible possible ; proposée d'abord : le camarade le plus blessé (relève : le
   premier à terre), l'ennemi visé. La commande est rejouée une fois la cible prise (cible_action). Renvoie 1 si l'action attend sa
   cible (cibleChoisir de l'émulateur) */
static int cible_choisir(int cmd, int arg, const bt_act_t *a) {
    if (BT.cible_prise) return 0;
    int sorte, n = 0, prop = -1;
    if (a->heal || a->rend_pm) {
        if (a->soin == 2) return 0;   /* toute l'équipe */
        sorte = a->soin == SOIN_RELEVE ? CIBLE_RELEVE : CIBLE_SOIN;
        float bk = 2;
        for (int q = 0; q < NMEMB; q++) {
            if (!in_team(q) || (sorte == CIBLE_RELEVE ? PARTY[q].hp > 0 : PARTY[q].hp <= 0)) continue;
            const float kq = sorte == CIBLE_RELEVE ? 0 : (float)PARTY[q].hp / PARTY[q].max;
            if (prop < 0 || kq < bk) { bk = kq; prop = q; }
            n++;
        }
    } else {
        if (act_zone(a, act_fx(a))) return 0;   /* tout le groupe */
        sorte = CIBLE_ENNEMI; n = battle_vivants();
        cible_maj(); prop = BT_ENEMY + BT.cible;
    }
    if (n < 2 || prop < 0) return 0;
    BT.pend_cmd = cmd; BT.pend_arg = arg; BT.pend_phase = BT.phase; BT.pend_sorte = sorte;
    BT.obj = -1; BT.allie = prop; BT.phase = BP_ALLIE;
    return 1;
}
/* L'action en attente part sur l'unité u (camarade, ou BT_ENEMY + k) : sa commande rejouée depuis sa phase, la cible prise */
static void cible_action(int u) {
    if (u >= BT_ENEMY) { BT.cible = u - BT_ENEMY; BT.pend_allie = -1; } else BT.pend_allie = u;
    BT.phase = BT.pend_phase; BT.cible_prise = 1;
    battle_command(BT.pend_cmd, BT.pend_arg);
    BT.cible_prise = 0;
}
static void perform(const bt_act_t *act) {
    member_t *m = &PARTY[BT.actor];
    if (act->mp) {
        if (m->mp < act->mp) { banner("PM INSUFFISANTS", NULL, AMBER, 0); return; }
        m->mp -= act->mp;
    }
    /* Technique d'un membre qui a des cut-ins : le bandeau d'abord (BP_CUTIN, CUT_S), puis l'action */
    if (BT.cut.img >= 0 && act->name == BT.cut.tech) {
        BT.cut.act = *act; BT.cut.t0 = E.clock; BT.phase = BP_CUTIN;
        son_jouer(SON_TECHNIQUE, 0.7f, 1);   /* son nom : dans le bandeau (draw_cutin) */
        return;
    }
    BT.cut.img = -1;
    lancer(act, 1);
}
/* L'action part ; annonce : son et bannière de la technique (le cut-in a déjà joué l'un et montré le nom) */
static void lancer(const bt_act_t *act, int annonce) {
    BT.phase = BP_ANIM;
    memset(&BT.an, 0, sizeof BT.an);
    BT.an.who = BT.actor; BT.an.act = *act; BT.an.t0 = E.clock;
    /* Effet du sort ; sinon celui de l'arme, ou celui de la technique (soin, objet : aucun) ; élan au contact selon
       l'effet ; technique et sort : effet plus grand */
    BT.an.fx = -1;
    if (act->sort) { BT.an.fx = act->fx; BT.an.col = fx_couleur(act->fx) ? fx_couleur(act->fx) : BT_C1; }
    else if (!(act->heal || act->item)) {
        fx_of(BT.actor, &BT.an.fx, &BT.an.col);
        if (act->fx > 0 && act->fx < FX_N) { BT.an.fx = act->fx; if (fx_couleur(act->fx)) BT.an.col = fx_couleur(act->fx); }
    }
    else if (act->item && act->fx > 0 && act->fx < FX_N) { BT.an.fx = act->fx; BT.an.col = fx_couleur(act->fx) ? fx_couleur(act->fx) : BT_C1; }   /* objet lancé, soin donné à un ennemi */
    BT.an.k = strcmp(act->name, "ATTAQUE") ? 1.4f : 1; BT.an.seed = (int)(brand() * 65536); BT.an.cue = 0;
    /* Pose (render.c, battle_anim) : élan et frappe au contact ; tir, laser, obus, flèche : ni élan ni frappe, recul au coup ;
       magie, piratage, éléments, soin, objet : la technique. Une technique sans effet propre se range d'abord d'après son nom
       (cut_genre_nom) : avant le 31/10/2026, un tir laser ou un PIRATAGE jouait la frappe de l'arme (lancer de l'émulateur) */
    const int g = act->name && strcmp(act->name, "ATTAQUE") && !(act->fx > 0 && act->fx < FX_N) ? cut_genre_nom(act->name) : CG_NEUTRE;
    const int g_magie = g >= CG_MAGIE && g <= CG_SOIN, g_loin = g == CG_TIR || g == CG_LASER || g == CG_CANON || g == CG_ARC;
    BT.an.melee = !act->sort && fx_dash(BT.an.fx) > 0 && !g_magie && !g_loin;
    BT.an.cast = BT.an.fx < 0 || BT.an.fx == FX_ARCANE || BT.an.fx >= FX_MAGIE || g_magie;
    BT.an.tir = !BT.an.melee && !BT.an.cast;
    if (act->cible1) BT.an.targets[BT.an.ntarget++] = act->cible1 - 1;   /* objet donné à un camarade ou à un ennemi, lancé sur un ennemi */
    else if (act->heal && act->soin == SOIN_RELEVE) {   /* relève (02/11/2026) : le premier membre de l'équipe au combat à terre */
        for (int i = 0; i < NMEMB && !BT.an.ntarget; i++) if (in_team(i) && PARTY[i].hp <= 0) BT.an.targets[BT.an.ntarget++] = i;
    } else if (act->heal || act->rend_pm) {   /* soignés : toute l'équipe (soin 2), sinon le plus blessé ; l'effet du sort est dessiné sur eux */
        int best = -1;
        for (int i = 0; i < NMEMB; i++) {
            if (!alive(i)) continue;
            if (act->soin == 2 && BT.an.ntarget < 4) BT.an.targets[BT.an.ntarget++] = i;
            else if (best < 0 || (float)PARTY[i].hp / PARTY[i].max < (float)PARTY[best].hp / PARTY[best].max) best = i;
        }
        if (act->soin != 2 && best >= 0) BT.an.targets[BT.an.ntarget++] = best;
    } else if (!act->item) {   /* ennemis touchés : tout le groupe (action de zone), sinon l'ennemi visé */
        cible_maj();
        BT.an.all = act_zone(act, BT.an.fx);
        for (int k = 0; k < BT.nen; k++)
            if (BT.en[k].hp > 0 && (BT.an.all || k == BT.cible)) BT.an.targets[BT.an.ntarget++] = BT_ENEMY + k;
    } else {   /* objet lancé sur tout le groupe */
        BT.an.all = 1;
        for (int k = 0; k < BT.nen; k++) if (BT.en[k].hp > 0) BT.an.targets[BT.an.ntarget++] = BT_ENEMY + k;
    }
    if (BT.an.melee) {   /* la course d'abord : BT.an.t0 est l'instant du contact ; le pas de celui qui choisissait en fait partie */
        const float d = elan_dist(BT.actor);
        BT.an.off0 = BT.avance[BT.actor] * PAS_ACTIF; BT.avance[BT.actor] = 0;
        BT.an.elan = fminf(ELAN_MAX, fmaxf(ELAN_MIN, (d - BT.an.off0) / ELAN_V));
        BT.an.retour = fminf(ELAN_MAX, fmaxf(ELAN_MIN, d / ELAN_V));
        BT.an.t0 += BT.an.elan;
    }
    if (annonce && !act->heal && strcmp(act->name, "ATTAQUE")) son_jouer(SON_TECHNIQUE, 0.7f, 1);
    if (annonce && strcmp(act->name, "ATTAQUE")) banner(act->name, NULL, BT_C1, 0);
}

/* Coup de l'équipe : soin du membre le plus blessé, ou dégâts (10 % de critiques × 1,6) et retard éventuel */
static void apply_hit(void) {
    const bt_act_t *act = &BT.an.act;
    char t[32];
    if (act->reaction) {   /* un soin donné à un ennemi : selon sa nature */
        for (int k = 0; k < BT.an.ntarget; k++) {
            const int u = BT.an.targets[k];
            bt_en_t *e = &BT.en[u - BT_ENEMY];
            if (e->hp <= 0) continue;
            const int machine = e->resiste >> MG_SOIN & 1;
            if ((e->faible >> MG_SOIN & 1) || (act->reaction == 2 && (e->faible >> MG_EAU & 1))) {
                const int pl = (int)(e->max * (BT.boss ? REACT_BOSS : REACT_PART)), d = act->heal < pl ? act->heal : pl;
                e->hp = e->hp - d > 0 ? e->hp - d : 0;
                e->flash = 0.16f; BT.hurt_t[u] = E.clock;
                snprintf(t, sizeof t, "%s %d", machine ? "COURT-CIRCUIT" : "ÇA BRÛLE", d); popup(u, t, 0xFFD34Au);
                effet_manette(ENG_EFFET_FRAPPE);
                if (BT.shake <= 0) BT.shake_amp = 1;
                BT.shake = fmaxf(BT.shake, 0.15f);
                son_jouer(SON_COUP, 0.8f, 1);
            }
            else if (machine) popup(u, "SANS EFFET", 0x8FA0B8u);
            else {
                const int h = act->heal < e->max - e->hp ? act->heal : e->max - e->hp;
                e->hp += h;
                snprintf(t, sizeof t, "+%d", h); popup(u, t, GREEN);
            }
        }
        return;
    }
    if (act->heal || act->rend_pm) {   /* soignés choisis par perform */
        for (int k = 0; k < BT.an.ntarget; k++) {
            const int u = BT.an.targets[k];
            member_t *m = &PARTY[u];
            if (act->soin == SOIN_RELEVE && m->hp > 0) continue;   /* relevé entre-temps */
            if (act->rend_pm) {   /* objet qui rend des PM (et parfois des PV) */
                const int rp = act->rend_pm < m->maxmp - m->mp ? act->rend_pm : m->maxmp - m->mp, rh = m->hp <= 0 ? 0 : act->heal < m->max - m->hp ? act->heal : m->max - m->hp;
                m->mp += rp; m->hp += rh;
                if (rh) snprintf(t, sizeof t, "+%d +%d PM", rh, rp); else snprintf(t, sizeof t, "+%d PM", rp);
                popup(u, t, GREEN);
                continue;
            }
            if (m->hp <= 0 && act->soin != SOIN_RELEVE) continue;   /* tombé entre-temps */
            const int h = act->heal < m->max - m->hp ? act->heal : m->max - m->hp;
            if (act->soin == SOIN_RELEVE) {   /* debout : il reprend sa place dans la frise, un de ses tours après celui qui agit */
                BT.ct[u] = BT.ct[BT.actor] + 100.0f / spd_of(u);
                BT.anim[u] = 0; BT.defending[u] = 0;
                popup(u, "DEBOUT", GREEN);
            }
            m->hp += h;
            snprintf(t, sizeof t, "+%d", h); popup(u, t, GREEN);
        }
        son_jouer(SON_SOIN, 0.6f, 1);
        return;
    }
    const int crit = !act->item && brand() < 0.1f;   /* un sort frappe avec le MAG, le reste avec l'ATQ ; un objet lancé : ses dégâts, sans critique */
    /* Chaque ennemi touché (l'ennemi visé, ou tout le groupe : × ZONE_F à plusieurs) : son tirage des dégâts, sa secousse */
    for (int i = 0; i < BT.an.ntarget; i++) {
        const int u = BT.an.targets[i];
        if (u < BT_ENEMY || u - BT_ENEMY >= BT.nen) continue;
        bt_en_t *e = &BT.en[u - BT_ENEMY];
        /* Faiblesse ou résistance de l'ennemi à l'élément de l'attaque : un sort, une technique à effet de sort, un objet lancé ;
           sinon (attaque, technique de l'arme) l'élément de l'arme qu'il porte (08/11/2026) */
        int el = act->fx >= FX_MAGIE && act->fx < FX_MAGIE + 3 * MAGIE_N ? (act->fx - FX_MAGIE) / 3 : -1;
        if (el < 0 && !act->sort && !act->item) el = arme_element(BT.an.who);
        const int fb = el >= 0 && (e->faible >> el & 1), rs = el >= 0 && (e->resiste >> el & 1);
        const float force = act->item ? (float)act->degats
                                      : (act->sort ? PARTY[BT.an.who].mag : PARTY[BT.an.who].atk) * (act->mult > 0 ? act->mult : 1) * (crit ? 1.6f : 1)
                                        * (BT.an.who < BT_ENEMY && BT.transfo[BT.an.who] ? 1.5f : 1)   /* transformée : ATQ × 1,5 */
                                        * (BT.an.ntarget > 1 ? ZONE_F : 1);
        const int dmg = (int)lroundf(force * rnd(0.9f, 1.1f) * (fb ? FAIBLE_X : rs ? RESISTE_X : 1));
        if (!act->item || act->degats) {   /* le fumigène ne blesse pas */
            e->hp = e->hp - dmg > 0 ? e->hp - dmg : 0;
            e->flash = 0.16f; BT.hurt_t[u] = E.clock;
            snprintf(t, sizeof t, "%s%d", fb ? "FAIBLE " : rs ? "RÉSISTE " : crit ? "CRITIQUE " : "", dmg); popup(u, t, fb ? 0xFFD34Au : rs ? 0x8FA0B8u : 0xFFFFFFu);
        }
        if (act->delay > 0 && e->hp > 0) { BT.ct[u] += 100.0f / e->spd * act->delay; popup(u, "TOUR RETARDÉ", BT_C1); }
    }
    effet_manette(ENG_EFFET_FRAPPE);   /* le coup porte : la manette vibre */
    if (BT.shake <= 0) BT.shake_amp = 1;   /* secousse d'un effet puissant en cours : gardée */
    BT.shake = fmaxf(BT.shake, crit || BT.an.fx == FX_OBUS ? 0.3f : 0.15f);
    if (!act->sort && !act->item) son_jouer(SON_COUP, crit ? 0.95f : 0.75f, crit ? 0.85f : 1);   /* un sort, un objet lancé : le son de son élément */
}

/* Attaque k du répertoire au prochain tour d'un ennemi, au lieu du tirage (essais : rendu_test « attaque:k » ; -1 : le tirage) */
int bt_atq_force = -1;
/* Tour d'un ennemi (BT.actor) : un de ses sorts (fiche : part de ses tours) ; sinon une attaque de son répertoire (ENAT,
   js/attaques.js de l'Atelier : 2 à 4 attaques qui lui ressemblent, tirées au poids ; celles sur toute l'équipe ont 30 % des
   tirages, et leurs puissances sont ramenées à la moyenne de la règle d'avant : l'espérance des dégâts ne change pas) ; sans
   répertoire, la règle d'avant : balayage (30 %) sur toute l'équipe, sinon rafale sur un membre au hasard (enemyAct de
   l'émulateur, tools/equilibrage/combat.js) */
static void enemy_act(void) {
    int alive_n = 0, list[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < NMEMB && alive_n < 4; i++) if (alive(i)) list[alive_n++] = i;
    /* Un de ses sorts d'attaque (fiche de l'ennemi : sorts, part de ses tours) ; niveau III : toute l'équipe */
    const int ek = BT.en[(BT.actor - BT_ENEMY) % BT_EMAX].ek;
    const enemy_t *en = ek >= 0 && ek < E.w.nenemies ? &E.w.enemies[ek] : NULL;
    int ls[MAX_SORTS], nl = 0;
    for (int s = 0; en && s < E.w.nsorts; s++) if ((en->sorts >> s & 1) && !E.w.sorts[s].soin) ls[nl++] = s;
    if (nl && alive_n && brand() * 100 < en->magie) {
        const sort_t *sp = &E.w.sorts[ls[(int)(brand() * nl) % nl]];
        const int tous = FX_NIV(sp->fx) == 3;
        banner(sp->nom, NULL, RED, 0);
        BT.phase = BP_ANIM;
        memset(&BT.an, 0, sizeof BT.an);
        BT.an.who = BT.actor; BT.an.all = tous; BT.an.t0 = E.clock;
        BT.an.fx = sp->fx; BT.an.col = fx_couleur(sp->fx); BT.an.k = 1.4f; BT.an.cast = 1;
        BT.an.act.name = sp->nom; BT.an.act.mult = sp->mult; BT.an.act.sort = 1; BT.an.act.fx = sp->fx;
        BT.an.seed = (int)(brand() * 65536);   /* sons et secousses du sort */
        son_jouer(SON_TECHNIQUE, 0.7f, 0.7f);
        if (tous) { memcpy(BT.an.targets, list, sizeof list); BT.an.ntarget = alive_n; }
        else { BT.an.targets[0] = list[(int)(brand() * alive_n) % alive_n]; BT.an.ntarget = 1; }
        return;
    }
    /* Attaque du répertoire : nom (bannière), effet (un sort : son dessin seulement, les dégâts sont ceux d'une attaque :
       apply_enemy_hit), ses sons ; un geste (morsure, griffes, tir) joue l'animation d'attaque, un effet magique (arcane,
       élément) celle de la technique ; une signature est plus grande, annoncée par le son d'une technique */
    if (en && en->natq > 0 && alive_n) {
        int tot = 0;
        for (int i = 0; i < en->natq; i++) tot += en->atq[i * ENAT_ATQ + 22];
        float r = brand() * (float)tot;
        const uint8_t *a = en->atq + (en->natq - 1) * ENAT_ATQ;
        for (int i = 0; i < en->natq; i++) { r -= en->atq[i * ENAT_ATQ + 22]; if (r < 0) { a = en->atq + i * ENAT_ATQ; break; } }
        if (bt_atq_force >= 0 && bt_atq_force < en->natq) a = en->atq + bt_atq_force * ENAT_ATQ;
        const int tous = a[21] & 1, sig = a[23] & 1, fx = a[20];
        const uint32_t col = (uint32_t)a[26] << 16 | (uint32_t)a[27] << 8 | a[28];
        banner((const char *)a, NULL, RED, 0);
        BT.phase = BP_ANIM;
        memset(&BT.an, 0, sizeof BT.an);
        BT.an.who = BT.actor; BT.an.all = tous; BT.an.t0 = E.clock;
        BT.an.fx = fx; BT.an.col = col ? col : fx_couleur(fx) ? fx_couleur(fx) : 0xFF3B4Eu; BT.an.k = sig ? 1.4f : 1;
        BT.an.act.name = (const char *)a; BT.an.act.mult = (a[24] | a[25] << 8) / 1000.0f; BT.an.act.fx = fx;
        BT.an.seed = (int)(brand() * 65536); BT.an.cue = 0; BT.an.cast = fx == FX_ARCANE || fx >= FX_MAGIE;
        if (sig) son_jouer(SON_TECHNIQUE, 0.7f, 0.7f);
        if (tous) { memcpy(BT.an.targets, list, sizeof list); BT.an.ntarget = alive_n; }
        else { BT.an.targets[0] = list[(int)(brand() * alive_n) % alive_n]; BT.an.ntarget = 1; }
        return;
    }
    const int all = brand() < 0.3f;
    banner(all ? "BALAYAGE THERMIQUE" : "RAFALE LASER", NULL, RED, 0);
    BT.phase = BP_ANIM;
    memset(&BT.an, 0, sizeof BT.an);
    BT.an.who = BT.actor; BT.an.all = all; BT.an.t0 = E.clock;
    BT.an.fx = all ? FX_BALAYAGE : FX_LASER; BT.an.col = all ? 0xFF7A3Du : 0xFF3B4Eu; BT.an.k = 1;
    BT.an.seed = (int)(brand() * 65536); BT.an.cue = 3; BT.an.cast = 1;   /* sons : ceux du tour de l'ennemi */
    son_jouer(SON_TECHNIQUE, 0.7f, all ? 0.6f : 0.8f);
    if (all) { memcpy(BT.an.targets, list, sizeof list); BT.an.ntarget = alive_n; }
    else if (alive_n) { BT.an.targets[0] = list[(int)(brand() * alive_n) % alive_n]; BT.an.ntarget = 1; }
}

static void apply_enemy_hit(void) {
    char t[24];
    int ko = 0;
    for (int k = 0; k < BT.an.ntarget; k++) {
        const int u = BT.an.targets[k];
        member_t *m = &PARTY[u];
        /* Défense (équipement) : dégâts × 200 / (200 + DÉF) */
        /* sort : ATQ × puissance (× 0,6 sur toute l'équipe) ; attaque du répertoire (ENAT) : ATQ × puissance × 6/7 à 8/7
           (l'écart d'avant, 1,2 à 1,6 autour de 1,4) ; sinon rafale ou balayage (règle d'avant) */
        const float fa = BT.an.act.sort ? BT.an.act.mult * (BT.an.all ? 0.6f : 1) * rnd(0.9f, 1.1f)
                       : BT.an.act.mult > 0 ? BT.an.act.mult * rnd(6 / 7.0f, 8 / 7.0f) : BT.an.all ? rnd(0.6f, 0.8f) : rnd(1.2f, 1.6f);
        int d = (int)lroundf(BT.en[(BT.an.who - BT_ENEMY) % BT_EMAX].atk * fa * 200.0f / (200.0f + m->def));
        if (BT.defending[u]) d = (int)lroundf(d / 2.0f);
        /* Élément du coup (sort, attaque élémentaire du répertoire) auquel l'armure ou l'accessoire du membre résiste : × RESISTE_X
           (08/11/2026, membre_resiste) */
        const int el = BT.an.act.fx >= FX_MAGIE && BT.an.act.fx < FX_N ? FX_ELEM(BT.an.act.fx) : -1, rs = el >= 0 && el != MG_SOIN && membre_resiste(u, el);
        if (rs) d = (int)lroundf(d * RESISTE_X);
        if (BT.transfo[u]) d = (int)lroundf(d * 0.7f);   /* transformée : dégâts reçus × 0,7 */
        const int avant = m->hp;
        m->hp = m->hp - d > 0 ? m->hp - d : 0; BT.hurt_t[u] = E.clock;
        /* Héroïne qui passe sous la moitié, puis sous le quart de ses PV : cut-in de dégâts (s'il y en a : édition 18+),
           une fois par combat et par degré, tiré et lu tout de suite, montré à la fin de l'attaque */
        const int deg = m->hp * 4 <= m->max ? 2 : m->hp * 2 <= m->max ? 1 : 0;
        if (deg && avant * (deg == 2 ? 4 : 2) > m->max && !(BT.degres_vus[u] & deg) && BT.reac_img < 0) {
            int pl, pa, iw, ih;
            const uint16_t *pal;
            BT.degres_vus[u] |= deg == 2 ? 3 : 1;
            BT.reac_img = cutin_tirer(cutin_pers(u), tenue_of(u, 0), BT.cut.last, 0, deg, 0, 0, &pl, &pa);
            if (BT.reac_img < 0 && deg == 2) BT.reac_img = cutin_tirer(cutin_pers(u), tenue_of(u, 0), BT.cut.last, 0, 1, 0, 0, &pl, &pa);
            if (BT.reac_img >= 0) img_ix(BT.reac_img, &iw, &ih, &pal, 0);
        }
        snprintf(t, sizeof t, "%s%d", rs ? "RÉSISTE " : "", d); popup(u, t, rs ? 0x8FA0B8u : 0xFF6A6Au);
        if (!m->hp) { popup(u, "KO", RED); son_jouer(SON_KO, 0.8f, 1); ko = 1; }
    }
    if (!BT.an.act.sort) son_jouer(SON_COUP, 0.7f, 1.15f);
    effet_manette(ko ? ENG_EFFET_KO : ENG_EFFET_COUP);   /* coup reçu, membre K.O. */
    if (BT.shake <= 0) BT.shake_amp = 1;   /* secousse d'un sort en cours : gardée */
    BT.shake = fmaxf(BT.shake, 0.3f);
}

void battle_update(float dt) {
    const float now = E.clock;
    /* Ennemis : blancs un instant quand ils sont touchés ; qui tombe clignote pendant sa chute (ENNEMI_CHUTE) */
    for (int k = 0; k < BT.nen; k++) {
        bt_en_t *e = &BT.en[k];
        if (e->hp <= 0 && e->mort_t > 0) e->flash = now - e->mort_t < ENNEMI_CHUTE && ((int)floorf((now - e->mort_t) / 0.09f) % 2) ? 0.1f : 0;
        else if (e->flash > 0) e->flash -= dt;
    }
    if (BT.phase == BP_INTRO && now - BT.t0 > INTRO_S) advance();
    for (int i = 0; i < NMEMB; i++) {   /* le membre qui choisit avance d'un pas, en marchant ; il y reste pendant son action ; les autres reviennent à leur place */
        const int agit = i == BT.actor && (bt_choix() || BT.phase == BP_CUTIN || (BT.phase == BP_ANIM && BT.an.who == i && !BT.an.melee));
        const float cible = agit ? 1.0f : 0.0f, v = dt * PAS_V / PAS_ACTIF;
        BT.avance[i] = cible > BT.avance[i] ? fminf(cible, BT.avance[i] + v) : fmaxf(cible, BT.avance[i] - v);
    }
    if (BT.phase == BP_ENEMY && now - BT.t0 > 0.8f) enemy_act();
    if (BT.phase == BP_CUTIN && now - BT.cut.t0 >= CUT_DUREE()) {
        BT.cut.last = BT.cut.img; BT.cut.img = -1;
        if (BT.cut.special == 1) eveil_fin();
        else if (BT.cut.reac) { BT.cut.reac = 0; end_turn(1); }
        else { lancer(&BT.cut.act, 0); BT.cut.special = 0; }
    }
    if (BT.phase == BP_ANIM) {
        const float t = now - BT.an.t0;
        const int who = BT.an.who;
        if (who >= BT_ENEMY && t < 0.4f) BT.en[(who - BT_ENEMY) % BT_EMAX].flash = ((int)floorf(t / 0.07f) % 2) ? 0.05f : 0;   /* l'ennemi clignote en attaquant */
        if (BT.an.melee && who < BT_ENEMY) {
            /* Élan au corps à corps : la course (t < 0), l'attaque sur place, le retour */
            const float d = elan_dist(who);
            BT.off[who] = t < 0 ? BT.an.off0 + (d - BT.an.off0) * (1 + t / BT.an.elan) : t < ELAN_FRAPPE ? d : d * fmaxf(0, 1 - (t - ELAN_FRAPPE) / BT.an.retour);
            BT.anim[who] += dt;
            if (BT.anim[who] > 0.09f) { BT.anim[who] = 0; BT.frame[who] = (BT.frame[who] + 1) % 4; }
        }
        /* Tir : recul de TIR_RECUL m au coup (en 0,05 s), revenu en TIR_RETOUR s */
        else if (BT.an.tir && who < BT_ENEMY) {
            const float u = t - fx_hit(BT.an.fx);
            BT.off[who] = u < -0.05f || u > TIR_RETOUR ? 0 : -TIR_RECUL * (u < 0 ? (u + 0.05f) / 0.05f : 1 - u / TIR_RETOUR);
        }
        fx_son_t so;
        while (fx_son(BT.an.fx, BT.an.cue, &so) && t >= so.t) { BT.an.cue++; son_jouer(so.son, so.vol, so.h); }
        fx_sec_t se;
        while (fx_sec(BT.an.fx, BT.an.scue, &se) && t >= se.t) { BT.an.scue++; BT.shake = fmaxf(BT.shake, se.dur); BT.shake_amp = se.amp; }
        if (!BT.an.hit && t > fx_hit(BT.an.fx)) { BT.an.hit = 1; if (who >= BT_ENEMY) apply_enemy_hit(); else apply_hit(); }
        if (t > (BT.an.melee ? fmaxf(fx_fin(BT.an.fx), ELAN_FRAPPE + BT.an.retour) : fx_fin(BT.an.fx))) {
            if (who < BT_ENEMY) { BT.off[who] = 0; BT.frame[who] = 0; }
            /* Ennemis abattus par l'action : leur chute commence (le dernier : celle de la fin du combat, BP_DYING) */
            for (int k = 0; k < BT.nen; k++) if (BT.en[k].hp <= 0 && BT.en[k].mort_t <= 0) BT.en[k].mort_t = now;
            if (battle_vivants() && who < BT_ENEMY) for (int k = 0; k < BT.nen; k++) if (BT.en[k].mort_t == now) { son_jouer(SON_EXPLOSION, 0.6f, 1.2f); break; }
            if (BT.reac_img >= 0) {   /* héroïne touchée : son cut-in de dégâts, puis le tour suivant */
                BT.cut.img = BT.reac_img; BT.reac_img = -1; BT.cut.reac = 1; BT.cut.plein = BT.cut.paire = 0;
                BT.cut.act.name = NULL; BT.cut.t0 = now; BT.phase = BP_CUTIN;
            } else end_turn(1);
        }
    }
    if (BT.phase == BP_FUI && now - BT.t0 > 0.8f) { battle_end(2); return; }
    if (BT.phase == BP_LOSE && (BT.police || BT.arene) && now - BT.t0 > POL_DEFAITE_S) { battle_end(0); return; }   /* l'arrestation (police.c), la sortie de l'arène */
    if (BT.phase == BP_DYING) {
        BT.shake = 0.1f; BT.shake_amp = 1;   /* le dernier tombe : il clignote (plus haut) */
        if (now - BT.t0 > ENNEMI_CHUTE) {
            BT.visible = 0; BT.phase = BP_WIN; BT.t0 = now; BT.shake = 0;
            effet_manette(ENG_EFFET_VICTOIRE);
            /* Récompense : expérience et crédits selon le niveau du combat (groupe : la somme des parts, GROUPE_XP), cœur
               d'une machine (groupe : chacune selon sa part), parfois un équipement de son palier, deux au plus pour un
               groupe, parfois un soin (DROP_SOIN) (tableau de draw_victoire de hud.c) */
            float parts = 0;
            for (int k = 0; k < BT.nen; k++) parts += BT.en[k].part;
            const int xp0 = (int)lroundf(exp_of(BT.lvl) * parts);
            BT.rw_xp = BT.boss == 2 ? (int)lroundf(xp0 * BOSS_RW_ANNEXE * ANNEXE_XP) : BT.boss ? xp0 * BOSS_RW : (int)lroundf(xp0 * RENC_XP);
            BT.rw_cr = (int)lroundf(credits_of(BT.lvl) * parts) * (BT.boss ? BOSS_RW : 1);
            if (BT.arene) { BT.rw_xp = (int)lroundf(BT.rw_xp * ARN_RW); BT.rw_cr = (int)lroundf(BT.rw_cr * ARN_RW); }   /* arène (arene.c) */
            BT.rw_loot = item_loot(); BT.rw_nloot = 0;
            for (int k = 0; k < BT.nen; k++) if (drops_heart(BT.en[k].perso) && (BT.en[k].part >= 1 || brand() < BT.en[k].part) && (BT.boss || brand() < CRISTAL_RENC)) BT.rw_nloot++;
            if (!BT.rw_nloot) BT.rw_loot = -1;
            BT.rw_eq = BT.rw_eq2 = -1;
            for (int k = 0; k < BT.nen && BT.rw_eq2 < 0; k++) { const int q = drop_equip_de(k); if (q >= 0) { if (BT.rw_eq < 0) BT.rw_eq = q; else BT.rw_eq2 = q; } }
            BT.rw_soin = brand() < DROP_SOIN ? soin_butin(BT.lvl) : -1;   /* un soin, une victoire sur trois ou presque */
            BT.rw_noyau = noyau_butin();   /* parfois un noyau élémentaire (08/11/2026) */
            apprentissage(0);   /* magie apprise : sorts des niveaux gagnés, points des cristaux (tableau de victoire) */
        }
    }
    if (BT.shake > 0) BT.shake -= dt;
}

/* Commande de l'éventail (boutons de hud.c) */
void battle_command(int cmd, int arg) {
    if (!BT.on || BT.actor < 0 || BT.actor >= BT_ENEMY) return;
    const int u = BT.actor;
    member_t *m = &PARTY[u];
    switch (cmd) {
    case BC_ATTACK:
        if (BT.phase != BP_CMD) return;
        memset(&BT.pending, 0, sizeof BT.pending);
        BT.pending.name = "ATTAQUE"; BT.pending.mult = 1;
        if (cible_choisir(BC_ATTACK, 0, &BT.pending)) break;   /* plusieurs ennemis debout : lequel ? */
        perform(&BT.pending);   /* l'action part sur l'ennemi visé (attackChoisie de l'émulateur) */
        break;
    case BC_TECHS: if (BT.phase == BP_CMD) BT.phase = BP_TECH; break;
    case BC_ITEMS: if (BT.phase == BP_CMD) { BT.phase = BP_ITEM; BT.mpage = 0; } break;
    case BC_DEFEND:
        if (BT.phase != BP_CMD) return;
        BT.defending[u] = 1; popup(u, "DÉFENSE", BT_C1); end_turn(0.6f);
        break;
    case BC_FLEE:
        if (BT.phase != BP_CMD) return;
        if (BT.boss || BT.police || BT.arene) { banner("FUITE IMPOSSIBLE", BT.police ? "Les agents vous encerclent" : BT.arene ? "Combat de l'arène" : "Combat de l'histoire", AMBER, 0); son_jouer(SON_ANNULER, 0.45f, 1); return; }
        if (brand() < fuite_chance()) { BT.phase = BP_FUI; BT.t0 = E.clock; banner("FUITE !", NULL, BT_C1, 0); son_jouer(SON_VALIDER, 0.6f, 1); }
        else { BT.fuites++; popup(u, "FUITE RATÉE", AMBER); end_turn(1); }
        break;
    case BC_TECH: {
        if (BT.phase != BP_TECH || arg < 0 || arg >= m->ntech) return;
        const tech_t *t = &m->tech[arg];
        if (m->mp < t->mp) { banner("PM INSUFFISANTS", NULL, AMBER, 0); return; }
        bt_act_t a = { t->name, t->mp, t->heal, 0, t->mult, t->delay, t->fx, 0, 0 };
        if (cible_choisir(BC_TECH, arg, &a)) break;   /* à qui ? */
        if (BT.cible_prise && BT.pend_allie >= 0) a.cible1 = BT.pend_allie + 1;
        /* coup de grâce (le dernier ennemi debout tombe même au plus bas du tirage des dégâts) : cut-in plein écran */
        /* genre de l'image : l'effet de la technique, sinon son nom (« TIR LASER »), sinon l'arme (soin : CG_SOIN) */
        int fx = t->fx; uint32_t col;
        int genre = fx > 0 && fx < FX_N ? cut_genre(fx, t->heal) : t->heal ? CG_SOIN : cut_genre_nom(t->name);
        if (genre == CG_NEUTRE) { fx_of(u, &fx, &col); genre = cut_genre(fx, 0); }
        cible_maj();
        cutin_preparer(t->name, !t->heal && battle_vivants() == 1 && m->atk * (t->mult > 0 ? t->mult : 1) * 0.9f >= BT.en[BT.cible].hp, 0, genre);
        perform(&a);
        break;
    }
    case BC_EVEIL: {   /* transformation : son cut-in (énergie par-dessus), puis transformée jusqu'à la fin du combat */
        if (BT.phase != BP_TECH || !eveil_dispo(u) || BT.transfo[u]) return;
        static const bt_act_t EV = { "ÉVEIL", 0, 0, 0, 0, 0, -1, 0, 0 };
        cutin_preparer(EV.name, 1, 1, CG_NEUTRE);
        if (BT.cut.img < 0) { eveil_fin(); break; }
        BT.cut.act = EV; BT.cut.t0 = E.clock; BT.phase = BP_CUTIN;
        son_jouer(SON_TECHNIQUE, 0.8f, 0.8f);
        break;
    }
    case BC_ULTIME: {   /* attaque ultime (transformée) : long cut-in plein écran, puis un sacré de niveau III × 6 */
        if (BT.phase != BP_TECH || !BT.transfo[u]) return;
        static const bt_act_t UL = { "ULTIME", ULTIME_PM, 0, 0, 6.0f, 0, FX_SORT(MG_SACRE, 3), 0, 0 };
        if (m->mp < UL.mp) { banner("PM INSUFFISANTS", NULL, AMBER, 0); return; }
        cutin_preparer(UL.name, 1, 2, CG_NEUTRE);
        BT.pending = UL; perform(&BT.pending);
        break;
    }
    case BC_POTION: {   /* arg : place de l'objet dans la page */
        uint8_t l[MAX_ITEMS]; int first, np;
        const int n = battle_objets(l), c = sorts_page(n, &first, &np);
        if (BT.phase != BP_ITEM || arg < 0 || arg >= c) return;
        const int i = l[first + arg];
        if (ITEMS[i].releve) {   /* relève (SERINGUE DE RAPPEL) : un camarade à terre de l'équipe au combat, le premier d'office */
            int ko = -1;
            for (int q = 0; q < NMEMB && ko < 0; q++) if (in_team(q) && PARTY[q].hp <= 0) ko = q;
            if (ko < 0) { banner("PERSONNE À TERRE", NULL, AMBER, 0); return; }
            BT.obj = i; BT.allie = ko; BT.phase = BP_ALLIE;
            break;
        }
        if (ITEMS[i].team) { objet_utiliser(i, -1); break; }
        /* À qui ? Lancé : d'abord l'ennemi visé ; un soin : d'abord le camarade le plus blessé (en PV, ou en PM pour un objet qui n'en rend que) */
        int best = u; float bk = 2;
        if (ITEMS[i].type == OBJ_LANCER) { cible_maj(); best = BT_ENEMY + (BT.cible > 0 ? BT.cible : 0); }
        else for (int q = 0; q < NMEMB; q++) {
            if (!in_team(q) || PARTY[q].hp <= 0) continue;
            const float kq = ITEMS[i].hp > 0 ? (float)PARTY[q].hp / PARTY[q].max : (float)PARTY[q].mp / (PARTY[q].maxmp > 0 ? PARTY[q].maxmp : 1);
            if (kq < bk) { bk = kq; best = q; }
        }
        BT.obj = i; BT.allie = best; BT.phase = BP_ALLIE;
        break;
    }
    case BC_ALLIE_SUIV:
        if (BT.phase != BP_ALLIE) break;
        BT.allie = cible_suivante(BT.allie, BT.obj);
        if (BT.allie >= BT_ENEMY) BT.cible = BT.allie - BT_ENEMY;
        break;
    case BC_ALLIE_OK: if (BT.phase == BP_ALLIE) { if (BT.obj >= 0) objet_utiliser(BT.obj, BT.allie); else cible_action(BT.allie); } break;
    case BC_BACK:   /* retour ; à l'éventail (rien à quitter), l'ennemi visé suivant (clavier, manette) */
        if (BT.phase == BP_ALLIE) BT.phase = BT.obj >= 0 ? BP_ITEM : BT.pend_phase;
        else if (BT.phase == BP_MAGIE) BT.phase = BP_TECH;
        else if (BT.phase == BP_TECH || BT.phase == BP_ITEM || BT.phase == BP_TARGET) { BT.phase = BP_CMD; BT.locked = 0; }
        else if (BT.phase == BP_CMD && battle_vivants() > 1) battle_command(BC_CIBLE, -1);
        break;
    case BC_CIBLE:   /* arg : l'ennemi visé désormais (-1 : le suivant debout) */
        if (!bt_choix() || BT.nen < 2) return;
        if (arg < 0) { for (int k = 1; k <= BT.nen; k++) if (BT.en[(BT.cible + k) % BT.nen].hp > 0) { arg = (BT.cible + k) % BT.nen; break; } }
        if (arg < 0 || arg >= BT.nen || BT.en[arg].hp <= 0 || arg == BT.cible) return;
        BT.cible = arg; son_jouer(SON_VALIDER, 0.35f, 1.3f);
        break;
    case BC_MAGIES: { int l[MAX_SORTS]; if (BT.phase == BP_TECH && sorts_of(u, l)) { BT.phase = BP_MAGIE; BT.mpage = 0; } break; }
    case BC_SUITE: if (BT.phase == BP_MAGIE || BT.phase == BP_ITEM) BT.mpage++; break;
    case BC_SORT: {   /* arg : place du sort dans la page */
        int l[MAX_SORTS], first, np;
        const int n = sorts_of(u, l), c = sorts_page(n, &first, &np);
        if (BT.phase != BP_MAGIE || arg < 0 || arg >= c) return;
        const sort_t *sp = &E.w.sorts[l[first + arg]];
        if (m->mp < sp->pm) { banner("PM INSUFFISANTS", NULL, AMBER, 0); return; }
        if (sp->soin == SOIN_RELEVE) {   /* relever : il faut quelqu'un à terre dans l'équipe au combat */
            int ko = 0; for (int i = 0; i < NMEMB; i++) ko |= in_team(i) && PARTY[i].hp <= 0;
            if (!ko) { banner("PERSONNE À TERRE", NULL, AMBER, 0); return; }
        }
        bt_act_t a = { sp->nom, sp->pm, sp->soin ? (int)lroundf(m->mag * sp->mult) : 0, 0, sp->mult, 0, sp->fx, 1, sp->soin };
        if (cible_choisir(BC_SORT, arg, &a)) break;   /* à qui ? */
        if (BT.cible_prise && BT.pend_allie >= 0) a.cible1 = BT.pend_allie + 1;
        if (sp->fx >= FX_MAGIE && FX_NIV(sp->fx) == 3) cutin_preparer(sp->nom, 1, 0, cut_genre(sp->fx, sp->soin));   /* grand sort : cut-in plein écran, de son élément */
        perform(&a);
        break;
    }
    }
}

/* Ennemi sous le doigt (x, y) : le plus proche de son sprite (entre le haut et les pieds, VISE_X de part et d'autre de son
   milieu), -1 : aucun (ennemiTouche de l'émulateur) */
#define VISE_X 48.0f
int battle_enemy_at(float x, float y) {
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k < BT.nen; k++) {
        const screen_pos_t *p = &E.scr_enemy[k];
        if (!p->vis || BT.en[k].hp <= 0 || fabsf(x - p->cx) > VISE_X || y < p->top - 20 || y > p->foot + 20) continue;
        const float d = hypotf(x - p->cx, y - p->cy);
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}
/* Camarade de l'équipe au combat sous le doigt (x, y), debout (à terre : ko) : le plus proche du milieu de son sprite, parmi ceux
   dont la boîte contient le doigt ; -1 : aucun (membreTouche de l'émulateur). Avant le 11/11/2026, le premier de PARTY : l'équipe est
   serrée, et un toucher sur Bors donnait l'objet à Varn */
static int membre_at(float x, float y, int ko) {
    int best = -1;
    float bd = 1e9f;
    for (int q = 0; q < NMEMB; q++) {
        const screen_pos_t *cq = &E.scr_party[q];
        if (!in_team(q) || (ko ? PARTY[q].hp > 0 : PARTY[q].hp <= 0) || !cq->vis || fabsf(x - cq->cx) >= 44 || y <= cq->top || y >= cq->foot + 12) continue;
        const float d = hypotf(x - cq->cx, y - cq->cy);
        if (d < bd) { bd = d; best = q; }
    }
    return best;
}
/* Toucher hors des boutons : commencer, terminer, viser un ennemi du groupe (pendant le choix d'une action), ou viser
   l'ennemi (à moins de 100 px de son centre) */
void battle_tap(float x, float y) {
    if (!BT.on) return;
    if (BT.phase == BP_INTRO) { BT.ban.on = 0; advance(); return; }
    if (BT.phase == BP_WIN) { battle_end(1); return; }
    if (BT.phase == BP_LOSE) return;   /* défaite : ses deux boutons (draw_defaite de hud.c, battle_defaite) */
    if (BT.phase == BP_ALLIE && BT.obj < 0) {   /* action en attente : toucher sa cible, un ennemi, ou un camarade pour un soin (à terre pour une relève) */
        if (BT.pend_sorte == CIBLE_ENNEMI) { const int k = battle_enemy_at(x, y); if (k >= 0) cible_action(BT_ENEMY + k); return; }
        const int q = membre_at(x, y, BT.pend_sorte == CIBLE_RELEVE);
        if (q >= 0) cible_action(q);
        return;
    }
    if (BT.phase == BP_ALLIE) {   /* toucher un camarade : l'objet est pour lui ; un ennemi : un objet lancé part, un soin demande un second toucher (le soigner par erreur coûte cher) */
        const int lance = ITEMS[BT.obj].type == OBJ_LANCER;
        if (lance || ITEMS[BT.obj].hp > 0) {
            const int k = battle_enemy_at(x, y);
            if (k >= 0) {
                if (lance || BT.allie == BT_ENEMY + k) objet_utiliser(BT.obj, BT_ENEMY + k);
                else { BT.allie = BT_ENEMY + k; BT.cible = k; }
                return;
            }
        }
        const int q = lance ? -1 : membre_at(x, y, ITEMS[BT.obj].releve);   /* une relève : un camarade à terre */
        if (q >= 0) objet_utiliser(BT.obj, q);
        return;
    }
    if (bt_choix() && BT.phase != BP_TARGET && BT.nen > 1) { const int k = battle_enemy_at(x, y); if (k >= 0) battle_command(BC_CIBLE, k); return; }
    const screen_pos_t *c = &E.scr_enemy[BT.cible > 0 ? BT.cible : 0];
    if (BT.phase == BP_TARGET && c->vis && hypotf(x - c->cx, y - c->cy) < 100) {
        if (!BT.locked) BT.locked = 1; else perform(&BT.pending);
    }
}

/* Ennemis visibles (06/11/2026, demande de l'utilisateur ; patrouilles de l'émulateur, à garder identiques) : à pied, une rencontre n'est plus un combat qui
   tombe du ciel. Quand encounter_step (game.c) la tire, un groupe de la taille d'une rencontre (encounter_taille, encounter_groupe) paraît à PATR_LOIN cases devant
   Sera (± 60° autour de sa direction), hors de l'écran. Il rôde autour de sa place (PATR_RAYON cases, à PATR_MARCHE, pauses de 1 à 3 s) ; Sera à moins de PATR_VUE
   cases (à pied, hors de l'eau) : il la chasse à PATR_COURSE (plus vite que Sera ne marche, moins vite qu'elle ne court : on peut l'éviter) ; à plus de PATR_PERDU
   cases, il rôde là où il est ; au contact (PATR_CONTACT) : le combat, au niveau d'une rencontre. Vaincu, il s'en va ; fui, il reste PATR_REPOS s sans chasser. À plus
   de PATR_OUBLI cases, à un changement de carte ou au début d'une scène, il disparaît. Les suiveurs vont en file derrière le chef, PATR_FILE case par rang. Vitesses
   en unités par seconde, comme la marche de Sera (÷ ech). PATR_MAX groupes au plus : sinon, rien ne paraît. En vol permanent, le combat direct d'avant */
patr_t PATR[PATR_MAX];
/* Zones sûres (07/11/2026, demande de l'utilisateur) : villes, villages, campements, astroport, village du circuit, bases amies des autres cartes (SURS, écrite par l'émulateur ; intérieur des
   enceintes). Aucune rencontre n'y naît (encounter_step, patrouille_poser), un groupe ne chasse pas Sera qui s'y trouve et n'y entre pas : il reste à la limite. La police et les gardes des villes
   ne sont pas concernés (police.c ; zoneSure de l'émulateur) */
int zone_sure(float x, float z) {
    const world_t *w = &E.w;
    for (int i = 0; i < w->nsures; i++) {
        const uint8_t *s = w->sures + i * 16;
        if (s[0] != w->carte) continue;
        float q[3]; memcpy(q, s + 4, 12);
        if (hypotf(x - q[0], z - q[1]) < q[2]) return 1;
    }
    return enceinte_dedans(x, z);
}
/* Retour de jeu du 07/11/2026 (« je ne vois aucun ennemi dans le monde ») : à ± 60°, 8 à 10 groupes paraissaient en 3 min de course mais 1 ou 2 engageaient le combat ; ils naissaient à
   10 cases de côté, hors de l'écran, et Sera les dépassait. Sur son chemin (PATR_ANGLE radians en tout), une vue plus longue (rendu_test marchevue:n:cap) */
#define PATR_ANGLE 0.5f
#define PATR_SERA_MAX 12.0f
#define PATR_AVANCE 4.0f   /* s : la chasse vise où Sera sera, jusqu'à ce temps d'avance : un groupe lui coupe la route (droit sur elle, il ne rattrapait jamais qui court) */
#define PATR_HAUT 1.2f     /* radians au plus (0,8 avant le zoom à 300 % partout, 07/11/2026) : direction tournée vers le haut de l'écran, qui montre 10 cases devant la caméra et 2 de côté ; sinon le groupe n'y entre qu'au dernier moment */
/* Sera qui court vers le bas de l'écran (plus de PATR_DOS entre sa course et le haut de l'écran) : le groupe naît sur son chemin, devant elle,
   hors de l'écran (on n'y voit qu'une ou deux cases vers le bas) ; né en haut de l'écran, il était derrière elle et ne la rattrapait jamais
   (08/11/2026, retour de jeu : un cinquième de la carte traversé en diagonale sans un combat ; rendu_test marchevue:n:cap) */
#define PATR_DOS 1.6f
#define PATR_LOIN 9.0f     /* cases (12 avant le zoom à 300 % partout : ils naissaient trop loin et de côté, vus 0,2 s avant le contact) */
#define PATR_RAYON 4.0f
#define PATR_MARCHE 1.4f
#define PATR_VUE 13.0f
#define PATR_COURSE 4.5f   /* un peu moins vite que Sera ne court (5,2 ; 07/11/2026, demande de l'utilisateur : qui court assez longtemps les sème, à PATR_PERDU ; rendu_test semer:d:n) */
#define PATR_PERDU 15.0f
#define PATR_CONTACT 1.0f
#define PATR_OUBLI 40.0f
#define PATR_FILE 0.9f
/* Souterrain (08/11/2026, retour de jeu : « aucun ennemi dans les souterrains ») : un groupe naissait à PATR_LOIN cases devant Sera sur toute case
   praticable, la roche des parois comprise (rien ne la bloque hors de la planète) ou de l'autre côté d'un canal : sur la roche, la zone des montagnes
   (oiseaux de la planète) au lieu de la caverne, et ni l'un ni l'autre n'atteignait Sera. Il naît désormais au bout d'un chemin droit depuis Sera
   (cases praticables, pas plus haut que PATR_PENTE par demi-case : ni eau ni marche de roche), à PATR_LOIN, 7 ou 5 cases, et ne grimpe pas en
   marchant (patr_pas) ; une rencontre qui ne trouve pas de place est retentée PATR_REESSAI cases plus loin (encounter_step) */
#define PATR_PENTE 0.6f
static int patr_chemin(float x0, float z0, float x1, float z1) {
    const int n = (int)ceilf(hypotf(x1 - x0, z1 - z0) / 0.5f);
    float hp = ground_y(x0, z0);
    for (int i = 1; i <= n; i++) {
        const float t = (float)i / n, x = x0 + (x1 - x0) * t, z = z0 + (z1 - z0) * t, h = ground_y(x, z);
        if (!sol_marchable(x, z) || fabsf(h - hp) > PATR_PENTE) return 0;
        hp = h;
    }
    return 1;
}
/* Un pas d'un ennemi du groupe de (x0, z0) à (x1, z1) : praticable, hors zone sûre, pas plus raide que PATR_PENTE par demi-case */
static int patr_pas(float x0, float z0, float x1, float z1) {
    return sol_marchable(x1, z1) && !zone_sure(x1, z1) && fabsf(ground_y(x1, z1) - ground_y(x0, z0)) <= PATR_PENTE * 2 * hypotf(x1 - x0, z1 - z0) + 0.02f;
}
int patrouille_poser(int force) {
    const world_t *w = &E.w;
    int s = 0;
    while (s < PATR_MAX && PATR[s].on) s++;
    if (s == PATR_MAX) return -1;
    (void)force;
    int membres = 0;
    for (int m = 0; m < NMEMB; m++) membres += in_team(m);
    /* sur le chemin de Sera, dans le brouillard du haut de l'écran, sur une case praticable ; si elle court vers le bas de l'écran,
       devant elle (PATR_DOS) */
    float a0 = E.sera.dir;
    { const float ec = remainderf(E.yaw - a0, 2 * PI_F); if (fabsf(ec) <= PATR_DOS) a0 += fabsf(ec) < PATR_HAUT ? ec : (ec < 0 ? -PATR_HAUT : PATR_HAUT); }
    for (int essai = 0; essai < 12; essai++) {
        const float loin = essai < 6 ? PATR_LOIN : essai < 9 ? 7.0f : 5.0f;   /* plus près si rien ne va (galeries) */
        const float a = a0 + (brand() - 0.5f) * PATR_ANGLE, x = E.sera.x + sinf(a) * loin, z = E.sera.z + cosf(a) * loin;
        if (!sol_marchable(x, z) || zone_sure(x, z) || !patr_chemin(E.sera.x, E.sera.z, x, z)) continue;
        const int k = encounter_pick_at(x, z);
        if (k < 0) continue;
        const int h = script_niveau(x, z) > 0 ? script_niveau(x, z) : E.level, n = encounter_taille(k, h, membres);   /* comme une rencontre */
        patr_t *p = &PATR[s];
        memset(p, 0, sizeof *p);
        p->n = encounter_groupe(k, x, z, n, p->ek);
        p->lvl = encounter_level();
        for (int i = 0; i < p->n; i++) {
            const perso_t *pe = &w->persos[w->enemies[p->ek[i]].perso];
            p->var[i] = (int)(brand() * (pe->nvar + 1));
            p->x[i] = x - sinf(a) * PATR_FILE * i; p->z[i] = z - cosf(a) * PATR_FILE * i; p->dir[i] = a + PI_F;
        }
        p->hx = p->tx = x; p->hz = p->tz = z; p->wait = 1; p->on = 1;
        return s;
    }
    return -1;
}
void patrouille_maj(float dt) {
    const world_t *w = &E.w;
    if (BT.on || E.in.on || CR.on || accueil_on()) return;
    if (script_running() || mise_bloque()) { for (int s = 0; s < PATR_MAX; s++) PATR[s].on = 0; return; }   /* une scène : ils s'en vont, pour ne pas s'y mêler */
    const int a_pied = E.ride < 0 && !E.nage;
    const float ech = w->ech > 0 ? w->ech : 1;
    /* Vitesse de Sera sur cette image (cases par seconde ; un téléport : aucune) */
    static float sx0, sz0;
    float svx = dt > 0 ? (E.sera.x - sx0) / dt : 0, svz = dt > 0 ? (E.sera.z - sz0) / dt : 0;
    if (hypotf(svx, svz) > PATR_SERA_MAX) svx = svz = 0;
    sx0 = E.sera.x; sz0 = E.sera.z;
    const int sure = zone_sure(E.sera.x, E.sera.z);   /* elle est à l'abri : ils s'arrêtent à la limite */
    for (int s = 0; s < PATR_MAX; s++) {
        patr_t *p = &PATR[s];
        if (!p->on) continue;
        const float dx = E.sera.x - p->x[0], dz = E.sera.z - p->z[0], d = hypotf(dx, dz);
        if (d > PATR_OUBLI) { p->on = 0; continue; }
        if (p->etat == 2 && (p->repos -= dt) <= 0) { p->etat = 0; p->hx = p->x[0]; p->hz = p->z[0]; }
        if (p->etat == 0 && a_pied && !sure && d < PATR_VUE) p->etat = 1;   /* elle voit Sera : la chasse */
        if (p->etat == 1 && (!a_pied || sure || d > PATR_PERDU)) { p->etat = 0; p->hx = p->x[0]; p->hz = p->z[0]; p->wait = 1; }
        if (p->etat == 1 && d < PATR_CONTACT) {   /* au contact : le combat */
            battle_start_groupe(p->ek, p->n, p->lvl, 0, GROUPE_PARTS, p->var);
            if (BT.on) { BT.patr = s; return; }
        }
        /* Le chef : vers Sera (chasse), sinon vers un point autour de sa place, avec des pauses */
        float tx = p->tx, tz = p->tz, v = PATR_MARCHE;
        if (p->etat == 1) { const float t = fminf(PATR_AVANCE, d / (PATR_COURSE / ech)); tx = E.sera.x + svx * t; tz = E.sera.z + svz * t; v = PATR_COURSE; }
        p->marche = 0;
        if (p->etat == 0 && p->wait > 0) p->wait -= dt;
        else if (p->etat != 2) {
            const float ex = tx - p->x[0], ez = tz - p->z[0], el = hypotf(ex, ez), pas = v / ech * dt;
            if (el < 0.15f) {   /* arrivée : une pause, puis un autre point */
                const float a = brand() * 2 * PI_F, r = brand() * PATR_RAYON;
                p->tx = p->hx + sinf(a) * r; p->tz = p->hz + cosf(a) * r; p->wait = 1 + brand() * 2;
            } else {
                const float nx = p->x[0] + ex / el * pas, nz = p->z[0] + ez / el * pas;
                int bouge = 0;
                if (patr_pas(p->x[0], p->z[0], nx, p->z[0])) { p->x[0] = nx; bouge = 1; }
                if (patr_pas(p->x[0], p->z[0], p->x[0], nz)) { p->z[0] = nz; bouge = 1; }
                if (bouge) { p->dir[0] = atan2f(ex, ez); p->marche = 1; }
                else if (p->etat == 0) { p->tx = p->hx; p->tz = p->hz; p->wait = 0.5f + brand(); }   /* bloquée : retour à sa place */
            }
        }
        /* Les suiveurs : chacun vers un point derrière celui qui le précède */
        for (int i = 1; i < p->n; i++) {
            const float fx = p->x[i - 1] - sinf(p->dir[i - 1]) * PATR_FILE, fz = p->z[i - 1] - cosf(p->dir[i - 1]) * PATR_FILE;
            const float ex = fx - p->x[i], ez = fz - p->z[i], el = hypotf(ex, ez);
            if (el > 0.05f) {
                const float pas = fminf(el, (p->etat == 1 ? PATR_COURSE : PATR_MARCHE) * 1.2f / ech * dt);
                p->x[i] += ex / el * pas; p->z[i] += ez / el * pas; p->dir[i] = atan2f(ex, ez);
            }
        }
        p->anim += dt;
        if (p->anim > (p->etat == 1 ? 0.09f : 0.13f)) { p->anim = 0; p->frame = (p->frame + 1) & 1023; }
    }
}
