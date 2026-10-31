"""The Sisir menu's import, end to end in a real Blender window (what blender -b cannot run: the
modal operator, the dialog over Blender, the locked windows, the build in timer ticks, Esc, undo).

  set SISIR_BRIDGE_TEST=accept|cancel-build     (SisirBridge presses Import / cancels the build itself)
  blender --factory-startup --python tools/blender_gui_smoke.py -- <scene.sir> <Sisir.exe> <out dir> <addon dir> <mode>

Run by tools/build_blender_addon.py --gui with BLENDER_USER_SCRIPTS / BLENDER_USER_CONFIG in a
scratch folder. Writes <out dir>/gui_<mode>.log and quits Blender. Do not click in the window.
"""
import os
import sys
import time
import traceback

import addon_utils
import bpy

sir, sisir_exe, out_dir, addon_dir, mode = sys.argv[sys.argv.index("--") + 1:][:5]
os.makedirs(out_dir, exist_ok=True)
log = open(os.path.join(out_dir, "gui_%s.log" % mode), "w", encoding="utf-8")
failures = []
state = {"locked_seen": False, "t0": 0.0, "before": None}


def say(text):
    log.write(text + "\n")
    log.flush()
    print(text)


def check(cond, what):
    say(("PASS " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


def foreground():
    """Who has the focus: the window's title and process id (this Blender's id is os.getpid())."""
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.windll.user32
    h = user32.GetForegroundWindow()
    title = ctypes.create_unicode_buffer(256)
    user32.GetWindowTextW(h, title, 256)
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
    return "foreground %r, pid %d, Blender pid %d" % (title.value, pid.value, os.getpid())


def override():
    win = bpy.context.window_manager.windows[0]
    area = next(a for a in win.screen.areas if a.type == "VIEW_3D")
    return bpy.context.temp_override(window=win, area=area, region=next(r for r in area.regions if r.type == "WINDOW"))


def finish():
    say("RESULT " + ("OK" if not failures else "FAIL (%d)" % len(failures)))
    log.close()
    bpy.ops.wm.quit_blender()


def start():
    try:
        check(addon_utils.enable("sisir_for_blender", default_set=False) is not None, "the add-on loads")
        import sisir_for_blender.api as sisir
        check(os.path.normcase(sisir.plugin_path()).startswith(os.path.normcase(addon_dir)), "the add-on under test is loaded")
        state["before"] = set(o.name for o in bpy.data.objects)
        state["t0"] = time.time()
        with override():
            r = bpy.ops.sisir.import_scene("INVOKE_DEFAULT", filepath=sir, sisir=sisir_exe,
                                           options="textureFolder=" + os.path.join(out_dir, "textures"))
        check("RUNNING_MODAL" in r, "the import runs behind the dialog")
        bpy.app.timers.register(poll, first_interval=0.1)
    except Exception:
        say("FAIL exception:\n" + traceback.format_exc())
        failures.append("exception")
        finish()
    return None


def focus_settled(bridge):
    """True once Blender holds the focus, or 2 s after the dialog closed. Windows hands the focus
    from the bridge's dialog to Blender through Blender's own message queue, so right after the
    dialog closes nobody has it yet; another app holding it after 2 s is the bug this checks."""
    if state.get("closed_at") is None:
        state["closed_at"] = time.time()
    if bridge._user32.GetForegroundWindow() in bridge.blender_windows() or time.time() - state["closed_at"] > 2.0:
        state["closed_at"] = None
        return True
    return False


def poll():
    from sisir_for_blender import bridge
    from sisir_for_blender.hair import is_hair
    try:
        if bridge.ModalLock._active:
            lock = bridge.ModalLock._active[0]
            if lock.windows and not bridge._user32.IsWindowEnabled(lock.windows[0]):
                state["locked_seen"] = True
            if time.time() - state["t0"] < 300:
                return 0.1
            check(False, "the import finishes within 5 minutes")
            bridge.ModalLock.release_all()
            finish()
            return None
        if not focus_settled(bridge):
            return 0.1
        say("finished after %.1fs" % (time.time() - state["t0"]))
        check(state["locked_seen"], "Blender's window was locked while the dialog was up")
        windows = bridge.blender_windows()
        check(all(bridge._user32.IsWindowEnabled(h) for h in windows), "Blender's window is unlocked afterwards")
        check(bridge._user32.GetForegroundWindow() in windows, "Blender has the focus back, not another app (%s)" % foreground())
        new = [o for o in bpy.data.objects if o.name not in state["before"]]
        hair = [o for o in new if is_hair(o)]
        if mode == "accept":
            check(len(hair) > 0 and any(o.name.endswith("_Sisir") for o in new), "the scene is built")
            with override():
                bpy.ops.ed.undo()
            check(not any(is_hair(o) for o in bpy.data.objects), "one undo removes the whole import")
            with override():
                bpy.ops.ed.redo()
            check(len([o for o in bpy.data.objects if is_hair(o)]) == len(hair), "redo brings it back")
            # Export Hair (ABC) through its dialog, from the import's root.
            root = next(o for o in bpy.data.objects if o.name.endswith("_Sisir"))
            for o in bpy.context.view_layer.objects:
                o.select_set(o == root)
            bpy.context.view_layer.objects.active = root
            state["abc"] = os.path.join(out_dir, "gui.abc")
            state["sisir_abc_units"] = sisir_abc_units()
            state["t0"] = time.time()
            state["locked_seen"] = False
            with override():
                r = bpy.ops.sisir.export_hair_abc("INVOKE_DEFAULT", filepath=state["abc"])
            check("RUNNING_MODAL" in r, "the export runs behind its dialog")
            bpy.app.timers.register(poll_export, first_interval=0.1)
            return None
        else:
            check(not new, "a cancelled build leaves no objects (%d left)" % len(new))
            check(not any(m.name.startswith("Autumn") for m in bpy.data.materials), "nor materials")
            check(not any(c.name.endswith("_Sisir") for c in bpy.data.collections), "nor collections")
    except Exception:
        say("FAIL exception:\n" + traceback.format_exc())
        failures.append("exception")
    finish()
    return None


def poll_export():
    from sisir_for_blender import bridge
    if bridge.ModalLock._active and time.time() - state["t0"] < 120:
        lock = bridge.ModalLock._active[0]
        if lock.windows and not bridge._user32.IsWindowEnabled(lock.windows[0]):
            state["locked_seen"] = True
        return 0.1
    if not focus_settled(bridge):
        return 0.1
    check(state["locked_seen"], "Blender's window was locked while the export dialog was up")
    check(os.path.isfile(state["abc"]) and os.path.getsize(state["abc"]) > 1000, "the export dialog wrote the .abc")
    check(all(bridge._user32.IsWindowEnabled(h) for h in bridge.blender_windows()), "Blender's window is unlocked afterwards")
    check(bridge._user32.GetForegroundWindow() in bridge.blender_windows(), "Blender has the focus back after the export dialog (%s)" % foreground())
    check(sisir_abc_units() == state["sisir_abc_units"], "Sisir's own remembered ABC choices are not even rewritten (%r)" % (state["sisir_abc_units"],))
    own = sisir_abc_units(r"Software\Sisir\SisirForBlender\AbcExport\Software\Sisir\Sisir\qt\abcExport")
    check(own is not None and own[0] == 2, "Blender's own ABC choices are kept apart, in Meters (%r)" % (own,))
    # Export Guides (PLY) through its unit popup, from the import's root (still selected).
    state["ply"] = os.path.join(out_dir, "gui.ply")
    state["t0"] = time.time()
    state["locked_seen"] = False
    with override():
        r = bpy.ops.sisir.export_guides_ply("INVOKE_DEFAULT", filepath=state["ply"])
    check("RUNNING_MODAL" in r, "Export Guides (PLY) runs behind its unit popup")
    bpy.app.timers.register(poll_ply, first_interval=0.1)
    return None


def sisir_abc_units(key_path=r"Software\Sisir\Sisir\qt\abcExport"):
    """(units, last write time) of the Export Hair (ABC) choices under `key_path`: by default Sisir's
    own (QSettings "Sisir"/"Sisir"), which a Blender export must not even rewrite."""
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path) as key:
            return winreg.QueryValueEx(key, "units")[0], winreg.QueryInfoKey(key)[2]
    except OSError:
        return None


def poll_ply():
    from sisir_for_blender import bridge
    if bridge.ModalLock._active and time.time() - state["t0"] < 120:
        lock = bridge.ModalLock._active[0]
        if lock.windows and not bridge._user32.IsWindowEnabled(lock.windows[0]):
            state["locked_seen"] = True
        return 0.1
    if not focus_settled(bridge):
        return 0.1
    check(state["locked_seen"], "Blender's window was locked while the unit popup was up")
    text = open(state["ply"], encoding="utf-8").read() if os.path.isfile(state["ply"]) else ""
    check("comment sisir_unit m\n" in text, "the PLY is in metres, Blender's default unit")
    check(bridge._user32.GetForegroundWindow() in bridge.blender_windows(), "Blender has the focus back after the unit popup (%s)" % foreground())
    finish()
    return None


bpy.app.timers.register(start, first_interval=1.0)
