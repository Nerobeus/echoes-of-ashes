/*
 * Format du paquet monde (monde.pak), exporté par l'émulateur (window.echoesExportPak)
 *
 * Petit-boutiste. En-tête : 'ECHO' (u32 0x4F484345), version u32 = 4. Puis des sections alignées sur
 * 4 octets : étiquette de 4 caractères, longueur u32, contenu (complété à un multiple de 4). Tout ce qui précède CDAT
 * reste en mémoire ; les chunks sont lus à la demande (eng_config_t.pak_read, sinon dans le paquet en mémoire).
 *
 *  HEAD  N u32 (côté de la grille), niveau de l'eau f32, GROUND_OFF u32, G0 u32, DECOR_SLOTS u32,
 *        taille des textures u32, positions (x, z) f32 : départ, marchand, arène (équipe en combat), ennemi en
 *        combat, épave (la Sentinelle attaque à moins de 3,1 cases de (x - 0,8 ; z + 0,1)),
 *        couleur du brouillard u8×3 + 1, direction du soleil f32×3, échelle du monde par rapport aux personnages
 *        f32 (facultative, 1 si absente), BIG_OFF u32 (textures des grands décors dans TEX, facultatif),
 *        zoom de départ de la caméra f32 (facultatif, 1,5 si absent ; 1 : distance d'origine, 5 au plus),
 *        zoom mini f32 (facultatif, 0,8 si absent), échelle globale des sprites f32 (facultative, 1 : personnages,
 *        véhicules et décors à leur taille ; ne change ni le sol, ni les bâtiments, ni la caméra)
 *  PAL   nombre de couleurs u32, 256 × (r, g, b, pleine lumière) u8, 8 index de l'eau u8
 *  EAUC  sortes d'eau (facultative ; sans elle, l'eau est opaque) : nombre u32 (64 au plus), puis 8 octets chacune :
 *        couleur du corps de l'eau u8, 0 u8 × 3, profondeur où il devient opaque f32. Sorte 0 : les rivières (leur
 *        opacité, fixe), puis une par terrain d'eau. Octet « eau » des cases (CDAT) : 1 + la sorte. Toute eau est
 *        transparente : le fond à son relief (texture de la case ; tuile 17 sans texture et lit des rivières), puis la
 *        surface (tuile 18 : reflets ; texel 0, le corps de l'eau tramé selon la profondeur, comme le brouillard)
 *  TEX   mémoire de textures 8 bits : tuile de décor n (16×16) à n × 256, texture 32×32 k à
 *        GROUND_OFF + k × 1024 (sol, routes, bâtiments, sol réduit). Numéro de tuile ≥ G0 : texture 32×32 G0 + k
 *  SPRT  largeur, hauteur de cellule u16, largeur de planche u16, lignes u16, planche 8 bits,
 *        nombre de tables de couleurs u32, tables de 256 octets (Sera, marchand, Varn, Bors, Lyse, KO)
 *  DECO  nombre u32, puis pour chaque grand décor, stocké à sa taille rognée (sprite à gauche, pied en bas) : décalage
 *        depuis BIG_OFF u32, largeur, hauteur du sprite u16, largeur stockée 2^sh u8, hauteur arrondie 2^shv u8 (masque
 *        des coordonnées), 0 u16. Lignes de 2^sh octets, hauteur lignes. Le quadrilatère suit le sprite.
 *  CHSS  grandes chasses (facultative, 06/11/2026 ; js/chasses.js) : version u8 (1), nombre u8, puis nom 24, indice 48 (UTF-8), événement u16 ; page CHASSES du JOURNAL
 *  ENFA  faiblesses des ennemis (facultative, 06/11/2026 ; js/attaques.js) : nombre u32 (celui de ENNE), puis u16 faible, u16 résiste par ennemi
 *        (bits des éléments MG_* de internal.h).
 *  ECHS  Échos à chercher, pour le JOURNAL (facultative, 06/11/2026 ; js/echos.js) : u8 version 1, u8 nombre de vies, puis pour chacune nom[24],
 *        u8 nombre, u8 0, u16 événement de chaque Écho (dans l'ordre des voix) ; le reste est dans le scénario (événements à drapeau 8).
 *  DEFI  bandeaux défilants des enseignes (facultative, depuis le 05/11/2026 ; enseignesTextures de l'émulateur) : version u8
 *        (1), nombre u8, largeur et hauteur de leurs textures u8 (une puissance de 2 et au plus 8 lignes), colonnes par
 *        seconde u8, 0 u8 × 3 ; puis par bandeau : grand décor u16 (sa texture dans DECO, réécrite à chaque image par
 *        render.c : defi_maj), couleurs allumée et éteinte u8, longueur u16, puis ses colonnes u8 (bit y : ligne y allumée),
 *        en boucle
 *  SPRS  planches propres à un personnage (facultative) : nombre u32, puis pour chacune : personnage u8 (0 = joueur,
 *        1 = personnage de PERS, 2 = autre animation de la dernière planche 0, 1 ou 3, 3 = tenue, voir TENU), directions u8 (8 : S, SE, E, NE,
 *        N, NW, W, SW), images u8, animation u8 (ANIM_* : 0 marche, 1 attente, 2 course, 3 attaque, 4 touché, 5 KO,
 *        6 technique, 7 défense, 8 victoire, 9 danse : métier danseuse dans les intérieurs, 10 départ : starter des circuits,
 *        11 nage : tenue de bain qui a aussi sa marche de terre, 04/11/2026), cellule fw, fh u16 (la même pour toutes les animations d'un personnage),
 *        portrait x, y, côté u16, taille à l'écran en % u16 (0 : 100), puis la planche : directions × fh lignes de
 *        images × fw index de palette (0 transparent)
 *  Paquet léger (ESP32 : la flash ; tools/paquet_sd.js, écrit par maj_monde.js) : les sections ci-dessous, sauf CDAT,
 *  INTR, CRTD et les pixels lourds ; monde.pak entier va sur la carte SD (/sd/echoes/monde.pak), lu par pak_read :
 *  SPRX  à la place de SPRS : les mêmes entrées de 16 octets, chacune suivie du décalage u32 de ses pixels dans monde.pak
 *        (lus par direction, à la demande : sheet_block de world.c)
 *  TEXS  textures du sol dans monde.pak : décalage u32, nombre u32 (1 Ko chacune, lues à la demande : ground_need) ; TEX
 *        ne garde alors que les tuiles du décor et les grands décors (HEAD : BIG_OFF = GROUND_OFF, taille de TEX)
 *  SDPK  taille u32 de monde.pak dont le paquet léger est tiré (le moteur la compare à eng_config_t.pak_size)
 *  Cartes (facultatives ; la carte 0 est celle de HEAD, CHNK, ZONE et MAP) : elles partagent palette, textures,
 *  personnages et scénario ; exemplaires (PNJS) et portes (PCAR) ont leur carte, le guidage aussi (GUID, octet 14) ;
 *  trafic et police ne sont que sur la carte 0.
 *  CART  cartes secondaires : nombre u32, puis 80 octets chacune : nom (UTF-8, 24 octets, fini par 0), côté N u32, ciel
 *        u8 (0 celui du monde, 1 souterrain : fond et brouillard de la carte, ni pluie ni heure, 2 espace : étoiles, 3 autre planète : ciel et brouillard de sa couleur, lumière de l'heure),
 *        drapeaux u8 (2 : carte de course de la carrière, ni téléport ni passage, voir CARR ; 1 : vol permanent, la carte du système solaire : Sera toujours à bord du vaisseau de l'équipe, le
 *        premier exemplaire vaisseau de son bloc de PNJS), drapeau de visite + 1 u16 (0 : aucun ; levé à l'arrivée, le
 *        saut y mène ensuite), niveau de l'eau f32, brouillard (r, g, b) u8, altitude du vol permanent u8 (1/10 de case ;
 *        0 : celle du vaisseau), départ x, z f32, arène des combats x, z f32, puis dans
 *        monde.pak (section CRTD) : index des chunks (format de CHNK, décalage des données absolu) décalage u32 et taille
 *        u32, zone des rencontres (format de ZONE) décalage u32 et taille u32, carte du menu (format de MAP) décalage u32
 *        et taille u32
 *  PASS  passages : nombre u32, puis 28 octets chacun : carte u8, carte d'arrivée u8, sorte u8 (0 passage, 1 puits,
 *        2 égout, 3 ascenseur, 4 faille, 5 astroport), 0 u8, zone x, z, rayon f32, arrivée x, z f32, condition u16
 *        (drapeau du scénario, 65535 aucune), 0 u16 ; Sera qui y entre à pied passe sur l'autre carte, rien ne repart
 *        avant qu'elle soit sortie de toute zone de passage
 *  CRTD  (après CDAT) données des cartes secondaires, lues au changement de carte (world_carte de world.c)
 *  ASTR  astres de la carte du système solaire (facultative ; vol permanent) : nombre u32, puis 48 octets chacun : nom
 *        UTF-8 sur 24 octets complétés de 0, carte u8 (la sienne), carte où l'on se pose u8 (255 : aucune), drapeau de
 *        visite u16 (65535 : toujours ; le saut du menu SYSTÈME › SAUT n'y mène qu'une fois levé), centre x, z et rayon de
 *        la sphère f32 (0 : repère seul ; le vaisseau n'entre pas à moins de VOL_MARGE de sa surface), point d'orbite x, z
 *        f32 (arrivée depuis ce monde, saut) ; flèches et noms sur le bord de l'écran (draw_astres de hud.c)
 *  PERS  personnages de la bibliothèque de l'atelier (facultative) : nombre u32, puis pour chacun : planche (index
 *        dans SPRS, 255 : aucune) u8, drapeaux u8 (PERS_FLANE, PERS_CONV, PERS_REMAP, PERS_VEHICULE ; bits 4 à 6 : type
 *        de véhicule VEH_* ; hors véhicule, bits 4 et 5 : genre du nom tiré par exemplaire, PERS_GENRE : 0 le nom du
 *        personnage, 1 féminin, 2 masculin, 3 neutre ; bit 6, PERS_ADULTE : planche de base 18+, voir EDIT ; bit 7,
 *        PERS_OBJET : objet inanimé, épave, qui ne se tourne pas vers Sera ; chez un véhicule, ce bit est PERS_TRAFIC),
 *        nombre de répliques u8, taille
 *        à l'écran en % u8 (0 : celle de la planche), table de couleurs 256 octets (si PERS_REMAP : variante de
 *        couleur, qui partage la planche de son modèle), nom puis répliques en UTF-8 terminées par 0 ; « NOM\tTEXTE » : réplique d'un autre interlocuteur (le joueur) ;
 *        premier octet 1 : réplique 18+ (voir EDIT ; aussi dans REPL)
 *  TENU  tenues (facultative) : nombre u32, puis 28 octets chacune : porteur u8 (255 : le joueur, sinon index dans PERS),
 *        planche (entrée de SPRS) u8, mise d'office u8 (0 portée, 1 nage, 2 course, 3 tant que le drapeau est levé ;
 *        4 TENUE_SAGE : planche de remplacement de son porteur 18+, le joueur compris, jamais portée, voir EDIT),
 *        drapeaux u8 (1 étanche : portée, on nage dans les eaux noires ; 2 tenue 18+), drapeau du scénario u16 (65535 aucun), 0 u16, nom
 *        UTF-8 sur 20 octets complétés de 0 (celui des objets de type tenue et de l'instruction « tenue »)
 *  PNJS  exemplaires (facultative) : nombre u32, puis place x, z f32, personnage u8 (index dans PERS), direction u8
 *        (huitièmes de tour), drapeaux u8 (1 : starter des circuits, son animation de départ pendant le compte à rebours
 *        et l'arrivée ; 2 : meneur d'une patrouille, il se promène ; 4 : suiveur), écart de rang u8 jusqu'à son meneur
 *        (suiveur : placé avant lui ; 0 sinon) ; bits 4 à 7 des drapeaux : la carte de l'exemplaire (0 la planète), les
 *        exemplaires rangés par carte, la planète d'abord ; la graine (rang dans PNJS) continue d'une carte à l'autre
 *  PVAR  palettes des personnages (facultative ; atelier : variantes « palette », Personnages.variantesAuto) : nombre u32,
 *        puis personnage (index de PERS) u16, rang u16 (1, 2… à la suite pour un même personnage), table de 256 couleurs ;
 *        chaque exemplaire en prend une ou ses couleurs : rue et intérieurs d'après sa graine (pvar_choix : (g ×
 *        0x9E3779B1) >> 16 mod (n + 1)), trafic et ennemi d'un combat au hasard, véhicule volé : celle du trafic
 *  NOMT  noms des PNJ (facultative ; atelier : js/noms.js) : nombres u16 ×4 (prénoms féminins, masculins, neutres, noms
 *        de famille), décalage u16 de chaque chaîne depuis le début des chaînes, puis les chaînes UTF-8 terminées par 0.
 *        Exemplaire d'un personnage à genre, graine g (rang dans PNJS ; intérieur : 2048 + (bâtiment visité × 4 +
 *        étage) × 8 + place) : r = g mod nP, q = g div nP, prénom (r × 1009 + 7) mod nP, nom (q × 613 + r × 211 + 3)
 *        mod nS (pnj_nom de hud.c, nomPnj de l'émulateur)
 *  TRAF  trafic (facultative) : véhicules autour de Sera u8 (0 : aucun), 0 u8, 0 u16 ; véhicules : PERS_TRAFIC
 *  ROUT  axes des routes (facultative) : nombre u32, puis pour chacun : points u16, largeur en dixièmes de case u8,
 *        0 u8, points (x, z) f32. Le trafic roule dessus, sur la voie de droite ; sans ROUT, de case de route en case
 *  AUDI  sons (facultative) : nombre u32, volume général en % u8, 0 u8 ×3, puis pour chaque son (ordre SON_* de
 *        audio.h) : échantillons u32, fréquence u16, boucle u8, 0 u8, échantillons mono i16 complétés à 4 octets
 *  VALT  altitude maxi des vaisseaux (facultative) : nombre u32, puis une f32 par personnage (cases ; 0 : 4 cases)
 *  PIST  piste de course (facultative, course.c) : points u32, demi-largeur f32 (cases), tours u8, 0 u8 ×3, point de la
 *        ligne de départ u32, puis les points de l'axe (x, z) f32, boucle fermée sans répéter le premier ; on la
 *        parcourt dans le sens des points. Glissières, damier et portique du départ sont dans la géométrie des chunks
 *  SONM  son du moteur de chaque personnage (facultative) : nombre u32, puis un u8 par personnage (SON_*, 255 : celui
 *        de son type de véhicule)
 *  PIVO  ancrage des planches (facultative) : nombre u32 (celui des entrées de SPRS), puis pour chacune 8 × (x, y) i16
 *        en demi-pixels de cellule, une paire par direction (S, SE, E, NE, N, NO, O, SO) : le point de l'image posé sur la
 *        position au sol (centre d'un véhicule vu sous cet angle). x = -32768 : pas d'ancrage (bas de la cellule)
 *  SNRM  normalisation des silhouettes (facultative, 01/11/2026) : nombre u32 (celui des entrées de SPRS), puis u16 par
 *        entrée, en centièmes (0 : 1) : la planche est dessinée × ce facteur (en plus de la taille du personnage, PERS),
 *        pour que sa silhouette debout (image 0 de l'attente, médiane des 8 directions) fasse celle de l'humain type
 *        (médiane des humains : ni véhicules, ni objets, ni ennemis ; Sera comprise) : la taille de PERS est alors la
 *        hauteur voulue par rapport à lui (100 : un humain) ; une animation (personnage 2) a celui de sa planche, une
 *        tenue en pose (nage, accroupie…) celui de la planche de son porteur, une planche prise dans l'animation d'un
 *        autre personnage (l'ÉPAVE) celui de ce personnage ; 1 pour les véhicules et les planches dont les pieds ne
 *        sont pas dans le quart bas de la cellule (normaliserSilhouettes de l'émulateur). Joueur : la taille de sa
 *        planche, ou celle du personnage de l'héroïne choisie (joueur_taille de game.c)
 *  OMBR  ombre de chaque personnage (facultative) : nombre u32 (celui de PERS), puis pour chacun et enfin pour le joueur
 *        8 octets : décalage horizontal i8 (% de la largeur de cellule, vers la droite), vertical i8 (% de la hauteur,
 *        vers le haut), largeur u8 et hauteur u8 (% de l'ombre d'origine), aucune ombre u8, 0 u8 ×3
 *  ROUL  véhicules qui roulent (facultative) : nombre u32 (celui de PERS), puis un u8 par personnage : 1 roule (image
 *        posée sur le sol, son bas sur la chaussée), 0 plane (un peu au-dessus du sol, en ondulant). Sans ROUL : tous les
 *        véhicules roulent, sauf le vaisseau
 *  VEHI  conduite des véhicules (facultative) : nombre u32 (celui de PERS), puis pour chaque personnage : vitesse maxi
 *        f32 (cases par seconde, sur route pour la voiture et le camion : 55 % hors route ; 45 % dans le trafic),
 *        accélération f32 (cases/s² ; le frein vaut le double). 0 : valeurs du type de véhicule
 *  VEH2  conduite et course (facultative, remplace VEHI) : nombre u32, puis 16 octets par personnage : vitesse maxi f32,
 *        accélération f32 (comme VEHI), adhérence f32 (en course : part de l'adhérence de la piste, 1 normale ; elle
 *        règle aussi le dérapage qui fait partir en tête-à-queue), drapeaux u8 (1 : utilisable en course, au menu de
 *        course comme véhicule du joueur ou adversaire), service u8 (trafic : 0 civil, 1 police, 2 secours, 3 pompiers, 4 armée,
 *        5 voirie, 6 transport), 0 u8 ×2. Sans VEH2 : adhérence 1, tous en course
 *  PNOM  noms des pistes (facultative) : nombre u32 (celui de PIS2), puis 24 octets par piste, nom du tracé en UTF-8
 *        complété de 0 (menu de course : les pistes dont la ligne de départ est à moins de 8 cases sont les tracés d'un
 *        même circuit). PIS2 depuis le 01/11/2026 (mode course) : l'octet après la surface est la carte de la piste (0 la
 *        planète ; les cartes de course), et les bits 4 à 6 de l'octet de chaque point sa surface + 1 (0 : celle de la piste ;
 *        bit 0 : sur un pont) ; course.c : 16 pistes au plus
 *  CARR  carrière du mode course (facultative ; js/carriere.js de l'Atelier, écrite par l'émulateur : emulateur/carriere.js ;
 *        carriere.c) : en-tête de 40 octets : version u8 (1), classes u8, circuits u8, coupes u8, véhicules u16, 0 u16,
 *        points par place u8 × 4 (10, 6, 3, 1), seuils des médailles d'une coupe u8 × 3 (bronze, argent, or : points sur
 *        ses trois courses), 0 u8, médailles de temps u16 × 3 (meilleur tour au plus ce multiple du tour de référence,
 *        1/1000 : bronze, argent, or), 0 u16, crédits par place u16 × 4, prime d'une médaille de coupe gagnée u16 × 3, 0 u16 ;
 *        puis les classes, 24 octets chacune : nom (UTF-8, 16 octets complétés de 0), vitesse de tous u16 (1/1000 de celle
 *        de leur véhicule), vitesse des adversaires u16 (1/100 de case/s, avant leur niveau), gains u16 (1/1000 : × les
 *        crédits par place), 0 u16 ; les circuits, 40 octets : nom 24 (celui de sa piste), piste u8 (rang dans PIS2), carte
 *        u8, tours u8, 0 u8, heure u16 (1/1000 de jour), 0 u16, tour de référence par classe u16 × 4 (1/100 s ; 0 : pas
 *        encore mesuré) ; les coupes, 32 octets : nom 24, classe u8, niveau des adversaires u8 (0 facile à 2 difficile),
 *        armes u8, 0 u8, circuits u8 × 3 (rangs ci-dessus), 0 u8 ; les véhicules, 8 octets : personnage u16 (PERS), départ u8
 *        (1 : donné à une nouvelle carrière), 0 u8, prix u32 (crédits).
 *        Version 2 (étape 2, 02/11/2026 ; la version 1 reste lue, sans épreuves ni réglages : ceux de carriere.c) : le u16
 *        après les véhicules dans l'en-tête est le nombre d'épreuves ; les véhicules prennent 16 octets : personnage u16,
 *        départ u8, drapeaux u8 (bit 0 : récompense d'un duel, pas à vendre), prix u32, vitesse u16 (1/100 de case/s),
 *        accélération u16 (1/100 de case/s²), adhérence u16 (1/1000) (0 : celles du véhicule, VEH2), 0 u16 ; puis les
 *        épreuves, 32 octets : nom 24, sorte u8 (1 contre-la-montre, 2 tour rapide, 3 élimination, 4 duel), circuit u8,
 *        classe u8, tours u8 (0 : ceux de la sorte), rival u16 (PERS, 0xFFFF : aucun ; le rival nommé d'un duel), niveau
 *        u8 (difficulté par défaut), armes u8 ; puis 32 octets de réglages : prix des améliorations u16 × 3 (niveaux 1 à
 *        3), part gagnée par niveau u16 × 3 (1/1000 : vitesse, accélération, adhérence), crédits par difficulté u16 × 3
 *        (1/1000), temps des médailles par difficulté u16 × 3 (1/1000), départ arrêté d'un contre-la-montre u16 (1/100 s,
 *        ajouté aux tours de référence), crédits du défi du jour u16 (1/1000), victoires par livrée u8, 0 u8 × 3.
 *        Version 3 (catégories de compétition, 03/11/2026 ; les versions 1 et 2 restent lues : une seule catégorie,
 *        OUVERTE) : l'octet 15 de l'en-tête est le nombre de catégories ; le dernier u16 d'une classe devient catégorie u8
 *        (rang ci-dessous, 0 : OUVERTE), 0 u8 ; les circuits prennent 64 octets : nom 24, piste u8, carte u8, tours u8,
 *        pluie u8 (% ; 0 : temps sec), heure u16, 0 u16, tour de référence par classe u16 × 16 (1/100 s ; 0 : pas mesuré) ;
 *        le dernier u16 d'un véhicule devient catégorie u8, 0 u8, et son octet « départ » a deux bits (0 : donné à
 *        l'ouverture de sa catégorie, 1 : à celle d'OUVERTE, comme le véhicule de départ des versions 1 et 2) ; après les
 *        réglages, les catégories, 48 octets chacune : nom 16 (UTF-8), aide 24 (UTF-8), ouverte par la catégorie u8 (255 :
 *        dès le départ), quand toutes les coupes de la classe u8 ont le bronze (255 : dès le départ), drapeaux u8 (bit 0 :
 *        son garage a tous les véhicules : OUVERTE), 0 u8 × 5. La catégorie d'une coupe ou d'une épreuve est celle de sa
 *        classe ; les classes d'une catégorie se suivent (rangs croissants)
 *  METE  météo (facultative) : pluie, part du temps en % u8 (0 : jamais d'elle-même), intensité en % u8, 0 u16
 *  RENC  rencontres (facultative) : distance moyenne entre deux rencontres u16 (unités du monde × échelle, comme la
 *        marche de l'émulateur ; 0 : aucune), 0 u16. Distance tirée selon une loi exponentielle de cette moyenne.
 *  ENNE  ennemis de la bibliothèque (facultative) : nombre u32, puis pour chacun : personnage (index dans PERS, avec
 *        une planche ; neuvième bit : bit 7 des drapeaux) u8, niveau u8, poids du tirage u8, drapeaux u8 (1 : vaincu, il laisse un CŒUR DE MACHINE ; bits 1-2
 *        depuis le 01/11/2026 : taille de groupe au plus en rencontre et en patrouille, 0 la règle du jeu (encounter_taille
 *        de game.c), 1 toujours seul, 2 ou 3 ; champ « Groupe » de la fiche, js/personnages.js), PV u32,
 *        ATQ u16, VIT u16, terrains u32 (bit k : terrain k de ZONE). Sprite de face (ligne S de sa planche), deux fois sa
 *        taille en combat ; PV et ATQ réduits pour une équipe de moins de 4 (× (membres + 1) / 5), et pour un groupe
 *        (battle.c : GROUPE_PV, GROUPE_ATQ ; boss et sbires : SBIRE_PART). Groupe du scénario : instruction BATTLE2 (51)
 *        du bytecode de SCEN (mode u8 : bit 0 sbires, bit 1 fretin, bit 2 personnages sur deux octets ; nombre u8, personnages).
 *  ENSO  sorts des ennemis (facultative) : nombre u32 (celui de ENNE), puis pour chaque ennemi de ENNE, dans son ordre
 *        (12 octets) : ses sorts d'attaque (masque de 64 bits sur le catalogue de JOBS, u32 bas, u32 haut), part de ses
 *        tours où il en lance (%) u8, 0 u8 ×3 ; dégâts ATQ × puissance, un sort de niveau III frappe toute l'équipe (× 0,6)
 *  ENAT  attaques des ennemis hors de leurs sorts (facultative, depuis le 03/11/2026 ; js/attaques.js de l'Atelier) : nombre
 *        u32 (celui de ENNE), puis pour chaque ennemi de ENNE, dans son ordre (132 octets) : nombre d'attaques u8 (0 à 4 ;
 *        0 : la règle d'avant, RAFALE LASER sur un membre 70 % de ses tours, ATQ × 1,2 à 1,6, BALAYAGE THERMIQUE sur toute
 *        l'équipe 30 %, ATQ × 0,6 à 0,8), 0 u8 × 3, puis 4 attaques de 32 octets (les places libres à 0) : nom 20 (UTF-8,
 *        majuscules, terminé par 0), effet u8 (FX_* de internal.h ; un sort, FX_SORT(élément, niveau) hors du soin : son
 *        dessin seulement), cible u8 (bit 0 : toute l'équipe, sinon un membre au hasard), poids u8 (tirage : poids sur la
 *        somme des poids de l'ennemi ; l'Atelier donne 30 en tout aux attaques sur toute l'équipe, 70 aux autres),
 *        drapeaux u8 (bit 0 : attaque signature, plus grande, annoncée par le son d'une technique), puissance u16 (1/1000 :
 *        dégâts ATQ × puissance × 6/7 à 8/7 ; l'Atelier ramène la moyenne pondérée à 1,4 sur un membre et 0,7 sur toute
 *        l'équipe : l'espérance des dégâts est celle de la règle d'avant), couleur r, g, b u8 (0, 0, 0 : celle de l'effet ou
 *        de son élément), 0 u8. Une attaque au nom sans 0, d'effet inconnu, sans poids ou sans puissance : l'ennemi garde la
 *        règle d'avant
 *  SCEN  scénario (facultative ; le même contenu peut être sur la carte SD, echoes/histoire.scn, qui l'emporte), fait pour
 *        être lu par morceaux (script.c ne garde que la table des événements, les noms des quêtes et les objets) :
 *        'SCN5', nombres de drapeaux, de chaînes, d'événements u32, décalage de l'index des chaînes u32, de la table
 *        des événements u32, longueur du plus long code u32, nombre de quêtes u32, décalage de leur table u32, nombre
 *        d'objets u32, décalage de leur table u32, décalage du plan de musique u32, sa taille u32 (0 : aucun), décalage de
 *        la table des sujets u32 (depuis le 31/10/2026 : en-tête de 56 octets ; un scénario plus ancien a un en-tête de 52
 *        octets, reconnu à la première chaîne qui commence avant 56, et pas de table), décalage de la table des ouvertures
 *        u32 (depuis le 03/11/2026 : en-tête de 60 octets ; reconnu à la première chaîne qui commence à 60 ou après ; plus
 *        ancien : aucune table, panneaux.c ne marque que les « parler » de l'étape 0) ; chaînes UTF-8
 *        terminées par 0 (la première : le nom du joueur) ; index : décalage u32 de chaque chaîne ; événements (28
 *        octets) : déclencheur u8 (0 lancement, 1 zone, 2 parler, 3 entrée dans un bâtiment de la carte dont le centre est à moins
 *        de r de x, z ; 4 lu sur un panneau de quêtes, le joueur le choisit dans sa liste : x, z, carte disent sa région), personnage u8 (index PERS, 255 aucun), drapeaux u8
 *        (1 une seule fois, 2 condition inversée, 4 événement 18+), étape u8, condition u16 (drapeau, 65535 aucune), longueur du code
 *        u16, centre x, z f32, rayon f32, décalage du code u32, quête u16 (65535 aucune), carte u8 (d'une zone : 0 la planète), 0 u8 ; sujets
 *        (2 octets par événement, juste après leur table) : intitulé u16 (chaîne, 65535 aucun) d'un « parler » dans le choix
 *        des sujets (script_talk de script.c) ; quêtes (8 octets) :
 *        nom u16 (chaîne), type u8 (0 principale, 1 secondaire), nombre d'étapes u8, décalage des étapes u32 ; étapes
 *        (8 octets) : objectif u16 (chaîne), cible x, z u16 (1/8 de case, 65535 aucune), niveau u16 (niveau des combats
 *        de l'étape et plafond des rencontres, 0 aucun : script_niveau de script.c ; bits 12 à 15 : carte de la cible) ; objets du sac (16
 *        octets, js/objets.js) : nom u16, description u16 (chaînes), type u8 (0 soin, 1 quête, 2 à revendre, 3
 *        divers, 4 tenue : PV = chaîne du nom de la tenue, 5 équipement : bonus dans JOBS), drapeaux u8 (1 butin des machines, 2 soin de toute l'équipe, 4 objet 18+ : tenue 18+, 8 relève un membre à terre : RELEVE_PART de ses PV, depuis le 02/11/2026 ; 16 armure qui se voit : PV = chaîne du nom de sa tenue ; 32 noyau élémentaire, PM = son élément MG_*, depuis le 08/11/2026), nombre au départ u8, icône u8 (k : icône
 *        k de OBJI, 0 aucune), PV u16, PM u16 (65535 : tout), prix u32 ; plan de musique (music.c ; pistes : chaînes,
 *        noms de musique.bin, 65535 aucune) : par défaut, combats, victoire, défaite, courses u16, nombre de zones u16,
 *        d'ennemis u16, de pistes de cartes u16, zones (8 octets : x, z, rayon u16 en 1/8 de case, piste u16 ; sur la
 *        planète seulement), ennemis (4 octets : personnage u8, 0 u8, piste u16), pistes de cartes (4 octets : carte u8
 *        de CART, 0 u8, piste u16 : partout sur cette carte) ; table des ouvertures (03/11/2026, js/scenario.js :
 *        ouverturesDe ; marques « ! » et panneaux de quêtes, panneaux.c) : nombre n u32, nombre de termes g u32, puis n
 *        ouvertures de 16 octets (un événement qui ouvre une quête : « quete commencer », « etape N » d'une quête à l'état
 *        0) : événement u16, quête ouverte u16, garde u16 (rang de son premier terme, 65535 : aucune condition), accroche
 *        u16 (chaîne, 65535 aucune), x, z u16 (1/8 de case : la place du personnage à qui parler, sinon le centre de la
 *        zone ; 65535 : aucun lieu), carte u8, drapeaux u8 (1 : sur les panneaux de quêtes, une quête secondaire ouverte
 *        hors de l'histoire principale ; 2 : quête d'affichage, déclencheur 4), personnage u8 (PERS, 255 aucun), 0 u8 ; puis
 *        g termes de 4 octets : sorte u8 (1 drapeau levé, 2 baissé, 3 héroïne choisie, 4 pas elle ; 5 quête à l'état
 *        valeur ou plus loin, 6 en deçà ; 254 : un autre chemin, 255 : fin), valeur u8, argument u16 (drapeau, PERS de
 *        l'héroïne ou 255 Sera, quête) : les conditions des « si » qui mènent à l'ouverture, un chemin ou un autre ; puis les codes
 *        (bytecode de js/scenario.js de l'Atelier). Décalages depuis le début de la section.
 *  PVIS  présence des personnages (facultative) : nombre u32, puis pour chacun (ordre de PERS) drapeau du scénario u16
 *        (65535 : toujours là), inversé u8 (1 : présent tant que le drapeau est baissé), 0 u8
 *  CIBL  cibles de chasse (facultative ; js/scenario.js : l'ennemi d'un événement de zone d'une quête, « combat NOM » hors
 *        des blocs si et pas acteur du script) : nombre u32 (64 lus au plus), puis pour chacune personnage u8 (index
 *        PERS), carte u8, quête u16, étape u8, 0 u8, x u16, z u16 (1/8 de case : centre de la zone) ; dessinée tant que
 *        la quête est à cette étape (aussi dans le paquet léger de l'ESP32)
 *  LUMI  lueur des feux (facultative ; décors sources de feu, tirés par l'émulateur de la table LUMIERES de js/decor.js,
 *        toutes cartes) : nombre u32, puis 12 octets chacune : x u16, z u16 (1/8 de case), y i16 (1/64 : la flamme),
 *        carte u8, rayon u8 (1/16 de case), couleur r, g, b u8, intensité u8 (1/255) ; la nuit, les 4 plus proches de
 *        Sera éclaircissent l'image (render.c : lum_band ; aussi dans le paquet léger de l'ESP32)
 *  ZONE  terrain des rencontres (facultative) : côté M u32 (0 : aucun), puis M × M index de terrain u8, un par bloc de
 *        2 × 2 cases (celui de la case du coin), ligne par ligne (255 : inconnu, aucune rencontre) ; lue à l'échelle du
 *        côté N de la carte (bloc de N / M cases : 64 × 64 sur le système solaire, une seule zone, « espace »)
 *  MAP   (ancienne, plus exportée depuis le 01/11/2026 : PLAN) côté M u32, M × M couleurs (r, g, b) u8 : carte du menu
 *  PLAN  (après CDAT, lue à la demande ; carte.c, planPaquet de l'émulateur) plans de la carte du menu de chaque carte :
 *        'PLN1' u32, nombre de cartes u32, pictogrammes u16 (Buildings.GLYPHES de l'Atelier), côté des tuiles u16 (32) ;
 *        pictogrammes : 16 lignes u16 chacun (bit 15 : pixel de gauche) ; puis pour chaque carte : décalage de son bloc
 *        depuis le début de PLAN u32, taille de son répertoire u32, taille du bloc u32. Bloc : en-tête de 16 octets (côté N
 *        u16, niveaux u8, couleurs nc u8, tuiles u16, noms u16, noms sûrs u16, 0 u16, 0 u32) ; niveaux (8 octets) : e i8
 *        (2^-e pixels par case), dense u8 (1 : toutes les tuiles, rangées tz × nt + tx ; 0 : celles des lieux bâtis, clé
 *        croissante), tuiles par ligne nt u16, première u16, nombre u16 ; palette : nc × (r, g, b) u8 pour les index 1 à nc
 *        (0 n'est jamais pris), complétée à 4 octets ; tuiles (8 octets) : clé tz × nt + tx u16, taille u16 (0 : tuile d'une
 *        couleur, décalage = la couleur ; 1 024 : brute ; sinon LZ4 de 32 × 32 index), décalage depuis le début du bloc
 *        u32 ; noms (32 octets) : x, z u16 (1/16 de case), sorte u8 (0 ville, 1 village, 2 campement, 3 lieu, 4 bâtiment,
 *        5 astre), pictogramme u8 (k + 1, 0 aucun), couleur u8 (0 aucune, 1 vert, 2 magenta, 3 cyan, 4 ambre), drapeaux
 *        u8 (1 : un nom sûr), nom UTF-8 sur 24 octets ; noms sûrs (26 octets : rang du nom u16, nom sur 24 octets), complété
 *        à 4 octets (fin du répertoire) ; puis les données des tuiles
 *  PLAX  (paquet léger de l'ESP32, tools/paquet_sd.js) : décalage de PLAN dans monde.pak u32, sa taille u32
 *  FONT, ICON : voir ui.c
 *  OBJI  icônes des objets du sac (facultative) : nombre u32, couleurs n u32, palette (r, g, b) u8 × n (index 1 à n ;
 *        0 transparent) complétée à 4 octets, puis 16 × 16 index u8 par icône, ligne par ligne
 *  JOBS  jobs et équipement (facultative ; js/equipement.js de l'Atelier) : nombre de types d'équipement nt u8 (32 au
 *        plus), de jobs nj u8 (16 au plus), de membres u8 (NMEMB = 10 depuis les recrues du 01/11/2026, AMBRE comprise ;
 *        4 ou 9 avant : ceux qui manquent n'ont ni job ni équipement de départ), version u8 ; types (version 0 : 20 octets, 1 : 24) :
 *        emplacement u8 (0 arme, 1 armure, 2 accessoire), effet en combat d'une arme u8 (FX_* de internal.h ; 0 en
 *        version 0), 0 u8 ×2, nom (UTF-8, 16 octets, fini par 0), puis en version 1 sa couleur (r, g, b) u8, 0 u8 ; jobs (52 octets) : nom (16 octets), PV,
 *        PM, ATQ en % i8, VIT i8 (points), affinité u8 × 32 (une par type : 0 ne peut pas le porter, 1 à 4 C, B, A, S :
 *        bonus de l'objet × 0,7, 0,85, 1, 1,2) ; membres (8 octets, ordre de PARTY) : job u8 (255 aucun), 0 u8, jobs
 *        connus u16 (bit j), objets portés au départ u8 ×3 (arme, armure, accessoire : index des objets du sac, 255
 *        aucun ; hors du sac), 0 u8 ; puis nombre d'objets d'équipement u32 et pour chacun (12 octets) : objet u8
 *        (index), type u8, image u8 (k : image k de OBJG, 0 aucune), VIT i8, ATQ, DÉF, PV, PM i16. Statistiques d'un
 *        membre : celles du niveau × (100 + % du job) / 100 (VIT + points), plus les bonus × affinité ; dégâts reçus
 *        × 200 / (200 + DÉF). Version 2 : puis nombre de sorts ns u8 (64 au plus), 0 u8 ×3, et pour chacun (24 octets) :
 *        nom (16 octets), effet u8 (FX_SORT : 10 + élément × 3 + niveau - 1), soin u8 (0 attaque, 1 un allié, 2 toute
 *        l'équipe, 3 relève un allié à terre : depuis le 02/11/2026), PM u16, puissance × 100 u16 (MAG × puissance : dégâts ou PV
 *        rendus), niveau d'équipe où on l'apprend u8 (0 : tout de suite ; 02/11/2026), 0 u8 ; puis pour chaque job,
 *        puis chaque objet d'équipement (dans l'ordre des objets ci-dessus : donnés à qui le porte), 12 octets : ses
 *        sorts (masque de 64 bits, bit k : sort k ; u32 bas, u32 haut) et son MAG (job : % i8, 0 u8 ×3 ; objet : i16,
 *        0 u16). MAG d'un membre : l'ATQ du niveau × (100 + MAG % du job) / 100, plus le MAG des objets × affinité.
 *        Version 3 (03/11/2026, magie : progression des jobs, cristaux du Chœur) : l'octet 10 de l'entrée de 12 octets d'un
 *        objet donne ses points d'apprentissage (cristal du Chœur : porté au combat, debout, chaque victoire en donne
 *        CRISTAL_PT_* ; à ce total, ses sorts sont appris pour de bon ; 0 : aucun ; l'octet 11, depuis le 08/11/2026, l'élément
 *        d'un équipement + 1 : MG_* de internal.h, 0 aucun ; une arme : l'attaque de qui la porte en prend l'élément pour les
 *        faiblesses de ENFA ; une armure, un accessoire : les coups ennemis de cet élément font × RESISTE_X) ; puis, pour chaque job, le niveau d'équipe où
 *        il enseigne chaque sort du catalogue (ns octets ; 0 : tout de suite ; seuls comptent ceux de son masque), complété
 *        à 4 octets. Avant la version 3, un job enseigne ses sorts au niveau du sort (octet 22)
 *  OBJG  images 32 × 32 des équipements (facultative) : nombre u32, puis pour chacune (1 124 octets) : couleurs n u8
 *        (32 au plus), 0 u8 ×3, palette (r, g, b) u8 × 32 (index 1 à n ; 0 transparent), 32 × 32 index u8
 *  CMEM  tailles des chunks développés (facultative, avant CHNK) : nombre de cartes u32 (la carte 0, puis les cartes
 *        secondaires dans l'ordre de CART), puis pour chacune NC u32 et NC × NC u16 (ligne par ligne, unités de 16
 *        octets) ; la fenêtre de chunks fait alors la pire somme de CWIN × CWIN chunks voisins (world.c : fen_pire,
 *        fen_alloc), au lieu de CWIN × CWIN fois le plus gros
 *  CHNK  CS u32 (16), chunks par côté NC u32, plus grandes tailles d'un chunk : brute, compressée, en mémoire u32,
 *        début des données de CDAT dans le fichier u32, puis pour chaque chunk (ligne par ligne) : décalage dans
 *        CDAT u32, taille compressée u32, taille brute u32
 *  CDAT  chunks compressés en LZ4 (format bloc). Coordonnées relatives au coin du chunk (CS × cx, CS × cz) :
 *        en-tête : morceaux de route, garde-corps, faces de blocs, faces de bâtiments, bâtiments, rectangles de
 *        collision, plantes u16×7, 0 u16 ; hauteurs aux sommets i16 (1/512) (CS+1)², lumière u8 (1/16) (CS+1)² ;
 *        tuile u16 CS² ; drapeaux (1 eau, 2 rivière, 4 route, 8 eau libre, 16 rue, 32 eau claire), eau (1 + sorte d'EAUC), case cachée sous un bâtiment u8 CS² ;
 *        morceaux de route par case u8 CS² ; hauteur de chaussée des cases de route i16 (1/512, -32768 : aucune) ;
 *        niveau de l'eau par case i16 (1/512, -32768 : sans plan d'eau ; lac ou mer, berge : eau voisine) ;
 *        texture réduite des blocs de 2 × 2 cases u16 (CS/2)² (0xFFFF : case par case) ;
 *        polygones (routes case par case, garde-corps, blocs, bâtiments) : tuile u16, drapeaux u8 (1 transparent,
 *        2 face arrière éliminée), n u8, normale f32×3 si drapeau 2, sommets x, y, z i16 (1/256), u, v i16
 *        (1/4 de texel), lumière u8 (1/16), 0 u8 (premier sommet d'une face de bâtiment accessoire : rang de son volume + 1, 61 au plus ; 62 : affiche collée au
 *        polygone précédent, son mur, dessinée dans le même seau ; 255 : objet posé sans plan,
 *        conteneur, citerne, avancé lui aussi ; morceau de route : 1, tablier de verre d'un circuit, tramé (ROUTE_VERRE) ; 0 : aucun ; render.c
        avance ce qui s'accroche au volume principal, plaques, auvents, toits, dans l'ordre de construction) ; bâtiments : centre x, z i16 (1/256), rayon u16 (1/256),
 *        premier polygone u16 (parmi ceux des bâtiments), nombre u16 ; collisions : x0, z0, x1, z1 i16 (1/256) ;
 *        décors (herbes, arbres, rochers) triés par case : case u8, x, z dans la case u8 (1/256), drapeaux u8
 *        (1 retourné, 2 se balance), taille u16 (1/256), tuile u16 (≥ TILE_BIG : grand décor), phase u8 (1/256 de tour),
 *        lumière u8 (1/16), y i16 (1/512)
 *  MIPS  textures réduites des bâtiments (facultative) : première tuile qui en a u32, nombre n u32, puis pour chacune ses
 *        tuiles de niveau 1 (16 × 16, couleur majoritaire de chaque bloc de 2 × 2) et 2 (8 × 8), répétées en 32 × 32 dans
 *        TEX, u16 ×2 ; render.c les prend pour une face vue de loin ou de biais (u, v divisés par 2 ou 4)
 *  GUID  lieux du menu GUIDAGE (facultative) : nombre u32, puis 40 octets chacun : point visé x, z f32 (bâtiment : devant
 *        sa porte principale), rayon d'arrivée f32 (le guidage s'arrête en deçà), sorte u8 (0 ville, 1 bâtiment spécial,
 *        2 lieu), groupe u8 (0 villes, 1 lieux, 2 + rang de la famille du catalogue, 15 astres), carte u8 (0 la planète,
 *        k : la k-ième de CART ; 0 avant le 01/11/2026, quand il n'y avait que la planète), 0 u8, nom UTF-8 sur 24 octets
 *        complétés de 0 (bâtiment : celui de son type, qui fait sa catégorie) ; ceux de la planète d'abord, puis ceux de
 *        chaque autre carte (lieux nommés, bâtiments nommés, puits, aire d'envol ; astres du système solaire)
 *  SPIX  planches des personnages et des tenues sur deux octets (facultative ; PERS et TENU n'ont qu'un octet, faux
 *        au-delà de 254 entrées de SPRS) : nombres de personnages et de tenues u32 ×2, puis entrée de SPRS u16 de chacun
 *        (65535 : aucune), complétées à 4 octets ; elles l'emportent sur PERS et TENU
 *  TIS2  tissu des tenues (facultative ; remplace TISS le 07/11/2026) : nombre u32, puis pour chaque planche masquée :
 *        entrée de SPRS u16, 0 u16, taille des bits u32, 8 octets par case (direction × images + image : x, y, largeur,
 *        hauteur u8 de la boîte du tissu dans la case, décalage de ses bits u32 ; sans tissu : tout à 0), les bits (ligne
 *        après ligne dans la boîte ; bit k de l'octet o : pixel 8 × o + k ; chaque boîte commence sur un octet), complétés
 *        à 4 octets
 *  TENV  variantes de couleur des tenues (facultative) : nombre u32, puis 260 octets chacune : tenue (index de TENU)
 *        u16, 0 u16, table de 256 couleurs appliquée aux seuls pixels du tissu de sa planche (TIS2) ; la tenue d'une
 *        variante partage la planche de la tenue d'origine (js/tissu.js de l'Atelier)
 *  PTIV  variantes de couleur des personnages (facultative) : nombre u32, puis 260 octets chacune : personnage (index de
 *        PERS) u16, 0 u16, table de 256 couleurs appliquée aux pixels du tissu de sa planche (TIS2) : danseuses d'une
 *        tenue de scène recolorée ; tables comptées avec celles de TENV
 *  OUVR  ouverture (facultative ; core/accueil.c) : version u8 (1), nombre de cartons u8 (24 au plus), nombre d'héroïnes
 *        au choix u8 (15 au plus, Sera en plus), 0 u8 ; fond de l'écran titre x, z f32 (cases), heure f32 (part de
 *        journée, < 0 : celle du départ) ; héroïnes (index PERS) u8, complétées à 4 octets ; par carton x, z f32 (x < 0 :
 *        sur le noir), heure f32 ; puis chaînes UTF-8 terminées par 0 : titre, sous-titre, musique de l'écran titre (nom
 *        de musique.bin), question du choix de l'héroïne, texte de chaque carton
 *  OUV8  héroïnes de OUVR proposées seulement quand le 18+ est déverrouillé (facultative, depuis le 01/11/2026 : AMBRE ;
 *        jamais dans l'édition enfant ; accueil.c her_vue) : masque u32, bit k : la k-ième héroïne de OUVR
 *  IMAG  images du jeu (facultative ; Stable Diffusion, tools/cutin du dépôt : cartons de l'introduction, cut-ins des
 *        techniques ; world.c img_ix, une à la fois dans un tampon commun) : nombre u16, 0 u16 ; 12 octets par image
 *        (largeur u16, hauteur u16, couleurs n u8, genre u8 (pose d'un cut-in, CG_* de internal.h : 0 neutre, 1 lame,
 *        2 tir, 3 laser, 4 corps à corps, 5 magie, 6 à 11 foudre, feu, glace, sacré, ténèbres, soin, 12 fouet, 13 canon,
 *        14 arc ; cutin_tirer prend l'image du genre de l'action), 0 u16, décalage des données depuis le début de la section
 *        u32) ; données : n couleurs r, g, b (index 1 à n), puis largeur × hauteur index (0 : noir)
 *  IMGX  paquet léger de l'ESP32 (engine/tools/paquet_sd.js) : la table de IMAG seule, décalages absolus dans monde.pak
 *        (pixels lus sur la carte SD)
 *  CUTI  cut-ins des techniques (facultative ; battle.c cutin_tirer, hud.c draw_cutin) : nombre u16, 0 u16 ; 8 octets par
 *        jeu : personnage u8 (index PERS, 255 : Sera), tenue u8 (index TENU, 255 : la sienne ; sans jeu pour la tenue
 *        portée, celui de la tenue de base), images u8, drapeaux u8 (1 plein écran : coup de grâce, sort de niveau III ;
 *        2 paires face / dos : images par deux, la face puis le dos ; 4 et 8 : dégâts de l'héroïne touchée, sous la moitié,
 *        puis le quart de ses PV ; 16 transformation, 32 attaque ultime (plein écran) ; 64 jeu 18+, 128 version sage
 *        d'un jeu 18+, montrée seulement verrouillé, voir EDIT ; 0 : bandeaux),
 *        première image u16 (IMAG), 0 u16
 *  OUVI  image de chaque carton de OUVR (facultative) : nombre u16, puis un u16 par carton (index IMAG, 65535 : aucune)
 *  SCNI  illustration de chaque scène du scénario (facultative ; instruction « image », OP 39 : rang de la scène) :
 *        nombre u16, puis un u16 par scène (index IMAG, 65535 : aucune ; bit 15 : scène 18+, voir EDIT) ; puis (depuis
 *        le 03/11/2026, absent des paquets d'avant) les variantes par héroïne choisie : nombre u16, puis 6 octets chacune :
 *        scène u16, héroïne u8 (index PERS), 0 u8, image u16 (index IMAG) ; avec cette héroïne, la scène montre la
 *        variante (clé « NOM@HÉROÏNE » de l'Atelier ; world.c scene_image), 18+ comme sa scène
 *  PORT  bâtiments qu'on visite (facultative ; ceux qui ont un intérieur) : nombres de bâtiments et de portes u32 ×2,
 *        puis 16 octets par bâtiment : centre x, z f32, accès u8 (0 libre, 1 horaires : ouvert le jour, 2 fermé, 3 scellé ;
 *        bit 7 : éclairé même fermé, bit 6 : toujours éteint, le cinéma),
 *        serrure u8 (0 mécanique : crocheter, 1 électronique : pirater), niveau u8, risque d'alarme en % u8, autorité u8
 *        (0 aucune, 1 l'Ordre, 2 le clan : la milice), commerce u8 (rang dans COMM + 1, 0 : aucun), intérieur u16 (entrée
 *        de INTR) ; puis 20 octets par porte : point devant elle x, z f32, normale vers la rue x, z f32, bâtiment u16, porte
 *        u8 (index dans son plan, 0 : la principale), largeur de l'ouverture u8 (1/10 de case ; 0 : 1,2) ; puis le nom de
 *        chaque bâtiment, 24 octets UTF-8 complétés de 0 (celui de son type : au-dessus de la bulle de la porte)
 *  TELE  téléport (facultative ; menu SYSTÈME › TÉLÉPORTER, hud.c) : nombre u32, puis 36 octets par lieu : nom UTF-8 sur
 *        24 octets complétés de 0, sorte u8 (0 monde : départ d'une carte, 1 ville, 2 lieu : circuit, astroport, puits,
 *        lieux nommés des autres cartes, sous le nom du guidage : Cartes.NOMS_LIEUX de l'Atelier, sans celui de la carte
 *        depuis le 01/11/2026), carte u8, 0 u16, point x, z f32 (case praticable de sa carte, posée par l'émulateur)
 *  PCAR  carte de chaque bâtiment de PORT (facultative ; absente : tous sur la planète) : nombre u32 (celui de PORT), puis
 *        un u8 chacun (0 la planète, k : la k-ième de CART) ; ses portes ne s'ouvrent que sur cette carte. PORT range ceux
 *        de la planète d'abord, puis ceux de chaque autre carte (base des Veilleurs, archives)
 *  COMM  commerces des intérieurs (facultative ; atelier : js/commerces.js) : nombre u32, puis chacun : métiers qui le
 *        tiennent u16 (bit k : métier k de Personnages.METIERS : commerçant, barman, médecin, mécanicien, prêtre, garde,
 *        employé, client, habitant, danseuse, commissaire), prix des soins de l'équipe u16, d'un verre u16 (crédits, 0 : aucun), types
 *        d'objets rachetés à moitié prix u8 (bit k : type k), objets vendus n u8, rangs dans la table des objets u8 × n
 *        (l'équipement n'y est pas : EQPL)
 *  EQPL  paliers de l'équipement (facultative ; atelier : js/paliers.js ; absente : ni équipement en boutique, ni butin
 *        d'équipement) : version u8 (1), nombre de paliers np u8 (15 au plus), marge des boutiques u8, 0 u8, niveau de
 *        chaque palier u8 × np (complété de 0 jusqu'à 4 octets) ; nombre d'objets u32, puis pour chaque objet du sac
 *        (ordre de la table de SCEN) : palier u8 (1 à np, 0 : aucun), drapeaux u8 (1 : vendu en boutique, 2 : lâché par les
 *        ennemis de son palier, 4 : lâché par les seuls élites, ennemis rares et boss de son palier, et trouvé dans les coffres :
 *        cristaux du Chœur, 03/11/2026 ; aucun : récompense de quête), complété de 0 jusqu'à 4 octets ; nombre de
 *        commerces u32 (ordre de COMM), puis types d'équipement vendus u32 chacun (bit k : type k de JOBS) ; nombre de
 *        bâtiments u32 (ordre de PORT), puis leur gamme : premier palier u8, dernier u8 (0, 0 : aucun équipement ; village
 *        1 à 4, quartiers de Lucerne 2 à 10, astroport 9 à 14, comptoir de la ceinture 10 à 14). Boutique : l'équipement
 *        des types de son commerce, des paliers de la gamme de son bâtiment dont le niveau ne dépasse pas max(équipe,
 *        histoire) + la marge (les EQ_RECENTS derniers), du plus haut au plus bas (battle.c : boutique_liste). Butin
 *        (battle.c : drop_equip) : DROP_BOSS
 *        (combat de l'histoire), DROP_RARE (sorts, ou poids de rencontre ≤ RARE_POIDS) ou DROP_BASE, un objet au hasard
 *        parmi ceux qui se lâchent du palier du niveau du combat. Depuis le 01/11/2026, tout bâtiment qu'on visite a une
 *        gamme (celle du lieu) : le palier du butin de son coffre (coffres.c)
 *  PANQ  panneaux de quêtes (facultative, depuis le 03/11/2026 ; atelier : js/panneaux.js ; avant CDAT, aussi dans le
 *        paquet léger de l'ESP32 ; panneaux.c) : nombre u32 (48 au plus, PANQ_MAX), puis 36 octets chacun : x, z f32 (sur sa
 *        carte), carte u8, drapeaux u8 (bit 0 : planche le long de z), 0 u16, nom UTF-8 sur 24 octets complétés de 0. Le
 *        panneau visible est fait de blocs du décor de son chunk (CDAT, CRTD : deux poteaux, la planche, son chapeau)
 *  TRES  trésors posés dehors (facultative ; atelier : js/tresors.js ; avant CDAT, aussi dans le paquet léger de l'ESP32) :
 *        nombre u32 (64 au plus), puis 52 octets chacun : x, z f32 (sur sa carte), carte u8, nombre d'objets u8, drapeau du
 *        scénario levé à l'ouverture u16 (65535 : aucun), crédits u32, nom UTF-8 sur 24 octets complétés de 0, objets (rang
 *        dans la table de SCEN u8, nombre u8) × 6 (255 : aucun). Leur coffre visible est fait de blocs du décor de leur
 *        chunk (CDAT, CRTD) ; fouillés une fois (sauvegarde ESVC, coffres.c)
 *  SURS  zones sûres (facultative, 07/11/2026) : nombre u32, puis 16 octets chacune : carte u8, 0 u8 × 3, centre x, z f32, rayon f32 (cases). Les rencontres n'y naissent pas et les ennemis n'y
 *        entrent ni n'y chassent ; l'intérieur des enceintes de ENCE en est aussi (battle.c, zone_sure)
 *  ENCE  enceintes des villes (facultative, depuis le 01/11/2026 : Lucerne ; émulateur : towns[].anneau et portes de
 *        js/settlements.js ; aussi dans le paquet léger de l'ESP32 ; absente : aucune interception, police.c) : nombre u32,
 *        puis chacune : centre x, z f32 (town + 0,5), drapeau du passage u16 (passage_halo ; 65535 : aucun, ville ouverte),
 *        secteurs A u16, portes np u16, 0 u16, nom UTF-8 sur 24 octets complétés de 0 ; rayon du mur de chaque secteur
 *        f32 × A (secteur k : angle k / A × 2π de +x vers +z, interpolé entre deux secteurs, Settlements.anneauR) ; portes
 *        (milieu x, z, direction de l'extérieur ox, oz f32) × np. Sans le drapeau, Sera à plus d'une case en deçà du mur
 *        est reconduite devant la porte la plus proche (police.c : enceinte_interdite, enceinte_maj)
 *  REPL  répliques génériques (facultative ; atelier : js/repliques.js) : nombre de listes u32 (une par métier, dans
 *        l'ordre de COMM, puis ville, village, campement), puis chacune : lignes u16, lignes (longueur u8, UTF-8) ; un
 *        personnage sans répliques en dit une au hasard : celle de son métier dans un intérieur, sinon celle de la ville
 *        (paquets d'avant le 03/11/2026 ; ceux d'après ont REPZ et REPT, qui l'emportent)
 *  REPZ  répliques par zone, index (facultative, 03/11/2026 ; atelier : js/repliques.js, TilesetGen.Repliques ; écrite par
 *        replPaquet de l'émulateur ; avant CDAT : aussi dans le paquet léger de l'ESP32, projeté depuis la flash) :
 *        'RPZ1', nombre de lots nl u16, nombre de phases np u8, lot par défaut u8 (dehors sans grille, lot 255 d'une
 *        carte ou d'un bloc), côté d'un bloc de la grille en cases u16, blocs par côté n u16 (0 : pas de grille), nombre
 *        de métiers nm u8, nombre de cartes nc u8, taille du plus gros lot en octets u16 (le tampon du moteur) ; drapeau
 *        du scénario de chaque phase u16 × np (65535 : absent ; phase de l'histoire = 1 + rang du plus haut drapeau levé,
 *        0 sans aucun : prologue), complété à 4 octets ; lot de chaque métier (ordre de COMM) u8 × nm puis de chaque
 *        carte secondaire (ordre de CART) u8 × nc (255 : le lot par défaut), complétés à 4 octets ; répertoire, 32
 *        octets par lot : décalage dans REPT après son en-tête u32, taille u32, nombre de répliques u16 (255 au plus),
 *        0 u16, nom UTF-8 sur 20 octets complétés de 0 ; grille de la planète n × n u8 (lot de chaque bloc, ligne par
 *        ligne ; 255 : le lot par défaut)
 *  REPT  textes des répliques par zone (facultative, avec REPZ ; après CDAT : sur la carte SD de la montre, lus lot par
 *        lot dans le tampon de repliques.c) : 'RPT1', puis les lots à la suite ; une réplique : longueur du texte u8,
 *        drapeaux u8 (bits 0 à 2 : phase mini, 3 à 5 : phase maxi, 6 : 18+, sautée tant que le 18+ est verrouillé),
 *        texte UTF-8 sans 0 final
 *  REPX  (paquet léger de l'ESP32, tools/paquet_sd.js) : décalage de REPT dans monde.pak u32, sa taille u32
 *  INTR  intérieurs (facultative, après CDAT : lus à l'entrée ; aussi écrits dans interieurs.bin pour la carte SD, qui
 *        l'emporte, par engine/tools/maj_monde.js) : 'INT2', nombre u32, plus grandes tailles brute et compressée u32,
 *        puis par intérieur (un par niveau : ordre des bâtiments de PORT, chacun son rez-de-chaussée puis ses étages, à
 *        la suite ; PORT donne le rang du rez-de-chaussée) : décalage depuis le début de la section u32, taille compressée
 *        u32, taille brute u32 ; puis les intérieurs compressés en LZ4 (format bloc). Un intérieur, coordonnées de son plan
 *        (le moteur les décale de IO) : largeur, profondeur, pas de la grille, hauteur, échelle humaine f32 ×5 ; côtés de
 *        la grille gw, gh, polygones, sols (les premiers polygones), objets, nœuds, sorties, personnages, étage (0 : rez-de-
 *        chaussée, sans sortie au-dessus), escaliers u16 ×10 ('INT1' : sans les deux derniers) ; polygones
 *        (comme ceux des chunks, normale toujours présente ; drapeau 4 : s'estompe devant Sera ; drapeau 1 et tuile ≥
 *        TILE_BIG : carte transparente d'un article, objet d'étalage ou affiche ; drapeau 8 : surface de l'eau d'un bassin,
 *        tuile T_SURF, sorte d'eau (EAUC) dans les 4 bits du haut, l de chaque sommet : opacité du corps × 16) ; objets (16 octets :
 *        rectangle au sol x0, z0, x1, z1 i16 1/256, premier polygone u16, nombre u16, 1 meuble low poly (triangles triés)
 *        u16, pan de mur qui le porte u16 : son rang + 1, 0 aucun ; depuis le 03/11/2026, une affiche au mur prend le fondu
 *        de ce mur, render.c int_draw_colle ; 0 avant) ; nœuds de l'arbre des pièces, le premier à la racine (12 octets :
 *        sorte u8 (0 feuille, 1 coupe en x, 2
 *        en z), 0 u8, coupe i16 1/256, nœuds lo, hi u16, premier objet u16, nombre u16 : ceux de la feuille, ou ceux posés
 *        sur la coupe) ; sorties (12 octets : x0, z0, x1, z1 i16 1/256, côté u8 (0 z0, 1 x1, 2 z1, 3 x0), porte u8, 0 u16) ;
 *        personnages à métier (8 octets : place x, z i16 1/256, personnage u8 (PERS), regard u8 (1/256 de tour), drapeaux u8
 *        (1 : qui danse, son animation de danse : danseuse, client sur la piste d'un club ; 2 : nageur, dans un bassin, sa
 *        tenue de nage ; 4 : scène de pole dance ; 8 : serveuse, va du comptoir aux clients, interieur.c int_service ;
 *        bit 7 : neuvième bit du personnage), métier + 1 u8 (0 : inconnu ou voiture garée, nageur ; listes de COMM et REPL) ;
 *        INT_PNJ_MAX au plus (20 depuis le 03/11/2026, 8 avant : la foule d'un club), des exemplaires de plus le temps de la
 *        visite) ; escaliers (16 octets : zone x0, z0, x1, z1 i16 1/256,
 *        vers i8 (+1 : on monte à l'intérieur suivant, -1 : on descend au précédent), regard à l'arrivée u8 (1/256 de
 *        tour), arrivée x, z i16 1/256 sur ce niveau-là, 0 u16 ; 4 au plus) ;
 *        grille de marche gw × gh u8 (pas res, décalée de 2 cases : 0 mur ou meuble, 1 libre, 2 seuil d'une sortie, 3 zone
 *        d'un escalier qui monte, 4 trémie d'un escalier qui descend : y entrer change de niveau, 5 eau d'un bassin : à la
 *        nage, surface à INT_EAU_Y × échelle humaine) ; depuis le 01/11/2026 (un paquet plus ancien s'arrête à la grille) :
 *        cellules de la prison (nombre u16, puis x0, z0, x1, z1 i16 1/256 chacune : pièces cellule ou cellules, police.c),
 *        puis coffre à butin (nombre u16, 1 sur le niveau qui l'a, sinon 0 ; puis son point d'action x, z i16 1/256, sorte
 *        de cachette u8 (0 coffre, 1 caisse, 2 armoire), 0 u8 ; coffres.c, coffreDe de l'émulateur) ; depuis le 03/11/2026
 *        (un paquet plus ancien s'arrête au coffre : sol plat partout), sols surélevés (nombre u16, puis 16 octets chacun :
 *        x0, z0, x1, z1 i16 1/256, hauteurs y0, y1 i16 1/256 au début et à la fin de la montée, axe u8 (0 plat : y0 ; 1 monte
 *        vers +x, 2 vers +z, 3 vers -x, 4 vers -z), 0 u8, 0 u16 ; le dernier qui contient le point l'emporte ; escaliers
 *        praticables, gradins d'un cinéma, estrades des danseuses : les personnages s'y tiennent à cette hauteur,
 *        interieur.c int_sol_y, interieurSolY de l'émulateur)
 *  PSAG  couleurs des planches de remplacement (facultative ; variante de couleur d'un personnage 18+) : nombre u32, puis
 *        personnage (index PERS) u16, 0 u16, table de 256 couleurs
 *  NSUR  noms sûrs (facultative ; édition unique) : nombre u32, puis 28 octets chacun : sorte u8 (0 bâtiment de PORT,
 *        1 lieu de GUID), 0 u8, rang u16, nom UTF-8 sur 24 octets complétés de 0 (le club : « SALLE DES FÊTES »)
 *  EDIT  édition du monde (facultative : absente, 18+) : édition u8 (0 : 18+, 1 : enfant, js/edition.js de l'Atelier :
 *        ni tenue osée, ni club, ni danseuse ; 2 : unique, le contenu 18+ marqué et caché tant que le code CODE18
 *        n'a pas été donné, menu SYSTÈME › CODE : personnages PERS_ADULTE dans leur planche TENUE_SAGE (couleurs
 *        PSAG), masqués sans elle ; tenues, objets, événements, jeux de cut-ins, scènes, répliques 18+ cachés ; noms
 *        NSUR ; jeux de cut-ins 128 seulement verrouillé ; déverrouillage gardé dans l'emplacement 6 de la sauvegarde,
 *        world.c edition_appliquer), 0 u8 × 3 ; le moteur de l'édition enfant (ECHOES_ENFANT) ne lance que les paquets
 *        de l'édition enfant
 *  END   (vide)
 */
#ifndef PAK_H
#define PAK_H

#include <stdint.h>

#define PAK_MAGIC 0x4F484345u
#define PAK_VERSION 5            /* 5 : grands décors à 45000 (4 : à 30000, trop bas pour les textures de plusieurs cartes) */
#define TILE_BIG 45000           /* tuile ≥ TILE_BIG : grand décor TILE_BIG + k, décrit par l'entrée k de la table DECO ; textures du sol de G0 à TILE_BIG - 1 */

#define F_WATER 1
#define F_RIVER 2
#define F_ROAD 4
#define F_OPEN 8
#define F_STREET 16              /* chaussée d'une ville ou d'un village (plan urbain) : le trafic y roule */
#define F_CLEAR 32               /* eau claire (hors des terrains « eau noire ») : Sera y nage */
#define F_RAIDE 64               /* trop raide (falaise, flanc de crevasse : CarteHistoire.rugosite) : infranchissable */

typedef struct {
    float x, y, z, u, v, l;
} pak_vert_t;

/* Polygone tel qu'il est rangé dans le paquet (taille variable : nv sommets à la suite) */
typedef struct {
    uint16_t tile;
    uint8_t flags, nv;
    float n[3];
    pak_vert_t v[];
} pak_poly_t;

#define POLY_ALPHA 1
#define POLY_CULL 2
/* En mémoire : rang du volume + 1 d'une face de bâtiment accessoire (0 : aucun, triée par sa profondeur) dans les bits 2 à 7 */
#define POLY_RANK_SH 2
#define POLY_RANK_MAX 63
#define POLY_COLLE 62                 /* rang 62 : affiche collée au polygone précédent (son mur), dans le même seau, après lui */
#define POLY_RANK(pp) ((pp)->flags >> POLY_RANK_SH)

typedef struct {
    uint32_t cell;               /* case dans le chunk */
    float x, y, z, size, phase, l;
    uint16_t tile;
    uint8_t flip, flags;         /* flags : 2 = se balance, 4 = solide (un véhicule lancé le casse), 8 = rocher, 16 = orientation fixe dans le monde (rocher, objet : render.c, fixe_maj) */
} pak_plant_t;

typedef struct {
    float cx, cz, r;
    uint32_t first, count;
} pak_building_t;

#endif
