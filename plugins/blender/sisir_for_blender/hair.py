"""The Sisir hair object: a native Curves object, a Geometry Nodes modifier, and the Sisir Hair panel.

Blender has no custom object types from Python, so the Maya shape node / Max object of the other
plug-ins becomes a hair Curves object holding the strands as Sisir generated them (the same arrays,
in Blender units and Z-up) plus the "Sisir Hair v1" node group, which does what their draw and
render code does:
  - Display % / Render %: keeps the strands whose random value is below the percentage (the viewport
    and the render each get their own, through Is Viewport), the same even subset as Max and Maya;
  - Width Scale: multiplies every radius.
The panel's settings live on the object (Object.sisir_hair) and are written into the modifier.
"""

import hashlib

import bpy
import numpy as np
from bpy.props import BoolProperty, EnumProperty, FloatProperty, FloatVectorProperty, IntProperty, PointerProperty, StringProperty

from . import tooltips

NODE_GROUP = "Sisir Hair v1"
MODIFIER = "Sisir Hair"

_COLOR_MODES = [
    ("AUTO", "Auto", tooltips.text("blender.hair.colorMode.auto"), 0),
    ("SISIR", "Sisir Color", tooltips.text("blender.hair.colorMode.sisir"), 1),
    ("SET", "Hair Set Color", tooltips.text("blender.hair.colorMode.set"), 2),
    ("MATERIAL", "Material Color", tooltips.text("blender.hair.colorMode.material"), 3),
]


# --- the node group --------------------------------------------------------------------------------

def _socket(group, name, default, lo, hi):
    s = group.interface.new_socket(name, in_out="INPUT", socket_type="NodeSocketFloat")
    s.default_value = default
    s.min_value = lo
    s.max_value = hi
    return s


def node_group():
    """The shared Sisir Hair group, made once per file. Its name carries a version: a later add-on
    that needs another graph makes "Sisir Hair v2" and leaves files made with this one working."""
    group = bpy.data.node_groups.get(NODE_GROUP)
    if group is not None and group.bl_idname == "GeometryNodeTree":
        return group
    group = bpy.data.node_groups.new(NODE_GROUP, "GeometryNodeTree")
    group.interface.new_socket("Geometry", in_out="INPUT", socket_type="NodeSocketGeometry")
    group.interface.new_socket("Geometry", in_out="OUTPUT", socket_type="NodeSocketGeometry")
    _socket(group, "Display %", 100.0, 0.0, 100.0)
    _socket(group, "Render %", 100.0, 0.0, 100.0)
    _socket(group, "Width Scale", 1.0, 0.0, 1000.0)
    nodes, links = group.nodes, group.links
    gin = nodes.new("NodeGroupInput")
    gin.location = (-900, 0)
    gout = nodes.new("NodeGroupOutput")
    gout.location = (500, 0)

    # percent = Render % + IsViewport * (Display % - Render %), as a fraction
    viewport = nodes.new("GeometryNodeIsViewport")
    viewport.location = (-900, -300)
    diff = nodes.new("ShaderNodeMath")
    diff.operation = "SUBTRACT"
    diff.location = (-700, -200)
    links.new(gin.outputs["Display %"], diff.inputs[0])
    links.new(gin.outputs["Render %"], diff.inputs[1])
    pick = nodes.new("ShaderNodeMath")
    pick.operation = "MULTIPLY_ADD"
    pick.location = (-500, -250)
    links.new(viewport.outputs[0], pick.inputs[0])
    links.new(diff.outputs[0], pick.inputs[1])
    links.new(gin.outputs["Render %"], pick.inputs[2])
    frac = nodes.new("ShaderNodeMath")
    frac.operation = "DIVIDE"
    frac.inputs[1].default_value = 100.0
    frac.location = (-300, -250)
    links.new(pick.outputs[0], frac.inputs[0])

    rnd = nodes.new("GeometryNodeInputNamedAttribute")
    rnd.data_type = "FLOAT"
    rnd.inputs["Name"].default_value = "strand_random"
    rnd.location = (-500, -450)
    cmp = nodes.new("FunctionNodeCompare")
    cmp.data_type = "FLOAT"
    cmp.operation = "GREATER_EQUAL"
    cmp.location = (-100, -300)
    links.new(rnd.outputs["Attribute"], cmp.inputs[0])
    links.new(frac.outputs[0], cmp.inputs[1])
    delete = nodes.new("GeometryNodeDeleteGeometry")
    delete.domain = "CURVE"
    delete.location = (-100, 0)
    links.new(gin.outputs["Geometry"], delete.inputs["Geometry"])
    links.new(cmp.outputs[0], delete.inputs["Selection"])

    radius = nodes.new("GeometryNodeInputRadius")
    radius.location = (-100, -550)
    scale = nodes.new("ShaderNodeMath")
    scale.operation = "MULTIPLY"
    scale.location = (100, -450)
    links.new(radius.outputs[0], scale.inputs[0])
    links.new(gin.outputs["Width Scale"], scale.inputs[1])
    set_radius = nodes.new("GeometryNodeSetCurveRadius")
    set_radius.location = (200, 0)
    links.new(delete.outputs["Geometry"], set_radius.inputs["Curve"])
    links.new(scale.outputs[0], set_radius.inputs["Radius"])
    links.new(set_radius.outputs[0], gout.inputs["Geometry"])
    return group


def _identifier(group, name):
    for item in group.interface.items_tree:
        if getattr(item, "in_out", None) == "INPUT" and item.name == name:
            return item.identifier
    return None


def modifier_input(mod, name):
    """A Geometry Nodes modifier input's value: an ID property up to Blender 4.x, an RNA property
    under modifier.properties.inputs from 5.0."""
    ident = _identifier(mod.node_group, name) if mod.node_group else None
    if ident is None:
        return None
    inputs = getattr(getattr(mod, "properties", None), "inputs", None)
    if inputs is not None and hasattr(inputs, ident):
        return getattr(inputs, ident).value
    return mod.get(ident)


def _set_modifier_input(mod, ident, value):
    inputs = getattr(getattr(mod, "properties", None), "inputs", None)
    if inputs is not None and hasattr(inputs, ident):
        socket = getattr(inputs, ident)
        if socket.value != value:
            socket.value = value
    elif mod.get(ident) != value:
        mod[ident] = value


def _write_modifier(obj):
    mod = obj.modifiers.get(MODIFIER)
    if mod is None or mod.node_group is None:
        return
    s = obj.sisir_hair
    for name, value in (("Display %", s.display_percent), ("Render %", s.render_percent), ("Width Scale", s.width_scale)):
        ident = _identifier(mod.node_group, name)
        if ident is not None:
            _set_modifier_input(mod, ident, float(value))
    obj.update_tag()


# --- the object's settings -------------------------------------------------------------------------

def _changed(self, context):
    obj = self.id_data
    if isinstance(obj, bpy.types.Object):
        _write_modifier(obj)


def _color_mode_changed(self, context):
    # The material reads it through an Object Attribute node, which reads numbers, not enums.
    index = {"AUTO": 0, "SISIR": 1, "SET": 2, "MATERIAL": 3}[self.color_mode]
    if self.color_mode_index != index:
        self.color_mode_index = index
    obj = self.id_data
    if isinstance(obj, bpy.types.Object):
        obj.update_tag()


def _set_color_changed(self, context):
    obj = self.id_data
    if isinstance(obj, bpy.types.Object):
        # Solid mode's Object colour shows it too.
        obj.color = (self.set_color[0], self.set_color[1], self.set_color[2], 1.0)
        obj.update_tag()


class SisirHairSettings(bpy.types.PropertyGroup):
    # The same names, ranges and defaults as Sisir for 3ds Max (HairObject.cpp) and Maya (HairShape.cpp).
    is_hair: BoolProperty(default=False, options={"HIDDEN"})
    width_scale: FloatProperty(name="Width Scale", description=tooltips.text("blender.hair.widthScale"),
                               default=1.0, min=0.0, max=1000.0, soft_max=4.0, step=1, precision=2, update=_changed)
    display_percent: FloatProperty(name="Display %", description=tooltips.text("blender.hair.displayPercent"),
                                   default=100.0, min=0.0, max=100.0, step=100, precision=0, update=_changed)
    render_percent: FloatProperty(name="Render %", description=tooltips.text("blender.hair.renderPercent"),
                                  default=100.0, min=0.0, max=100.0, step=100, precision=0, update=_changed)
    color_mode: EnumProperty(name="Color Mode", description=tooltips.text("blender.hair.colorMode"),
                             items=_COLOR_MODES, default="AUTO", update=_color_mode_changed)
    color_mode_index: FloatProperty(default=0.0, options={"HIDDEN"})
    set_color: FloatVectorProperty(name="Set Color", description=tooltips.text("blender.hair.setColor"),
                                   subtype="COLOR", size=3, min=0.0, max=1.0, default=(0.9, 0.8, 0.65), update=_set_color_changed)
    material_id: IntProperty(default=0, min=0, max=9, options={"HIDDEN"})
    groom_roughness: FloatProperty(default=0.05, min=0.0, max=1.0, options={"HIDDEN"})
    groom_ao: FloatProperty(default=1.0, min=0.0, max=1.0, options={"HIDDEN"})
    source_file: StringProperty(default="", subtype="FILE_PATH", options={"HIDDEN"})
    hair_set: StringProperty(default="", options={"HIDDEN"})
    set_index: IntProperty(default=0, min=0, options={"HIDDEN"})  # its place in Sisir's list: the export's order
    # Blender units per Sisir metre at import: what Export Hair (ABC) divides by to get metres back.
    units_per_metre: FloatProperty(default=1.0, options={"HIDDEN"})


def create_hair(name, h, s, collection, settings):
    """A hair Curves object from a bake's HAIR chunk `h`, `s` Blender units per metre, Y-up -> Z-up."""
    curves = bpy.data.hair_curves.new(name)
    curves.add_curves(h["pointCounts"].tolist())  # Python ints only
    p = h["positions"].reshape(-1, 3)
    pos = np.empty_like(p)
    pos[:, 0] = p[:, 0] * s
    pos[:, 1] = -p[:, 2] * s
    pos[:, 2] = p[:, 1] * s
    curves.attributes["position"].data.foreach_set("vector", pos.ravel())
    curves.attributes.new("radius", "FLOAT", "POINT").data.foreach_set("value", h["widths"] * (0.5 * s))
    albedo = curves.attributes.new("sisir_albedo", "BYTE_COLOR", "POINT")
    albedo.data.foreach_set("color_srgb", h["albedo"].astype(np.float32) * (1.0 / 255.0))
    try:
        curves.color_attributes.active_color = albedo  # Solid mode's Attribute colour (4.x)
    except AttributeError:
        pass
    curves.attributes.new("root_uv", "FLOAT2", "CURVE").data.foreach_set("vector", h["rootUvs"])
    curves.attributes.new("clump_id", "INT", "CURVE").data.foreach_set("value", h["clumpIds"])
    curves.attributes.new("strand_random", "FLOAT", "CURVE").data.foreach_set("value", h["randoms"])

    obj = bpy.data.objects.new(name, curves)
    collection.objects.link(obj)
    mod = obj.modifiers.new(MODIFIER, "NODES")
    mod.node_group = node_group()
    hs = obj.sisir_hair
    hs.is_hair = True
    for key, value in settings.items():
        setattr(hs, key, value)
    _write_modifier(obj)
    _set_color_changed(hs, None)
    return obj, curves


def is_hair(obj):
    return obj is not None and obj.type == "CURVES" and obj.sisir_hair.is_hair


def hair_stats(obj):
    """"strands=N;points=N;hash=<16 hex>" over the stored strands (Max's sisir.hairStats, for tests:
    a save and reload must give the same)."""
    c = obj.data
    n_curves, n_points = len(c.curves), len(c.points)
    digest = hashlib.blake2b(digest_size=8)
    sizes = np.empty(n_curves, dtype=np.int32)
    c.curves.foreach_get("points_length", sizes)
    digest.update(sizes.tobytes())
    for name, width in (("position", 3), ("radius", 1), ("sisir_albedo", 4), ("root_uv", 2), ("clump_id", 1), ("strand_random", 1)):
        a = c.attributes.get(name)
        if a is None:
            continue
        domain = n_points if a.domain == "POINT" else n_curves
        key = {"FLOAT_VECTOR": "vector", "FLOAT2": "vector", "BYTE_COLOR": "color_srgb"}.get(a.data_type, "value")
        buf = np.empty(domain * width, dtype=np.int32 if a.data_type == "INT" else np.float32)
        a.data.foreach_get(key, buf)
        digest.update(buf.tobytes())
    return "strands=%d;points=%d;hash=%s" % (n_curves, n_points, digest.hexdigest())


class OBJECT_PT_sisir_hair(bpy.types.Panel):
    bl_label = "Sisir Hair"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "object"

    @classmethod
    def poll(cls, context):
        return is_hair(context.object)

    def draw(self, context):
        obj = context.object
        s = obj.sisir_hair
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False
        layout.prop(s, "width_scale")
        layout.prop(s, "display_percent")
        layout.prop(s, "render_percent")
        layout.prop(s, "color_mode")
        layout.prop(s, "set_color")
        info = layout.box().column(align=True)
        info.label(text=s.hair_set or obj.name)
        info.label(text="{:,} strands, {:,} points".format(len(obj.data.curves), len(obj.data.points)))


classes = (SisirHairSettings, OBJECT_PT_sisir_hair)


def register():
    for c in classes:
        bpy.utils.register_class(c)
    bpy.types.Object.sisir_hair = PointerProperty(type=SisirHairSettings)


def unregister():
    del bpy.types.Object.sisir_hair
    for c in reversed(classes):
        bpy.utils.unregister_class(c)
