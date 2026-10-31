/*
 * Combat à timeline (CTB), portage de l'émulateur (startBattle, timeline, advance, perform, updateBattle, endBattle)
 *
 * - Chaque unité (les 4 membres de l'équipe, puis l'ennemi) a un compteur ct : la plus petite valeur joue, puis
 *   avance de 100 / VIT × rang (rang 1 pour une action, 0,6 pour la défense). La timeline affiche les 7 prochains tours.
 * - Tour de l'équipe : ATTAQUE, TECHNIQ., OBJETS (potion du sac), DÉFENSE (dégâts divisés par deux jusqu'au tour
 *   suivant), FUITE (rencontres seulement : fuite_chance). Une attaque vise l'ennemi : un premier toucher le verrouille, le second frappe.
 * - Tour de l'ennemi : 0,8 s d'attente, puis une rafale sur un membre ou un balayage sur toute l'équipe.
 * - Mêmes valeurs et mêmes durées que l'émulateur (en secondes ici, E.clock) ; le rendu est dans render.c (caméra
 *   fixe sur l'arène, équipe de dos, ennemi de face ou Sentinelle en boîtes), l'interface dans hud.c.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "internal.h"
#include "audio.h"

battle_t BT;

#define RED 0xFF3B4Eu
#define AMBER 0xFFB020u
#define GREEN 0x7DFF9Au

/* Générateur du combat (Math.random de l'émulateur) */
static uint32_t rng_b = 0x2545F491u;
static float brand(void) { rng_b ^= rng_b << 13; rng_b ^= rng_b >> 17; rng_b ^= rng_b << 5; return (float)(rng_b >> 8) * (1.0f / 16777216.0f); }
static float rnd(float a, float b) { return a + brand() * (b - a); }

/* Unité en jeu : membre de l'équipe (in_team) debout, ou l'ennemi */
static int alive(int u) { return u < BT_ENEMY ? in_team(u) && PARTY[u].hp > 0 : BT.hp > 0; }
static int spd_of(int u) { return u < BT_ENEMY ? PARTY[u].spd : BT.spd; }

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
        if (c->perso != perso || c->carte != w->carte || c->quest >= w->sc_nq || E.quests[c->quest] != c->step) continue;
        if (hypotf(c->x - E.sera.x, c->z - E.sera.z) < PAL_VUE && (best < 0 || hypotf(E.npcs[best].x - c->x, E.npcs[best].z - c->z) >= CIBLE_DOUBLE)) return 0;
    }
    return best >= 0 ? E.npcs[best].var : 0;
}

void battle_start(int k, int lvl, int boss) {
    const world_t *w = &E.w;
    if (BT.on || k < 0 || k >= w->nenemies) return;
    effet_manette(ENG_EFFET_COMBAT);
    BT.ret_x = E.sera.x; BT.ret_z = E.sera.z;
    E.has_dest = 0;
    const enemy_t *en = &w->enemies[k];
    /* palette : combat du scénario (boss) : celle de l'ennemi vu juste avant (palette_vue, avant hud_battle_start qui
       oublie l'interlocuteur) ; rencontre : une de ses palettes (PVAR), au hasard */
    const int vue = boss ? palette_vue(en->perso) : -1;
    hud_battle_start();
    BT.perso = en->perso; BT.ek = k; BT.boss = boss; BT.fuites = 0;
    BT.var = vue >= 0 ? vue : (int)(brand() * (w->persos[en->perso].nvar + 1));
    snprintf(BT.name, sizeof BT.name, "%s", w->persos[en->perso].nom);
    /* Niveau du combat : PV et ATQ de la fiche suivent l'équipe, × (niveau + 6) / (niveau de la fiche + 6) */
    BT.lvl = lvl > 0 ? (lvl > NIV_MAX ? NIV_MAX : lvl) : en->lvl;
    const float fl = (BT.lvl + 6.0f) / (en->lvl + 6.0f);
    BT.hp = BT.max = (int)lroundf(en->hp * fl); BT.atk = (int)lroundf(en->atk * fl); BT.spd = en->spd;
    /* L'ennemi se règle sur la taille de l'équipe : PV et attaque à (n + 1) / 5, et un peu moins contre une petite équipe
       (TAILLE_PV, TAILLE_ATQ : Sera seule au prologue, sans soins ni magie ; startBattle de l'émulateur) */
    static const float TAILLE_PV[5] = { 1, 0.75f, 0.8f, 0.9f, 1 }, TAILLE_ATQ[5] = { 1, 0.65f, 0.75f, 0.85f, 1 };
    int n = 0;
    for (int i = 0; i < 4; i++) n += in_team(i);
    const float f = (n + 1) / 5.0f;
    BT.hp = BT.max = (int)lroundf(BT.hp * f * TAILLE_PV[n]); BT.atk = (int)lroundf(BT.atk * f * TAILLE_ATQ[n]);
    if (BT.hp < 1) BT.hp = BT.max = 1;
    BT.flash = 0; BT.visible = 1;
    for (int i = 0; i < 4; i++) {
        BT.off[i] = BT.anim[i] = 0; BT.frame[i] = 0; BT.defending[i] = 0;
        if (PARTY[i].hp <= 0) PARTY[i].hp = 1;
    }
    for (int u = 0; u < BT_UNITS; u++) BT.ct[u] = 100.0f / spd_of(u) * (0.3f + brand() * 0.7f);
    BT.phase = BP_INTRO; BT.t0 = E.clock; BT.actor = -1; BT.locked = 0;
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
}

/* ---------- Niveau de l'équipe ---------- */
/* Expérience d'un ennemi du niveau lvl ; d'un niveau au suivant : (15 + 0,1 L) ennemis de ce niveau (15 au début, 21
   au niveau 60 : boss final vers 60 en 40 h d'histoire à ~22 combats par heure, 90 et plus pour les quêtes hardcore) */
static int exp_of(int lvl) { return (int)lroundf(10 * powf((float)lvl, 1.45f) + 10); }
int exp_next(int L) { return (int)lroundf((15 + 0.1f * L) * exp_of(L)); }
static int credits_of(int lvl) { return (int)lroundf(5 * powf((float)lvl, 1.3f) + 10); }

/* ---------- Jobs et équipement ---------- */
static const float AFF_F[5] = { 0, 0.7f, 0.85f, 1, 1.2f };
int sorts_of(int m, int *out) {
    uint64_t b = m >= 0 && m < 4 && E.job[m] < E.w.njobs ? E.w.jobs[E.job[m]].sorts : 0;
    for (int s = 0; m >= 0 && m < 4 && s < 3; s++) { const eqitem_t *r = eq_item(E.eq[m][s]); if (r) b |= r->sorts; }
    int n = 0;
    for (int k = 0; k < E.w.nsorts; k++) if (b >> k & 1) out[n++] = k;
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
    const int a = E.job[m] < E.w.njobs ? E.w.jobs[E.job[m]].aff[r->cat] : 3;
    return a > 4 ? 4 : a;
}
/* Statistiques du membre m : celles du niveau, son job, puis son équipement selon l'affinité */
void party_refresh(int k) {
    member_t *m = &PARTY[k];
    float hp = m->lhp, mp = m->lmp, atk = m->latk, mag = m->latk, def = 0, spd = (float)m->bspd;
    if (E.job[k] < E.w.njobs) {
        const job_t *j = &E.w.jobs[E.job[k]];
        hp *= 1 + j->pv / 100.0f; mp *= 1 + j->pm / 100.0f; atk *= 1 + j->atq / 100.0f; mag *= 1 + j->mag / 100.0f; spd += (float)j->vit;
    }
    for (int s = 0; s < 3; s++) {
        const eqitem_t *r = eq_item(E.eq[k][s]);
        if (!r) continue;
        const float f = AFF_F[eq_aff(k, E.eq[k][s])];
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
    for (int k = 0; k < 4; k++) {
        E.job[k] = E.w.job0[k]; E.jobs_known[k] = E.w.jobs0[k];
        for (int s = 0; s < 3; s++) E.eq[k][s] = eq_item(E.w.eq0[k][s]) ? E.w.eq0[k][s] : EQ_AUCUN;
    }
}

void party_set_level(int L) {
    E.level = L < 1 ? 1 : L > NIV_MAX ? NIV_MAX : L;
    const float f = (E.level + 6) / 26.0f;
    for (int i = 0; i < 4; i++) {
        member_t *m = &PARTY[i];
        if (!m->bhp) {   /* première fois : les valeurs de PARTY sont celles du niveau 20 */
            m->bhp = m->max; m->bmp = m->maxmp; m->batk = m->atk; m->bspd = m->spd;
            for (int t = 0; t < m->ntech; t++) m->tech[t].bheal = m->tech[t].heal;
        }
        const float rh = m->max ? (float)m->hp / m->max : 1, rm = m->maxmp ? (float)m->mp / m->maxmp : 1;
        m->lhp = m->bhp * f; m->lmp = m->bmp * f; m->latk = m->batk * f;
        party_refresh(i);
        m->hp = (int)lroundf(m->max * rh); m->mp = (int)lroundf(m->maxmp * rm);
        if (m->hp < 1) m->hp = 1;
        for (int t = 0; t < m->ntech; t++) if (m->tech[t].bheal) m->tech[t].heal = (int)lroundf(m->tech[t].bheal * f);
    }
}

int party_gain_exp(int n) {
    int up = 0;
    E.exp += n;
    while (E.level < NIV_MAX && E.exp >= exp_next(E.level)) { E.exp -= exp_next(E.level); party_set_level(E.level + 1); up++; }
    return up;
}

/* Ennemi dont la fiche laisse un cœur (pas les humains) */
static int drops_heart(void) {
    for (int e = 0; e < E.w.nenemies; e++) if (E.w.enemies[e].perso == BT.perso) return E.w.enemies[e].coeur;
    return 0;
}

/* Fin du combat : l'équipe est soignée, une machine vaincue laisse son cœur. Victoire : retour à la position d'avant ;
   défaite : au point de départ, et le script du scénario qui avait lancé le combat s'arrête ; fuite (won 2) : retour à la
   position d'avant, sans récompense. */
static void battle_end(int won) {
    const int fui = won == 2;
    if (fui) won = 0;
    /* Combat du scénario gagné : la cible de chasse de cet ennemi, là où il était, n'est plus dessinée (endBattle de
       l'émulateur) ; sinon l'ennemi vaincu restait debout pendant les répliques qui suivent */
    if (won && BT.boss)
        for (int k = 0; k < E.w.ncibles; k++) {
            cible_t *c = &E.w.cibles[k];
            if (c->perso == BT.perso && c->carte == E.w.carte && hypotf(c->x - BT.ret_x, c->z - BT.ret_z) < PAL_VUE) c->vaincue = 1;
        }
    if (won) {   /* récompense, puis soins (au niveau pris) */
        E.credits += BT.rw_cr;
        if (BT.rw_loot >= 0 && ITEMS[BT.rw_loot].count < 99) ITEMS[BT.rw_loot].count++;
        if (party_gain_exp(BT.rw_xp)) { char t[48]; snprintf(t, sizeof t, "NIVEAU %d : PV, PM et attaque en hausse", E.level); hud_toast(t); effet_manette(ENG_EFFET_NIVEAU); }
    }
    if (!won && !fui) script_abort();
    for (int i = 0; i < 4; i++) { PARTY[i].hp = PARTY[i].max; PARTY[i].mp = PARTY[i].maxmp; }
    for (int i = 0; i < 4; i++) { if (!PARTY[i].hp) PARTY[i].hp = 1; BT.off[i] = 0; }
    BT.on = 0; BT.ban.on = 0; BT.shake = 0; BT.fade_t = E.clock;
    for (int i = 0; i < BT_POPUPS; i++) BT.pop[i].on = 0;
    if (won || fui) { E.sera.x = BT.ret_x; E.sera.z = BT.ret_z; }
    else if (E.w.carte) world_carte(0, E.w.start_x, E.w.start_z);   /* défaite sur une autre carte : départ de la carte 0 */
    else { E.sera.x = E.w.start_x; E.sera.z = E.w.start_z; }
    E.has_dest = 0;
    E.stream_hold = 1;   /* fenêtre recentrée sur Sera à la mise à jour suivante */
}

/* Tour suivant : fin du combat, ou l'unité en tête de la timeline */
static void advance(void) {
    if (BT.ban.hold) BT.ban.on = 0;
    if (!alive(BT_ENEMY)) { BT.phase = BP_DYING; BT.t0 = E.clock; son_jouer(SON_EXPLOSION, 0.9f, 1); return; }
    if (!alive(0) && !alive(1) && !alive(2) && !alive(3)) {
        BT.phase = BP_LOSE; BT.t0 = E.clock;
        banner("DÉFAITE", "Toucher pour revenir au point de départ", RED, 1);
        effet_manette(ENG_EFFET_DEFAITE);
        return;
    }
    int a = BT_ENEMY;
    battle_timeline(1, &a);
    BT.actor = a; BT.locked = 0; memset(&BT.pending, 0, sizeof BT.pending);
    if (a == BT_ENEMY) { BT.phase = BP_ENEMY; BT.t0 = E.clock; }
    else { BT.defending[a] = 0; BT.phase = BP_CMD; }
}

static void end_turn(float rank) { BT.ct[BT.actor] += 100.0f / spd_of(BT.actor) * rank; advance(); }

/* Fuite d'une rencontre : chance selon la vitesse du plus rapide de l'équipe encore debout contre celle de l'ennemi,
   plus FUITE_ESSAI à chaque essai raté, entre FUITE_MIN et FUITE_MAX (fuiteChance de l'émulateur) ; un combat du
   scénario (BT.boss) ne se fuit pas */
#define FUITE_BASE 0.55f
#define FUITE_VIT 0.05f
#define FUITE_ESSAI 0.2f
#define FUITE_MIN 0.2f
#define FUITE_MAX 0.95f
static float fuite_chance(void) {
    int v = 0;
    for (int i = 0; i < 4; i++) if (alive(i) && PARTY[i].spd > v) v = PARTY[i].spd;
    const float c = FUITE_BASE + FUITE_VIT * (v - BT.spd) + FUITE_ESSAI * BT.fuites;
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
/* Effet et couleur de l'arme du membre k (sans arme : IMPACT, couleur du thème) */
static void fx_of(int k, int *fx, uint32_t *col) {
    const eqitem_t *r = eq_item(E.eq[k][EQ_ARME]);
    const eqtype_t *t = r ? &E.w.eqtypes[r->cat] : NULL;
    if (t && t->slot == EQ_ARME) { *fx = t->effet; *col = t->couleur; } else { *fx = FX_IMPACT; *col = BT_C1; }
}

/* Cut-ins (CUTI) : personnage du membre m (le premier : l'héroïne choisie, 255 pour Sera), image tirée parmi les siennes */
static int cutin_pers(int m) { return m == 0 ? (E.heroine >= 0 ? E.heroine : 255) : E.w.party_perso[m]; }
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
    if (m < 0 || m >= 4 || E.level < NIV_EVEIL) return 0;
    for (int j = 0; j < E.w.njobs && j < 16; j++) if (!strcmp(E.w.jobs[j].nom, "ÉVEIL") && ((E.jobs_known[m] >> j) & 1)) return 1;
    return 0;
}
uint32_t eveil_aura(int m) {
    static const struct { const char *nom; uint32_t c; } AURA[6] = {
        { "SERA", 0xFF7AD9 }, { "JADE", 0xA45CFF }, { "AMBRE", 0xFF8A2A }, { "AZUR", 0x46E4FF }, { "SAULE", 0x34E88E }, { "ONYX", 0xFF2E4A } };
    const char *nom = m == 0 && E.heroine >= 0 ? E.w.persos[E.heroine].nom : m >= 0 && m < 4 ? PARTY[m].name : NULL;
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

static void lancer(const bt_act_t *act, int annonce);
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
    BT.an.k = strcmp(act->name, "ATTAQUE") ? 1.4f : 1; BT.an.seed = (int)(brand() * 65536); BT.an.cue = 0;
    /* Pose (render.c, battle_anim) : élan et frappe au contact ; tir, laser, obus, flèche : ni élan ni frappe, recul au coup ;
       magie, piratage, éléments, soin, objet : la technique. Une technique sans effet propre se range d'abord d'après son nom
       (cut_genre_nom) : avant le 31/10/2026, un tir laser ou un PIRATAGE jouait la frappe de l'arme (lancer de l'émulateur) */
    const int g = act->name && strcmp(act->name, "ATTAQUE") && !(act->fx > 0 && act->fx < FX_N) ? cut_genre_nom(act->name) : CG_NEUTRE;
    const int g_magie = g >= CG_MAGIE && g <= CG_SOIN, g_loin = g == CG_TIR || g == CG_LASER || g == CG_CANON || g == CG_ARC;
    BT.an.melee = !act->sort && fx_dash(BT.an.fx) > 0 && !g_magie && !g_loin;
    BT.an.cast = BT.an.fx < 0 || BT.an.fx == FX_ARCANE || BT.an.fx >= FX_MAGIE || g_magie;
    BT.an.tir = !BT.an.melee && !BT.an.cast;
    if (act->heal) {   /* soignés : toute l'équipe (soin 2), sinon le plus blessé ; l'effet du sort est dessiné sur eux */
        int best = -1;
        for (int i = 0; i < 4; i++) {
            if (!alive(i)) continue;
            if (act->soin == 2) BT.an.targets[BT.an.ntarget++] = i;
            else if (best < 0 || (float)PARTY[i].hp / PARTY[i].max < (float)PARTY[best].hp / PARTY[best].max) best = i;
        }
        if (act->soin != 2 && best >= 0) BT.an.targets[BT.an.ntarget++] = best;
    }
    if (annonce && !act->heal && strcmp(act->name, "ATTAQUE")) son_jouer(SON_TECHNIQUE, 0.7f, 1);
    if (annonce && strcmp(act->name, "ATTAQUE")) banner(act->name, NULL, BT_C1, 0);
}

/* Coup de l'équipe : soin du membre le plus blessé, ou dégâts (10 % de critiques × 1,6) et retard éventuel */
static void apply_hit(void) {
    const bt_act_t *act = &BT.an.act;
    char t[24];
    if (act->heal) {   /* soignés choisis par perform */
        for (int k = 0; k < BT.an.ntarget; k++) {
            const int u = BT.an.targets[k];
            member_t *m = &PARTY[u];
            const int h = act->heal < m->max - m->hp ? act->heal : m->max - m->hp;
            m->hp += h;
            snprintf(t, sizeof t, "+%d", h); popup(u, t, GREEN);
        }
        son_jouer(SON_SOIN, 0.6f, 1);
        return;
    }
    const int crit = brand() < 0.1f;   /* un sort frappe avec le MAG, le reste avec l'ATQ */
    const int dmg = (int)lroundf((act->sort ? PARTY[BT.an.who].mag : PARTY[BT.an.who].atk) * (act->mult > 0 ? act->mult : 1) * rnd(0.9f, 1.1f) * (crit ? 1.6f : 1)
                                 * (BT.an.who < 4 && BT.transfo[BT.an.who] ? 1.5f : 1));   /* transformée : ATQ × 1,5 */
    BT.hp = BT.hp - dmg > 0 ? BT.hp - dmg : 0;
    effet_manette(ENG_EFFET_FRAPPE);   /* le coup porte : la manette vibre */
    if (BT.shake <= 0) BT.shake_amp = 1;   /* secousse d'un effet puissant en cours : gardée */
    BT.flash = 0.16f; BT.shake = fmaxf(BT.shake, crit || BT.an.fx == FX_OBUS ? 0.3f : 0.15f); BT.hurt_t[BT_ENEMY] = E.clock;
    if (!act->sort) son_jouer(SON_COUP, crit ? 0.95f : 0.75f, crit ? 0.85f : 1);   /* un sort : le son de son élément */
    snprintf(t, sizeof t, "%s%d", crit ? "CRITIQUE " : "", dmg); popup(BT_ENEMY, t, 0xFFFFFFu);
    if (act->delay > 0) { BT.ct[BT_ENEMY] += 100.0f / BT.spd * act->delay; popup(BT_ENEMY, "TOUR RETARDÉ", BT_C1); }
}

/* Tour de l'ennemi : balayage (30 %) sur toute l'équipe, sinon rafale sur un membre au hasard */
static void enemy_act(void) {
    int alive_n = 0, list[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 4; i++) if (alive(i)) list[alive_n++] = i;
    /* Un de ses sorts d'attaque (fiche de l'ennemi : sorts, part de ses tours) ; niveau III : toute l'équipe */
    const enemy_t *en = BT.ek >= 0 && BT.ek < E.w.nenemies ? &E.w.enemies[BT.ek] : NULL;
    int ls[MAX_SORTS], nl = 0;
    for (int s = 0; en && s < E.w.nsorts; s++) if ((en->sorts >> s & 1) && !E.w.sorts[s].soin) ls[nl++] = s;
    if (nl && alive_n && brand() * 100 < en->magie) {
        const sort_t *sp = &E.w.sorts[ls[(int)(brand() * nl) % nl]];
        const int tous = FX_NIV(sp->fx) == 3;
        banner(sp->nom, NULL, RED, 0);
        BT.phase = BP_ANIM;
        memset(&BT.an, 0, sizeof BT.an);
        BT.an.who = BT_ENEMY; BT.an.all = tous; BT.an.t0 = E.clock;
        BT.an.fx = sp->fx; BT.an.col = fx_couleur(sp->fx); BT.an.k = 1.4f; BT.an.cast = 1;
        BT.an.act.name = sp->nom; BT.an.act.mult = sp->mult; BT.an.act.sort = 1; BT.an.act.fx = sp->fx;
        BT.an.seed = (int)(brand() * 65536);   /* sons et secousses du sort */
        son_jouer(SON_TECHNIQUE, 0.7f, 0.7f);
        if (tous) { memcpy(BT.an.targets, list, sizeof list); BT.an.ntarget = alive_n; }
        else { BT.an.targets[0] = list[(int)(brand() * alive_n) % alive_n]; BT.an.ntarget = 1; }
        return;
    }
    const int all = brand() < 0.3f;
    banner(all ? "BALAYAGE THERMIQUE" : "RAFALE LASER", NULL, RED, 0);
    BT.phase = BP_ANIM;
    memset(&BT.an, 0, sizeof BT.an);
    BT.an.who = BT_ENEMY; BT.an.all = all; BT.an.t0 = E.clock;
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
        /* sort : ATQ × puissance (× 0,6 sur toute l'équipe) ; sinon rafale ou balayage */
        const float fa = BT.an.act.sort ? BT.an.act.mult * (BT.an.all ? 0.6f : 1) * rnd(0.9f, 1.1f) : BT.an.all ? rnd(0.6f, 0.8f) : rnd(1.2f, 1.6f);
        int d = (int)lroundf(BT.atk * fa * 200.0f / (200.0f + m->def));
        if (BT.defending[u]) d = (int)lroundf(d / 2.0f);
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
        snprintf(t, sizeof t, "%d", d); popup(u, t, 0xFF6A6Au);
        if (!m->hp) { popup(u, "KO", RED); son_jouer(SON_KO, 0.8f, 1); ko = 1; }
    }
    if (!BT.an.act.sort) son_jouer(SON_COUP, 0.7f, 1.15f);
    effet_manette(ko ? ENG_EFFET_KO : ENG_EFFET_COUP);   /* coup reçu, membre K.O. */
    if (BT.shake <= 0) BT.shake_amp = 1;   /* secousse d'un sort en cours : gardée */
    BT.shake = fmaxf(BT.shake, 0.3f);
}

void battle_update(float dt) {
    const float now = E.clock;
    if (BT.flash > 0 && BT.phase != BP_DYING) BT.flash -= dt;
    if (BT.phase == BP_INTRO && now - BT.t0 > INTRO_S) advance();
    for (int i = 0; i < 4; i++) {   /* le membre qui choisit avance d'un pas, les autres reculent à leur place */
        const float cible = i == BT.actor && bt_choix() ? 1.0f : 0.0f;
        BT.avance[i] += (cible - BT.avance[i]) * fminf(1, dt * 10);
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
        if (who == BT_ENEMY && t < 0.4f) BT.flash = ((int)floorf(t / 0.07f) % 2) ? 0.05f : 0;   /* l'ennemi clignote en attaquant */
        if (BT.an.melee && who < BT_ENEMY) {
            /* Élan au corps à corps : 0,3 s vers l'ennemi, 0,35 s sur place, 0,3 s de retour */
            const float k = t < 0.3f ? t / 0.3f : t < 0.65f ? 1 : fmaxf(0, 1 - (t - 0.65f) / 0.3f);
            float bx, bz; battle_member(who, &bx, &bz);
            const float d = hypotf(E.w.epos_x - bx, E.w.epos_z - bz) * E.w.ech;   /* jusqu'au contact */
            BT.off[who] = k * fmaxf(0, d - BAT_CONTACT) * fx_dash(BT.an.fx);
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
        if (!BT.an.hit && t > fx_hit(BT.an.fx)) { BT.an.hit = 1; if (who == BT_ENEMY) apply_enemy_hit(); else apply_hit(); }
        if (t > fx_fin(BT.an.fx)) {
            if (who < BT_ENEMY) { BT.off[who] = 0; BT.frame[who] = 0; }
            if (BT.reac_img >= 0) {   /* héroïne touchée : son cut-in de dégâts, puis le tour suivant */
                BT.cut.img = BT.reac_img; BT.reac_img = -1; BT.cut.reac = 1; BT.cut.plein = BT.cut.paire = 0;
                BT.cut.act.name = NULL; BT.cut.t0 = now; BT.phase = BP_CUTIN;
            } else end_turn(1);
        }
    }
    if (BT.phase == BP_FUI && now - BT.t0 > 0.8f) { battle_end(2); return; }
    if (BT.phase == BP_DYING) {
        BT.flash = ((int)floorf((now - BT.t0) / 0.09f) % 2) ? 0.1f : 0; BT.shake = 0.1f; BT.shake_amp = 1;
        if (now - BT.t0 > 1.2f) {
            BT.visible = 0; BT.phase = BP_WIN; BT.t0 = now; BT.shake = 0;
            effet_manette(ENG_EFFET_VICTOIRE);
            /* Récompense : expérience et crédits selon le niveau de l'ennemi, cœur d'une machine (tableau de
               draw_victoire de hud.c) */
            BT.rw_xp = exp_of(BT.lvl) * (BT.boss ? BOSS_RW : 1); BT.rw_cr = credits_of(BT.lvl) * (BT.boss ? BOSS_RW : 1); BT.rw_loot = drops_heart() ? item_loot() : -1;
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
        perform(&BT.pending);   /* un seul ennemi par combat : l'action part tout de suite, sans viser (attackChoisie de l'émulateur) */
        break;
    case BC_TECHS: if (BT.phase == BP_CMD) BT.phase = BP_TECH; break;
    case BC_ITEMS: if (BT.phase == BP_CMD) BT.phase = BP_ITEM; break;
    case BC_DEFEND:
        if (BT.phase != BP_CMD) return;
        BT.defending[u] = 1; popup(u, "DÉFENSE", BT_C1); end_turn(0.6f);
        break;
    case BC_FLEE:
        if (BT.phase != BP_CMD) return;
        if (BT.boss) { banner("FUITE IMPOSSIBLE", "Combat de l'histoire", AMBER, 0); son_jouer(SON_ANNULER, 0.45f, 1); return; }
        if (brand() < fuite_chance()) { BT.phase = BP_FUI; BT.t0 = E.clock; banner("FUITE !", NULL, BT_C1, 0); son_jouer(SON_VALIDER, 0.6f, 1); }
        else { BT.fuites++; popup(u, "FUITE RATÉE", AMBER); end_turn(1); }
        break;
    case BC_TECH: {
        if (BT.phase != BP_TECH || arg < 0 || arg >= m->ntech) return;
        const tech_t *t = &m->tech[arg];
        if (m->mp < t->mp) { banner("PM INSUFFISANTS", NULL, AMBER, 0); return; }
        bt_act_t a = { t->name, t->mp, t->heal, 0, t->mult, t->delay, t->fx, 0, 0 };
        /* coup de grâce (l'ennemi tombe même au plus bas du tirage des dégâts) : cut-in plein écran */
        /* genre de l'image : l'effet de la technique, sinon son nom (« TIR LASER »), sinon l'arme (soin : CG_SOIN) */
        int fx = t->fx; uint32_t col;
        int genre = fx > 0 && fx < FX_N ? cut_genre(fx, t->heal) : t->heal ? CG_SOIN : cut_genre_nom(t->name);
        if (genre == CG_NEUTRE) { fx_of(u, &fx, &col); genre = cut_genre(fx, 0); }
        cutin_preparer(t->name, !t->heal && m->atk * (t->mult > 0 ? t->mult : 1) * 0.9f >= BT.hp, 0, genre);
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
    case BC_POTION: {
        if (BT.phase != BP_ITEM) return;
        const int pi = item_potion();
        if (pi < 0 || ITEMS[pi].count <= 0) { banner("PLUS DE POTIONS", NULL, AMBER, 0); return; }
        ITEMS[pi].count--;
        bt_act_t a = { ITEMS[pi].name, 0, ITEMS[pi].hp, 1, 0, 0, 0, 0, 0 };
        perform(&a);
        break;
    }
    case BC_BACK:
        if (BT.phase == BP_MAGIE) BT.phase = BP_TECH;
        else if (BT.phase == BP_TECH || BT.phase == BP_ITEM || BT.phase == BP_TARGET) { BT.phase = BP_CMD; BT.locked = 0; }
        break;
    case BC_MAGIES: { int l[MAX_SORTS]; if (BT.phase == BP_TECH && sorts_of(u, l)) { BT.phase = BP_MAGIE; BT.mpage = 0; } break; }
    case BC_SUITE: if (BT.phase == BP_MAGIE) BT.mpage++; break;
    case BC_SORT: {   /* arg : place du sort dans la page */
        int l[MAX_SORTS], first, np;
        const int n = sorts_of(u, l), c = sorts_page(n, &first, &np);
        if (BT.phase != BP_MAGIE || arg < 0 || arg >= c) return;
        const sort_t *sp = &E.w.sorts[l[first + arg]];
        if (m->mp < sp->pm) { banner("PM INSUFFISANTS", NULL, AMBER, 0); return; }
        bt_act_t a = { sp->nom, sp->pm, sp->soin ? (int)lroundf(m->mag * sp->mult) : 0, 0, sp->mult, 0, sp->fx, 1, sp->soin };
        if (sp->fx >= FX_MAGIE && FX_NIV(sp->fx) == 3) cutin_preparer(sp->nom, 1, 0, cut_genre(sp->fx, sp->soin));   /* grand sort : cut-in plein écran, de son élément */
        perform(&a);
        break;
    }
    }
}

/* Toucher hors des boutons : commencer, terminer, ou viser l'ennemi (à moins de 100 px de son centre) */
void battle_tap(float x, float y) {
    if (!BT.on) return;
    if (BT.phase == BP_INTRO) { BT.ban.on = 0; advance(); return; }
    if (BT.phase == BP_WIN) { battle_end(1); return; }
    if (BT.phase == BP_LOSE) { battle_end(0); return; }
    if (BT.phase == BP_TARGET && E.scr_enemy.vis && hypotf(x - E.scr_enemy.cx, y - E.scr_enemy.cy) < 100) {
        if (!BT.locked) BT.locked = 1; else perform(&BT.pending);
    }
}
