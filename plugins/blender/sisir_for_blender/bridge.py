"""Finding Sisir and talking to SisirBridge.exe (plugins/blender/bridge/SisirBridge.cpp).

Sisir.exe is found the way plugins/common/Baker.cpp finds it -- an explicit path, then the Sisir
started last (the .sir file association Sisir rewrites on every launch), then the one remembered
from last time or from installing the add-on -- and SisirBridge.exe is the one next to it.

A Session runs one bridge command. Its output is read on a thread into a queue, so Blender's
event loop never blocks on the pipe; `lines()` drains what has arrived.
"""

import ctypes
import os
import queue
import subprocess
import threading
from ctypes import wintypes

from . import options as _options

PROTOCOL = 1
BRIDGE_EXE = "SisirBridge.exe"


class BridgeError(Exception):
    pass


def _registry_open_command():
    try:
        import winreg
    except ImportError:
        return ""
    for root, path in ((winreg.HKEY_CURRENT_USER, r"Software\Classes\Sisir.Scene\shell\open\command"),
                       (winreg.HKEY_CLASSES_ROOT, r"Sisir.Scene\shell\open\command")):
        try:
            with winreg.OpenKey(root, path) as key:
                command, _ = winreg.QueryValueEx(key, "")
        except OSError:
            continue
        command = str(command).strip()
        if command.startswith('"'):
            exe = command[1:].split('"', 1)[0]
        else:
            exe = command.split(" ", 1)[0]
        if os.path.isfile(exe):
            return exe
    return ""


def find_sisir(explicit=""):
    """Sisir.exe paths worth trying, best first (as Baker::findSisir orders them)."""
    out = []
    for exe in (explicit, _registry_open_command(), _options.remembered_sisir()):
        if exe and os.path.isfile(exe) and os.path.normcase(os.path.abspath(exe)) not in (os.path.normcase(e) for e in out):
            out.append(os.path.abspath(exe))
    return out


def find_bridge(explicit=""):
    """(SisirBridge.exe, Sisir.exe), or raises BridgeError with what to do about it."""
    candidates = find_sisir(explicit)
    for exe in candidates:
        bridge = os.path.join(os.path.dirname(exe), BRIDGE_EXE)
        if os.path.isfile(bridge):
            return bridge, exe
    if candidates:
        raise BridgeError("this Sisir is too old to import into Blender: update Sisir")
    raise BridgeError("Sisir was not found on this computer")


def remember_sisir(exe):
    try:
        import winreg
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, _options.SETTINGS_KEY) as key:
            winreg.SetValueEx(key, "sisirExecutable", 0, winreg.REG_SZ, os.path.normpath(exe))
    except (ImportError, OSError):
        pass


# --- Blender's windows ---------------------------------------------------------------------------

_user32 = ctypes.WinDLL("user32", use_last_error=True) if os.name == "nt" else None
if _user32:
    _EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    _user32.EnumWindows.argtypes = [_EnumProc, wintypes.LPARAM]
    _user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    _user32.GetWindowThreadProcessId.restype = wintypes.DWORD
    _user32.IsWindowVisible.argtypes = [wintypes.HWND]
    _user32.IsWindowEnabled.argtypes = [wintypes.HWND]
    _user32.EnableWindow.argtypes = [wintypes.HWND, wintypes.BOOL]
    _user32.GetForegroundWindow.restype = wintypes.HWND
    _user32.AllowSetForegroundWindow.argtypes = [wintypes.DWORD]


def blender_windows():
    """This Blender's visible top-level windows, the foreground one first."""
    if not _user32:
        return []
    pid = os.getpid()
    found = []

    def visit(hwnd, _):
        owner = wintypes.DWORD()
        _user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and _user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    _user32.EnumWindows(_EnumProc(visit), 0)
    front = _user32.GetForegroundWindow()
    found.sort(key=lambda h: 0 if h == front else 1)
    return found


class ModalLock:
    """Blender's windows disabled while a bridge dialog is up, as a modal dialog of its own would.

    Blender, not the bridge, turns them back on, so a bridge that dies can never leave Blender
    locked; `release` is safe to call any number of times."""

    _active = []

    def __init__(self):
        self.windows = [h for h in blender_windows() if _user32.IsWindowEnabled(h)]
        for h in self.windows:
            _user32.EnableWindow(h, False)
        ModalLock._active.append(self)

    @property
    def owner(self):
        return self.windows[0] if self.windows else 0

    def release(self):
        for h in self.windows:
            _user32.EnableWindow(h, True)
        self.windows = []
        if self in ModalLock._active:
            ModalLock._active.remove(self)

    @classmethod
    def release_all(cls):
        for lock in list(cls._active):
            lock.release()


# --- Sessions ------------------------------------------------------------------------------------

class Session:
    def __init__(self, bridge, args):
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        try:
            self.process = subprocess.Popen([bridge] + list(args), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                            stderr=subprocess.DEVNULL, creationflags=flags)
        except OSError as e:
            raise BridgeError("cannot start %s: %s" % (bridge, e))
        if _user32:
            # Lets the bridge's dialog come to the front over Blender.
            _user32.AllowSetForegroundWindow(self.process.pid)
        self._queue = queue.Queue()
        self._handshake = None
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for raw in self.process.stdout:
            self._queue.put(raw.decode("utf-8", "replace").rstrip("\r\n"))
        self._queue.put(None)  # end of output

    def lines(self, timeout=None):
        """What the bridge said since the last call. None in the list means it has exited."""
        out = []
        try:
            out.append(self._queue.get(timeout=timeout) if timeout else self._queue.get_nowait())
            while True:
                out.append(self._queue.get_nowait())
        except queue.Empty:
            pass
        checked = []
        for line in out:
            if self._handshake is None and line is not None:
                self._handshake = line
                if line != "SISIR_BRIDGE %d" % PROTOCOL:
                    raise BridgeError("this Sisir does not match its Blender add-on: update the add-on from Sisir's Plugins menu")
                continue
            checked.append(line)
        return checked

    def send(self, line):
        try:
            self.process.stdin.write((line.replace("\n", " ") + "\n").encode("utf-8"))
            self.process.stdin.flush()
        except (OSError, ValueError):
            pass

    def close(self, wait=2.0, kill=False):
        """Ends the session: the bridge sees its input close and finishes. One still showing a
        message (a failed build's reason) is left to close it; `kill` stops it outright."""
        try:
            self.process.stdin.close()
        except (OSError, ValueError):
            pass
        if kill:
            self.process.kill()
        try:
            self.process.wait(timeout=wait)
        except subprocess.TimeoutExpired:
            threading.Thread(target=self.process.wait, daemon=True).start()


def run(bridge, args, on_line=None):
    """A bridge command start to finish, without Blender's event loop (scripted use): returns the
    lines it printed; `on_line` sees each as it arrives and may return False to cancel."""
    session = Session(bridge, args)
    out = []
    try:
        while True:
            for line in session.lines(timeout=0.25):
                if line is None:
                    return out
                out.append(line)
                if on_line is not None and on_line(line) is False:
                    session.send("CANCEL")
    finally:
        session.close()
