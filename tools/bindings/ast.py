"""Small, fail-closed libclang reader for the public C data declarations."""
from __future__ import annotations

import ctypes as C
import os
from pathlib import Path
import re
import tempfile


class Cursor(C.Structure):
    _fields_ = [("kind", C.c_int), ("xdata", C.c_int), ("data", C.c_void_p * 3)]


class CXString(C.Structure):
    _fields_ = [("data", C.c_void_p), ("private_flags", C.c_uint)]


class CXType(C.Structure):
    _fields_ = [("kind", C.c_int), ("data", C.c_void_p * 2)]


class CXToken(C.Structure):
    _fields_ = [("int_data", C.c_uint * 4), ("ptr_data", C.c_void_p)]


def load_api(root: Path, runtime: Path | None = None):
    if runtime is None:
        configured = os.environ.get("OWH_JEXTRACT_RUNTIME")
        if not configured:
            raise RuntimeError("Pass the jextract runtime path or run generation/tests through Gradle")
        runtime = Path(configured)
    dll_dir = runtime / "bin"
    dll = dll_dir / "libclang.dll"
    if not dll.is_file():
        raise RuntimeError(f"Pinned jextract libclang is missing: {dll}; run JVM binding setup first")
    os.add_dll_directory(str(dll_dir))
    lib = C.CDLL(str(dll))

    def fn(name, args, result):
        value = getattr(lib, name)
        value.argtypes, value.restype = args, result
        return value

    cstring = fn("clang_getCString", [CXString], C.c_char_p)
    dispose_string = fn("clang_disposeString", [CXString], None)

    def string(value):
        ptr = cstring(value)
        result = ptr.decode("utf-8", "replace") if ptr else ""
        dispose_string(value)
        return result

    api = {
        "lib": lib, "string": string,
        "create_index": fn("clang_createIndex", [C.c_int, C.c_int], C.c_void_p),
        "parse": fn("clang_parseTranslationUnit", [C.c_void_p, C.c_char_p,
                    C.POINTER(C.c_char_p), C.c_int, C.c_void_p, C.c_uint, C.c_uint], C.c_void_p),
        "tu_cursor": fn("clang_getTranslationUnitCursor", [C.c_void_p], Cursor),
        "visit": fn("clang_visitChildren", [Cursor, C.CFUNCTYPE(C.c_uint, Cursor, Cursor, C.c_void_p), C.c_void_p], C.c_uint),
        "spelling": fn("clang_getCursorSpelling", [Cursor], CXString),
        "kind_spelling": fn("clang_getCursorKindSpelling", [C.c_int], CXString),
        "cursor_type": fn("clang_getCursorType", [Cursor], CXType),
        "type_spelling": fn("clang_getTypeSpelling", [CXType], CXString),
        "type_size": fn("clang_Type_getSizeOf", [CXType], C.c_longlong),
        "type_kind": fn("clang_getCanonicalType", [CXType], CXType),
        "canonical_kind": fn("clang_getCanonicalType", [CXType], CXType),
        "array_size": fn("clang_getArraySize", [CXType], C.c_longlong),
        "element_type": fn("clang_getArrayElementType", [CXType], CXType),
        "field_offset": fn("clang_Cursor_getOffsetOfField", [Cursor], C.c_longlong),
        "enum_signed": fn("clang_getEnumConstantDeclValue", [Cursor], C.c_longlong),
        "enum_unsigned": fn("clang_getEnumConstantDeclUnsignedValue", [Cursor], C.c_ulonglong),
        "diagnostic_count": fn("clang_getNumDiagnostics", [C.c_void_p], C.c_uint),
        "diagnostic": fn("clang_getDiagnostic", [C.c_void_p, C.c_uint], C.c_void_p),
        "format_diagnostic": fn("clang_formatDiagnostic", [C.c_void_p, C.c_uint], CXString),
    }
    api["cursor_name"] = lambda c: string(api["spelling"](c))
    api["type_name"] = lambda t: string(api["type_spelling"](t))
    return api, runtime


def annotation(api, cursor) -> str:
    parts: list[str] = []
    visitor_type = C.CFUNCTYPE(C.c_uint, Cursor, Cursor, C.c_void_p)

    @visitor_type
    def visit(child, parent, data):
        # CXCursor_AnnotateAttr is 406 in the pinned libclang C API.
        if child.kind == 406:
            value = api["cursor_name"](child)
            if value:
                parts.append(value)
        return 1

    api["visit"](cursor, visit, None)
    return ";".join(parts)


def parse(root: Path, headers: list[Path], runtime: Path | None = None) -> dict:
    api, runtime = load_api(root, runtime)
    options = ["-x", "c", "-std=c17", "-target", "x86_64-pc-windows-msvc",
               "-I" + str(root / "include"), "-isystem", str(runtime / "conf/jextract")]
    argv = (C.c_char_p * len(options))(*(x.encode() for x in options))
    idx = api["create_index"](0, 0)
    with tempfile.TemporaryDirectory(prefix="owh-ast-") as directory:
        translation = Path(directory) / "types.c"
        translation.write_text("\n".join(f'#include "{h.resolve().as_posix()}"' for h in headers), encoding="utf-8")
        tu = api["parse"](idx, str(translation).encode(), argv, len(options), None, 0, 0)
    if not tu:
        raise RuntimeError("libclang could not parse the public type headers")
    diagnostics = [api["string"](api["format_diagnostic"](api["diagnostic"](tu, i), 0))
                   for i in range(api["diagnostic_count"](tu))]
    errors = [d for d in diagnostics if d.startswith("error:") or ": error:" in d]
    if errors:
        raise RuntimeError("libclang diagnostics:\n" + "\n".join(errors))

    records, enums = {}, {}
    field_kind, enum_kind, enum_constant_kind = 6, 5, 7
    visitor_type = C.CFUNCTYPE(C.c_uint, Cursor, Cursor, C.c_void_p)

    def walk_children(cursor):
        found = []
        @visitor_type
        def callback(child, parent, data):
            found.append(child)
            return 1
        api["visit"](cursor, callback, None)
        return found

    @visitor_type
    def top(cursor, parent, data):
        if cursor.kind == 2:
            name = api["cursor_name"](cursor)
            meta = annotation(api, cursor)
            if name.startswith("owh_") and api["type_size"](api["cursor_type"](cursor)) >= 0:
                fields = []
                for child in walk_children(cursor):
                    if child.kind != field_kind:
                        continue
                    t = api["cursor_type"](child)
                    canonical = api["canonical_kind"](t)
                    kind = canonical.kind
                    spelling = api["type_name"](t)
                    item = {"name": api["cursor_name"](child), "type": spelling,
                            "canonical_type": api["type_name"](canonical),
                            "kind": kind, "size": api["type_size"](t),
                            "offset_bits": api["field_offset"](child),
                            "metadata": annotation(api, child)}
                    if kind == 112:  # CXType_ConstantArray
                        item["array_count"] = api["array_size"](t)
                        element = api["element_type"](t)
                        item["element_type"] = api["type_name"](element)
                    if kind == 101:
                        item["pointee"] = spelling[:-1].strip()
                    fields.append(item)
                if fields:
                    records[name] = {"name": name, "metadata": meta, "size": api["type_size"](api["cursor_type"](cursor)), "fields": fields}
        elif cursor.kind == enum_kind:
            values = []
            for child in walk_children(cursor):
                if child.kind == enum_constant_kind:
                    signed = api["enum_signed"](child)
                    unsigned = api["enum_unsigned"](child)
                    values.append({"name": api["cursor_name"](child), "value": signed if signed < 0 else unsigned})
            for value in values:
                if value["name"].startswith("OWH_"):
                    enums[value["name"]] = value["value"]
        return 1

    api["visit"](api["tu_cursor"](tu), top, None)
    if not records:
        raise RuntimeError("libclang found no public record definitions")
    return {"records": records, "constants": enums, "diagnostics": diagnostics,
            "target": "x86_64-pc-windows-msvc", "toolchain": str(runtime / "bin/libclang.dll")}


def metadata(text: str) -> dict[str, str | bool]:
    values: dict[str, str | bool] = {}
    for part in text.split(";"):
        if not part:
            continue
        if "=" in part:
            key, value = part.split("=", 1)
            values[key.strip()] = value.strip()
        else:
            values[part.strip()] = True
    return values


def camel(name: str) -> str:
    words = name.split("_")
    value = words[0] + "".join(word[:1].upper() + word[1:] for word in words[1:])
    return value if not value[:1].isdigit() else "value" + value


def java_type(record_by_c: dict[str, str], field: dict, for_input=False) -> str:
    kind = field["kind"]
    if kind == 112:
        elem = field["element_type"].replace("const ", "").strip()
        if elem in {"unsigned char", "uint8_t", "char", "int8_t"}:
            return "ByteView" if not for_input else "byte[]"
        raise ValueError(f"Unsupported fixed array element type {elem} in {field['name']}")
    if kind == 101:
        md = metadata(field["metadata"])
        if "span" in md or "cstring" in md:
            pointee = field.get("pointee", "").replace("const ", "").strip().removeprefix("struct ")
            byte_span = "span" in md and pointee == "void"
            if pointee not in {"unsigned char", "uint8_t", "char", "int8_t"} and not byte_span:
                kind_name = "span" if "span" in md else "cstring"
                raise ValueError(f"Unsupported {kind_name} element type {pointee} in {field['name']}")
            return "ByteView" if not for_input else "byte[]"
        if md.get("pointer") == "borrowed":
            if not for_input and md.get("copy") != "clear":
                raise ValueError(f"Output borrowed pointer {field['name']} needs copy=clear metadata")
            return "MemorySegment"
        reason = "output pointer lacks a copy/owner annotation" if not for_input and md.get("pointer") else "Unannotated pointer"
        raise ValueError(f"{reason} {field['type']} field {field['name']}")
    spelling = field.get("canonical_type", field["type"]).replace("const ", "").strip()
    mapping = {"uint8_t": "int", "int8_t": "byte", "uint16_t": "int", "int16_t": "short",
               "uint32_t": "long", "int32_t": "int", "uint64_t": "long", "int64_t": "long",
               "unsigned char": "int", "signed char": "byte", "char": "byte",
               "unsigned short": "int", "short": "short", "unsigned int": "long", "int": "int",
               "unsigned long long": "long", "long long": "long", "float": "float", "double": "double"}
    if spelling in mapping:
        return mapping[spelling]
    base = spelling.removeprefix("struct ")
    if base in record_by_c:
        return record_by_c[base]
    raise ValueError(f"Unsupported C field type {field['type']} ({field['name']})")
