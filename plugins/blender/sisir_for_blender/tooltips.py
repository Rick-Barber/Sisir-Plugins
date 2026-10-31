"""The add-on's tooltip catalogue, keyed like the app's and the other plug-ins' (UiTooltips ids).

The dialogs SisirBridge shows take theirs from plugins/common/PluginTooltips.cpp ("blender.import.*");
these are the Blender-side ones: the Sisir menu and the Sisir Hair panel. One id, one text, so a
future translation table covers the add-on too.
"""

_CATALOGUE = {
    "blender.menu.importScene": "Bake and import a Sisir .sir scene",
    "blender.menu.exportHairAbc": "The selected Sisir hair (and the visible hair under selected objects) as an Alembic groom laid out like Sisir's own export",
    "blender.menu.exportGuidesPly": "The selected curves (and the visible curves under selected objects) as Sisir guides",
    "blender.menu.locateSisir": "Show where Sisir.exe is",

    "blender.hair.widthScale": "Multiplies every strand's width, in the viewport and in renders.",
    "blender.hair.displayPercent": "How much of the hair the viewport draws, thinned evenly. Renders are not affected.",
    "blender.hair.renderPercent": "How much of the hair renders, the same even subset the viewport thins to.",
    "blender.hair.colorMode": "Auto shows Sisir's colours while the hair wears its Sisir material, and the material's colour once another one is assigned.",
    "blender.hair.colorMode.auto": "Sisir's colours while the hair wears its Sisir material.",
    "blender.hair.colorMode.sisir": "The colour Sisir generated for every point of every strand.",
    "blender.hair.colorMode.set": "One flat colour: Set Color.",
    "blender.hair.colorMode.material": "The colour of the Sisir hair material's melanin.",
    "blender.hair.setColor": "The hair set's colour from Sisir, used by Hair Set Color.",
}


def text(tooltip_id):
    return _CATALOGUE.get(tooltip_id, "")
