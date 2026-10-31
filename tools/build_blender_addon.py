"""Build what Sisir for Blender needs and test it in every Blender installed here.

  python tools/build_blender_addon.py           # build SisirBridge, Sisir and build/Release/DCC/Blender
  python tools/build_blender_addon.py --test    # and run the smoke test in each Blender
  python tools/build_blender_addon.py --test --no-build 4.2
  python tools/build_blender_addon.py --gui --no-build    # the menu's import in a real Blender window

The add-on itself is Python (plugins/blender/sisir_for_blender) and needs no compiling; its Qt half,
SisirBridge.exe, builds with Sisir (CMakeLists.txt), which also copies the add-on into
build/Release/DCC/Blender for the Plugins menu. Uses the .venv's cmake when there is one (the one on
PATH is a broken shim on Sisir's build machine), else cmake from PATH.

In the public Sisir-Plugins repo there is no Sisir source: the build is SisirBridge.exe alone
(build/Release, from the standalone CMakeLists.txt there), and the tests use the SisirBridge.exe next
to SISIR_EXE -- copy yours there first.

--test runs Blender headless (blender -b, no licence, no window) with BLENDER_USER_SCRIPTS pointing
at a scratch copy of the add-on, so an installed copy is never the one tested. Needs Sisir.exe
(SISIR_EXE, else build/Release/Sisir.exe) and a scene (SISIR_SMOKE_SCENE, else
D:/Sample/Autumn_08.sir). Each run writes %TEMP%/sisir_blender_smoke_<version>/smoke.log.

--gui opens Blender for real (tools/blender_gui_smoke.py), twice per version: the import accepted,
then cancelled during the build (SisirBridge clicks by itself, SISIR_BRIDGE_TEST). Blender's
preferences go to a scratch BLENDER_USER_CONFIG. Do not click in those windows while they run.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
standalone = not (root / "src" / "main.cpp").is_file()  # the public Sisir-Plugins repo
cmake = root / ".venv" / "Scripts" / "cmake.exe"
if not cmake.is_file():
    cmake = shutil.which("cmake") or "cmake"
sisir_exe = os.environ.get("SISIR_EXE") or root / "build" / "Release" / "Sisir.exe"
blender_root = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Blender Foundation"
addon = root / "plugins" / "blender" / "sisir_for_blender"


def stage_addon(addons):
    """The add-on as Sisir ships it (CMakeLists.txt): its folder plus the shared PLY writer."""
    shutil.copytree(addon, addons / addon.name, ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copy2(root / "plugins" / "common" / "sisir_ply.py", addons / addon.name)


def installed_blenders():
    found = {}
    for d in blender_root.glob("Blender *"):
        m = re.match(r"Blender (\d+\.\d+)", d.name)
        if m and (d / "blender.exe").is_file():
            found[m.group(1)] = d / "blender.exe"
    return dict(sorted(found.items(), key=lambda kv: [int(x) for x in kv[0].split(".")]))


def run(args, **kw):
    print(">", " ".join(str(a) for a in args), flush=True)
    return subprocess.run([str(a) for a in args], cwd=root, **kw)


def smoke(version, exe):
    scene = os.environ.get("SISIR_SMOKE_SCENE", "D:/Sample/Autumn_08.sir")
    out = Path(tempfile.gettempdir()) / ("sisir_blender_smoke_" + version)
    if out.exists():
        shutil.rmtree(out)
    scripts = out / "scripts"
    stage_addon(scripts / "addons")
    env = dict(os.environ, BLENDER_USER_SCRIPTS=str(scripts))
    try:
        run([exe, "-b", "--factory-startup", "--python", root / "tools" / "blender_addon_smoke.py", "--",
             scene, sisir_exe, out, scripts / "addons"], env=env, timeout=1800)
    except subprocess.TimeoutExpired:
        print("Blender %s did not finish within 30 minutes" % version)
    log = out / "smoke.log"
    text = log.read_text(encoding="utf-8") if log.exists() else ""
    print(text)
    return "RESULT OK" in text


def gui(version, exe):
    scene = os.environ.get("SISIR_SMOKE_SCENE", "D:/Sample/Autumn_08.sir")
    out = Path(tempfile.gettempdir()) / ("sisir_blender_gui_" + version)
    if out.exists():
        shutil.rmtree(out)
    scripts, config = out / "scripts", out / "config"
    stage_addon(scripts / "addons")
    config.mkdir(parents=True)  # Blender ignores BLENDER_USER_CONFIG unless the folder exists
    ok = True
    for mode in ("accept", "cancel-build"):
        env = dict(os.environ, BLENDER_USER_SCRIPTS=str(scripts), BLENDER_USER_CONFIG=str(config), SISIR_BRIDGE_TEST=mode)
        try:
            run([exe, "--factory-startup", "--window-geometry", "40", "40", "960", "640", "--python",
                 root / "tools" / "blender_gui_smoke.py", "--", scene, sisir_exe, out,
                 scripts / "addons", mode], env=env, timeout=600)
        except subprocess.TimeoutExpired:
            print("Blender %s did not finish within 10 minutes" % version)
        log = out / ("gui_%s.log" % mode)
        text = log.read_text(encoding="utf-8") if log.exists() else ""
        print(text)
        ok = ok and "RESULT OK" in text
    return ok


def main():
    test = "--test" in sys.argv or "--gui" in sys.argv
    wanted = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--no-build" not in sys.argv:
        if run([cmake, "-S", root, "-B", root / "build"]).returncode != 0:
            sys.exit("configure failed")
        targets = ["SisirBridge"] if standalone else ["Sisir", "SisirDccPlugins"]
        if run([cmake, "--build", root / "build", "--config", "Release", "--target"] + targets).returncode != 0:
            sys.exit("build failed")
    if not test:
        return
    blenders = installed_blenders()
    versions = wanted or list(blenders)
    results = {}
    for v in versions:
        if v not in blenders:
            print("Blender %s is not installed" % v)
            results[v] = False
            continue
        results[v] = gui(v, blenders[v]) if "--gui" in sys.argv else smoke(v, blenders[v])
    print()
    for v, ok in results.items():
        print("Blender %s: %s" % (v, "OK" if ok else "FAILED"))
    sys.exit(0 if results and all(results.values()) else 1)


if __name__ == "__main__":
    main()
