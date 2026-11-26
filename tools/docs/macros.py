"""Expose the single SDK version source to Zensical's built-in macros extension."""
from pathlib import Path


def define_env(env):
    env.variables["sdk_version"] = (Path(__file__).resolve().parents[2] / "version.txt").read_text().strip()
