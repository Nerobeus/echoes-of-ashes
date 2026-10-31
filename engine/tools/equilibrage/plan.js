// Plan de niveaux (40 h d'histoire, boss final vers 60 ; 60 h avec les annexes ; hardcore 90+) : niveau de chaque étape
// des quêtes de js/histoire.js (niveau des combats de l'étape, plafond des rencontres), sur le rythme du document
// « Echoes of Ashes — Scénario et lore » : prologue 2 h (1 → 5), acte I 8 h (6 → 18), acte II 9 h (18 → 32), acte III
// 8 h (32 → 45), acte IV 7 h (45 → 54), acte V 6 h (54 → 60). Les actes IV et V du document sont ici « acte4 » (la
// Source), en attendant d'être écrits.
const ETAPES = {
  prologue: [1, 2, 3, 4, 5],
  acte1: [6, 9, 11, 13, 14, 16, 17, 18],
  acte2: [23, 28, 32],
  acte3: [43],
  acte4: [56, 60],
  odric: [60],
  h01: [3, 3], h02: [7, 7], h04: [19, 19], e04: [8, 8, 8, 8],
  v01: [12, 12], v02: [12, 12], v04: [14, 14], v07: [11, 11], v09: [11, 11],
  // Hardcore (après la Source)
  h10: [90, 90], h11: [92, 94, 95, 96, 96], r05: [97, 99],
};
// Boss : quête, étape, équipe (0 SERA, 1 VARN, 2 BORS, 3 LYSE), victoire visée à leur niveau, PV × et ATQ × du profil
// avant l'étalonnage (un combat long et moins violent : un boss de fin de jeu)
const T4 = [0, 1, 2, 3];
const BOSS = {
  'SENTINELLE': ['prologue', 4, [0], 0.9], 'GLANEUR BLANC': ['h01', 1, [0], 0.9], 'ESSAIM': ['h02', 1, [0], 0.9],
  'INQUISITRICE': ['acte1', 3, [0, 1], 0.9], 'PATROUILLE': ['acte1', 5, [0, 1, 2], 0.95], 'FOUISSEUR': ['acte1', 7, [0, 1, 2], 0.9],
  'RÔDEUR': ['h04', 1, [0, 1, 2], 0.85], 'TRAQUEUSE': ['acte2', 1, [0, 1, 2], 0.9], 'GARDIENNE': ['acte3', 1, T4, 0.9],
  'GRAND INQUISITEUR': ['acte4', 1, T4, 0.9], 'MALDEN': ['acte4', 2, T4, 0.88, 1.6, 0.8],
  'ROI DES FERRAILLES': ['h10', 1, T4, 0.85, 1.5, 0.95],
  'LÉVIATHAN D\'ACIER': ['h11', 1, T4, 0.85, 1.5, 0.95], 'PHÉNIX DE BRAISE': ['h11', 2, T4, 0.85, 1.5, 0.95],
  'IMPÉRATRICE DE HALO': ['h11', 3, T4, 0.85, 1.5, 0.95], 'TITAN DE LA SOURCE': ['h11', 4, T4, 0.85, 1.5, 0.95],
  'COLOSSE JUMEAU': ['r05', 2, T4, 0.82, 1.6, 0.95],
};
// Ennemis nouveaux de l'histoire (variantes) : profil de départ = la fiche de leur modèle, sorts
const NOUVEAUX = {
  'ROI DES FERRAILLES': { modele: 'ROBOT DE GUERRE', sorts: ['foudre2', 'neant3'], magie: 30 },
  'COLOSSE JUMEAU': { modele: 'COLOSSE', sorts: ['meteore3', 'terre2'], magie: 35 },
};
const niveauEtape = (q, st) => (ETAPES[q] || [])[st - 1] || 0;
module.exports = { ETAPES, BOSS, NOUVEAUX, niveauEtape };
