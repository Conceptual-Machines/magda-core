"""Machine and revision metadata for native application qualification."""

import hashlib
import os
import platform
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def command_output(command, timeout=10):
    try:
        return subprocess.run(command, capture_output=True, text=True,
                              timeout=timeout).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        return ""


def cpu_brand():
    system = platform.system()
    if system == "Darwin":
        return command_output(["sysctl", "-n", "machdep.cpu.brand_string"])
    if system == "Linux":
        try:
            for line in Path("/proc/cpuinfo").read_text().splitlines():
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
        except OSError:
            pass
    return platform.processor()


def memory_bytes():
    system = platform.system()
    if system == "Darwin":
        value = command_output(["sysctl", "-n", "hw.memsize"])
        return int(value) if value.isdigit() else 0
    if system == "Linux":
        try:
            for line in Path("/proc/meminfo").read_text().splitlines():
                if line.startswith("MemTotal:"):
                    return int(line.split()[1]) * 1024
        except OSError:
            pass
    if system == "Windows":
        import ctypes

        class MemoryStatus(ctypes.Structure):
            _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                        ("ullTotalPhys", ctypes.c_ulonglong),
                        ("ullAvailPhys", ctypes.c_ulonglong),
                        ("ullTotalPageFile", ctypes.c_ulonglong),
                        ("ullAvailPageFile", ctypes.c_ulonglong),
                        ("ullTotalVirtual", ctypes.c_ulonglong),
                        ("ullAvailVirtual", ctypes.c_ulonglong),
                        ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]

        status = MemoryStatus()
        status.dwLength = ctypes.sizeof(MemoryStatus)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            return status.ullTotalPhys
    return 0


def describe_machine():
    """What the machine is, and the key its history is filed under.

    The key hashes the hardware only, so an OS update keeps the history it is compared with;
    the OS is recorded beside every run so a step in the numbers can be read against it.
    """
    machine = {
        "host": platform.node().split(".")[0],
        "system": platform.system(),
        "release": platform.release(),
        "arch": platform.machine(),
        "cpu": cpu_brand(),
        "cores": os.cpu_count() or 0,
        "memory_gb": round(memory_bytes() / (1024 ** 3), 1),
    }
    hardware = "%s|%s|%d|%s" % (machine["cpu"], machine["arch"], machine["cores"],
                                machine["memory_gb"])
    slug = re.sub(r"[^A-Za-z0-9]+", "-", machine["host"]).strip("-").lower() or "machine"
    machine["id"] = "%s-%s" % (slug, hashlib.sha1(hardware.encode()).hexdigest()[:8])
    return machine


def git_state():
    commit = command_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"])
    dirty = bool(command_output(["git", "-C", str(ROOT), "status", "--porcelain", "-uno"]))
    return commit, dirty
