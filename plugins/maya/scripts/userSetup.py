# Sisir for Maya: load the plug-in when Maya starts.
#
# Maya runs every userSetup.py on its Python path, and this module's scripts folder is on it, so
# installing the module (Sisir > Plugins > Maya, or install.mel) is all it takes: nothing to tick in
# the Plug-in Manager. With a UI, deferred so the UI exists when the plug-in adds its Sisir menu;
# without one (mayapy, batch renders) right away, as deferred calls never run there.
import maya.cmds as cmds
import maya.utils


def _sisir_load_plugin():
    try:
        if not cmds.pluginInfo("SisirForMaya", query=True, loaded=True):
            cmds.loadPlugin("SisirForMaya", quiet=True)
    except Exception as error:
        print("Sisir for Maya could not be loaded: %s" % error)


if cmds.about(batch=True):
    _sisir_load_plugin()
else:
    maya.utils.executeDeferred(_sisir_load_plugin)
