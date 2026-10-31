"""The import options, as plugins/common/ImportOptions.cpp spells them.

One option string ("key=value;key=value") serves the dialog (SisirBridge hands it back on Import),
sisir_for_blender.api.import_file's `options` and the remembered settings, exactly as in Sisir for
Maya and 3ds Max, so the names, defaults and escaping can never drift between the plug-ins.
"""

from dataclasses import dataclass, field, replace

SETTINGS_KEY = r"Software\Sisir\SisirForBlender"  # QSettings("Sisir", "SisirForBlender") on Windows

DEFAULT_DISPLAY_PERCENT = 100.0
DEFAULT_UNIT = "m"  # Blender's own: one Blender unit is a metre (Maya and 3ds Max default to cm)
UNIT_METRES = {"mm": 0.001, "cm": 0.01, "m": 1.0}


@dataclass
class ImportOptions:
    mesh: bool = True
    refMesh: bool = True
    materials: bool = True
    guides: bool = True
    hair: bool = True
    cards: bool = True
    hairSets: list = field(default_factory=list)
    guideLayers: list = field(default_factory=list)
    cardLayers: list = field(default_factory=list)
    includeHidden: bool = False
    # What one Blender unit stands for: "mm", "cm" or "m"; empty: DEFAULT_UNIT. Older option strings
    # may give "scene" (scale=realWorld: the scene's Unit Scale) or "mesh" (scale=matchMesh: the
    # mesh file's unit).
    unit: str = ""
    guideDegree: int = 1
    displayPercent: float = DEFAULT_DISPLAY_PERCENT
    smoothCards: bool = True
    textureFolder: str = ""
    sisirExecutable: str = ""

    def resolved_unit(self):
        return self.unit or DEFAULT_UNIT

    def selected(self, wanted, name, visible):
        if wanted:
            return name in wanted
        return visible or self.includeHidden

    def to_string(self):
        kv = [
            "mesh=" + _flag(self.mesh), "refMesh=" + _flag(self.refMesh), "materials=" + _flag(self.materials),
            "guides=" + _flag(self.guides), "hair=" + _flag(self.hair), "cards=" + _flag(self.cards),
            "hairSets=" + _join(self.hairSets), "guideLayers=" + _join(self.guideLayers),
            "cardLayers=" + _join(self.cardLayers), "includeHidden=" + _flag(self.includeHidden),
            "unit=" + self.unit,
            "guideDegree=" + str(self.guideDegree),
            "displayPercent=" + _number(self.displayPercent), "smoothCards=" + _flag(self.smoothCards),
            "textureFolder=" + _join([self.textureFolder]), "sisir=" + _join([self.sisirExecutable]),
        ]
        return ";".join(kv)

    @classmethod
    def from_string(cls, text, base=None):
        o = replace(base) if base is not None else cls()
        o.hairSets, o.guideLayers, o.cardLayers = list(o.hairSets), list(o.guideLayers), list(o.cardLayers)
        for part in (text or "").split(";"):
            if not part:
                continue
            eq = part.find("=")
            if eq <= 0:
                continue
            key, v = part[:eq].strip(), part[eq + 1:].strip()
            if key in ("mesh", "refMesh", "materials", "guides", "hair", "cards", "includeHidden", "smoothCards"):
                setattr(o, key, _as_bool(v, getattr(o, key)))
            elif key in ("hairSets", "guideLayers", "cardLayers"):
                setattr(o, key, _split(v))
            elif key == "unit":
                if v.lower() in UNIT_METRES or v.lower() in ("", "scene", "mesh"):
                    o.unit = v.lower()
            elif key == "scale":  # before Units: Real World / Match Source Mesh
                o.unit = "scene" if v.lower() == "realworld" else "mesh"
            elif key == "guideDegree":
                o.guideDegree = 3 if _as_int(v) == 3 else 1
            elif key == "displayPercent":
                o.displayPercent = min(max(_as_float(v), 0.0), 100.0)
            elif key == "textureFolder":
                o.textureFolder = (_split(v) or [""])[0]
            elif key == "sisir":
                o.sisirExecutable = (_split(v) or [""])[0]
        return o

    @classmethod
    def load_remembered(cls):
        """The options the dialog last imported with (what ImportOptions::loadRemembered reads)."""
        o = cls.from_string(_read_setting("importOptions"))
        # Remembered before Units existed, Scale was always saved: the default unit takes over.
        if o.unit in ("scene", "mesh"):
            o.unit = ""
        # Selections are per scene and the texture folder per project: never remembered.
        o.hairSets, o.guideLayers, o.cardLayers, o.textureFolder = [], [], [], ""
        return o


def remembered_sisir():
    """Sisir.exe as Sisir for Blender last used it, or as Sisir's Plugins menu installed it."""
    return _read_setting("sisirExecutable")


def _read_setting(name):
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, SETTINGS_KEY) as key:
            value, _ = winreg.QueryValueEx(key, name)
            return str(value)
    except (ImportError, OSError):
        return ""


def _flag(b):
    return "1" if b else "0"


def _number(v):
    # QString::number(double): up to 6 significant digits, no trailing zeros.
    return format(float(v), ".6g")


def _as_bool(v, fallback):
    if v == "1" or v.lower() in ("true", "on"):
        return True
    if v == "0" or v.lower() in ("false", "off"):
        return False
    return fallback


def _as_int(v):
    try:
        return int(v)
    except ValueError:
        return 0


def _as_float(v):
    try:
        return float(v)
    except ValueError:
        return 0.0


# Names may hold anything but the separators; '|' separates list entries.
def _join(items):
    return "|".join(s.replace("%", "%25").replace("|", "%7C").replace(";", "%3B").replace("=", "%3D") for s in items)


def _split(v):
    if not v:
        return []
    return [s.replace("%3D", "=").replace("%3B", ";").replace("%7C", "|").replace("%25", "%") for s in v.split("|")]
