"""Stage C declarations from the public headers; no second type/value model."""
from __future__ import annotations

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]

# Explanations belong to documentation; numeric values and layouts come only
# from the public headers. Fail on a new enum until its meaning is documented.
MEANINGS = {
    "OWH_OK": "The operation succeeded. Read any separate feature/publication result as well.",
    "OWH_NO_DATA": "A nonblocking event poll found no matching record.",
    "OWH_CLOSED": "The resource or stream has closed.",
    "OWH_NOT_FOUND": "The selected installation, process, mapping or resident Agent was not found; meaning depends on the operation.",
    "OWH_BUSY": "No slot/short admission was available, or a state lock deadline expired.",
    "OWH_BUFFER_TOO_SMALL": "A supplied capacity is insufficient.",
    "OWH_INVALID_ARGUMENT": "An argument, size, range or selected identity is invalid.",
    "OWH_ALREADY_EXISTS": "A resource or binding already exists.",
    "OWH_INCOMPATIBLE": "The Client/Agent or transport format is incompatible.",
    "OWH_CORRUPT": "Metadata, bounds or shared state is invalid; a state mutex may have been abandoned.",
    "OWH_SYSTEM_ERROR": "An underlying operation failed; inspect error_domain and native_code.",
    "OWH_RESOURCE_EXHAUSTED": "An allocation or accepted resource limit was exhausted.",
    "OWH_CANCELLED": "This wait ended because its cancellation token was signaled.",
    "OWH_TIMEOUT": "This bounded wait expired; already issued work is not cancelled.",
    "OWH_UNSUPPORTED": "The required path or normal-close mechanism is unsupported.",
    "OWH_AGENT_NOT_READY": "There is no ready attachment/Agent boundary for this operation.",
    "OWH_AGENT_LOST": "The attached Agent/session is no longer reachable.",
    "OWH_STALE_GENERATION": "The source belongs to an obsolete native lifecycle generation.",
    "OWH_DEVICE_LOST": "The graphics device is no longer usable.",
    "OWH_REPLACED": "A queued image was replaced before publication.",
    "OWH_OPT_IN_REQUIRED": "Attach did not enable the experimental path requested by this operation.",
    "OWH_ERROR_NONE": "No underlying native error domain.",
    "OWH_ERROR_WIN32": "native_code contains an unsigned Windows error code.",
    "OWH_ERROR_HRESULT": "native_code contains the unsigned HRESULT bits.",
    "OWH_AGENT_STARTING": "The resident SDK Agent is starting; this is not feature readiness.",
    "OWH_AGENT_READY": "The resident SDK Agent can accept sessions; query feature readiness separately.",
    "OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE": "ClientOptions.experimental_features bit enabling raw D3D11 binding.",
    "OWH_ATTACH_RESTART": "Default attach: request normal exit if needed, relaunch and use the early startup boundary.",
    "OWH_ATTACH_HOT": "Explicit attach to a running Overwolf process; accepts an initial setup race that can crash Overwolf.",
    "OWH_CHECK_HOST_IDENTITY": "The selected process passed Overwolf main-process identity validation.",
    "OWH_CHECK_AGENT_MODULE": "Overwolf's OWAgent.dll was found and its on-disk image could be opened.",
    "OWH_CHECK_AGENT_HASH": "The digest of Overwolf's OWAgent.dll could be read; unknown digests also pass.",
    "OWH_CHECK_COMMON_TARGETS": "Required common native source targets resolved uniquely in the on-disk image.",
    "OWH_CHECK_LIFECYCLE_TARGETS": "Required native lifecycle targets resolved in the on-disk image.",
    "OWH_CHECK_PIXEL_BUFFER_TARGETS": "Required pixel-buffer source targets resolved in the on-disk image.",
    "OWH_CHECK_SHARED_TEXTURE_TARGETS": "Required shared-texture source targets resolved in the on-disk image.",
    "OWH_BUILD_UNREVIEWED": "The readable OWAgent.dll digest is absent from retained review data.",
    "OWH_BUILD_REVIEWED": "The OWAgent.dll digest appears in retained review data; not an Overwolf endorsement or safety guarantee.",
    "OWH_CHANNEL_INFO": "Discovery observed a provider state mapping for this channel.",
    "OWH_CHANNEL_EVENTS": "Discovery observed a provider event mapping for this channel.",
    "OWH_SUBSCRIPTION_STARTING": "Subscription setup is in progress.",
    "OWH_SUBSCRIPTION_ACTIVE": "The subscription is active; a provider can still produce no records.",
    "OWH_SUBSCRIPTION_STOPPING": "Subscription cleanup has started.",
    "OWH_SUBSCRIPTION_LOST": "The subscription lost its source/session.",
    "OWH_SUBSCRIPTION_FAILED": "Subscription setup or operation failed; inspect last_result.",
    "OWH_EVENTS_ALL_CHANNELS": "EventOptions.source_channel default: do not filter by transport channel.",
    "OWH_SUBSCRIPTION_WARNING_UNREVIEWED_CONSUMER": "The OWGameEventsConsumer.dll digest is absent from retained review data; capture is not blocked solely for this warning.",
    "OWH_SUBSCRIPTION_WARNING_HASH_UNAVAILABLE": "The event consumer digest could not be read; inspect warnings separately from subscription state.",
    "OWH_CAP_PIXEL_BUFFER": "Overlay capability bit for complete-image pixel-buffer submission.",
    "OWH_CAP_D3D11_SHARED_TEXTURE": "Overlay capability bit for experimental raw D3D11 shared allocation binding.",
    "OWH_OVERLAY_PIXEL_BUFFER": "OverlayInfo.kind: a source supplied through shared pixel-buffer slots.",
    "OWH_OVERLAY_SHARED_TEXTURE": "OverlayInfo.kind: a source bound to a D3D11 shared allocation.",
    "OWH_QUEUE_FIFO": "Preserve accepted image order; managed submit waits for bounded queue capacity.",
    "OWH_QUEUE_LATEST_READY": "Keep the latest unread image; managed submit replaces pending content without overwriting an in-flight read.",
    "OWH_PIXEL_BGRA8_UNORM": "Four bytes per pixel in B, G, R, A order; straight alpha.",
    "OWH_OVERLAY_AWAITING_TEXTURE": "An experimental source exists but has no bound allocation.",
    "OWH_OVERLAY_READY": "The native source is ready; not proof of visible display.",
    "OWH_OVERLAY_LOST": "The source/native lifecycle was lost; inspect last_result.",
    "OWH_OVERLAY_CLOSING": "Logical close has begun; physical cleanup may be deferred.",
    "OWH_SUBMISSION_PENDING": "Publication has no final outcome yet.",
    "OWH_SUBMISSION_PUBLISHED": "The native publish call succeeded; not proof that the image was displayed.",
    "OWH_SUBMISSION_REJECTED": "Publication failed; inspect the separate result field.",
    "OWH_SUBMISSION_REPLACED": "A newer queued image replaced this image before publication.",
    "OWH_SUBMISSION_LOST": "The source/session was lost before a normal publication outcome.",
    "OWH_STATE_DEFAULT_TIMEOUT_MS": "Default state-lock deadline used by convenience reads, in milliseconds.",
    "OWH_OVERWOLF_PATH_BYTES": "Capacity in bytes of each NUL-terminated UTF-8 Overwolf path array.",
    "OWH_OVERWOLF_VERSION_BYTES": "Capacity in bytes of an Overwolf product-version string array.",
    "OWH_SDK_VERSION_CAPACITY": "Capacity in bytes of the SDK release-version string array.",
}

STAGES = {
    "NONE": "No failure stage", "ARGUMENT": "Input validation",
    "TARGET_DISCOVERY": "Overwolf process discovery/identity",
    "BOOTSTRAP": "Agent loading and startup boundary",
    "HANDSHAKE": "Client/Agent session negotiation",
    "PROFILE_RESOLVE": "Native signature/target resolution",
    "AGENT_LIFETIME": "Native Agent/root lifecycle",
    "CAPABILITY": "Feature readiness", "ALLOCATION": "Resource allocation",
    "WINDOW": "Native source-window work", "SOURCE_REGISTER": "Source registration",
    "SOURCE_PUBLISH": "Native source publication", "D3D11_ADAPTER": "D3D11 adapter selection",
    "D3D11_DEVICE": "D3D device creation", "D3D11_IMPORT": "Shared allocation import",
    "D3D11_VALIDATE": "Texture descriptor validation", "CONTROL_IO": "Service transport",
    "CLEANUP": "Resource cleanup", "WAIT": "A wait operation",
    "OVERWOLF_START": "Overwolf launch", "OVERWOLF_STOP": "Overwolf normal-close request/exit",
}
MEANINGS.update({"OWH_STAGE_" + key: value + "." for key, value in STAGES.items()})


def public_headers():
    for name in (ROOT / "cmake/public-headers.txt").read_text().splitlines():
        if name.strip():
            path = ROOT / "include/overwolf_hook" / name.strip()
            yield name.strip(), path.read_text(encoding="utf-8")


def declarations():
    for path, text in public_headers():
        for match in re.finditer(r"OWH_CLIENT\s+\w+\s+__cdecl\s+(owh_\w+)\s*\([^;]*\);", text):
            yield path, match.group(1), match.group()


def enum_constants(text):
    for block in re.findall(r"enum\s*\{([^}]+)\}", text):
        yield from re.findall(r"(OWH_\w+)\s*=\s*([^,\n]+)", block)


def record_declarations(text):
    # Public records have no nested anonymous structs. Strip compiler metadata.
    for match in re.finditer(r'typedef struct\s+OWH_META\((.*?)\)\s*(owh_\w+)\s*\{(.*?)\}\s*\2;', text, re.S):
        metadata, name, _ = match.groups()
        model = re.search(r'model=(\w+)', metadata)[1]
        declaration = re.sub(r'\s*OWH_META\([^)]*\)', '', match.group())
        declaration = re.sub(r'typedef struct\s+', 'typedef struct ', declaration, count=1)
        yield name, model, declaration


def stage_reference(put):
    base = ROOT / "docs/assets/generated/reference"
    for _, name, declaration in declarations():
        # Keep declaration content exact, but remove source indentation from
        # continuation lines so it remains readable in the documentation column.
        declaration = re.sub(r"\s+", " ", declaration)
        if len(declaration) > 80:
            head, parameters = declaration.split("(", 1)
            arguments = parameters.removesuffix(");").split(",")
            declaration = head + "(\n" + ",\n".join("    " + s.strip() for s in arguments) + ");"
        put(base / (name + ".h"), (declaration + "\n").encode())
    constants = []
    records = []
    for path, text in public_headers():
        if not path.startswith("types/"):
            continue
        rows = []
        for name, value in enum_constants(text):
            if name not in MEANINGS:
                raise ValueError(f"Document the public constant {name} in tools/docs/reference.py")
            rows.append(f"| <a id=\"{name.lower()}\"></a>`{name}` | `{value.strip()}` | {MEANINGS[name]} |")
        if path == "types/common.h":
            rows.extend([
                '| <a id="owh-abi-version"></a>`OWH_ABI_VERSION` | `' + re.search(r"#define OWH_ABI_VERSION (\S+)", text)[1] + '` | C ABI layout/contract version; not the SDK release version. |',
                '| <a id="owh-wait-infinite"></a>`OWH_WAIT_INFINITE` | `UINT32_MAX` | Unbounded event/submission wait; not accepted by state or Overwolf start/stop. |',
            ])
        if rows:
            constants += [f"### `{path}`", "", "| Constant | Value | Meaning |", "| --- | --- | --- |", *rows, ""]
        for name, model, declaration in record_declarations(text):
            records += [f'<a id="{name}"></a>', f"### `{name}` (`{model}`)", "", f"Declared in `{path}`. See the field meanings and defaults in [data records](#data-records).", "", "```c", declaration, "```", ""]
        initializers = re.findall(r'^#define OWH_\w+_INIT[^\n]+', text, re.M)
        if initializers:
            records += [f"Initialization macros from `{path}`:", "", "```c", *initializers, "```", ""]
    put(base / "constants.txt", ("\n".join(constants)).encode())
    put(base / "records.txt", ("\n".join(records)).encode())
