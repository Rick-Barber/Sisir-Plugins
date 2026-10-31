"""Build Sisir for Maya for every Maya installed here, then refresh Sisir's package setup.

  python tools/build_maya_plugins.py            # build every Maya found
  python tools/build_maya_plugins.py 2024 2026  # only these
  python tools/build_maya_plugins.py --test     # and run the smoke test on each

Each version builds into build_maya<version> (Release, see plugins/maya/CMakeLists.txt). Sisir's
own build is then configured again, so the package and build/Release/DCC carry every version.
Uses the .venv's cmake when there is one (the one on PATH is a broken shim on Sisir's build
machine), else cmake from PATH. --test needs Sisir.exe (SISIR_EXE, else the Sisir build
build/Release/Sisir.exe) and a scene (SISIR_SMOKE_SCENE, else D:/Sample/Autumn_08.sir).

In the public Sisir-Plugins repo there is no Sisir source: the plug-in builds are the whole job,
and --test needs SISIR_EXE (an installed Sisir) and SISIR_SMOKE_SCENE.
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
autodesk = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Autodesk"


def installed_mayas():
    found = []
    for d in autodesk.glob("Maya20[0-9][0-9]"):
        if (d / "include" / "maya" / "MFnPlugin.h").is_file():
            found.append(re.search(r"\d{4}", d.name).group(0))
    return sorted(found)


def run(args, **kw):
    print(">", " ".join(str(a) for a in args), flush=True)
    return subprocess.run([str(a) for a in args], cwd=root, **kw)


def main():
    test = "--test" in sys.argv
    wanted = [a for a in sys.argv[1:] if not a.startswith("--")]
    versions = wanted or installed_mayas()
    if not versions:
        sys.exit("No Maya with devkit headers found in %s" % autodesk)
    results = {}
    for v in versions:
        build = root / ("build_maya" + v)
        ok = run([cmake, "-S", root / "plugins" / "maya", "-B", build, "-DMAYA_VERSION=" + v]).returncode == 0
        ok = ok and run([cmake, "--build", build, "--config", "Release"]).returncode == 0
        if ok and test:
            scene = os.environ.get("SISIR_SMOKE_SCENE", "D:/Sample/Autumn_08.sir")
            out = Path(tempfile.gettempdir()) / ("sisir_smoke_" + v)
            mayapy = autodesk / ("Maya" + v) / "bin" / "mayapy.exe"
            # An empty Maya profile: an installed Sisir for Maya would otherwise load itself.
            env = dict(os.environ, MAYA_APP_DIR=tempfile.mkdtemp(prefix="sisir_maya_profile_"))
            ok = run([mayapy, root / "tools" / "maya_plugin_smoke.py", build / "module" / "SisirForMaya",
                      scene, sisir_exe, out], env=env).returncode == 0
        results[v] = ok
    # Sisir picks the builds up at configure time.
    if not standalone:
        run([cmake, "-S", root, "-B", root / "build"])
        run([cmake, "--build", root / "build", "--config", "Release"])
    print()
    for v, ok in results.items():
        print("Maya %s: %s" % (v, "OK" if ok else "FAILED"))
    sys.exit(0 if all(results.values()) else 1)


if __name__ == "__main__":
    main()
