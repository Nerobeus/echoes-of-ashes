"""Engins de la course anti-gravité (13/11/2026, demande de l'utilisateur : « pour les vaisseaux il faut télécharger des lowpoly
spaceship »). Packs CC0 téléchargés dans assets/modeles/vaisseaux/telecharges/ (provenance : assets/modeles/LICENCES.md) ; chaque engin
du CATALOGUE est importé dans Blender, réduit en deux niveaux de détail (décimation), ses couleurs relevées face par face (couleur du
matériau, ou de sa texture au milieu de la face, comme tools/combat3d/armes_importer.py), mis dans le repère du moteur (x à droite, y en
haut, z devant ; Blender : x, z, y), le nez vers +z, à la longueur d'un engin (AG_LONGUEUR), le dessous à -0,04, puis écrit pour
tools/antigrav/generer.js :

    assets/modeles/vaisseaux/antigrav/<id>.json   { id, nom, pack, auteur, licence, source, couleurs, texture, lods: [{ tris, coul, uv }] }
    assets/modeles/vaisseaux/antigrav/<id>.rgb    sa texture réduite (TEX × TEX, rouge, vert, bleu u8, lignes de haut en bas) : generer.js
                                                  en fait la livrée de chaque écurie (couleur d'accent remplacée par la sienne)
    assets/modeles/vaisseaux/antigrav/planche.png  vues de dessus et de côté de chaque engin (python des outils : --vignettes)

    "D:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python engine/tools/antigrav/engins_importer.py -- [id ...]
    tools/.venv/Scripts/python.exe engine/tools/antigrav/engins_importer.py --vignettes

Faces : triangles dont le sens suit la normale extérieure de Blender (bmesh : recalc_face_normals) passée dans le repère du moteur, qui
écarte une face de dos d'après (b - a) × (c - a) (render.c, ag_engins) ; le passage de Blender au moteur échange deux axes, il retourne donc
le sens des faces : chaque triangle est remis dans le bon sens ici."""
import json
import sys
import zipfile
from pathlib import Path

import numpy as np
try:
    import bpy
except ImportError:   # Python des outils : vignettes seulement
    bpy = None

RACINE = Path(__file__).resolve().parents[3]
TELE = RACINE / "assets" / "modeles" / "vaisseaux" / "telecharges"
SORTIE = RACINE / "assets" / "modeles" / "vaisseaux" / "antigrav"
TRAVAIL = RACINE / "assets" / "raw" / "vaisseaux_travail"
AG_LONGUEUR = 1.4          # cases, au plus ; largeur AG_LARGEUR au plus (antigrav.c : AG_R 0,42 de demi-largeur contre les murs)
AG_LARGEUR = 1.1
LODS = (700, 320, 140)     # triangles au plus : près, moyen, loin (generer.js : LOD_DIST) ; à 80, la décimation déchirait les engins ; les
                           # vaisseaux de Quaternius (750 à 8 300 triangles) perdaient trop leur forme à 320 (13/11/2026, retour de l'utilisateur)
TEX = 256                  # côté de la texture gardée (coordonnées de texture des faces : u8 dans le paquet)
COULEURS = 10              # couleurs au plus par engin

KS = ("kenney_space/kenney_space-kit.zip", "Models/GLTF format/", ".glb")
QS = ("quaternius_vaisseaux/ultimate_spaceships_-_may_2021.zip", "Ultimate Spaceships - May 2021/", ".blend")   # <Vaisseau>/Blend/<Vaisseau>.blend, textures dans <Vaisseau>/Textures
MJ = ("majadroid/vaisseaux_majadroid.zip", "Ships/", ".glb")   # (14/11/2026 : « LowPoly Spaceships » de majadroid, chaque vaisseau du ReadyToFly.fbx exporté à part en glb)
QS18 = ("quaternius_2018/spaceship_pack_2018.zip", "Spaceship Pack by Quaternius/Blends/", ".blend")   # (14/11/2026 : l'archive 7z d'OpenGameArt remise en zip ; textures dans Blends/Textures)
PACKS = {KS: ("Space Kit", "Kenney", "https://kenney.nl/assets/space-kit"),
         QS: ("Ultimate Spaceships", "Quaternius", "https://opengameart.org/content/lowpoly-spaceships-pack"),
         QS18: ("Spaceship Pack", "Quaternius", "https://opengameart.org/content/low-poly-spaceships"),
         MJ: ("LowPoly Spaceships", "majadroid (Maik Hoffmann)", "https://opengameart.org/content/3d-lowpoly-spaceships-and-components")}
# id, pack, fichier, nom, rotation autour du haut (degrés : le nez vers +z), largeur au plus (part de la longueur ; un cargo trop large serait rogné)
CATALOGUE = [
    ("k_racer", KS, "craft_racer", "Racer", 0),
    ("k_speeder_a", KS, "craft_speederA", "Speeder A", 0),
    ("k_speeder_b", KS, "craft_speederB", "Speeder B", 0),
    ("k_speeder_c", KS, "craft_speederC", "Speeder C", 0),
    ("k_speeder_d", KS, "craft_speederD", "Speeder D", 0),
] + [("q_" + n.lower(), QS, n + "/Blend/" + n, n, 180) for n in   # (Quaternius : le nez vers -y de Blender, donc vers l'arrière sans ce demi-tour)
      ("Bob", "Challenger", "Dispatcher", "Executioner", "Imperial", "Insurgent", "Omen", "Pancake", "Spitfire", "Striker", "Zenith")] + [("q18_" + n.lower().replace(" ", "_"), QS18, n, n, 0) for n in   # (pack de 2018 : orientation à vérifier sur la planche)
      ("BigSpaceship", "CroissantShip", "LongSpaceship", "MediumShip", "Small Spaceship")] + [("mj_" + n.lower(), MJ, n, n.replace("_", " "), 180) for n in   # (comme Quaternius : le nez vers -y de Blender)
      ("M1_Ship_1", "M1_Ship_2", "M1_Ship_3", "M1_Ship_4", "M1_Ship_5", "M2_Ship_1", "M2_Ship_2", "M2_Ship_3", "M3_Ship_1", "M3_Ship_2",
       "M3_Ship_3", "M4_Ship_1", "M4_Ship_2", "M4_Ship_3")]


def extraire(pack, nom):
    """le fichier et ses textures dans un dossier de travail à lui"""
    zf, dossier, ext = pack
    d = TRAVAIL / Path(zf).stem / nom
    d.mkdir(parents=True, exist_ok=True)
    z = zipfile.ZipFile(TELE / zf)
    voulu = dossier + nom + ext
    for m in z.namelist():
        if m.endswith("/"):
            continue
        haut = dossier + nom.split("/")[0] + "/" if "/" in nom else dossier   # (Quaternius : les textures du dossier du vaisseau)
        garde = m == voulu or (m.startswith(haut) and m.lower().endswith((".png", ".jpg")))
        if ext in (".gltf", ".obj") and m.startswith(dossier + nom) and m.lower().endswith((".bin", ".mtl")):
            garde = True
        if garde:
            cible = d / m[len(dossier):] if m.startswith(dossier) else d / Path(m).name
            cible.parent.mkdir(parents=True, exist_ok=True)
            cible.write_bytes(z.read(m))
    return d / (nom + ext)


def importer(f):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if f.suffix == ".blend":
        bpy.ops.wm.open_mainfile(filepath=str(f), load_ui=False, use_scripts=False)
    elif f.suffix == ".fbx":
        bpy.ops.import_scene.fbx(filepath=str(f))
    elif f.suffix == ".obj":
        bpy.ops.wm.obj_import(filepath=str(f))
    else:
        bpy.ops.import_scene.gltf(filepath=str(f))
    objs = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not objs:
        raise RuntimeError("aucun maillage dans " + f.name)
    return objs


def couleur_srgb(c):
    c = np.clip(np.asarray(c[:3], dtype=float), 0, 1)
    return np.where(c <= .0031308, 12.92 * c, 1.055 * c ** (1 / 2.4) - .055)


PIXELS = {}


def couleur_matiere(mat, uv=None):
    """couleur sRVB d'une face : sa texture au point uv, sinon la couleur de base du matériau"""
    if mat is None:
        return np.array([.7, .7, .7])
    if mat.use_nodes:
        sortie = next((n for n in mat.node_tree.nodes if n.type == "OUTPUT_MATERIAL" and n.inputs["Surface"].is_linked), None)
        shaders = [sortie.inputs["Surface"].links[0].from_node] if sortie else []
        shaders += [n for n in mat.node_tree.nodes if n.type.startswith("BSDF") or n.type == "EMISSION"]
        for n in shaders:
            e = n.inputs.get("Base Color") or n.inputs.get("Color")
            if e is None:
                continue
            if e.is_linked:
                src = e.links[0].from_node
                if src.type == "TEX_IMAGE" and src.image and uv is not None:
                    im = src.image
                    w, h = im.size
                    if w and h:
                        px = PIXELS.get(im.name)
                        if px is None:   # foreach_get : im.pixels[:] (une liste Python de 16 millions de flottants pour 2048²) bloquait Blender
                            a = np.empty(w * h * im.channels, dtype=np.float32)
                            im.pixels.foreach_get(a)
                            px = PIXELS[im.name] = a.reshape(h, w, im.channels)
                        x = int((uv[0] % 1) * w) % w
                        y = int((uv[1] % 1) * h) % h
                        c = px[y, x, :3]
                        return c if im.colorspace_settings.name != "Linear Rec.709" else couleur_srgb(c)
            return couleur_srgb(e.default_value)
    return couleur_srgb(mat.diffuse_color)


def texture_modele(o):
    """la première image de texture des matériaux de l'objet, réduite en TEX × TEX (moyenne des blocs), lignes de haut en bas ; None sans"""
    for m in o.data.materials:
        if not m or not m.use_nodes:
            continue
        for nd in m.node_tree.nodes:
            if nd.type == "TEX_IMAGE" and nd.image and nd.image.size[0]:
                im = nd.image
                w, h = im.size
                a = np.empty(w * h * im.channels, dtype=np.float32)
                im.pixels.foreach_get(a)
                px = a.reshape(h, w, im.channels)[::-1, :, :3]
                if im.colorspace_settings.name == "Linear Rec.709":
                    px = couleur_srgb(px)
                if w % TEX == 0 and h % TEX == 0:
                    px = px.reshape(TEX, h // TEX, TEX, w // TEX, 3).mean((1, 3))
                else:
                    px = px[(np.arange(TEX) * h // TEX)][:, (np.arange(TEX) * w // TEX)]
                return np.clip(np.round(px * 255), 0, 255).astype(np.uint8)
    return None


def un_objet(objs):
    """les maillages joints en un seul objet, transformations appliquées"""
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    o = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return o


def faces(o, cible):
    """triangles (repère de Blender), normale extérieure et couleur de chaque triangle, l'objet réduit à cible triangles au plus"""
    import bmesh
    dg = bpy.context.evaluated_depsgraph_get()
    me0 = o.evaluated_get(dg).to_mesh()
    n0 = sum(len(p.vertices) - 2 for p in me0.polygons)
    o.evaluated_get(dg).to_mesh_clear()
    mod = None
    if n0 > cible:
        mod = o.modifiers.new("reduction", "DECIMATE")
        mod.decimate_type = "COLLAPSE"
        mod.ratio = cible / n0
        mod.use_collapse_triangulate = True
        dg = bpy.context.evaluated_depsgraph_get()
    oe = o.evaluated_get(dg)
    me = oe.to_mesh()
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    uvl = bm.loops.layers.uv.active
    tris, nrm, cols, uvs = [], [], [], []
    for fa in bm.faces:
        mat = o.material_slots[fa.material_index].material if fa.material_index < len(o.material_slots) else None
        uv = np.mean([np.array(l[uvl].uv) for l in fa.loops], axis=0) if uvl else None
        tris.append(np.array([np.array(l.vert.co) for l in fa.loops]))
        nrm.append(np.array(fa.normal))
        cols.append(couleur_matiere(mat, uv))
        uvs.append(np.array([np.array(l[uvl].uv) for l in fa.loops]) if uvl else np.zeros((3, 2)))
    bm.free()
    oe.to_mesh_clear()
    if mod:
        o.modifiers.remove(mod)
    return np.array(tris), np.array(nrm), np.array(cols), np.array(uvs)


def palette(cols, n):
    """couleurs ramenées à n au plus (k-moyennes en partant des plus fréquentes) : palette, indice de chaque face"""
    cq = np.round(np.clip(cols, 0, 1) * 31) / 31
    pal, idx, cpt = np.unique(cq, axis=0, return_inverse=True, return_counts=True)
    idx = idx.reshape(-1)
    if len(pal) <= n:
        return pal, idx
    C = pal[np.argsort(-cpt)[:n]].copy()
    for _ in range(12):
        k = ((cq[:, None, :] - C[None]) ** 2).sum(2).argmin(1)
        for j in range(n):
            if (k == j).any():
                C[j] = cq[k == j].mean(0)
    k = ((cq[:, None, :] - C[None]) ** 2).sum(2).argmin(1)
    u = np.unique(k)
    ren = {v: i for i, v in enumerate(u)}
    return C[u], np.array([ren[v] for v in k])


def moteur(tris, nrm, rot, uvs=None):
    """repère du moteur (x, y, z) = Blender (x, z, y), tourné de rot degrés autour du haut ; triangles remis dans le sens de leur normale"""
    a = np.radians(rot)
    R = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0], [-np.sin(a), 0, np.cos(a)]])
    T = tris[..., [0, 2, 1]] @ R.T
    N = nrm[..., [0, 2, 1]] @ R.T
    c = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
    inv = (c * N).sum(1) < 0
    T[inv] = T[inv][:, [0, 2, 1]]
    if uvs is not None:
        uvs = uvs.copy()
        uvs[inv] = uvs[inv][:, [0, 2, 1]]
        return T, uvs
    return T


def main():
    a = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    SORTIE.mkdir(parents=True, exist_ok=True)
    for ident, pack, fichier, nom, rot in CATALOGUE:
        if a and ident not in a:
            continue
        objs = importer(extraire(pack, fichier))
        o = un_objet(objs)
        lods, toutes = [], []
        tex = texture_modele(o)
        for cible in LODS:
            t, n, c, uv = faces(o, cible)
            T, uv = moteur(t, n, rot, uv)
            lods.append((T, c, uv))
            toutes.append(c)
        # Taille et place : d'après le niveau 0 ; longueur le long de z
        P = lods[0][0].reshape(-1, 3)
        lo, hi = P.min(0), P.max(0)
        k = min(AG_LONGUEUR / (hi[2] - lo[2]), AG_LARGEUR / (hi[0] - lo[0]))
        decal = np.array([(lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2])
        pal, _ = palette(np.concatenate(toutes), COULEURS)
        sortie_lods = []
        for T, c, uv in lods:
            Q = (T - decal) * k + np.array([0, -.04, 0])
            idx = ((np.round(np.clip(c, 0, 1) * 31)[:, None, :] / 31 - pal[None]) ** 2).sum(2).argmin(1)
            lod = {"tris": [round(float(v), 4) for v in Q.reshape(-1)], "coul": [int(i) for i in idx]}
            if tex is not None:
                lod["uv"] = [round(float(v), 4) for v in uv.reshape(-1)]
            sortie_lods.append(lod)
        if tex is not None:
            (SORTIE / (ident + ".rgb")).write_bytes(tex.tobytes())
        nom_pack, auteur, url = PACKS[pack]
        fiche = {"id": ident, "nom": nom, "pack": nom_pack, "fichier": fichier, "auteur": auteur, "licence": "CC0 1.0", "source": url,
                 "largeur": round(float((hi[0] - lo[0]) * k), 3), "hauteur": round(float((hi[1] - lo[1]) * k), 3),
                 "couleurs": ["#%02x%02x%02x" % tuple(int(round(v * 255)) for v in c) for c in pal],
                 "texture": {"fichier": ident + ".rgb", "w": TEX, "h": TEX} if tex is not None else None, "lods": sortie_lods}
        (SORTIE / (ident + ".json")).write_text(json.dumps(fiche, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
        print("ENGIN", ident, nom, "triangles", [len(l["coul"]) for l in sortie_lods], "couleurs", len(pal),
              "largeur", fiche["largeur"], "hauteur", fiche["hauteur"], flush=True)


def vignettes():
    """planche : chaque engin vu de dessus (nez en haut) et de côté (nez à droite), niveaux 0 et 1, éclairage plat"""
    from PIL import Image, ImageDraw
    fs = sorted(SORTIE.glob("*.json"))
    C = 150
    im = Image.new("RGB", (C * 6, C * len(fs)), (24, 22, 32))
    d = ImageDraw.Draw(im)
    for r, f in enumerate(fs):
        x = json.loads(f.read_text(encoding="utf-8"))
        pal = [tuple(int(h[i:i + 2], 16) for i in (1, 3, 5)) for h in x["couleurs"]]
        for li, lod in enumerate(x["lods"]):
            T = np.array(lod["tris"]).reshape(-1, 3, 3)
            for vue in range(2):
                ox, oy = C * (vue + 2 * li), C * r
                if vue == 0:   # dessus : x à droite, z vers le haut ; profondeur y
                    P2 = np.stack([T[..., 0], -T[..., 2]], -1); prof = T[..., 1].mean(1)
                else:          # côté : z à droite, y vers le haut ; profondeur -x
                    P2 = np.stack([T[..., 2], -T[..., 1]], -1); prof = -T[..., 0].mean(1)
                for i in np.argsort(prof):
                    n = np.cross(T[i, 1] - T[i, 0], T[i, 2] - T[i, 0])
                    ln = np.linalg.norm(n) or 1
                    l = .55 + .45 * max(0, float(np.dot(n / ln, [.4, .8, .45])))
                    c = tuple(int(min(255, v * l)) for v in pal[lod["coul"][i]])
                    d.polygon([(ox + C / 2 + p[0] * 95, oy + C / 2 + p[1] * 95) for p in P2[i]], fill=c)
            d.text((C * 2 * li + 4, C * r + 4), x["id"] + " " + str(len(lod["coul"])), fill=(220, 220, 230))
    im.save(SORTIE / "planche.png")
    print("PLANCHE", SORTIE / "planche.png", len(fs), "engins")


if __name__ == "__main__":
    if "--vignettes" in sys.argv:
        vignettes()
    else:
        main()
