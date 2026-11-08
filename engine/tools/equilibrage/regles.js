// Règles d'équilibrage proposées (niveau 100 au plus, boss final vers 60) : courbe d'expérience, niveau des combats,
// statistiques des ennemis selon leur niveau. Mêmes formules que engine/core/battle.c et l'émulateur une fois portées.
const NIV_MAX = 100;
const expOf = l => Math.round(10 * Math.pow(l, 1.45) + 10);
// Combats à parité pour passer un niveau : 2 + 18 (L − 1) / 58 (02/11/2026, consigne de l'utilisateur ; 15 + 0,1 L avant)
const rang = L => 2 + 18 * (L - 1) / 58;
const expNext = L => Math.floor(((116 + 18 * (L - 1)) * expOf(L) + 29) / 58);   // exp_next de battle.c (calcul entier)
// Fiche : PV et ATQ valent à son niveau ; à un autre niveau, ils suivent l'équipe (× (L + 6) / (niv + 6))
const stats = (e, L) => ({ ...e, lvl: L, hp: Math.max(1, Math.round(e.hp * (L + 6) / (e.lvl + 6))), atk: Math.max(1, Math.round(e.atk * (L + 6) / (e.lvl + 6))) });
// Rencontre : le niveau suit l'équipe (niveau + 1, moins 0 à 2) sans dépasser celui de l'histoire N
const niveauRencontre = (N, Lequipe, r) => Math.max(1, Math.min(NIV_MAX, Math.min(N, Lequipe + 1) - Math.floor(r * 3)));
module.exports = { NIV_MAX, expOf, expNext, rang, stats, niveauRencontre };
