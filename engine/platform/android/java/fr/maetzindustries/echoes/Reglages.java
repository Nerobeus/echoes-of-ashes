// Réglages de l'app (icône « Echoes réglages », processus :reglages) : adresse du serveur de l'Atelier (vide : recherche
// automatique, comme maj_android.c), régénération du monde par le serveur (tâche « chaine », comme le panneau
// « Serveur : monde → moteur » de l'onglet Jouer), puis relance du jeu, qui prend le nouveau paquet au démarrage.
// Vues Android de base, sans bibliothèque : écran rond de la montre comme écran de téléphone.
package fr.maetzindustries.echoes;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.ComponentName;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.HttpURLConnection;
import java.net.InetAddress;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public class Reglages extends Activity {
    static final String JEU = "android.app.NativeActivity", JEU_NATIF = "fr.maetzindustries.echoes.Natif";
    private final Handler ui = new Handler(Looper.getMainLooper());
    private EditText adresse;
    private TextView etat, suivi;
    private CheckBox continent, batiments, tuiles, moteur;
    private Button regenerer;
    private volatile String serveur;           // « hôte:port » du serveur trouvé
    private volatile boolean ouvert = true;
    private volatile long tacheSuivie = -1;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        final int w = getResources().getDisplayMetrics().widthPixels, marge = w / 7;
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setPadding(marge, marge, marge, marge * 2);
        texte(l, "Echoes — réglages", 17).setGravity(Gravity.CENTER);
        texte(l, "\nServeur de l'Atelier", 14);
        adresse = new EditText(this);
        adresse.setHint("automatique (Wi-Fi)");
        adresse.setSingleLine(true);
        adresse.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        adresse.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        adresse.setText(lire(fichier("serveur_impose.txt")).trim());
        l.addView(adresse);
        bouton(l, "Enregistrer et chercher", () -> {
            final String a = adresse.getText().toString().trim();
            if (a.isEmpty()) fichier("serveur_impose.txt").delete();
            else ecrire(fichier("serveur_impose.txt"), a + "\n");
            chercher();
        });
        etat = texte(l, "", 13);
        // Édition du monde (pas dans l'app de l'édition enfant, qui n'a que la sienne) : edition.txt, lu par maj_android.c
        if (!appEnfant()) {
            texte(l, "\nÉdition du monde", 14);
            RadioGroup g = new RadioGroup(this);
            RadioButton a18 = new RadioButton(this), ae = new RadioButton(this);
            a18.setText("18+"); a18.setId(1); ae.setText("Enfant"); ae.setId(2);
            a18.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13); ae.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
            g.addView(a18); g.addView(ae);
            g.check(enfant() ? 2 : 1);
            g.setOnCheckedChangeListener((gr, id) -> { ecrire(fichier("edition.txt"), id == 2 ? "enfant\n" : "adulte\n"); chercher(); });
            l.addView(g);
        }
        texte(l, "\nRégénérer le monde", 14);
        continent = caseACocher(l, "Continent", false);
        batiments = caseACocher(l, "Bâtiments", false);
        tuiles = caseACocher(l, "Tuiles du sol", false);
        moteur = caseACocher(l, "Moteur du PC (page web)", true);
        regenerer = bouton(l, "Régénérer", this::regenerer);
        suivi = texte(l, "", 13);
        texte(l, "\nJouer : relance le jeu avec le monde du serveur (la partie non sauvegardée est perdue)", 12);
        bouton(l, "Jouer", () -> jouer(false));
        bouton(l, "Jouer en 466", () -> jouer(true));
        ScrollView sv = new ScrollView(this);
        sv.addView(l);
        setContentView(sv);
        chercher();
    }

    @Override protected void onDestroy() { ouvert = false; super.onDestroy(); }

    /* ---------- Vues ---------- */
    private TextView texte(LinearLayout l, String t, int sp) {
        TextView v = new TextView(this);
        v.setText(t);
        v.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        l.addView(v);
        return v;
    }
    private CheckBox caseACocher(LinearLayout l, String t, boolean coche) {
        CheckBox c = new CheckBox(this);
        c.setText(t);
        c.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        c.setChecked(coche);
        l.addView(c);
        return c;
    }
    private Button bouton(LinearLayout l, String t, Runnable r) {
        Button b = new Button(this);
        b.setText(t);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        b.setAllCaps(false);
        b.setOnClickListener(v -> r.run());
        l.addView(b);
        return b;
    }
    private void afficher(TextView v, String t) { ui.post(() -> v.setText(t)); }
    private void fond(Runnable r) { new Thread(r).start(); }

    /* ---------- Fichiers (dossier privé, partagé avec le jeu) ---------- */
    private File fichier(String nom) { return new File(getFilesDir(), nom); }
    private static String lireFlux(InputStream in) throws java.io.IOException {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] b = new byte[8192];
        for (int n; (n = in.read(b)) > 0;) o.write(b, 0, n);
        in.close();
        return o.toString("UTF-8");
    }
    private String lire(File f) {
        try { return lireFlux(new java.io.FileInputStream(f)); } catch (Exception e) { return ""; }
    }
    private String asset(String nom) {
        try { return lireFlux(getAssets().open(nom)); } catch (Exception e) { return ""; }
    }
    private void ecrire(File f, String t) {
        try (OutputStream o = new FileOutputStream(f)) { o.write(t.getBytes(StandardCharsets.UTF_8)); } catch (Exception e) { /* tant pis */ }
    }
    private static long nombre(String t) {
        try { return Long.parseLong(t.trim()); } catch (Exception e) { return -1; }
    }

    /* ---------- Serveur ---------- */
    private boolean appEnfant() { return getPackageName().endsWith(".enfant"); }
    /** Édition jouée : celle de l'app enfant, sinon celle choisie ici (edition.txt) */
    private boolean enfant() { return appEnfant() || lire(fichier("edition.txt")).trim().equals("enfant"); }
    /** Copie téléchargée du monde de l'édition jouée (nom de maj_android.c) */
    private String mondeLocal() { return enfant() && !appEnfant() ? "monde_enfant" : "monde"; }
    private String cheminMonde() { return enfant() ? "/api/sorties/monde_enfant.pak" : "/api/sorties/monde.pak"; }
    private static String complete(String a) {
        a = a.trim().replaceFirst("^https?://", "").replaceFirst("/.*$", "");
        return a.isEmpty() || a.contains(":") ? a : a + ":8080";
    }

    /** Requête HTTP : { code, corps } ; en-têtes X-Mtime et Content-Length dans entetes (si non nul) */
    private static Object[] http(String meth, String url, String corps, int attente, long[] entetes) {
        try {
            HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
            c.setRequestMethod(meth);
            c.setConnectTimeout(attente);
            c.setReadTimeout(10000);
            if (corps != null) {
                c.setDoOutput(true);
                c.setRequestProperty("Content-Type", "application/json");
                try (OutputStream o = c.getOutputStream()) { o.write(corps.getBytes(StandardCharsets.UTF_8)); }
            }
            final int code = c.getResponseCode();
            if (entetes != null) { entetes[0] = nombre(String.valueOf(c.getHeaderField("X-Mtime"))); entetes[1] = c.getContentLengthLong(); }
            String t = "";
            if (!"HEAD".equals(meth)) {
                InputStream in = code < 400 ? c.getInputStream() : c.getErrorStream();
                if (in != null) t = lireFlux(in);
            }
            c.disconnect();
            return new Object[] { code, t };
        } catch (Exception e) { return null; }
    }
    /** Le serveur de l'Atelier répond-il à cette adresse ? (liste des projets : elle existe même sans monde exporté) */
    private static boolean tester(String a, int attente) {
        Object[] r = http("GET", "http://" + a + "/api/projets", null, attente, null);
        return r != null && (int) r[0] == 200 && ((String) r[1]).startsWith("[");
    }
    /** Date et taille du paquet monde de l'édition jouée au serveur, ou null s'il n'y en a pas */
    private long[] infoMonde(String a) {
        long[] e = new long[2];
        Object[] r = http("HEAD", "http://" + a + cheminMonde(), null, 3000, e);
        return r != null && (int) r[0] == 200 && e[0] >= 0 ? e : null;
    }

    /** Comme maj_android.c : adresse imposée, sinon les adresses connues en même temps, puis la diffusion UDP */
    private void chercher() {
        serveur = null;
        afficher(etat, "Recherche du serveur…");
        fond(() -> {
            final String impose = complete(lire(fichier("serveur_impose.txt")));
            List<String> c = new ArrayList<>();
            if (!impose.isEmpty()) c.add(impose);
            else {
                for (String t : new String[] { lire(fichier("serveur.txt")), asset("serveur.txt"), "10.0.2.2" })
                    for (String a : t.split("\\s+")) if (!a.isEmpty() && !c.contains(complete(a))) c.add(complete(a));
            }
            final String[] trouve = { null };
            final int[] restants = { c.size() };
            for (String a : c) fond(() -> {
                final boolean ok = tester(a, impose.isEmpty() ? 800 : 4000);
                synchronized (trouve) { if (ok && trouve[0] == null) trouve[0] = a; restants[0]--; trouve.notifyAll(); }
            });
            synchronized (trouve) {
                final long fin = System.currentTimeMillis() + 6000;
                while (trouve[0] == null && restants[0] > 0 && System.currentTimeMillis() < fin)
                    try { trouve.wait(100); } catch (InterruptedException e) { break; }
            }
            String s = trouve[0];
            if (s == null && impose.isEmpty()) s = diffusion();
            serveur = s;
            if (s == null) {
                afficher(etat, impose.isEmpty() ? "Serveur introuvable sur ce réseau (node serveur.js lancé ? même Wi-Fi ?)" : "Pas de réponse de " + impose);
                return;
            }
            if (impose.isEmpty()) ecrire(fichier("serveur.txt"), s + "\n");
            afficher(etat, "Serveur : " + s + "\n" + dates(infoMonde(s)));
            suivreEnCours();
        });
    }
    private String diffusion() {
        try (DatagramSocket d = new DatagramSocket()) {
            d.setBroadcast(true);
            d.setSoTimeout(400);
            byte[] q = "ECHOES?".getBytes(StandardCharsets.US_ASCII), b = new byte[64];
            for (int k = 0; k < 3; k++) {
                d.send(new DatagramPacket(q, q.length, InetAddress.getByName("255.255.255.255"), 8080));
                try {
                    DatagramPacket p = new DatagramPacket(b, b.length);
                    d.receive(p);
                    String r = new String(p.getData(), 0, p.getLength(), StandardCharsets.US_ASCII);
                    if (!r.startsWith("ECHOES ")) continue;
                    String a = p.getAddress().getHostAddress() + ":" + r.substring(7).trim();
                    if (tester(a, 2000)) return a;
                } catch (java.net.SocketTimeoutException e) { /* encore */ }
            }
        } catch (Exception e) { /* pas de réseau */ }
        return null;
    }
    private String dates(long[] srv) {
        SimpleDateFormat f = new SimpleDateFormat("dd/MM HH:mm", Locale.FRANCE);
        final String m = mondeLocal();
        long app = Math.max(m.equals("monde") ? nombre(asset("monde.pak.date")) : -1, fichier(m + ".pak").exists() ? nombre(lire(fichier(m + ".pak.date"))) : -1);
        String t = (enfant() ? "Édition enfant\n" : "") + (srv == null ? "Monde du serveur : aucun" + (enfant() ? " (régénère-le ci-dessous)" : "") : "Monde du serveur : " + f.format(new Date(srv[0])) + " (" + srv[1] / 1048576 + " Mo)");
        t += "\nMonde de l'app : " + (app > 0 ? f.format(new Date(app)) : "aucun");
        if (srv != null && srv[0] > app) t += "\n→ téléchargé au prochain lancement";
        return t;
    }

    /* ---------- Régénération (tâche « chaine » du serveur) ---------- */
    private void regenerer() {
        final String s = serveur;
        if (s == null) { afficher(suivi, "Pas de serveur."); return; }
        regenerer.setEnabled(false);
        afficher(suivi, "Lancement…");
        final boolean co = continent.isChecked(), ba = batiments.isChecked(), tu = tuiles.isChecked(), mo = moteur.isChecked();
        fond(() -> {
            try {
                JSONObject o = new JSONObject().put("continent", co).put("batiments", ba).put("tuiles", tu).put("moteur", mo).put("esp32", false)
                    .put("edition", enfant() ? "enfant" : "adulte");
                Object[] r = http("POST", "http://" + s + "/api/taches", new JSONObject().put("tache", "chaine").put("options", o).toString(), 3000, null);
                if (r == null) throw new Exception("pas de réponse du serveur");
                if ((int) r[0] == 409) { afficher(suivi, "Une tâche tourne déjà sur le serveur : je la suis."); suivreEnCours(); return; }
                JSONObject t = new JSONObject((String) r[1]);
                if ((int) r[0] != 200) throw new Exception(t.optString("erreur", "erreur " + r[0]));
                suivre(t.getLong("id"));
            } catch (Exception e) {
                afficher(suivi, "Échec : " + e.getMessage());
                ui.post(() -> regenerer.setEnabled(true));
            }
        });
    }
    /** Tâche « chaine » en cours sur le serveur (lancée ici, par la page ou un autre appareil) : suivie */
    private void suivreEnCours() {
        Object[] r = http("GET", "http://" + serveur + "/api/taches", null, 3000, null);
        try {
            JSONArray a = new JSONArray((String) r[1]);
            for (int i = 0; i < a.length(); i++) {
                JSONObject t = a.getJSONObject(i);
                if ("en cours".equals(t.optString("etat"))) { ui.post(() -> regenerer.setEnabled(false)); suivre(t.getLong("id")); return; }
            }
        } catch (Exception e) { /* pas de liste */ }
    }
    private void suivre(long id) {
        if (tacheSuivie == id) return;
        tacheSuivie = id;
        fond(() -> {
            final long t0 = System.currentTimeMillis();
            while (ouvert) {
                Object[] r = http("GET", "http://" + serveur + "/api/taches/" + id, null, 3000, null);
                try {
                    JSONObject t = new JSONObject((String) r[1]);
                    JSONObject e = t.optJSONObject("etape");
                    JSONArray j = t.optJSONArray("journal");
                    String ligne = j != null && j.length() > 0 ? j.getString(j.length() - 1) : "";
                    if (ligne.startsWith("— ")) ligne = "";   // titre de l'étape, déjà affiché
                    String nom = "chaine".equals(t.optString("tache")) ? "Régénération" : "Tâche « " + t.optString("tache") + " »";
                    if (!"en cours".equals(t.optString("etat"))) {
                        final boolean ok = "terminée".equals(t.optString("etat"));
                        afficher(suivi, ok ? nom + " terminée : touche Jouer." : nom + " en échec : " + t.optString("erreur"));
                        ui.post(() -> regenerer.setEnabled(true));
                        afficher(etat, "Serveur : " + serveur + "\n" + dates(infoMonde(serveur)));
                        break;
                    }
                    afficher(suivi, nom + " (" + (System.currentTimeMillis() - t0) / 1000 + " s)\n"
                        + (e != null ? "Étape " + e.optInt("n") + "/" + e.optInt("total") + " : " + e.optString("titre") + "\n" : "") + ligne);
                } catch (Exception x) { afficher(suivi, "Liaison avec le serveur perdue…"); }
                try { Thread.sleep(1000); } catch (InterruptedException x) { break; }
            }
            tacheSuivie = -1;
        });
    }

    /* ---------- Relance du jeu ---------- */
    private void jouer(boolean natif) {
        final ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        final String p = getPackageName();
        fond(() -> {
            // Les processus du jeu (les deux modes) arrêtés : le moteur ne se lance qu'une fois par processus
            for (int k = 0; k < 20; k++) {
                boolean reste = false;
                List<ActivityManager.RunningAppProcessInfo> l = am.getRunningAppProcesses();
                if (l != null) for (ActivityManager.RunningAppProcessInfo i : l)
                    if (i.processName.equals(p) || i.processName.equals(p + ":natif")) { android.os.Process.killProcess(i.pid); reste = true; }
                if (!reste) break;
                try { Thread.sleep(100); } catch (InterruptedException e) { break; }
            }
            ui.post(() -> {
                startActivity(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
                    .setComponent(new ComponentName(p, natif ? JEU_NATIF : JEU))
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
                finish();
            });
        });
    }
}
