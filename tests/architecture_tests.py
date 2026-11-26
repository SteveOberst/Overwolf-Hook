"""Check source, public-header, and configured CMake dependency boundaries."""
from argparse import ArgumentParser
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
MODULES = ("core", "platform", "ipc", "overwolf", "client", "agent")
FORBIDDEN = {
    "core": ("owh/ipc/", "owh/platform/", "owh/overwolf/", "overwolf_hook/abi/", "windows.h", "boost/"),
    "ipc": ("owh/client/", "owh/agent/", "owh/overwolf/", "overwolf_hook/abi/"),
    "overwolf": ("owh/core/service_contracts.hpp", "owh/core/services.hpp", "owh/ipc/", "overwolf_hook/abi/", "owh/client/", "owh/agent/"),
    "platform": ("owh/ipc/", "owh/overwolf/", "overwolf_hook/abi/"),
    "client": ("owh/agent/",),
    "agent": ("owh/client/", "overwolf_hook/abi/"),
}
PUBLIC_PRIVATE_MODULES = {"core", "platform", "ipc", "overwolf", "client", "agent"}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
PIXEL_OWNERSHIP = re.compile(
    r"\b(?:PixelConsumer|PixelRead|try_take_ready|has_ready|settle_for_close|mark_lost|"
    r"complete_read|pending_read|pixel_read_lease)\b"
)
ALLOWED_MODULE_LINKS = {
    "core": {"core"},
    "platform": {"core", "platform"},
    "ipc": {"core", "platform", "ipc"},
    "overwolf": {"core", "platform", "overwolf"},
    "client": {"core", "platform", "ipc", "overwolf", "client"},
    "agent": {"core", "platform", "ipc", "overwolf", "agent"},
}


def module_for(path: Path):
    try:
        relative = path.resolve().relative_to((ROOT / "modules").resolve())
    except ValueError:
        return None
    return relative.parts[0] if relative.parts and relative.parts[0] in MODULES else None


def resolve_include(source: Path, name: str):
    """Resolve quoted/relative paths and repository include-root spellings."""
    candidate = (source.parent / name).resolve()
    if candidate.exists():
        return candidate
    for base in (ROOT / "include", *(ROOT / "modules" / m / "include" for m in MODULES)):
        candidate = (base / name).resolve()
        if candidate.exists():
            return candidate
    return None


def source_checks(failures):
    for module in MODULES:
        base = ROOT / "modules" / module
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.suffix not in {".h", ".hpp", ".cpp", ".cc", ".cxx"}:
                continue
            try:
                lines = path.read_text(encoding="utf-8").splitlines()
            except UnicodeDecodeError:
                continue
            for number, line in enumerate(lines, 1):
                match = INCLUDE.match(line)
                if not match:
                    continue
                name = match.group(1).replace("\\", "/")
                resolved = resolve_include(path, match.group(1))
                destination_module = module_for(resolved) if resolved else None
                forbidden = any(part in name for part in FORBIDDEN[module])
                if destination_module and destination_module not in ALLOWED_MODULE_LINKS[module]:
                    forbidden = True
                if forbidden:
                    failures.append(
                        f"{path.relative_to(ROOT)}:{number}: {name} violates {module} dependency boundary"
                    )

    # Domain-side client code may consume the facade, but not call back through
    # the low-level adapter that implements it.
    client_sources = ROOT / "modules/client/src"
    if client_sources.exists():
        for path in client_sources.rglob("*.cpp"):
            if "abi" in path.relative_to(client_sources).parts:
                continue
            if re.search(r"#\s*include\s*[<\"]overwolf_hook/abi/", path.read_text(encoding="utf-8")):
                failures.append(f"{path.relative_to(ROOT)}: domain implementation includes the C adapter")

    # Public C/C++ facade headers cannot expose private module contracts.
    public = ROOT / "include/overwolf_hook"
    public_headers = list(public.rglob("*.h")) + list(public.rglob("*.hpp")) if public.exists() else []
    for path in public_headers:
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            match = INCLUDE.match(line)
            if not match:
                continue
            resolved = resolve_include(path, match.group(1))
            destination = module_for(resolved) if resolved else None
            if destination in PUBLIC_PRIVATE_MODULES:
                failures.append(
                    f"{path.relative_to(ROOT)}:{number}: public facade includes private {destination} header"
                )

    # Keep the ownership rule semantic: Overwolf may publish a PixelView, but
    # must not own the agent's consumer, read lease, queue, or settlement.
    for base in (ROOT / "modules/overwolf/include", ROOT / "modules/overwolf/src"):
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.suffix not in {".h", ".hpp", ".cpp"}:
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                match = PIXEL_OWNERSHIP.search(line)
                if match:
                    failures.append(
                        f"{path.relative_to(ROOT)}:{number}: {match.group(0)} crosses Pixel ownership into Overwolf"
                    )

    for domain in ("overlays/overlay_domain", "events/event_hub"):
        for suffix in (".hpp", ".cpp"):
            path = ROOT / "modules/agent/src" / (domain + suffix)
            text = path.read_text(encoding="utf-8")
            if "service_contracts.hpp" in text or re.search(r"core::[A-Za-z]+Response\b", text):
                failures.append(f"{path.relative_to(ROOT)}: RPC envelope crosses the domain boundary")

    for name in ("overlays", "events", "session"):
        catalog = ROOT / "modules/core/include/owh/core" / f"{name}.def"
        if not catalog.is_file():
            failures.append(f"Missing canonical {name} service catalog")


ALLOWED_PRODUCT_LINKS = {
    "core": set(),
    "platform": {"core"},
    "ipc": {"core", "platform"},
    "overwolf": {"core", "platform", "overwolf"},
    "client": {"core", "platform", "ipc", "overwolf"},
    "agent": {"core", "platform", "ipc", "overwolf", "agent"},
}


def target_module(name):
    if name in {"owh_core"}:
        return "core"
    product_targets = {
        "owh_platform": "platform",
        "owh_ipc": "ipc",
        "owh_overwolf_passive": "overwolf",
        "owh_overwolf_active": "overwolf",
        "owh_overwolf_fixture": "overwolf",
        "owh_client": "client",
        "owh_client_fixture": "client",
        "owh_agent_core": "agent",
        "owh_agent": "agent",
    }
    return product_targets.get(name)


def cmake_file_api_checks(build_dir: Path, failures):
    reply = build_dir / ".cmake/api/v1/reply"
    indices = sorted(reply.glob("index-*.json"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not indices:
        failures.append(f"No CMake File API codemodel reply in {build_dir}")
        return
    try:
        index = json.loads(indices[0].read_text(encoding="utf-8"))
        codemodel = index["reply"]["codemodel-v2"]
        model = json.loads((reply / codemodel["jsonFile"]).read_text(encoding="utf-8"))
        for config in model["configurations"]:
            ids = {entry["id"]: entry["name"] for entry in config["targets"]}
            objects = {}
            for entry in config["targets"]:
                if not target_module(entry["name"]):
                    continue  # Only product link boundaries belong to this check.
                target_path = reply / entry["jsonFile"]
                objects[entry["id"]] = json.loads(target_path.read_text(encoding="utf-8"))
            for source_id, obj in objects.items():
                source_name = ids[source_id]
                source_module = target_module(source_name)
                if not source_module:
                    continue
                for dependency in obj.get("dependencies", []):
                    destination_name = ids.get(dependency.get("id"))
                    destination_module = target_module(destination_name) if destination_name else None
                    if destination_module and destination_module not in ALLOWED_PRODUCT_LINKS[source_module]:
                        failures.append(
                            f"CMake target {source_name} links disallowed product target {destination_name}"
                        )
    except (KeyError, StopIteration, OSError, json.JSONDecodeError) as error:
        failures.append(f"Could not read CMake File API codemodel: {error}")


def main():
    parser = ArgumentParser()
    parser.add_argument("--build-dir", type=Path, help="configured CMake build tree with codemodel-v2 reply")
    args = parser.parse_args()
    failures = []
    source_checks(failures)
    if args.build_dir:
        cmake_file_api_checks(args.build_dir.resolve(), failures)
    if failures:
        raise AssertionError("\n".join(failures))
    print("PASS resolved native boundaries, facade direction, pixel ownership, and service catalogs")


if __name__ == "__main__":
    main()
