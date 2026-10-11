# PlatformIO pre-build script: runs prepare.py with the custom_dsp_* options of
# platformio.ini and adds its include folders.
import os
import subprocess
import sys

Import("env")  # noqa: F821  (SCons)

here = env.subst("$PROJECT_DIR")  # noqa: F821


def option(name):
    return env.GetProjectOption(name, "").strip()  # noqa: F821


cmd = [sys.executable, os.path.join(here, "prepare.py")]
if option("custom_dsp_drivers"):
    cmd += ["--drivers", option("custom_dsp_drivers")]
if option("custom_dsp_embed_rom"):
    cmd += ["--embed-game", option("custom_dsp_embed_game"), "--embed-rom", option("custom_dsp_embed_rom")]
    for kind in ("tape", "disk", "cart"):
        for f in option(f"custom_dsp_embed_{kind}").split(";"):
            if f.strip():
                cmd += [f"--embed-{kind}", f.strip()]
    for a in option("custom_dsp_embed_args").split(";"):
        if a.strip():
            cmd += [f"--embed-arg={a.strip()}"]
subprocess.check_call(cmd)
flags = subprocess.check_output([sys.executable, os.path.join(here, "prepare.py"), "--print-flags"]).decode().split()
env.Append(CPPPATH=[f[2:] for f in flags if f.startswith("-I")])  # noqa: F821
