# Musiques du jeu

Préparées par `tools/audio/preparer_musique.py` d'après `pistes.json` (nom de chaque piste dans le jeu, boucle,
niveau) dans `engine/data/musique.bin`, à copier sur la carte SD (`/sd/echoes/musique.bin`).

## Histoire (`histoire/`)

Générées avec Gemini (Lyria) le 26/10/2026 d'après les prompts de la bande-son ; chaque MP3 garde son manifeste
C2PA (provenance IA). 44,1 kHz stéréo, 192 kb/s.

| Piste | Fichier | Durée |
|---|---|---|
| friches | The_Lonely_Scavenger.mp3 | 2 min 54 |
| halo | Beneath_the_Steel_Clouds.mp3 | 2 min 47 |
| vent | When_the_Wind_Finally_Stops.mp3 | 2 min 41 |

## Combat (`combat/`), Gemini

Générée avec Gemini (Lyria), ajoutée le 01/11/2026 ; le MP3 garde son manifeste C2PA (provenance IA). 44,1 kHz
stéréo. Le morceau a une fin : l'intro est jouée une fois, puis la boucle prise au milieu (de 0:38,5 à 2:34,6,
`debut_boucle` et `fin_boucle` de `pistes.json`, trouvés par `chercher_boucle` de `tools/audio/generer_musique_ia.py`).

| Piste | Fichier | Durée |
|---|---|---|
| combat | combat/Steel_Against_the_Horizon.mp3 | 2 min 57 |

## Courses (`courses/`), Gemini

Générées avec Gemini (Lyria), ajoutées le 01/11/2026 ; chaque MP3 garde son manifeste C2PA (provenance IA). 44,1 kHz
stéréo. Même découpe que le combat : l'intro est jouée une fois, puis la boucle prise au milieu. Les trois forment la
série de course : chaque course prend la suivante (`engine/core/music.c`).

| Piste | Fichier | Durée | Boucle |
|---|---|---|---|
| course | courses/Beyond_The_Gravity_Well.mp3 | 3 min 02 | 0:48,8 à 2:24,8 |
| course_2 | courses/Apex_Point.mp3 | 2 min 57 | 0:41,7 à 2:11,3 |
| course_3 | courses/Redline_Horizon.mp3 | 2 min 49 | 1:04,2 à 2:29,6 |

## Domaine public (CC0)

Usage libre, attribution non requise. Licence vérifiée sur chaque page le 26/10/2026.

| Piste | Fichier | Boucle | Source |
|---|---|---|---|
| boss | combat/epic_boss_battle_loop.wav | oui | [Boss Battle Music (Juhani Junkala, SubspaceAudio)](https://opengameart.org/content/boss-battle-music) |
| poursuite | combat/determined_pursuit_loop.wav | oui | [Determined Pursuit (Emma_MA)](https://opengameart.org/content/determined-pursuit-epic-orchestra-loop) |
| victoire | jingles/victory_fanfare_short.wav | non | [Victory Fanfare Short (cynicmusic)](https://opengameart.org/content/victory-fanfare-short) |
| defaite | jingles/sad_game_over.wav | non | [Sad game over (Emma_MA)](https://opengameart.org/content/sad-game-over) |
| defaite_courte | jingles/game_over_jingle.ogg | non | [Game over short jingle (Kosmo The Cat)](https://opengameart.org/content/game-over-short-jingle) |
| tristesse | ambiance/sadness.ogg | oui | [Sadness (Kistol)](https://opengameart.org/content/sadness) |
| donjon | ambiance/dungeon_ambient_1.ogg | oui | [Loopable Dungeon Ambience (JaggedStone)](https://opengameart.org/content/loopable-dungeon-ambience) |
| caverne | ambiance/dark_cavern_ambient_002.ogg (la version en boucle continue ; 001 : fondus) | oui | [Dark Cavern Ambient (Paul Wortmann)](https://opengameart.org/node/131394) |
| course_finale | courses/neon_sign_circuit_climax_bpm160.ogg | oui | [Neon sign Circuit (MintoDog)](https://opengameart.org/content/neon-sign-circuit) |
| course_nuit | courses/darkness_road_bpm165.ogg | oui | [Darkness Road (MintoDog)](https://opengameart.org/content/darkness-road) |
| course_nuit_finale | courses/darkness_road_climax_bpm180.ogg | oui | [Darkness Road (MintoDog)](https://opengameart.org/content/darkness-road) |
| course_choeur | courses/darkness_road_remake_bpm165.ogg | oui | [Darkness Road (MintoDog)](https://opengameart.org/content/darkness-road) |
| course_choeur_finale | courses/darkness_road_climax_remake_bpm180.ogg | oui | [Darkness Road (MintoDog)](https://opengameart.org/content/darkness-road) |
| course_ville | courses/S31-City_on_Speed.ogg | fondu | [City on Speed (section31)](https://opengameart.org/content/city-on-speed) |
| course_fuite | courses/S31-Hit_and_Run.ogg | fondu | [Hit and Run (section31)](https://opengameart.org/content/hit-and-run) |
| lune | mondes/airy_0.mp3 | fondu | [Dark Sci-Fi Audio Pack, « Airy » (SRG774)](https://opengameart.org/node/188336), CC0, vérifié le 29/10/2026 |
| mars | mondes/sector_0.mp3 | fondu | [Dark Sci-Fi Audio Pack, « Sector » (SRG774)](https://opengameart.org/node/188336), CC0, vérifié le 29/10/2026 |
| luxuriante | mondes/outer_space_2.mp3 | fondu | [Outer Space Loop (wipics)](https://opengameart.org/content/outer-space-loop), CC0, vérifié le 29/10/2026 |

Plus dans le jeu depuis le 01/11/2026, gardées dans `combat/` : `jrpg_battle_full.mp3`, l'ancienne piste de combat
([JRPG Epic Rock Battle Theme #1 (HydroGene)](https://opengameart.org/content/jrpg-epic-rock-battle-theme-1), CC0,
intro 1,62 s puis boucle), et `jrpg_battle_intro.mp3`, son introduction seule (elle servait à mesurer le début de la
boucle).

Plus dans le jeu depuis le 01/11/2026, gardée dans `courses/` : `neon_sign_circuit_bpm145.ogg`, l'ancienne piste de
course ([Neon sign Circuit (MintoDog)](https://opengameart.org/content/neon-sign-circuit), CC0) ; sa version finale
(`course_finale`) reste dans le jeu.
