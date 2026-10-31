# Polices de l'interface du jeu (export du paquet côté serveur)

Téléchargées depuis Google Fonts (fonts.gstatic.com) le 27/10/2026, licence SIL Open Font License 1.1 :

- `Oxanium-400.ttf`, `Oxanium-600.ttf` : Oxanium, Severin Meyer (https://fonts.google.com/specimen/Oxanium)
- `ShareTechMono-400.ttf` : Share Tech Mono, Carrois Apostrophe (https://fonts.google.com/specimen/Share+Tech+Mono)

Ce sont les polices que la page de l'émulateur charge depuis Google Fonts ; `serveur/navigateur.js` les déclare au
canvas natif pour que le serveur dessine les mêmes glyphes (sections FONT et ICON du paquet).
