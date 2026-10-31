"""Build Sisir for 3ds Max for every 3ds Max SDK installed here, then refresh Sisir's package setup.

  python tools/build_max_plugins.py            # build every SDK found
  python tools/build_max_plugins.py 2022 2026  # only these
  python tools/build_max_plugins.py --test     # and run the smoke test in each installed 3ds Max

Each version builds into build_max<version> (Release, see plugins/max/CMakeLists.txt). Sisir's own
build is then configured again, so the package and build/Release/DCC carry every version. Uses the
.venv's cmake when there is one (the one on PATH is a broken shim on Sisir's build machine), else
cmake from PATH.

--test needs Sisir.exe (SISIR_EXE, else the Sisir build build/Release/Sisir.exe) and a scene
(SISIR_SMOKE_SCENE, else D:/Sample/Autumn_08.sir). 3ds Max has no licence-free headless mode
(3dsmaxbatch fails its licence check), so the test runs in a real 3ds Max window: close 3ds Max
first, and do not click in the window while it runs. It quits by itself;
tools/max_plugin_smoke.py writes <out>/smoke.log.

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


def installed_sdks():
    found = []
    for d in autodesk.glob("3ds Max 20[0-9][0-9] SDK"):
        if (d / "maxsdk" / "include" / "max.h").is_file():
            found.append(re.search(r"\d{4}", d.name).group(0))
    return sorted(found)


def run(args, **kw):
    print(">", " ".join(str(a) for a in args), flush=True)
    return subprocess.run([str(a) for a in args], cwd=root, **kw)


def max_running():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq 3dsmax.exe", "/NH"], capture_output=True, text=True).stdout
    return "3dsmax.exe" in out.lower()


def smoke(version, build):
    exe = autodesk / ("3ds Max " + version) / "3dsmax.exe"
    if not exe.is_file():
        print("3ds Max %s is not installed: built only" % version)
        return True
    if max_running():
        print("3ds Max is running: close it first (its licence is held by the open session)")
        return False
    scene = os.environ.get("SISIR_SMOKE_SCENE", "D:/Sample/Autumn_08.sir")
    out = Path(tempfile.gettempdir()) / ("sisir_max_smoke_" + version)
    out.mkdir(parents=True, exist_ok=True)
    log = out / "smoke.log"
    if log.exists():
        log.unlink()
    env = dict(os.environ, SISIR_MAX_SMOKE="|".join(str(p) for p in (
        build / "bundle" / "SisirForMax.bundle", scene, sisir_exe, out)))
    script = str(root / "tools" / "max_plugin_smoke.py").replace("\\", "/")
    try:
        run([exe, "-silent", "-mxs", 'python.ExecuteFile @"%s"' % script], env=env, timeout=1800)
    except subprocess.TimeoutExpired:
        print("3ds Max did not finish within 30 minutes")
    text = log.read_text(encoding="utf-8") if log.exists() else ""
    print(text)
    return "RESULT OK" in text


def main():
    test = "--test" in sys.argv
    wanted = [a for a in sys.argv[1:] if not a.startswith("--")]
    versions = wanted or installed_sdks()
    if not versions:
        sys.exit("No 3ds Max SDK found in %s" % autodesk)
    results = {}
    for v in versions:
        build = root / ("build_max" + v)
        ok = run([cmake, "-S", root / "plugins" / "max", "-B", build, "-DMAX_VERSION=" + v]).returncode == 0
        ok = ok and run([cmake, "--build", build, "--config", "Release"]).returncode == 0
        if ok and test:
            ok = smoke(v, build)
        results[v] = ok
    # Sisir picks the builds up at configure time.
    if not standalone:
        run([cmake, "-S", root, "-B", root / "build"])
        run([cmake, "--build", root / "build", "--config", "Release"])
    print()
    for v, ok in results.items():
        print("3ds Max %s: %s" % (v, "OK" if ok else "FAILED"))
    sys.exit(0 if all(results.values()) else 1)


if __name__ == "__main__":
    main()
