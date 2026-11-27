"""Build-owned tool paths used by the generator's runtime tests."""
import os
from pathlib import Path


def tool_path(name: str) -> Path:
    value = os.environ.get(name)
    if not value:
        raise RuntimeError(f"Missing {name}; run these tests through jvm/gradlew check")
    path = Path(value)
    if not path.exists():
        raise RuntimeError(f"Build tool input does not exist: {name}={path}")
    return path


def java_tools() -> tuple[Path, Path]:
    return tool_path("OWH_JAVAC_EXECUTABLE"), tool_path("OWH_JAVA_EXECUTABLE")


def kotlin_classpath() -> tuple[Path, ...]:
    value = os.environ.get("OWH_KOTLIN_COMPILER_CLASSPATH")
    if not value:
        raise RuntimeError("Missing Kotlin compiler classpath; run jvm/gradlew check")
    jars = tuple(Path(item) for item in value.split(os.pathsep))
    stdlib = next((path for path in jars if path.name.startswith("kotlin-stdlib-")), None)
    if not stdlib or not all(path.is_file() for path in jars):
        raise RuntimeError("Incomplete build-provided Kotlin compiler classpath")
    return stdlib, *(path for path in jars if path != stdlib)
