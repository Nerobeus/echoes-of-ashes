/*
 * Arène de combat (04/11/2026, demande de l'utilisateur : « une arène de combat pour gagner des équipements, de l'exp et des
 * crédits » ; section « Arène » de l'émulateur, à garder identiques).
 *
 * À Lucerne, le bâtiment « Arène » (js/batiments.js ; reconnu à son nom de PORT, comme la prison) : son maître (métier
 * commissaire, commerce de COMM : potions, soins) propose aussi COMBATTRE (commerce_ouvrir de hud.c). Cinq rangs, FER,
 * BRONZE, ARGENT, OR, CHAMPION, à un niveau fixe chacun (ARN_NIV : on revient plus fort pour le suivant) ; FER est ouvert,
 * chaque série gagnée ouvre le rang suivant (E.arene_gagnes, gardé dans la sauvegarde). Droit d'entrée : les crédits d'un
 * combat du rang. Une série : ARN_COMBATS combats d'affilée sans soins (PV et PM gardés, objets permis) : un ennemi seul,
 * puis un groupe (trois avec trois membres au moins, sinon deux), puis un champion (une élite et deux sbires) ; ennemis
 * des rencontres (une zone, un poids), de fiche proche du niveau du rang (ARN_FENETRE, sinon tous). Chaque victoire rapporte
 * expérience et crédits × ARN_RW (et le butin ordinaire) ; entre deux combats, COMBAT k / 3 ou ABANDONNER (ce qui est
 * gagné reste). Série gagnée : ARN_PRIME combats de crédits (le double la première fois) et un équipement du palier du rang
 * (ARN_PALIER de plus à OR et CHAMPION), dans le panneau du butin. Défaite : ni écran de défaite ni RÉESSAYER (bannière
 * ÉLIMINÉE), la série et l'entrée perdues, ceux à terre ressortent à 1 PV. Pas de fuite.
 */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "internal.h"
#include "audio.h"

static const char *const ARN_NOM[ARN_RANGS] = { "FER", "BRONZE", "ARGENT", "OR", "CHAMPION" };
static const int ARN_NIV[ARN_RANGS] = { 6, 16, 30, 48, 70 };
static const int ARN_PALIER[ARN_RANGS] = { 0, 0, 0, 1, 1 };
#define ARN_FENETRE 12      /* niveaux de fiche d'écart au plus avec le rang (sinon tous les ennemis des rencontres) */
#define ARN_ELITE_PV 600    /* élite : des sorts, ou plus de tant de PV par niveau de fiche + 6 (ELITE_PV de game.c) */

/* Série en cours : rang, combats gagnés ; attente : 0 le choix du rang, 1 entre deux combats ; rangs proposés au choix */
static struct { int on, rang, combat, attente, nopt, opt[3]; } ARN;
static uint32_t rng_a = 0x6172656eu;
static float arand(void) { rng_a ^= rng_a << 13; rng_a ^= rng_a >> 17; rng_a ^= rng_a << 5; return (float)(rng_a >> 8) * (1.0f / 16777216.0f); }

const char *arene_rang_nom(int r) { return r >= 0 && r < ARN_RANGS ? ARN_NOM[r] : ""; }
int arene_ici(void) {
    if (!E.in.on || E.in.bat < 0 || E.in.bat >= E.w.nbats) return 0;
    const char *n = E.w.bats[E.in.bat].nom;
    return !strncmp(n, "Arène", 6) || !strncmp(n, "ARÈNE", 6);
}
static int arn_entree(int r) { return credits_combat(ARN_NIV[r]); }
/* FER, puis le rang qui suit chaque série gagnée */
static int arn_ouverts(void) { int n = 1; while (n < ARN_RANGS && (E.arene_gagnes >> (n - 1) & 1)) n++; return n; }

/* Un ennemi des rencontres : élite ou non, autre que exclu, de fiche à moins de ARN_FENETRE niveaux de lvl (sinon tous) */
static int arn_elite(const enemy_t *en) { return en->sorts || en->hp > ARN_ELITE_PV * (en->lvl + 6); }
static int arn_tirer(int lvl, int elite, int exclu) {
    for (int pass = 0; pass < 3; pass++) {   /* (pass 2 : même sans l'élite voulue) */
        int n = 0;
#define ARN_OK(q) (E.w.enemies[q].zones && E.w.enemies[q].poids > 0 && (q) != exclu && (pass == 2 || arn_elite(&E.w.enemies[q]) == elite) \
                   && (pass >= 1 || abs(E.w.enemies[q].lvl - lvl) <= ARN_FENETRE))
        for (int q = 0; q < E.w.nenemies; q++) n += ARN_OK(q);
        if (!n) continue;
        int r = (int)(arand() * n) % n;
        for (int q = 0; q < E.w.nenemies; q++) if (ARN_OK(q) && r-- == 0) return q;
#undef ARN_OK
    }
    return -1;
}

/* Le combat suivant de la série */
static void arn_combat(void) {
    const int L = ARN_NIV[ARN.rang];
    int ks[BT_EMAX], n = 1, mode = GROUPE_PARTS;
    if (ARN.combat == 0) ks[0] = arn_tirer(L, 0, -1);
    else if (ARN.combat == 1) {   /* un groupe : trois avec trois membres au moins */
        n = equipe_nombre() >= 3 ? 3 : 2;
        if (n > BT_EMAX) n = BT_EMAX;
        for (int i = 0; i < n; i++) ks[i] = arn_tirer(L, 0, -1);
    } else {   /* le champion : une élite et ses deux sbires */
        ks[0] = arn_tirer(L, 1, -1);
        n = BT_EMAX >= 3 ? 3 : BT_EMAX; mode = GROUPE_SBIRES;
        for (int i = 1; i < n; i++) ks[i] = arn_tirer(L, 0, ks[0]);
    }
    for (int i = 0; i < n; i++) if (ks[i] < 0) { ARN.on = 0; hud_toast("Pas d'adversaire pour l'arène"); return; }
    battle_start_groupe(ks, n, L, 0, mode, NULL);
    if (!BT.on) { ARN.on = 0; return; }
    BT.arene = ARN.rang + 1;
    char t[48]; snprintf(t, sizeof t, "ARÈNE · %s · COMBAT %d / %d", ARN_NOM[ARN.rang], ARN.combat + 1, ARN_COMBATS);
    hud_toast(t);
}

void arene_proposer(void) {
    if (ARN.on) return;
    char o[4][48], t[40];
    int n = 0;
    for (int r = arn_ouverts() - 1; r >= 0 && n < 3; r--) {
        snprintf(o[n], 48, "%s · NIV %d · %d CR", ARN_NOM[r], ARN_NIV[r], arn_entree(r));
        ARN.opt[n++] = r;
    }
    ARN.nopt = n;
    snprintf(o[n++], 48, "PARTIR");
    snprintf(t, sizeof t, "ARÈNE · %d CR", E.credits);
    ARN.attente = 0;
    hud_choice_arene(o, n, t, "Trois combats d'affilée, sans soins", "Série gagnée : un équipement");
}

void arene_choix(int k) {
    if (ARN.attente == 1) {   /* entre deux combats : le suivant, ou l'abandon (ce qui est gagné reste) */
        ARN.attente = 0;
        if (!ARN.on) return;
        if (k == 0) arn_combat(); else { ARN.on = 0; hud_toast("Vous quittez l'arène"); }
        return;
    }
    if (k < 0 || k >= ARN.nopt) return;   /* PARTIR */
    const int r = ARN.opt[k], prix = arn_entree(r);
    if (E.credits < prix) { char t[48]; snprintf(t, sizeof t, "Pas assez de crédits (%d CR)", prix); hud_toast(t); son_jouer(SON_ANNULER, 0.45f, 1); return; }
    E.credits -= prix;
    ARN.on = 1; ARN.rang = r; ARN.combat = 0;
    arn_combat();
}

void arene_apres_combat(int won) {
    if (!ARN.on) return;
    if (!won) {   /* éliminée : la série perdue ; ceux à terre ressortent à 1 PV */
        ARN.on = 0;
        for (int i = 0; i < NMEMB; i++) if (PARTY[i].hp <= 0) PARTY[i].hp = 1;
        hud_toast("Éliminée : la série est perdue");
        return;
    }
    if (++ARN.combat < ARN_COMBATS) {
        char o[2][48], t[40];
        snprintf(o[0], 48, "COMBAT %d / %d", ARN.combat + 1, ARN_COMBATS);
        snprintf(o[1], 48, "ABANDONNER");
        snprintf(t, sizeof t, "ARÈNE · %s", ARN_NOM[ARN.rang]);
        ARN.attente = 1;
        hud_choice_arene(o, 2, t, "PV et PM gardés, pas de soins", "Ce qui est gagné reste");
        return;
    }
    /* Série gagnée : la prime, un équipement du rang, le rang suivant ouvert */
    ARN.on = 0;
    const int r = ARN.rang, premier = !(E.arene_gagnes >> r & 1);
    E.arene_gagnes |= 1 << r;
    const int prime = credits_combat(ARN_NIV[r]) * ARN_PRIME * (premier ? 2 : 1);
    E.credits += prime;
    const int q = equip_hasard(palier_de(ARN_NIV[r]) + ARN_PALIER[r], EQ_BUTIN | EQ_VENDU), un = 1;
    if (q >= 0 && ITEMS[q].count < 99) ITEMS[q].count++;
    char t[48];
    if (premier && r + 1 < ARN_RANGS) snprintf(t, sizeof t, "%s GAGNÉ · %s OUVERT", ARN_NOM[r], ARN_NOM[r + 1]);
    else snprintf(t, sizeof t, "ARÈNE · %s GAGNÉ", ARN_NOM[r]);
    hud_butin(t, prime, &q, &un, q >= 0 ? 1 : 0);
    son_jouer(SON_VALIDER, 0.5f, 1.2f);
}
