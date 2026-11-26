"""Generate safe Java data views from the public C AST."""
from __future__ import annotations

from pathlib import Path
import re

from .ast import camel, java_type, metadata


PACKAGE = "dev.sxlver.overwolfhook.model"


def render_runtime() -> dict[str, str]:
    return {
        "ValueOwner.java": f'''package {PACKAGE};

import java.lang.foreign.Arena;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;

/** Shared lifetime for generated immutable values and all of their views. */
public final class ValueOwner {{
    private final Arena arena = Arena.ofAuto();
    private ValueOwner() {{}}
    MemorySegment allocateBytes(long bytes, long alignment) {{ return arena.allocate(bytes, alignment); }}
    static Owned allocate(MemoryLayout layout) {{
        ValueOwner owner = new ValueOwner();
        return new Owned(owner, owner.arena.allocate(layout));
    }}
    record Owned(ValueOwner owner, MemorySegment segment) {{}}
}}
''',
        "ByteView.java": f'''package {PACKAGE};

import java.lang.foreign.MemorySegment;

/** Read-only byte range retaining its generated value owner. */
public final class ByteView {{
    private final ValueOwner owner;
    private final MemorySegment bytes;
    ByteView(ValueOwner owner, MemorySegment bytes) {{ this.owner = owner; this.bytes = bytes.asReadOnly(); }}
    public long size() {{ return bytes.byteSize(); }}
    public byte get(long index) {{ return bytes.getAtIndex(java.lang.foreign.ValueLayout.JAVA_BYTE, index); }}
    public MemorySegment asMemorySegment() {{ return bytes; }}
    public byte[] copyBytes() {{ return bytes.toArray(java.lang.foreign.ValueLayout.JAVA_BYTE); }}
    /** Interpret bytes up to the first NUL as UTF-8, for native text fields. */
    public String utf8() {{
        long length = 0;
        while (length < bytes.byteSize() && get(length) != 0) length++;
        return new String(bytes.asSlice(0, length).toArray(java.lang.foreign.ValueLayout.JAVA_BYTE), java.nio.charset.StandardCharsets.UTF_8);
    }}
}}
''',
        "OwhConstants.java": f'''package {PACKAGE};

/** Native OWH_* enum constants generated directly from the public C headers. */
public final class OwhConstants {{
    private OwhConstants() {{}}
{{constants}}}}
''',
    }


def accessor(record: str, field: str) -> str:
    return f"{record}.{field}"


def number_get(field: dict, expr: str) -> str:
    spelling = field.get("canonical_type", field["type"]).replace("const ", "").strip()
    if spelling in {"unsigned char", "unsigned short", "unsigned int"}:
        return {"unsigned char": f"Byte.toUnsignedInt({expr})", "unsigned short": f"Short.toUnsignedInt({expr})",
                "unsigned int": f"Integer.toUnsignedLong({expr})"}[spelling]
    return expr


def copy_into_lines(c_name: str, record: dict, class_names: dict[str, str], records: dict[str, dict],
                    dst: str, src: str, owner: str, depth: int = 0) -> list[str]:
    """Emit ownership-aware field copies into an already allocated POD record."""
    pad = "        " * depth
    lines: list[str] = []
    fields = record["fields"]
    for field in fields:
        name = field["name"]
        md = metadata(field["metadata"])
        binding = f"dev.sxlver.overwolfhook.bindings.{c_name}.{name}"
        c_type = field["type"].replace("const ", "").strip().removeprefix("struct ")
        if field["kind"] == 101:
            if "cstring" in md:
                bound = int(md["max"])
                lines += [f"{pad}MemorySegment p_{name} = {binding}({dst});",
                          f"{pad}if (!p_{name}.equals(MemorySegment.NULL)) {{",
                          f"{pad}    MemorySegment scan_{name} = p_{name}.reinterpret({bound}L);",
                          f"{pad}    long n_{name} = 0;",
                          f"{pad}    while (n_{name} < {bound}L && scan_{name}.getAtIndex(ValueLayout.JAVA_BYTE, n_{name}) != 0) n_{name}++;",
                          f"{pad}    if (n_{name} == {bound}L) throw new IllegalArgumentException(\"unterminated {name} bytes\");",
                          f"{pad}    MemorySegment owned_{name} = {owner}.allocateBytes(n_{name} + 1, 1);",
                          f"{pad}    owned_{name}.copyFrom(scan_{name}.asSlice(0, n_{name} + 1));",
                          f"{pad}    {binding}({dst}, owned_{name});", f"{pad}}}"]
            elif "span" in md:
                span = str(md["span"])
                span_field = next((x for x in fields if x["name"] == span), None)
                if span_field is None:
                    raise ValueError(f"{c_name}.{name}: span field {span} is absent")
                span_binding = f"dev.sxlver.overwolfhook.bindings.{c_name}.{span}"
                span_value = f"{span_binding}({dst})"
                if span_field.get("canonical_type", span_field["type"]) in {"uint32_t", "unsigned int"}:
                    span_value = f"Integer.toUnsignedLong({span_value})"
                bound = int(md["max"])
                lines += [f"{pad}long n_{name} = {span_value};",
                          f"{pad}if (n_{name} > {bound}L) throw new IllegalArgumentException(\"{name} span exceeds annotated bound\");",
                          f"{pad}MemorySegment p_{name} = {binding}({dst});",
                          f"{pad}if (n_{name} != 0 && p_{name}.equals(MemorySegment.NULL)) throw new IllegalArgumentException(\"null {name} span\");",
                          f"{pad}{binding}({dst}, MemorySegment.NULL);",
                          f"{pad}if (n_{name} != 0) {{",
                          f"{pad}    MemorySegment source_{name} = p_{name}.reinterpret(n_{name});",
                          f"{pad}    MemorySegment owned_{name} = {owner}.allocateBytes(n_{name}, 1);",
                          f"{pad}    owned_{name}.copyFrom(source_{name});",
                          f"{pad}    {binding}({dst}, owned_{name});", f"{pad}}}"]
            elif md.get("pointer") == "borrowed" and md.get("copy") == "clear":
                lines.append(f"{pad}{binding}({dst}, MemorySegment.NULL);")
            else:
                raise ValueError(f"{c_name}.{name}: unsupported pointer ownership metadata")
        elif c_type in class_names:
            if c_type not in records:
                raise ValueError(f"{c_name}.{name}: nested record {c_type} has no parsed definition")
            nested = class_names[c_type]
            nested_dst = f"{binding}({dst})"
            nested_src = f"{binding}({src})"
            lines += [f"{pad}{nested}.copyInto({owner}, {nested_dst}, {nested_src});"]
    return lines


def generate_models(ast: dict, out: Path) -> list[Path]:
    records = ast["records"]
    annotated = {}
    for c_name, record in records.items():
        md = metadata(record["metadata"])
        if "model" not in md and "internal" not in md and "ignore" not in md:
            raise ValueError(f"{c_name}: public record needs OWH_META model=... input/output metadata")
        if "model" in md:
            if not ("input" in md or "output" in md):
                raise ValueError(f"{c_name}: model metadata must say input or output")
            if "input" in md and not ("init" in md or md.get("defaults") == "zero"):
                raise ValueError(f"{c_name}: input model needs init=<native function> or defaults=zero metadata")
            if "output" in md and "size" in md and md["size"] not in {field["name"] for field in record["fields"]}:
                raise ValueError(f"{c_name}: output size metadata names an absent field")
            annotated[c_name] = (record, md)

    model_names = [str(md["model"]) for _, md in annotated.values()]
    if len(set(model_names)) != len(model_names):
        raise ValueError("Two native records declare the same generated model name")

    class_names = {c: str(md["model"]) for c, (_, md) in annotated.items()}
    for c_name, (record, md) in annotated.items():
        for field in record["fields"]:
            java_type(class_names, field, "input" in md)
            nested_name = field["type"].replace("const ", "").strip().removeprefix("struct ")
            if nested_name in class_names:
                nested_md = annotated[nested_name][1]
                if "input" in md and "input" not in nested_md:
                    raise ValueError(f"{c_name}.{field['name']}: input POD cannot compose an output view")
                if "output" in md and "output" not in nested_md:
                    raise ValueError(f"{c_name}.{field['name']}: output cannot compose an input view")

    constants = []
    for name, value in sorted(ast["constants"].items()):
        constants.append(f"    public static final long {name} = {value & ((1 << 64) - 1)}L;")
    runtime = render_runtime()
    runtime["OwhConstants.java"] = runtime["OwhConstants.java"].replace("{constants}", "\n".join(constants) + "\n")
    generated: list[Path] = []
    for filename, source in runtime.items():
        path = out / filename
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        generated.append(path)

    for c_name, (record, md) in annotated.items():
        class_name = class_names[c_name]
        fields = record["fields"]
        source = [f"package {PACKAGE};", "", "import java.lang.foreign.Arena;", "import java.lang.foreign.MemorySegment;", "import java.lang.foreign.ValueLayout;", "", f"/** Generated from {c_name}; field access is delegated to jextract. */", f"public final class {class_name} {{"]
        if "output" in md:
            source += ["    private final ValueOwner owner;", "    private final MemorySegment segment;", f"    private {class_name}(ValueOwner owner, MemorySegment segment) {{ this.owner = owner; this.segment = segment; }}"]
            allocate_lines = [f"    public static MemorySegment allocate(Arena arena) {{", f"        MemorySegment segment = dev.sxlver.overwolfhook.bindings.{c_name}.allocate(arena);"]
            if "size" in md:
                size_field = str(md["size"])
                allocate_lines.append(f"        dev.sxlver.overwolfhook.bindings.{c_name}.{size_field}(segment, (int) dev.sxlver.overwolfhook.bindings.{c_name}.layout().byteSize());")
            allocate_lines += ["        return segment;", "    }"]
            source += allocate_lines
            source += [f"    public static {class_name} copyOf(MemorySegment source) {{",
                       f"        var owned = ValueOwner.allocate(dev.sxlver.overwolfhook.bindings.{c_name}.layout());",
                       "        MemorySegment copy = owned.segment();",
                       "        copyInto(owned.owner(), copy, source);",
                       f"        return new {class_name}(owned.owner(), copy);", "    }",
                       f"    static void copyInto(ValueOwner owner, MemorySegment copy, MemorySegment source) {{",
                       "        copy.copyFrom(source.asSlice(0, copy.byteSize()));"]
            source += copy_into_lines(c_name, record, class_names, records, "copy", "source", "owner", 2)
            source += ["    }", "    static " + class_name + " view(ValueOwner owner, MemorySegment segment) { return new " + class_name + "(owner, segment); }"]
            for field in fields:
                f_name = field["name"]
                prop = camel(f_name)
                typ = java_type(class_names, field)
                expr = accessor("dev.sxlver.overwolfhook.bindings." + c_name, f_name) + "(segment)"
                if field["kind"] == 112:
                    source.append(f"    public ByteView get{prop[:1].upper() + prop[1:]}() {{ return new ByteView(owner, {expr}); }}")
                elif field["kind"] == 101:
                    fmd = metadata(field["metadata"])
                    if "span" in fmd:
                        length_field = next(f for f in fields if f["name"] == fmd["span"])
                        length = accessor("dev.sxlver.overwolfhook.bindings." + c_name, length_field["name"]) + "(segment)"
                        if length_field.get("canonical_type", length_field["type"]) in {"unsigned int"}:
                            length = f"Integer.toUnsignedLong({length})"
                        source.append(f"    public ByteView get{prop[:1].upper() + prop[1:]}() {{ MemorySegment p = {expr}; long n = {length}; return new ByteView(owner, n == 0 || p.equals(MemorySegment.NULL) ? MemorySegment.NULL : p.reinterpret(n)); }}")
                    elif "cstring" in fmd:
                        max_bytes = int(fmd["max"])
                        source.append(f"    public ByteView get{prop[:1].upper() + prop[1:]}() {{ MemorySegment p = {expr}; if (p.equals(MemorySegment.NULL)) return new ByteView(owner, MemorySegment.NULL); MemorySegment scan = p.reinterpret({max_bytes}L); long n = 0; while (n < {max_bytes}L && scan.getAtIndex(ValueLayout.JAVA_BYTE, n) != 0) n++; if (n == {max_bytes}L) throw new IllegalStateException(\"unterminated {f_name} bytes\"); return new ByteView(owner, scan.asSlice(0, n)); }}")
                    else:
                        source.append(f"    public MemorySegment get{prop[:1].upper() + prop[1:]}() {{ return {expr}.asReadOnly(); }}")
                elif field["type"].replace("const ", "").strip().removeprefix("struct ") in class_names:
                    nested = class_names[field["type"].replace("const ", "").strip().removeprefix("struct ")]
                    source.append(f"    public {nested} get{prop[:1].upper() + prop[1:]}() {{ return {nested}.view(owner, {expr}); }}")
                else:
                    canonical = field.get("canonical_type", field["type"]).replace("const ", "").strip()
                    if canonical == "unsigned long long":
                        source.append("    /** uint64_t raw bits; use Long.compareUnsigned/Long.toUnsignedString when numeric unsigned order is needed. */")
                    source.append(f"    public {typ} get{prop[:1].upper() + prop[1:]}() {{ return {number_get(field, expr)}; }}")
        else:
            source += ["    private long[] numbers;", "    private MemorySegment[] pointers;", "    private byte[][] arrays;", "    private Object[] nested;", "    private boolean[] assigned;", f"    public {class_name}() {{ this.numbers = new long[{len(fields)}]; this.pointers = new MemorySegment[{len(fields)}]; this.arrays = new byte[{len(fields)}][]; this.nested = new Object[{len(fields)}]; this.assigned = new boolean[{len(fields)}]; }}", f"    public MemorySegment materialize(Arena arena) {{", f"        MemorySegment segment = dev.sxlver.overwolfhook.bindings.{c_name}.allocate(arena);"]
            init_function = md.get("init")
            if init_function:
                if not re.fullmatch(r"owh_[A-Za-z0-9_]+", str(init_function)):
                    raise ValueError(f"{c_name}: invalid native init function name {init_function!r}")
                source.append(f"        dev.sxlver.overwolfhook.bindings.OverwolfNative.{init_function}(segment);")
            abi_field = str(md.get("abi", "")).split(":", 1)[0] if "abi" in md else None
            length_pointer = {str(metadata(field["metadata"])["span"]): index
                              for index, field in enumerate(fields)
                              if field["kind"] == 101 and "span" in metadata(field["metadata"])}
            for i, field in enumerate(fields):
                f_name = field["name"]
                fmd = metadata(field["metadata"])
                expr = accessor("dev.sxlver.overwolfhook.bindings." + c_name, f_name)
                if f_name == md.get("size") and not init_function:
                    source.append(f"        {expr}(segment, (int) dev.sxlver.overwolfhook.bindings.{c_name}.layout().byteSize());")
                elif f_name == abi_field and not init_function:
                    value = str(md["abi"]).split(":", 1)[1]
                    source.append(f"        {expr}(segment, {value});")
                elif f_name in {md.get("size"), abi_field}:
                    continue
                elif f_name in length_pointer:
                    pointer_index = length_pointer[f_name]
                    max_bytes = int(metadata(fields[pointer_index]["metadata"])["max"])
                    source.append(f"        if (assigned[{pointer_index}]) {{ int spanLength{i} = arrays[{pointer_index}].length; if (spanLength{i} > {max_bytes}) throw new IllegalArgumentException(\"{fields[pointer_index]['name']} span exceeds annotated bound\"); {expr}(segment, spanLength{i}); }} else if (assigned[{i}]) {expr}(segment, ({native_cast(field)}) numbers[{i}]);")
                elif field["kind"] == 112:
                    count = field["array_count"]
                    source.append(f"        if (assigned[{i}]) {{ byte[] a{i} = arrays[{i}]; if (a{i}.length > {count}) throw new IllegalArgumentException(\"{f_name} exceeds native array\"); if (a{i}.length != 0) {expr}(segment).asSlice(0, a{i}.length).copyFrom(MemorySegment.ofArray(a{i})); }}")
                elif field["kind"] == 101:
                    if "pointer" in fmd:
                        source.append(f"        if (assigned[{i}]) {expr}(segment, pointers[{i}] == null ? MemorySegment.NULL : pointers[{i}]);")
                    elif "span" in fmd or "cstring" in fmd:
                        max_bytes = int(fmd["max"])
                        terminator = f"; p{i}.set(ValueLayout.JAVA_BYTE, a{i}.length, (byte) 0)" if "cstring" in fmd else ""
                        extra = "+ 1" if "cstring" in fmd else ""
                        source.append(f"        if (assigned[{i}]) {{ byte[] a{i} = arrays[{i}]; if (a{i}.length > {max_bytes}) throw new IllegalArgumentException(\"{f_name} exceeds annotated bound\"); MemorySegment p{i} = a{i}.length == 0 ? MemorySegment.NULL : arena.allocate(a{i}.length{extra}, 1); if (a{i}.length != 0) p{i}.asSlice(0, a{i}.length).copyFrom(MemorySegment.ofArray(a{i})){terminator}; {expr}(segment, p{i}); }}")
                    else:
                        source.append(f"        if (assigned[{i}]) {{ byte[] a{i} = arrays[{i}]; MemorySegment p{i} = arena.allocate(a{i}.length, 1); if (a{i}.length != 0) p{i}.copyFrom(MemorySegment.ofArray(a{i})); {expr}(segment, p{i}); }}")
                elif field["type"].replace("const ", "").strip().removeprefix("struct ") in class_names:
                    nested = class_names[field["type"].replace("const ", "").strip().removeprefix("struct ")]
                    source.append(f"        if (assigned[{i}]) {{ {nested} v{i} = ({nested}) nested[{i}]; {expr}(segment, v{i}.materialize(arena)); }}")
                else:
                    source.append(f"        if (assigned[{i}]) {expr}(segment, ({native_cast(field)}) numbers[{i}]);")
            source += ["        return segment;", "    }"]
            for i, field in enumerate(fields):
                prop = camel(field["name"])
                typ = java_type(class_names, field, True)
                cap = prop[:1].upper() + prop[1:]
                fmd = metadata(field["metadata"])
                if field["name"] == md.get("size") or ("abi" in md and field["name"] == abi_field) or field["name"] in length_pointer:
                    continue
                if field["kind"] == 112 or (field["kind"] == 101 and not metadata(field["metadata"]).get("pointer")):
                    source.append(f"    public {class_name} set{cap}({typ} value) {{ arrays[{i}] = value == null ? new byte[0] : value.clone(); assigned[{i}] = true; return this; }}")
                elif field["kind"] == 101:
                    if "pointer" in fmd:
                        source.append(f"    public {class_name} set{cap}(MemorySegment value) {{ pointers[{i}] = value; assigned[{i}] = true; return this; }}")
                    else:
                        source.append(f"    public {class_name} set{cap}(byte[] value) {{ arrays[{i}] = value == null ? new byte[0] : value.clone(); assigned[{i}] = true; return this; }}")
                elif field["type"].replace("const ", "").strip().removeprefix("struct ") in class_names:
                    nested = class_names[field["type"].replace("const ", "").strip().removeprefix("struct ")]
                    source.append(f"    public {class_name} set{cap}({nested} value) {{ nested[{i}] = value; assigned[{i}] = true; return this; }}")
                else:
                    canonical = field.get("canonical_type", field["type"]).replace("const ", "").strip()
                    if canonical == "unsigned char":
                        check = "if (value < 0 || value > 0xff) throw new IllegalArgumentException(\"value outside uint8 range\"); "
                    elif canonical == "unsigned short":
                        check = "if (value < 0 || value > 0xffff) throw new IllegalArgumentException(\"value outside uint16 range\"); "
                    elif canonical == "unsigned int":
                        check = "if (value < 0 || value > 0xffff_ffffL) throw new IllegalArgumentException(\"value outside uint32 range\"); "
                    else:
                        check = ""
                    source.append(f"    public {class_name} set{cap}({typ} value) {{ {check}numbers[{i}] = value; assigned[{i}] = true; return this; }}")
        source += ["}", ""]
        path = out / f"{class_name}.java"
        path.write_text("\n".join(source), encoding="utf-8")
        generated.append(path)
    return generated


def native_cast(field: dict) -> str:
    spelling = field.get("canonical_type", field["type"]).replace("const ", "").strip()
    return {"uint8_t": "byte", "int8_t": "byte", "uint16_t": "short", "int16_t": "short",
            "uint32_t": "int", "int32_t": "int", "uint64_t": "long", "int64_t": "long",
            "unsigned char": "byte", "char": "byte"}.get(spelling, "long" if spelling.startswith("uint") else "int")
