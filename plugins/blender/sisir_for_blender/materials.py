"""Cycles / Eevee materials for an import, from the bake's META.

The mapping is Sisir for 3ds Max's (plugins/max/src/SceneBuilder.cpp) with Arnold's shaders swapped
for Blender's: Principled Hair BSDF coloured by the per-point albedo Sisir generated, Principled
BSDF for the mesh and the cards. Colour maps are sRGB, data maps (roughness, metalness, normals,
masks) Non-Color, as Sisir reads them.
"""

import math
import os

import bpy

# What Arnold renders for aiStandardHair at melanin 0..1 (columns) and redness 0, 0.5, 1 (rows),
# sRGB: plugins/common/HairViewColor.h's measured table, for the Material Color mode.
_MELANIN = (
    ((255, 255, 255), (230, 225, 205), (177, 163, 119), (122, 104, 59), (79, 61, 29), (50, 35, 16), (31, 20, 11), (20, 13, 9), (14, 10, 8), (11, 9, 8), (9, 8, 8)),
    ((255, 255, 255), (232, 227, 202), (183, 167, 114), (131, 109, 55), (89, 66, 27), (59, 39, 15), (38, 23, 11), (25, 15, 9), (18, 11, 9), (13, 9, 8), (11, 9, 8)),
    ((255, 255, 255), (235, 229, 199), (189, 172, 110), (140, 115, 52), (99, 72, 26), (69, 43, 15), (47, 26, 11), (32, 17, 10), (23, 12, 9), (17, 10, 8), (13, 9, 8)),
)


def srgb_to_linear(v):
    v = min(max(float(v), 0.0), 1.0)
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def melanin_color(melanin, redness):
    """Linear colour for a melanin / redness pair, bilinear over the measured table."""
    m = min(max(float(melanin), 0.0), 1.0) * 10.0
    r = min(max(float(redness), 0.0), 1.0) * 2.0
    m0, r0 = min(int(m), 9), min(int(r), 1)
    fm, fr = m - m0, r - r0
    out = []
    for k in range(3):
        def at(ri, mi):
            return srgb_to_linear(_MELANIN[ri][mi][k] / 255.0)
        lo = at(r0, m0) + (at(r0, m0 + 1) - at(r0, m0)) * fm
        hi = at(r0 + 1, m0) + (at(r0 + 1, m0 + 1) - at(r0 + 1, m0)) * fm
        out.append(lo + (hi - lo) * fr)
    return out


# Sisir's card shader (cards_standalone.hlsl) reflects the environment with GGX roughness
# max(hair_to_ggx_roughness(r), 0.2) at 0.68 * fresnel * specular * 0.5, the Fresnel taken against
# a fake normal that faces the viewer (square to the strand): it never reaches grazing, so the sheen
# is its F0 (0.04629) at every angle -- about 1%. A Fresnel against the card's real normal, as
# Principled's specular has, goes to 100% along every card edge and turns the cards into glass.
GGX_ROUGHNESS_FLOOR = 0.2
CARD_SHEEN = 0.68 * 0.5 * 0.04629


def hair_to_ggx(r):
    r = min(max(float(r), 0.0), 1.0)
    return max(min(0.70505 * r - 0.117104 * r * r + 0.0259047 * r * r * r, 1.0), GGX_ROUGHNESS_FLOOR)


def new_material(name):
    m = bpy.data.materials.new(name)
    if bpy.app.version < (5, 0, 0):
        m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    return m


def _rgb(c, fallback):
    try:
        return [float(c[0]), float(c[1]), float(c[2])]
    except (TypeError, IndexError, ValueError):
        return list(fallback)


class Materials:
    """Materials and images one import makes, each made once; `created` lists every new ID."""

    def __init__(self, meta, assets_dir, scene_name, units_per_metre, created):
        self.meta = meta
        self.assets_dir = assets_dir
        self.scene = scene_name
        self.upm = units_per_metre
        self.created = created
        self._hair = {}
        self._cards = {}
        self._images = {}
        # A card material cut out through Alpha: each cut-out surface a Cycles ray passes costs a
        # transparent bounce (see Builder._raise_transparent_bounces).
        self.card_alpha = False

    # --- textures --------------------------------------------------------------------------------

    def image(self, relative, color):
        if not relative:
            return None
        path = os.path.join(self.assets_dir, relative)
        if not os.path.isfile(path):
            return None
        key = (os.path.normcase(os.path.abspath(path)), color)
        if key in self._images:
            return self._images[key]
        before = set(bpy.data.images)
        img = bpy.data.images.load(path, check_existing=True)
        if img not in before:
            self.created.append(img)
        if not color:
            try:
                img.colorspace_settings.name = "Non-Color"
            except TypeError:
                pass
        if bpy.data.filepath:
            try:
                img.filepath = bpy.path.relpath(img.filepath)
            except ValueError:  # another drive
                pass
        self._images[key] = img
        return img

    @staticmethod
    def _tex(nt, img, x, y, label):
        n = nt.nodes.new("ShaderNodeTexImage")
        n.image = img
        n.label = label
        n.location = (x, y)
        return n

    @staticmethod
    def _normal(nt, tex, strength, invert_green, x, y):
        """tex -> (green inverted) -> Normal Map: the Normal socket's input."""
        out = tex.outputs["Color"]
        if invert_green:
            sep = nt.nodes.new("ShaderNodeSeparateColor")
            sep.location = (x, y)
            inv = nt.nodes.new("ShaderNodeMath")
            inv.operation = "SUBTRACT"
            inv.inputs[0].default_value = 1.0
            inv.location = (x + 160, y - 40)
            comb = nt.nodes.new("ShaderNodeCombineColor")
            comb.location = (x + 320, y)
            nt.links.new(out, sep.inputs[0])
            nt.links.new(sep.outputs[0], comb.inputs[0])
            nt.links.new(sep.outputs[1], inv.inputs[1])
            nt.links.new(inv.outputs[0], comb.inputs[1])
            nt.links.new(sep.outputs[2], comb.inputs[2])
            out = comb.outputs[0]
            x += 480
        nm = nt.nodes.new("ShaderNodeNormalMap")
        nm.location = (x, y)
        nm.inputs["Strength"].default_value = float(strength)
        nt.links.new(out, nm.inputs["Color"])
        return nm.outputs["Normal"]

    @staticmethod
    def _hair_to_ggx_nodes(nt, r, x, y):
        """hair_to_ggx(r) in nodes, for a roughness map: max(r*(0.70505 + r*(-0.117104 + r*0.0259047)), 0.2)."""
        def math(op, a, b, c=None, dx=0):
            n = nt.nodes.new("ShaderNodeMath")
            n.operation = op
            n.location = (x + dx, y)
            for i, v in enumerate((a, b) if c is None else (a, b, c)):
                if isinstance(v, (int, float)):
                    n.inputs[i].default_value = v
                else:
                    nt.links.new(v, n.inputs[i])
            return n.outputs[0]
        a = math("MULTIPLY_ADD", r, 0.0259047, -0.117104)
        b = math("MULTIPLY_ADD", a, r, 0.70505, dx=150)
        c = math("MULTIPLY", b, r, dx=300)
        return math("MAXIMUM", c, GGX_ROUGHNESS_FLOOR, dx=450)

    def _surface(self, name):
        m = new_material(name)
        self.created.append(m)
        nt = m.node_tree
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        out.location = (300, 0)
        bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
        bsdf.location = (0, 0)
        nt.links.new(bsdf.outputs[0], out.inputs["Surface"])
        return m, nt, bsdf

    # --- mesh ------------------------------------------------------------------------------------

    def mesh(self, reference, texture_rel):
        m = self.meta.get("meshMaterial", {}) or {}
        mat, nt, bsdf = self._surface(self.scene + ("_RefMeshMat" if reference else "_MeshMat"))
        albedo = self.image(texture_rel if reference else m.get("albedoTexture", ""), True)
        if albedo is not None:
            nt.links.new(self._tex(nt, albedo, -500, 300, "Albedo").outputs["Color"], bsdf.inputs["Base Color"])
        elif not reference:
            bsdf.inputs["Base Color"].default_value = _rgb(m.get("albedoColor"), (1, 1, 1)) + [1.0]
        if reference:
            bsdf.inputs["Roughness"].default_value = 0.6
            return mat
        bsdf.inputs["Roughness"].default_value = float(m.get("roughness", 0.5))
        rough = self.image(m.get("roughnessTexture", ""), False)
        if rough is not None:
            nt.links.new(self._tex(nt, rough, -500, 0, "Roughness").outputs["Color"], bsdf.inputs["Roughness"])
        bsdf.inputs["Metallic"].default_value = float(m.get("metalness", 0.0))
        metal = self.image(m.get("metalnessTexture", ""), False)
        if metal is not None:
            nt.links.new(self._tex(nt, metal, -500, -300, "Metalness").outputs["Color"], bsdf.inputs["Metallic"])
        nrm = self.image(m.get("normalTexture", ""), False)
        if nrm is not None:
            # Sisir's Flip Y is on for OpenGL-convention maps, which Blender reads unflipped.
            tex = self._tex(nt, nrm, -1100, -600, "Normal")
            nt.links.new(self._normal(nt, tex, m.get("normalStrength", 1.0), not m.get("normalFlipY", True), -800, -600),
                         bsdf.inputs["Normal"])
        if m.get("subsurface", False):
            # As for Arnold: Sisir's weight drives its viewport approximation, a physical one reads far
            # stronger at the same number. A tenth of it matches.
            bsdf.inputs["Subsurface Weight"].default_value = 0.1 * float(m.get("subsurfaceWeight", 0.5))
            bsdf.inputs["Subsurface Radius"].default_value = _rgb(m.get("subsurfaceColor"), (1.0, 0.4, 0.3))
            bsdf.inputs["Subsurface Scale"].default_value = float(m.get("subsurfaceRadius", 0.07)) * self.upm
        return mat

    # --- hair ------------------------------------------------------------------------------------

    def hair(self, material_id):
        material_id = min(max(int(material_id), 0), 9)
        if material_id in self._hair:
            return self._hair[material_id]
        mats = self.meta.get("hairMaterials", []) or []
        m = mats[material_id] if material_id < len(mats) else {}
        mat = new_material("%s_HairMat%d" % (self.scene, material_id))
        self.created.append(mat)
        nt = mat.node_tree
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        out.location = (600, 0)
        bsdf = nt.nodes.new("ShaderNodeBsdfHairPrincipled")
        bsdf.location = (300, 0)
        try:
            bsdf.model = "CHIANG"
        except TypeError:
            pass
        bsdf.parametrization = "COLOR"
        # Sisir's longitudinal roughness (default 0.05) to a perceptual one, as for Arnold.
        bsdf.inputs["Roughness"].default_value = min(max(math.sqrt(max(float(m.get("roughness", 0.05)), 0.0)), 0.02), 1.0)
        bsdf.inputs["IOR"].default_value = float(m.get("ior", 1.55))
        nt.links.new(bsdf.outputs[0], out.inputs["Surface"])

        # Colour Mode (the Sisir Hair panel), per object: Sisir Color / Auto = the generated albedo,
        # Hair Set Color = the object's set colour, Material Color = this material's melanin.
        albedo = nt.nodes.new("ShaderNodeAttribute")
        albedo.attribute_type = "GEOMETRY"
        albedo.attribute_name = "sisir_albedo"
        albedo.location = (-700, 200)
        mode = nt.nodes.new("ShaderNodeAttribute")
        mode.attribute_type = "OBJECT"
        mode.attribute_name = "sisir_hair.color_mode_index"
        mode.location = (-700, -100)
        set_color = nt.nodes.new("ShaderNodeAttribute")
        set_color.attribute_type = "OBJECT"
        set_color.attribute_name = "sisir_hair.set_color"
        set_color.location = (-700, 0)
        material_color = nt.nodes.new("ShaderNodeRGB")
        material_color.label = "Material Color"
        material_color.outputs[0].default_value = melanin_color(m.get("melanin", 0.0), m.get("melaninRedness", 0.0)) + [1.0]
        material_color.location = (-700, -300)

        def is_mode(value, y):
            c = nt.nodes.new("ShaderNodeMath")
            c.operation = "COMPARE"
            c.inputs[1].default_value = float(value)
            c.inputs[2].default_value = 0.5
            c.location = (-450, y)
            nt.links.new(mode.outputs["Fac"], c.inputs[0])
            return c.outputs[0]

        mix_set = nt.nodes.new("ShaderNodeMix")
        mix_set.data_type = "RGBA"
        mix_set.location = (-200, 100)
        nt.links.new(is_mode(2, -100), mix_set.inputs["Factor"])
        nt.links.new(albedo.outputs["Color"], mix_set.inputs[6])
        nt.links.new(set_color.outputs["Color"], mix_set.inputs[7])
        mix_mat = nt.nodes.new("ShaderNodeMix")
        mix_mat.data_type = "RGBA"
        mix_mat.location = (50, 0)
        nt.links.new(is_mode(3, -300), mix_mat.inputs["Factor"])
        nt.links.new(mix_set.outputs[2], mix_mat.inputs[6])
        nt.links.new(material_color.outputs[0], mix_mat.inputs[7])
        nt.links.new(mix_mat.outputs[2], bsdf.inputs["Color"])
        self._hair[material_id] = mat
        return mat

    # --- cards -----------------------------------------------------------------------------------

    def card(self, uv_id, layer_name, layer_color, legal_name):
        key = ("uv", uv_id) if uv_id >= 0 else ("layer", layer_name)
        if key in self._cards:
            return self._cards[key]
        name = "%s_CardMat_UV%d" % (self.scene, uv_id + 1) if uv_id >= 0 else "%s_CardMat_%s" % (self.scene, legal_name(layer_name))
        mats = self.meta.get("cardMaterials", []) or []
        m = mats[uv_id] if 0 <= uv_id < len(mats) else {}
        mat, nt, bsdf = self._surface(name)
        # Cards are sheets seen from both sides.
        mat.use_backface_culling = False
        if "Thin Wall" in bsdf.inputs:
            bsdf.inputs["Thin Wall"].default_value = True
        # Sisir shades cards with a hair model (cards_standalone.hlsl): Roughness is a hair roughness
        # and Specular scales a faint sheen, not a surface reflection. Taken as Principled's own, 0.35
        # and 0.5 make a flat card a glossy mirror. So: Principled without its specular, plus the
        # sheen as Sisir computes it (see _card_sheen).
        bsdf.inputs["Roughness"].default_value = hair_to_ggx(float(m.get("roughness", 0.35)))
        bsdf.inputs["Specular IOR Level"].default_value = 0.0
        color_img = self.image(m.get("colorTexture", ""), True) if uv_id >= 0 else None
        color_tex = None
        if color_img is not None:
            color_tex = self._tex(nt, color_img, -800, 300, "Color")
            color_out = color_tex.outputs["Color"]
            tint = _rgb(m.get("baseColorTint"), (1, 1, 1))
            if tint != [1.0, 1.0, 1.0]:
                # Sisir's tint is sRGB: linearised, as its card shader multiplies by it.
                mul = nt.nodes.new("ShaderNodeMix")
                mul.data_type = "RGBA"
                mul.blend_type = "MULTIPLY"
                mul.inputs["Factor"].default_value = 1.0
                mul.inputs[7].default_value = [srgb_to_linear(c) for c in tint] + [1.0]
                mul.location = (-450, 300)
                nt.links.new(color_out, mul.inputs[6])
                color_out = mul.outputs[2]
            nt.links.new(color_out, bsdf.inputs["Base Color"])
        else:
            bsdf.inputs["Base Color"].default_value = _rgb(layer_color, (0.8, 0.8, 0.8)) + [1.0]
        if uv_id >= 0:
            # Opacity: the mask, else the colour map's alpha -- cut at Sisir's Alpha Cutoff, as its
            # card shader clips, so every renderer sees the same silhouette.
            mask_img = self.image(m.get("maskTexture", ""), False)
            alpha = None
            if mask_img is not None:
                alpha = self._tex(nt, mask_img, -800, 0, "Mask").outputs["Color"]
            elif color_tex is not None:
                alpha = color_tex.outputs["Alpha"]
            if alpha is not None:
                cut = nt.nodes.new("ShaderNodeMath")
                cut.operation = "GREATER_THAN"
                cut.inputs[1].default_value = float(m.get("alphaCutoff", 0.5))
                cut.location = (-300, 0)
                nt.links.new(alpha, cut.inputs[0])
                nt.links.new(cut.outputs[0], bsdf.inputs["Alpha"])
                self.card_alpha = True
                if hasattr(mat, "surface_render_method"):
                    mat.surface_render_method = "DITHERED"
                if bpy.app.version < (4, 2, 0) and hasattr(mat, "blend_method"):
                    mat.blend_method = "CLIP"
            rough = self.image(m.get("roughnessTexture", ""), False)
            if rough is not None:
                # A Roughness Map IS the (hair) roughness in Sisir, so it takes the same conversion.
                r = self._tex(nt, rough, -1400, -300, "Roughness").outputs["Color"]
                nt.links.new(self._hair_to_ggx_nodes(nt, r, -1150, -300), bsdf.inputs["Roughness"])
            ao_img = self.image(m.get("aoTexture", ""), False)
            if ao_img is not None and color_tex is not None:
                # Sisir darkens the environment light by the AO map and the direct light by half
                # of it; under an HDRI that is nearly all of it on the colour.
                ao = nt.nodes.new("ShaderNodeMix")
                ao.data_type = "RGBA"
                ao.blend_type = "MULTIPLY"
                ao.inputs["Factor"].default_value = 0.75
                ao.location = (-200, 300)
                nt.links.new(bsdf.inputs["Base Color"].links[0].from_socket, ao.inputs[6])
                nt.links.new(self._tex(nt, ao_img, -800, 550, "AO").outputs["Color"], ao.inputs[7])
                nt.links.new(ao.outputs[2], bsdf.inputs["Base Color"])
            nrm = self.image(m.get("normalTexture", ""), False)
            if nrm is not None:
                tex = self._tex(nt, nrm, -1400, -600, "Normal")
                nt.links.new(self._normal(nt, tex, m.get("normalStrength", 1.0), bool(m.get("normalFlipGreen", False)), -1100, -600),
                             bsdf.inputs["Normal"])
        self._card_sheen(nt, bsdf, min(max(float(m.get("specular", 0.5)), 0.0), 1.0))
        self._cards[key] = mat
        return mat

    @staticmethod
    def _card_sheen(nt, bsdf, specular):
        """Principled (no specular) + a GGX sheen of constant weight 0.68 * 0.5 * F0 * specular, with
        the card's roughness and normal; the cutout moves from Principled's Alpha to a Mix Shader
        with Transparent, since an Add Shader would not carry it."""
        out = next(n for n in nt.nodes if n.bl_idname == "ShaderNodeOutputMaterial")
        out.location = (900, 0)
        gloss = nt.nodes.new("ShaderNodeBsdfGlossy")  # ShaderNodeBsdfAnisotropic underneath in 5.x
        gloss.location = (300, -300)
        try:
            gloss.distribution = "GGX"
        except TypeError:
            pass
        sheen = CARD_SHEEN * specular
        gloss.inputs["Color"].default_value = (sheen, sheen, sheen, 1.0)
        for name in ("Roughness", "Normal"):
            src = bsdf.inputs[name]
            if src.is_linked:
                nt.links.new(src.links[0].from_socket, gloss.inputs[name])
            elif name == "Roughness":
                gloss.inputs[name].default_value = src.default_value
        add = nt.nodes.new("ShaderNodeAddShader")
        add.location = (550, 0)
        nt.links.new(bsdf.outputs[0], add.inputs[0])
        nt.links.new(gloss.outputs[0], add.inputs[1])
        surface = add.outputs[0]
        alpha = bsdf.inputs["Alpha"]
        if alpha.is_linked:
            cut = alpha.links[0].from_socket
            nt.links.remove(alpha.links[0])
            clear = nt.nodes.new("ShaderNodeBsdfTransparent")
            clear.location = (550, 200)
            mix = nt.nodes.new("ShaderNodeMixShader")
            mix.location = (750, 0)
            nt.links.new(cut, mix.inputs["Fac"])
            nt.links.new(clear.outputs[0], mix.inputs[1])
            nt.links.new(surface, mix.inputs[2])
            surface = mix.outputs[0]
        nt.links.new(surface, out.inputs["Surface"])
