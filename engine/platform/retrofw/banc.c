/*
 * Banc d'essai de RetroFW (platform/retrofw/main_retrofw.c, 11/11/2026) : ce qu'il faut de l'intérieur du moteur, comme
 * tools/rendu_test.c, à part pour que main_retrofw.c n'ait que l'API publique (eng.h).
 */
#include "../../core/eng.h"
#include "../../core/internal.h"

/* Prologue coupé (le script d'une partie neuve démarre à l'image suivante et interromprait l'essai de eng_essai_monde) */
void banc_couper_script(void) { script_abort(); }

/* Passage de la première enceinte (Lucerne) levé : les gardes laissent entrer (le mot « passage » de rendu_test) ; sans
   lui, une partie neuve est reconduite à la porte et la scène de Lucerne mesurait un dialogue */
void banc_passage(void) {
    if (!E.w.nence || !E.w.ence) return;
    const int f = E.w.ence[8] | E.w.ence[9] << 8;
    if (f < SC_MAX_FLAGS) E.flags[f >> 3] |= (uint8_t)(1 << (f & 7));
}

/* Simulateur (RETROFW_SIMU, mots « charger:p » et « rencontre » du script ; 11/11/2026 : crash au lancement d'un combat sur la LDK) :
   une partie chargée, une rencontre tirée là où est Sera (le mot « enc » de rendu_test) */
int banc_charger(int p) { return hud_charger(p); }
void banc_rencontre(void) {
    int g[BT_EMAX];
    const int k = encounter_pick(), nm = equipe_nombre();
    const int ng = k >= 0 ? encounter_groupe(k, E.sera.x, E.sera.z, encounter_taille(k, script_niveau(E.sera.x, E.sera.z), nm), g) : 0;
    if (k >= 0) battle_start_groupe(g, ng, encounter_level(), 0, GROUPE_PARTS, NULL);
}
