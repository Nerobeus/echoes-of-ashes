/*
 * Coffres de trésor (comme la section « Coffres » de l'émulateur : coffreDe, coffreOuvrir, butinCoffre…, à garder
 * identiques). On fouille chacun une seule fois ; les bits des coffres fouillés vont dans la sauvegarde (ESVC).
 *
 * Intérieurs : au plus un coffre à butin par bâtiment visité, choisi par l'émulateur à l'export (coffreDe : un meuble
 * coffre de js/interieurs.js, à défaut une caisse, à défaut une armoire ou un casier ; chance selon le type du bâtiment et
 * le lieu) et écrit dans le niveau qui le porte (bloc « coffres » de INTR, interieur.c). Butin : palier p = celui du niveau de l'équipe ou de l'histoire, borné à la gamme du
 * bâtiment (EQPL ; sans gamme : celle du commerce le plus proche sur sa carte), + 1 fermé à clé, + 2 scellé ; crédits
 * (credits_of du niveau du palier × 0,6 à 1,2), un soin (au hasard parmi les trois plus chers qui ne coûtent pas plus de
 * 1,5 fois ces crédits ; deux dans un bâtiment scellé), parfois un équipement du palier (COF_EQ), rarement du palier
 * suivant (COF_RARE) ; jamais une récompense de quête (ni vendue ni lâchée). Équilibre : engine/tools/equilibrage/coffres.js.
 *
 * Dehors : trésors de js/tresors.js (section TRES), sur toutes les cartes, dessinés comme blocs du décor de leur chunk
 * (l'émulateur les y pose à l'export) : contenu fixé (crédits, objets), drapeau du scénario levé à l'ouverture (une quête
 * peut le lire : « si drapeau … »).
 *
 * Bulle d'action devant le coffre (hud.c), son d'ouverture, panneau du butin (hud_butin, comme le tableau de victoire).
 */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "internal.h"
#include "audio.h"

#define TRES_OCT 52             /* x, z f32 ; carte u8, n u8, drapeau u16 ; crédits u32 ; nom 24 octets ; objets (rang u8, nombre u8) × 6 */
#define COF_PORTEE 1.1f         /* cases (dehors) ou mètres à l'échelle humaine (dedans) : la bulle du coffre */
#define COF_GAMME_R 80.0f       /* cases : commerce dont un bâtiment sans gamme prend la gamme */
static const float COF_EQ[3] = { 0.08f, 0.15f, 0.25f };     /* chance d'un équipement du palier : ouvert, fermé à clé, scellé */
static const float COF_RARE[3] = { 0.0f, 0.04f, 0.08f };    /* chance d'un équipement du palier suivant (objet rare) */
static const char *const COFFRE_NOMS[3] = { "COFFRE", "CAISSE", "ARMOIRE" };   /* cachette (sorte du bloc « coffres » de INTR) */

static uint32_t rng_c = 0x7F4A7C15u;
static float crand(void) { rng_c ^= rng_c << 13; rng_c ^= rng_c >> 17; rng_c ^= rng_c << 5; return (float)(rng_c >> 8) * (1.0f / 16777216.0f); }
static inline uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline float rdf(const uint8_t *p) { float f; uint32_t u = rd32(p); memcpy(&f, &u, 4); return f; }
static int bit(const uint8_t *b, int k) { return (b[k >> 3] >> (k & 7)) & 1; }
static int cof_credits(int L) { return (int)lroundf(5 * powf((float)(L < 1 ? 1 : L), 1.3f) + 10); }   /* credits_of de battle.c */

/* TRES : nombre u32, puis TRES_OCT octets par trésor (pak.h) */
void coffres_load(void) {
    uint32_t sz = 0;
    const uint8_t *p = pak_section("TRES", &sz);
    E.w.tres = NULL; E.w.ntres = 0; E.coffre = -1;
    if (!p || sz < 4) return;
    const uint32_t n = rd32(p);
    if (4 + n * TRES_OCT > sz) return;
    E.w.tres = p + 4; E.w.ntres = (int)(n < TRESORS_MAX ? n : TRESORS_MAX);
}

/* Palier du butin d'un coffre du bâtiment bat (voir plus haut) ; 0 sans paliers dans le paquet */
int coffre_palier(int bat) {
    const int_bat_t *b = &E.w.bats[bat];
    int gmin = b->gmin, gmax = b->gmax;
    if (!gmin) {
        float bd = COF_GAMME_R;
        for (int i = 0; i < E.w.nbats; i++) {
            const int_bat_t *o = &E.w.bats[i];
            const float d = hypotf(o->cx - b->cx, o->cz - b->cz);
            if (o->gmin && o->carte == b->carte && d < bd) { bd = d; gmin = o->gmin; gmax = o->gmax; }
        }
    }
    const int h = script_niveau_histoire(), niv = E.level > h ? E.level : h;
    int p = palier_de(niv);
    if (!p) return 0;
    if (gmin) p = p < gmin ? gmin : p > gmax ? gmax : p;
    p += b->mode == ACC_SCELLE ? 2 : b->mode == ACC_FERME ? 1 : 0;
    return p > E.w.npal ? E.w.npal : p;
}
/* Un soin : au hasard parmi les trois plus chers qui ne coûtent pas plus de 1,5 c (c : crédits d'un combat), sinon le
   moins cher ; -1 : aucun */
static int cof_soin(int c) {
    int best[3] = { -1, -1, -1 }, moins = -1;
    for (int i = 0; i < n_items; i++) {
        const item_t *it = &ITEMS[i];
        if (it->type != 0 || (it->hp <= 0 && it->mp <= 0) || it->prix <= 0 || !item_vu(it) || eq_item(i)) continue;   /* un soin (PV ou PM) */
        if (moins < 0 || it->prix < ITEMS[moins].prix) moins = i;
        if (it->prix > 1.5f * c) continue;
        for (int k = 0; k < 3; k++)
            if (best[k] < 0 || it->prix > ITEMS[best[k]].prix) { for (int j = 2; j > k; j--) best[j] = best[j - 1]; best[k] = i; break; }
    }
    int n = 0;
    while (n < 3 && best[n] >= 0) n++;
    return n ? best[(int)(crand() * n) % n] : moins;
}
/* Un équipement du palier p, vendu ou lâché (cristaux du Chœur compris : EQ_ELITE ; jamais une récompense de quête) ; -1 : aucun */
static int cof_equip(int p) {
    int n = 0;
    for (int i = 0; i < n_items; i++) n += E.w.eq_pal[i] == p && (E.w.eq_fl[i] & (EQ_VENDU | EQ_BUTIN | EQ_ELITE)) && eq_item(i) && item_vu(&ITEMS[i]);
    if (!n) return -1;
    int r = (int)(crand() * n) % n;
    for (int i = 0; i < n_items; i++) if (E.w.eq_pal[i] == p && (E.w.eq_fl[i] & (EQ_VENDU | EQ_BUTIN | EQ_ELITE)) && eq_item(i) && item_vu(&ITEMS[i]) && r-- == 0) return i;
    return -1;
}
static void ajouter(int *obj, int *nb, int *n, int i, int k) {
    if (i < 0 || k <= 0) return;
    for (int j = 0; j < *n; j++) if (obj[j] == i) { nb[j] += k; return; }
    if (*n < 6) { obj[*n] = i; nb[*n] = k; (*n)++; }
}

/* Coffre à portée : celui de l'intérieur visité (pas encore fouillé) à moins de COF_PORTEE de son point d'action, sinon le
   trésor de la carte le plus proche (coffreProche de l'émulateur) ; ni en véhicule, ni en combat, en course, en scène */
void coffre_update(void) {
    E.coffre = -1;
    if (BT.on || CR.on || E.ride >= 0 || police_bloque() || hud_in_dialog() || hud_butin_on() || script_running()) return;
    if (E.in.on) {
        const interieur_t *I = &E.in;
        const float f = I->f > 0 ? I->f : 1;
        if (I->ncof && I->bat < COFFRES_BAT && !bit(E.cof_ouv, I->bat) && hypotf(E.sera.x - (IO + I->cof[0]), E.sera.z - (IO + I->cof[1])) < COF_PORTEE * f) E.coffre = COFFRE_INT;
        return;
    }
    float bd = COF_PORTEE / E.w.ech;
    for (int k = 0; k < E.w.ntres; k++) {
        const uint8_t *t = E.w.tres + k * TRES_OCT;
        if (t[8] != E.w.carte || bit(E.tres_ouv, k)) continue;
        const float d = hypotf(rdf(t) - E.sera.x, rdf(t + 4) - E.sera.z);
        if (d < bd) { bd = d; E.coffre = k; }
    }
}
int coffre_position(float *x, float *y, float *z) {
    if (E.coffre == COFFRE_INT && E.in.on) { const float f = E.in.f > 0 ? E.in.f : 1; *x = IO + E.in.cof[0]; *z = IO + E.in.cof[1]; *y = 0.9f * f; return 1; }
    if (E.coffre < 0 || E.coffre >= E.w.ntres) return 0;
    const uint8_t *t = E.w.tres + E.coffre * TRES_OCT;
    *x = rdf(t); *z = rdf(t + 4); *y = ground_y(*x, *z) + 0.9f / E.w.ech;
    return 1;
}

/* Le coffre à portée passe devant l'interlocuteur (OK, bouton d'action) : personne à qui parler, ou le coffre plus près de
   Sera que lui (coffreAvant de l'émulateur) */
int coffre_avant(void) {
    float x, y, z, tx, tz;
    if (E.coffre < 0 || !coffre_position(&x, &y, &z)) return 0;
    if (E.talk >= 0 && E.talk < E.nnpc) { tx = E.npcs[E.talk].x; tz = E.npcs[E.talk].z; }
    else if (E.talk <= TALK_TRAF(0) && TALK_TRAF(0) - E.talk < TRAFFIC_MAX) { tx = E.traffic[TALK_TRAF(0) - E.talk].x; tz = E.traffic[TALK_TRAF(0) - E.talk].z; }
    else return 1;
    return hypotf(x - E.sera.x, z - E.sera.z) < hypotf(tx - E.sera.x, tz - E.sera.z);
}
/* Nom montré au-dessus de la bulle : la cachette de l'intérieur (COFFRE, CAISSE, ARMOIRE), ou le nom du trésor */
const char *coffre_nom(void) {
    static char nom[25];
    if (E.coffre == COFFRE_INT) return COFFRE_NOMS[E.in.cof_sorte % 3];
    if (E.coffre < 0 || E.coffre >= E.w.ntres) return "COFFRE";
    memcpy(nom, E.w.tres + E.coffre * TRES_OCT + 16, 24); nom[24] = 0;
    return nom[0] ? nom : "TRÉSOR";
}

/* Fouille du coffre à portée : le butin dans le sac et la bourse, le coffre gardé fouillé, le panneau du butin */
void coffre_ouvrir(void) {
    int obj[6], nb[6], n = 0, cr = 0;
    char titre[48];
    if (E.coffre == COFFRE_INT && E.in.on) {
        const int bat = E.in.bat;
        if (bat >= COFFRES_BAT) return;
        const int_bat_t *b = &E.w.bats[bat];
        const int a = b->mode == ACC_SCELLE ? 2 : b->mode == ACC_FERME ? 1 : 0, p = coffre_palier(bat);
        const int h = script_niveau_histoire(), L = p ? E.w.pal_niv[p] : (E.level > h ? E.level : h), c = cof_credits(L);
        cr = (int)lroundf(c * (0.6f + 0.6f * crand()));
        for (int k = 0; k < 1 + (a == 2); k++) ajouter(obj, nb, &n, cof_soin(c), 1);
        if (p && crand() < COF_EQ[a]) ajouter(obj, nb, &n, cof_equip(p), 1);
        if (p && p < E.w.npal && crand() < COF_RARE[a]) ajouter(obj, nb, &n, cof_equip(p + 1), 1);
        E.cof_ouv[bat >> 3] |= (uint8_t)(1u << (bat & 7));
        snprintf(titre, sizeof titre, "%s · %s", COFFRE_NOMS[E.in.cof_sorte % 3], b->nom[0] ? b->nom : "BÂTIMENT");
    } else if (E.coffre >= 0 && E.coffre < E.w.ntres) {
        const int k = E.coffre;
        const uint8_t *t = E.w.tres + k * TRES_OCT;
        const int no = t[9] < 6 ? t[9] : 6, dr = t[10] | t[11] << 8;
        cr = (int)rd32(t + 12);
        for (int i = 0; i < no; i++) if (t[40 + i * 2] < n_items) ajouter(obj, nb, &n, t[40 + i * 2], t[41 + i * 2]);
        if (dr < SC_MAX_FLAGS) E.flags[dr >> 3] |= (uint8_t)(1u << (dr & 7));   /* une quête peut le lire */
        E.tres_ouv[k >> 3] |= (uint8_t)(1u << (k & 7));
        char nom[25]; memcpy(nom, t + 16, 24); nom[24] = 0;
        snprintf(titre, sizeof titre, "%s", nom[0] ? nom : "TRÉSOR");
    } else return;
    E.credits += cr;
    for (int i = 0; i < n; i++) { const int v = ITEMS[obj[i]].count + nb[i]; ITEMS[obj[i]].count = v > 99 ? 99 : v; }
    son_jouer(SON_PORTE, 0.5f, 1.35f); son_jouer(SON_VALIDER, 0.4f, 1.1f);
    E.coffre = -1; E.has_dest = 0;
    hud_butin(titre, cr, obj, nb, n);
}
