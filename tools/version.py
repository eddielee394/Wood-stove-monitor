# PlatformIO pre-build script: stamps the firmware with `git describe`.
#   v0.6.0               a release build (tagged commit, clean tree)
#   v0.6.0-3-g8d07049    3 commits after v0.6.0
#   v0.6.0-dirty         uncommitted changes
# Written to include/version_gen.h (gitignored) only when it changes, so only
# src/version.cpp recompiles.
import os
import subprocess

Import("env")  # noqa: F821 (provided by PlatformIO)

root = env["PROJECT_DIR"]  # noqa: F821


def describe():
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--match", "v[0-9]*", "--dirty", "--always"],
            cwd=root, stderr=subprocess.DEVNULL, text=True).strip()
    except Exception:
        return "unknown"


v = describe()
if not v.startswith("v"):  # no release tag yet: just the commit hash
    v = "v0.0.0-g" + v
path = os.path.join(root, "include", "version_gen.h")
text = '#pragma once\n#define FW_VERSION "%s"\n' % v
old = open(path).read() if os.path.exists(path) else ""
if old != text:
    with open(path, "w") as f:
        f.write(text)
print("Firmware version: " + v)
