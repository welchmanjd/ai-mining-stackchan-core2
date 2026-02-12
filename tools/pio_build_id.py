Import("env")

import os
import subprocess


def _run_git(project_dir, args):
    try:
        out = subprocess.check_output(
            ["git"] + args,
            cwd=project_dir,
            stderr=subprocess.DEVNULL,
        )
        return out.decode("utf-8", errors="ignore").strip()
    except Exception:
        return ""


def _git_short_hash(project_dir):
    return _run_git(project_dir, ["rev-parse", "--short=12", "HEAD"])


def _is_dirty(project_dir):
    out = _run_git(project_dir, ["status", "--porcelain"])
    return bool(out)


def _git_version(project_dir):
    tag = _run_git(project_dir, ["describe", "--tags", "--exact-match", "HEAD"])
    if tag:
        return tag[1:] if tag.startswith("v") else tag

    nearest = _run_git(project_dir, ["describe", "--tags", "--abbrev=0"])
    if nearest:
        nearest_norm = nearest[1:] if nearest.startswith("v") else nearest
        count = _run_git(project_dir, ["rev-list", f"{nearest}..HEAD", "--count"])
        if count and count != "0":
            return f"{nearest_norm}-dev.{count}"
        return nearest_norm

    return "0.0.0-dev"


project_dir = env.subst("$PROJECT_DIR")
build_id = _git_short_hash(project_dir) if project_dir else ""
if not build_id:
    build_id = "nogit"
if _is_dirty(project_dir):
    build_id += "-dirty"

app_version = _git_version(project_dir) if project_dir else "0.0.0-dev"
if _is_dirty(project_dir):
    app_version += "-dirty"

env.Append(CPPDEFINES=[("MC_BUILD_ID", '\\"%s\\"' % build_id)])
env.Append(CPPDEFINES=[("MC_APP_VERSION", '\\"%s\\"' % app_version)])
