/*
 * Mise à jour depuis le serveur de l'Atelier (voir maj_android.h). Une adresse imposée dans les réglages
 * (serveur_impose.txt, Reglages.java ; nom d'hôte possible) est seule essayée. Sinon le serveur se cherche à des adresses connues,
 * essayées en même temps : la dernière qui a répondu (serveur.txt du dossier privé), celles du PC qui a construit l'APK
 * (asset serveur.txt, écrit par tools/build_android.js) et 10.0.2.2 (le PC vu d'un émulateur du SDK) ; sinon par une
 * diffusion UDP « ECHOES? » sur le réseau Wi-Fi, à laquelle le serveur répond « ECHOES <port> ».
 * Fichiers : HEAD donne leur date au serveur (en-tête X-Mtime, en ms) ; plus récents que la copie de l'APK (asset
 * <nom>.date) et que la dernière téléchargée (<nom>.date du dossier privé), ils sont téléchargés en <nom>.part puis
 * renommés. Une copie téléchargée devenue plus vieille que celle de l'APK (APK réinstallé) est effacée.
 */
#include "maj_android.h"
#include "../../core/eng.h"
#include <android/log.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "echoes", __VA_ARGS__)
#define PORT_DEFAUT 8080                 /* port de node serveur.js */
#define ATTENTE_MS 600                   /* connexion aux adresses candidates (réseau local) */

volatile int maj_phase;
volatile float maj_part;

static const char *DIR;
static AAssetManager *AM;
static int64_t total, recu;              /* octets à télécharger, reçus */

/* asset : nom dans l'APK (et pour maj_fichier) ; nom : copie téléchargée dans le dossier privé. Édition enfant choisie
   dans les réglages (edition.txt, Reglages.java) d'une app 18+ : monde_enfant.pak du serveur, gardé à part, et jamais
   le paquet 18+ de l'APK */
static struct fichier {
    const char *asset, *nom, *url;
    int64_t d_apk, d_dl;                 /* dates (ms) de la copie de l'APK et de la téléchargée ; -1 : pas de copie */
    int64_t d_srv, taille;               /* au serveur ; -1 : absent */
    int prendre;                         /* 1 : la copie téléchargée, 0 : celle de l'APK, -1 : aucune */
} F[] = {
    { "monde.pak", "monde.pak", ECHOES_ENFANT ? "/api/sorties/monde_enfant.pak" : "/api/sorties/monde.pak" },
    { "musique.bin", "musique.bin", "/api/moteur/musique.bin" },
};
enum { NF = sizeof F / sizeof F[0] };

static void chemin(char *p, size_t n, const char *nom, const char *ext) { snprintf(p, n, "%s/%s%s", DIR, nom, ext); }

/* Texte d'un fichier du dossier privé ou d'un asset (à libérer), NULL s'il n'y en a pas */
static char *texte_fichier(const char *p) {
    FILE *f = fopen(p, "r");
    if (!f) return NULL;
    char *t = calloc(1, 4096);
    if (t) fread(t, 1, 4095, f);
    fclose(f);
    return t;
}
static char *texte_asset(const char *nom) {
    AAsset *a = AAssetManager_open(AM, nom, AASSET_MODE_BUFFER);
    if (!a) return NULL;
    char *t = calloc(1, 4096);
    if (t) AAsset_read(a, t, 4095);
    AAsset_close(a);
    return t;
}
static int64_t date_de(char *t) {
    const int64_t d = t ? atoll(t) : -1;
    free(t);
    return d;
}
static void ecrire_date(const char *p, int64_t d) {
    FILE *f = fopen(p, "w");
    if (f) { fprintf(f, "%lld\n", (long long)d); fclose(f); }
}

/* Connexion TCP avec délai ; le socket rendu attend au plus 10 s chaque envoi ou réception */
static int connecter(const struct sockaddr_in *sa, int ms) {
    int s = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s < 0) return -1;
    const int fl = fcntl(s, F_GETFL);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
    int r = connect(s, (const struct sockaddr *)sa, sizeof *sa);
    if (r && errno == EINPROGRESS) {
        struct pollfd p = { s, POLLOUT, 0 };
        int err = -1;
        socklen_t el = sizeof err;
        if (poll(&p, 1, ms) == 1 && !getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &el) && !err) r = 0;
    }
    if (r) { close(s); return -1; }
    fcntl(s, F_SETFL, fl);
    struct timeval tv = { 10, 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    return s;
}

/* Requête HTTP/1.1 : rend le socket placé après les en-têtes (les premiers octets du corps, déjà lus, dans corps : au
   moins 2 048 octets), ou -1. Statut, Content-Length et X-Mtime dans *e (-1 s'ils manquent). debut > 0 : la suite du
   fichier à partir de cet octet (Range ; réponse 206). */
typedef struct { int statut; int64_t taille, mtime; } entete_t;
static int requete(const struct sockaddr_in *sa, const char *meth, const char *url, int64_t debut, entete_t *e, char *corps, int *nc) {
    const int s = connecter(sa, 3000);   /* adresse déjà joignable (essayer_toutes) ou serveur trouvé */
    if (s < 0) return -1;
    char b[2048], ip[INET_ADDRSTRLEN], range[48] = "";
    inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof ip);
    if (debut > 0) snprintf(range, sizeof range, "Range: bytes=%lld-\r\n", (long long)debut);
    int n = snprintf(b, sizeof b, "%s %s HTTP/1.1\r\nHost: %s:%d\r\n%sConnection: close\r\n\r\n", meth, url, ip, ntohs(sa->sin_port), range);
    char *fin = NULL;
    if (send(s, b, n, MSG_NOSIGNAL) != n) goto ko;
    for (n = 0; !fin;) {
        if (n >= (int)sizeof b - 1) goto ko;
        const int r = (int)recv(s, b + n, sizeof b - 1 - n, 0);
        if (r <= 0) goto ko;
        n += r; b[n] = 0;
        fin = strstr(b, "\r\n\r\n");
    }
    *fin = 0;
    e->statut = 0; e->taille = e->mtime = -1;
    if (sscanf(b, "HTTP/1.%*d %d", &e->statut) != 1) goto ko;
    for (const char *l = strstr(b, "\r\n"); l; l = strstr(l + 2, "\r\n")) {
        if (!strncasecmp(l + 2, "content-length:", 15)) e->taille = atoll(l + 17);
        else if (!strncasecmp(l + 2, "x-mtime:", 8)) e->mtime = atoll(l + 10);
    }
    if (corps) { *nc = n - (int)(fin + 4 - b); memcpy(corps, fin + 4, *nc); }
    return s;
ko:
    close(s);
    return -1;
}

/* Le serveur de l'Atelier répond-il à cette adresse ? (liste des projets : elle existe même sans monde exporté) */
static int essayer(const struct sockaddr_in *sa) {
    static char b[2048];
    entete_t e;
    int nc = 0;
    const int s = requete(sa, "GET", "/api/projets", 0, &e, b, &nc);
    if (s < 0) return 0;
    if (nc <= 0) nc = (int)recv(s, b, sizeof b, 0);   /* corps pas encore arrivé avec les en-têtes */
    close(s);
    /* Liste JSON, envoyée en morceaux (Transfer-Encoding: chunked : la taille du morceau d'abord) */
    return e.statut == 200 && nc > 0 && memchr(b, '[', (size_t)nc) != NULL;
}

/* Adresses candidates « a.b.c.d[:port] », une par ligne, ajoutées à c (n au plus) */
enum { NC = 16 };
static int ajouter(char *t, struct sockaddr_in *c, int k) {
    for (const char *l = t; l && *l && k < NC; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
        char h[128];
        int port = PORT_DEFAUT;
        if (sscanf(l, " %127[^:/ \t\r\n]:%d", h, &port) < 1) continue;
        memset(&c[k], 0, sizeof c[k]);
        c[k].sin_family = AF_INET;
        c[k].sin_port = htons((uint16_t)port);
        if (inet_pton(AF_INET, h, &c[k].sin_addr) == 1) { k++; continue; }
        /* Nom d'hôte (adresse imposée dans les réglages) */
        struct addrinfo q = { 0 }, *r = NULL;
        q.ai_family = AF_INET;
        q.ai_socktype = SOCK_STREAM;
        if (!getaddrinfo(h, NULL, &q, &r) && r) { c[k].sin_addr = ((struct sockaddr_in *)r->ai_addr)->sin_addr; k++; }
        if (r) freeaddrinfo(r);
    }
    free(t);
    return k;
}

/* Connexions à toutes les candidates en même temps (le PC a souvent plusieurs adresses, dont certaines injoignables
   d'ici : une à une, chacune coûterait le délai) ; la première qui répond comme le serveur de l'Atelier l'emporte */
static int essayer_toutes(const struct sockaddr_in *c, int n, int ms, struct sockaddr_in *sa) {
    struct pollfd p[NC];
    int ok = 0;
    for (int i = 0; i < n; i++) {
        p[i].fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        p[i].events = POLLOUT;
        if (p[i].fd >= 0 && connect(p[i].fd, (const struct sockaddr *)&c[i], sizeof c[i]) && errno != EINPROGRESS) { close(p[i].fd); p[i].fd = -1; }
    }
    for (int t = 0; t < ms && !ok; t += 50) {
        if (poll(p, n, 50) <= 0) continue;
        for (int i = 0; i < n && !ok; i++) {
            if (p[i].fd < 0 || !p[i].revents) continue;
            int err = -1;
            socklen_t el = sizeof err;
            getsockopt(p[i].fd, SOL_SOCKET, SO_ERROR, &err, &el);
            close(p[i].fd); p[i].fd = -1;
            if (!err && essayer(&c[i])) { *sa = c[i]; ok = 1; }
        }
    }
    for (int i = 0; i < n; i++) if (p[i].fd >= 0) close(p[i].fd);
    return ok;
}

static int diffusion(struct sockaddr_in *sa) {
    const int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    int oui = 1, ok = 0;
    if (s < 0) return 0;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &oui, sizeof oui);
    struct sockaddr_in to = { 0 };
    to.sin_family = AF_INET;
    to.sin_port = htons(PORT_DEFAUT);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    for (int k = 0; k < 3 && !ok; k++) {
        sendto(s, "ECHOES?", 7, 0, (const struct sockaddr *)&to, sizeof to);
        struct pollfd p = { s, POLLIN, 0 };
        while (!ok && poll(&p, 1, 400) == 1) {
            char b[32];
            socklen_t l = sizeof *sa;
            const int n = (int)recvfrom(s, b, sizeof b - 1, 0, (struct sockaddr *)sa, &l);
            if (n <= 7 || memcmp(b, "ECHOES ", 7)) continue;
            b[n] = 0;
            sa->sin_port = htons((uint16_t)atoi(b + 7));
            ok = essayer(sa);
        }
    }
    close(s);
    return ok;
}

/* Adresse imposée dans les réglages (serveur_impose.txt, écrit par Reglages.java) : elle seule, avec plus de temps
   (serveur ailleurs que sur le Wi-Fi) ; sinon la recherche automatique */
static int trouver(struct sockaddr_in *sa) {
    char p[512];
    struct sockaddr_in c[NC];
    chemin(p, sizeof p, "serveur_impose.txt", "");
    int n = ajouter(texte_fichier(p), c, 0);
    const int impose = n > 0;
    chemin(p, sizeof p, "serveur.txt", "");
    if (!impose) {
        n = ajouter(texte_fichier(p), c, 0);
        n = ajouter(texte_asset("serveur.txt"), c, n);
        n = ajouter(strdup("10.0.2.2"), c, n);
    }
    if (!essayer_toutes(c, n, impose ? 4000 : ATTENTE_MS, sa) && (impose || !diffusion(sa))) return 0;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof ip);
    FILE *f = fopen(p, "w");
    if (f) { fprintf(f, "%s:%d\n", ip, ntohs(sa->sin_port)); fclose(f); }
    LOGI("serveur de l'Atelier : %s:%d", ip, ntohs(sa->sin_port));
    return 1;
}

static int telecharger(const struct sockaddr_in *sa, struct fichier *f) {
    static char b[65536];
    char part[512], fin[512], date[512];
    chemin(part, sizeof part, f->nom, ".part");
    chemin(fin, sizeof fin, f->nom, "");
    chemin(date, sizeof date, f->nom, ".date");
    const int fd = open(part, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return 0;
    /* Connexion coupée (Wi-Fi qui décroche) : la suite reprise là où elle s'est arrêtée (Range), 8 essais ; le fichier
       ne doit pas avoir changé au serveur entre-temps (même X-Mtime) */
    int64_t n = 0, taille = -1, mtime = -1;
    int ok = 0, essais = 0;
    while (!ok && essais++ < 8) {
        if (essais > 1) { LOGI("%s : reprise à %lld Ko (essai %d)", f->nom, (long long)(n / 1024), essais); sleep(1); }
        entete_t e;
        int nc = 0;
        const int s = requete(sa, "GET", f->url, n, &e, b, &nc);
        if (s < 0) continue;
        /* Serveur sans reprise (200 à une demande de suite) : tout recommence */
        if (n > 0 && e.statut == 200 && e.mtime == mtime && e.taille == taille && !ftruncate(fd, 0) && lseek(fd, 0, SEEK_SET) == 0) { recu -= n; n = 0; }
        const int bon = n == 0 ? e.statut == 200 && e.taille >= 0 : e.statut == 206 && e.mtime == mtime && n + e.taille == taille;
        if (!bon) { close(s); if (n > 0) break; continue; }   /* changé au serveur : abandon */
        if (n == 0) { taille = e.taille; mtime = e.mtime; }
        for (int r = nc; n < taille; r = 0) {
            if (r <= 0 && (r = (int)recv(s, b, sizeof b, 0)) <= 0) break;
            if (r > taille - n) r = (int)(taille - n);
            if (write(fd, b, r) != r) { essais = 99; break; }   /* disque plein */
            n += r; recu += r;
            maj_part = total ? (float)recu / total : 1;
        }
        close(s);
        ok = n == taille;
    }
    ok = ok && !fsync(fd);
    close(fd);
    /* La date d'abord effacée : un arrêt entre le renommage et la date laisse une copie sans date, ignorée */
    if (ok) { unlink(date); ok = !rename(part, fin); }
    if (ok) { ecrire_date(date, mtime); f->d_dl = mtime; }
    else unlink(part);
    LOGI("%s : %s (%lld Ko sur %lld)", f->nom, ok ? "téléchargé" : "téléchargement interrompu", (long long)(n / 1024), (long long)(taille / 1024));
    return ok;
}

static void *fil(void *arg) {
    char p[512];
    chemin(p, sizeof p, "edition.txt", "");
    char *ed = texte_fichier(p);
    const int enfant = !ECHOES_ENFANT && ed && !strncmp(ed, "enfant", 6);
    free(ed);
    if (enfant) { F[0].nom = "monde_enfant.pak"; F[0].url = "/api/sorties/monde_enfant.pak"; LOGI("édition enfant (réglages)"); }
    for (int i = 0; i < NF; i++) {
        struct fichier *f = &F[i];
        AAsset *a = enfant && i == 0 ? NULL : AAssetManager_open(AM, f->asset, AASSET_MODE_STREAMING);
        if (a) {
            AAsset_close(a);
            snprintf(p, sizeof p, "%s.date", f->asset);
            f->d_apk = date_de(texte_asset(p));
            if (f->d_apk < 0) f->d_apk = 0;
        } else f->d_apk = -1;
        chemin(p, sizeof p, f->nom, "");
        f->d_dl = access(p, R_OK) ? -1 : (chemin(p, sizeof p, f->nom, ".date"), date_de(texte_fichier(p)));
    }
    struct sockaddr_in sa;
    if (trouver(&sa)) {
        int a_prendre[NF] = { 0 };
        for (int i = 0; i < NF; i++) {
            struct fichier *f = &F[i];
            entete_t e;
            const int s = requete(&sa, "HEAD", f->url, 0, &e, NULL, NULL);
            if (s >= 0) close(s);
            f->d_srv = s >= 0 && e.statut == 200 ? e.mtime : -1;
            f->taille = e.taille;
            const int64_t local = f->d_apk > f->d_dl ? f->d_apk : f->d_dl;
            if (f->d_srv > local && f->taille > 0) { a_prendre[i] = 1; total += f->taille; }
            LOGI("%s : serveur %lld, APK %lld, téléchargé %lld%s", f->nom, (long long)f->d_srv, (long long)f->d_apk, (long long)f->d_dl, a_prendre[i] ? " -> à télécharger" : "");
        }
        if (total) maj_phase = MAJ_TELECHARGE;
        for (int i = 0; i < NF; i++) if (a_prendre[i]) telecharger(&sa, &F[i]);
    } else LOGI("serveur de l'Atelier introuvable : copie la plus récente");
    for (int i = 0; i < NF; i++) {
        struct fichier *f = &F[i];
        f->prendre = f->d_dl >= 0 && f->d_dl >= f->d_apk ? 1 : f->d_apk >= 0 ? 0 : -1;
        if (f->d_dl >= 0 && f->prendre != 1) {
            chemin(p, sizeof p, f->nom, ""); unlink(p);
            chemin(p, sizeof p, f->nom, ".date"); unlink(p);
            LOGI("%s : copie téléchargée plus vieille que celle de l'APK, effacée", f->nom);
        }
    }
    maj_phase = MAJ_FINI;
    return NULL;
}

void maj_lancer(const char *dir, AAssetManager *am) {
    DIR = dir; AM = am;
#if ECHOES_AUTONOME
    maj_phase = MAJ_FINI; return;   /* APK autonome : ni serveur ni copie téléchargée */
#endif
    maj_phase = MAJ_RECHERCHE;
    pthread_t th;
    if (pthread_create(&th, NULL, fil, NULL)) { maj_phase = MAJ_FINI; return; }
    pthread_detach(th);
}

int maj_apk(const char *name) {
#if ECHOES_AUTONOME
    return 1;   /* APK autonome : toujours celui de l'APK */
#endif
    for (int i = 0; i < NF; i++) if (!strcmp(F[i].asset, name)) return F[i].d_apk >= 0;
    return 0;
}

int maj_fichier(const char *name, char *path, size_t n) {
#if ECHOES_AUTONOME
    return 0;
#endif
    for (int i = 0; i < NF; i++)
        if (!strcmp(F[i].asset, name)) {
            chemin(path, n, F[i].nom, "");
            return F[i].prendre;
        }
    return -1;
}
