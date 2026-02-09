Import("env")

import os
import subprocess


def _git_short_hash(project_dir):
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "--short=12", "HEAD"],
            cwd=project_dir,
            stderr=subprocess.DEVNULL,
        )
        return out.decode("utf-8", errors="ignore").strip()
    except Exception:
        return ""


def _is_dirty(project_dir):
    try:
        out = subprocess.check_output(
            ["git", "status", "--porcelain"],
            cwd=project_dir,
            stderr=subprocess.DEVNULL,
        )
        return bool(out.decode("utf-8", errors="ignore").strip())
    except Exception:
        return False


project_dir = env.subst("$PROJECT_DIR")
build_id = _git_short_hash(project_dir) if project_dir else ""
if not build_id:
    build_id = "nogit"
if _is_dirty(project_dir):
    build_id += "-dirty"

env.Append(CPPDEFINES=[("MC_BUILD_ID", '\\"%s\\"' % build_id)])
