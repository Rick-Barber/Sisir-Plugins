# SisirMtoA: MtoA loads this beside SisirMtoA.dll. The Arnold section of a Sisir hair shape's
# Attribute Editor: the common shape render flags, as MtoA shows them for its own shapes.
import mtoa.ui.ae.templates as templates


class SisirHairShapeTemplate(templates.ShapeTranslatorTemplate):
    def setup(self):
        self.commonShapeAttributes()
        self.addSeparator()
        self.renderStatsAttributes()


templates.registerTranslatorUI(SisirHairShapeTemplate, "sisirHairShape", "sisirHair")
