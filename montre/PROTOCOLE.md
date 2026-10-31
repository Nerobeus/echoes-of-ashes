# Protocole Bluetooth montre ↔ téléphone

La montre (app « montre » de l'ESP32-S3, NimBLE en rôle périphérique) diffuse sous le nom **Echoes Montre**. Le téléphone
(app Android « Echoes Montre », rôle central) se connecte, s'appaire (« just works », liaison gardée des deux côtés), demande
une MTU de 247, puis écrit et s'abonne aux caractéristiques ci-dessous.

Service principal : `e5c0a5e0-6d6f-6e74-7265-4563686f6573` (« montreEchoes »).

| Caractéristique | UUID | Sens | Contenu |
|---|---|---|---|
| HEURE | `e5c0a5e1-6d6f-6e74-7265-4563686f6573` | téléphone → montre (écriture) | 10 octets petit-boutistes : secondes UTC (int64), décalage du fuseau en minutes (int16, heure d'été comprise). Écrite à chaque connexion, puis toutes les heures et à chaque changement de fuseau. |
| NOTIF | `e5c0a5e2-6d6f-6e74-7265-4563686f6573` | téléphone → montre (écriture, écriture longue permise) | UTF-8, champs séparés par 0x1F : `id`, `app` (nom lisible de l'application), `titre`, `texte` (tronqué à 400 octets), `heure` (secondes UTC). Un `id` déjà reçu remplace l'ancienne. |
| EFFACER | `e5c0a5e3-6d6f-6e74-7265-4563686f6573` | téléphone → montre (écriture) | UTF-8 : `id` d'une notification retirée du téléphone, ou `*` pour tout effacer. |
| APPEL | `e5c0a5e4-6d6f-6e74-7265-4563686f6573` | téléphone → montre (écriture) | UTF-8, champs séparés par 0x1F : `etat` (`sonne`, `en_cours`, `fin`), `nom` (contact, ou vide), `numero`. |
| ACTION | `e5c0a5e5-6d6f-6e74-7265-4563686f6573` | montre → téléphone (notification) | UTF-8 : `appel:repondre`, `appel:refuser`, `notif:ouvrir:<id>`, `notif:effacer:<id>`, `trouver` (faire sonner le téléphone). |
| ETAT | `e5c0a5e6-6d6f-6e74-7265-4563686f6573` | montre → téléphone (lecture, notification) | UTF-8, champs séparés par 0x1F : `batterie` (%), `charge` (0/1), `version` du firmware. |

Service standard de batterie (0x180F, niveau 0x2A19) aussi exposé par la montre.

Règles :
- La montre garde les 20 dernières notifications en RAM (et sur la carte SD : `/sd/montre/notifs.txt`), les plus récentes d'abord.
- Un appel `sonne` affiche un écran plein (nom, numéro, boutons RÉPONDRE / REFUSER) et fait vibrer… (pas de vibreur sur la
  1,75" C : bip et écran allumé) ; `fin` le ferme.
- Le téléphone reconnecte tout seul dès que la montre est à portée ; la montre ne cherche jamais (économie d'énergie) : elle
  diffuse lentement (intervalle 1 s) en veille, plus vite (100 ms) les 30 s qui suivent un réveil.
