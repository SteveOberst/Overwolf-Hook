"""Check built HTML links, fragments, images, downloadable starters, and source leakage offline."""
from __future__ import annotations
import argparse
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import tomllib
from urllib.parse import unquote, urljoin, urlsplit
import zipfile
from reference import declarations, enum_constants, public_headers, record_declarations

ROOT = Path(__file__).resolve().parents[2]
SDK_VERSION_TOKEN = b"@OWH_SDK_VERSION@"


class Page(HTMLParser):
    def __init__(self, path: Path):
        super().__init__()
        self.ids: set[str] = set()
        self.text: list[str] = []
        self.links: list[str] = []
        self.images: list[str] = []
        self.mermaid: list[str] = []
        self.in_mermaid = False
        self.navigation_links: list[tuple[str, str]] = []
        self.nav_stack: list[bool] = []
        self.nav_link: tuple[str, list[str]] | None = None
        self.feed(path.read_text(encoding="utf-8"))

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        data = dict(attrs)
        if tag == "nav":
            self.nav_stack.append("md-nav--primary" in data.get("class", "").split()
                                  or any(self.nav_stack))
        if tag == "a" and any(self.nav_stack) and data.get("href") is not None:
            self.nav_link = (data["href"], [])
        if tag == "pre" and "mermaid" in data.get("class", "").split():
            self.in_mermaid = True
            self.mermaid.append("")
        if data.get("id"):
            self.ids.add(data["id"])
        for key in ("href", "src"):
            if data.get(key):
                self.links.append(data[key])
        if tag == "meta" and data.get("http-equiv", "").lower() == "refresh":
            match = re.match(r"\s*\d+\s*;\s*url\s*=\s*(.+)", data.get("content", ""), re.I)
            if match:
                self.links.append(match[1].strip("\"' "))
        if tag == "img" and data.get("src"):
            self.images.append(data["src"])
            if not data.get("alt"):
                raise ValueError("Documentation images require an alt description")

    def handle_data(self, data: str) -> None:
        self.text.append(data)
        if self.nav_link is not None:
            self.nav_link[1].append(data)
        if self.in_mermaid:
            self.mermaid[-1] += data

    def handle_endtag(self, tag: str) -> None:
        if tag == "a" and self.nav_link is not None:
            href, parts = self.nav_link
            self.navigation_links.append((" ".join("".join(parts).split()), href))
            self.nav_link = None
        if tag == "nav" and self.nav_stack:
            self.nav_stack.pop()
        if tag == "pre":
            self.in_mermaid = False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site-dir", type=Path, default=ROOT / "build/docs/site")
    parser.add_argument("--config", type=Path, default=ROOT / "zensical.toml")
    parser.add_argument("--base", help="Override the site URL path prefix, e.g. /Overwolf-Hook/")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    site = args.site_dir.resolve()
    project = tomllib.loads(args.config.read_text(encoding="utf-8"))["project"]
    version = (ROOT / "version.txt").read_text().strip()
    site_url = urlsplit(project["site_url"])
    base = args.base if args.base is not None else site_url.path
    base = "/" + base.strip("/") + "/" if base.strip("/") else "/"
    redirects = project.get("plugins", {}).get("redirects", {}).get("redirect_maps", {})
    files = sorted(site.rglob("*.html"))
    if not files:
        raise ValueError("Build the documentation site before checking it")
    pages = {path: Page(path) for path in files}
    errors = []
    count = 0

    def markdown_page(url: str) -> tuple[Path, str]:
        parsed = urlsplit(url)
        relative = Path(unquote(parsed.path))
        if relative.name == "index.md":
            relative = relative.with_suffix(".html")
        else:
            relative = relative.with_suffix("") / "index.html"
        return (site / relative).resolve(), unquote(parsed.fragment)

    # A page can exist and have valid in-content links while its named sidebar
    # entry is swallowed by section-index navigation. Check the rendered links.
    def nav_pages(items):
        for item in items:
            for title, target in item.items():
                if isinstance(target, list):
                    yield from nav_pages(target)
                elif not urlsplit(target).scheme and urlsplit(target).path.endswith(".md"):
                    yield title, target

    homepage = pages.get(site / "index.html")
    if homepage:
        links = {(title, urlsplit(urljoin(project["site_url"], href)).path)
                 for title, href in homepage.navigation_links}
        for title, source in nav_pages(project["nav"]):
            target, _ = markdown_page(source)
            relative = target.relative_to(site).as_posix()
            url = base + (relative[:-10] if relative.endswith("index.html") else relative)
            if (title, url) not in links:
                errors.append(f"Missing named sidebar entry: {title} -> {source}")

    anchor_redirects = {markdown_page(source) for source in redirects if urlsplit(source).fragment}

    def check_target(target: Path, fragment: str, origin: str) -> None:
        target = target.resolve()
        if not target.is_relative_to(site) or not target.is_file():
            errors.append(f"Missing target: {origin}")
        elif fragment and target in pages and fragment not in pages[target].ids and (target, fragment) not in anchor_redirects:
            errors.append(f"Missing fragment: {origin}")

    for path, page in pages.items():
        text = path.read_text(encoding="utf-8")
        if SDK_VERSION_TOKEN.decode() in text:
            errors.append(f"Unresolved SDK version in {path.relative_to(site)}")
        if re.search(r"(?:[A-Za-z]:[/\\]+Users[/\\]+|/Users/|/home/)", text) or ".workspace/" in text:
            errors.append(f"Local source path leaked into {path.relative_to(site)}")
        for url in page.links:
            parsed = urlsplit(url)
            if (parsed.scheme or parsed.netloc) and parsed.netloc != site_url.netloc:
                continue
            decoded = unquote(parsed.path)
            if decoded.startswith(base):
                target = site / decoded[len(base):]
            elif decoded.startswith("/"):
                errors.append(f"URL escapes project base: {path.relative_to(site)} -> {url}")
                continue
            else:
                target = (path.parent / decoded) if decoded else path
            if target.is_dir():
                target = target / "index.html"
            check_target(target, unquote(parsed.fragment), f"{path.relative_to(site)} -> {url}")
            count += 1
    for source, destination in redirects.items():
        source_page, _ = markdown_page(source)
        check_target(source_page, "", f"redirect source {source}")
        if not urlsplit(destination).scheme:
            target, fragment = markdown_page(destination)
            check_target(target, fragment, f"redirect {source} -> {destination}")
    for language in ("native", "java", "kotlin"):
        archive_path = site / "downloads" / f"{language}-starter.zip"
        if not archive_path.is_file():
            errors.append(f"Missing starter archive: {language}")
            continue
        with zipfile.ZipFile(archive_path) as archive:
            names = archive.namelist()
            if not names or any(set(Path(name).parts) & {"build", ".gradle", ".kotlin", ".workspace"} for name in names):
                errors.append(f"Invalid starter archive: {language}")
            for name in names:
                relative = Path(name)
                if relative.parts[0] != f"{language}-starter" or ".." in relative.parts:
                    errors.append(f"Invalid archive member: {name}")
                    continue
                original = ROOT / "starters" / language / Path(*relative.parts[1:])
                if not original.is_file() or archive.read(name) != original.read_bytes():
                    errors.append(f"Starter archive differs from source: {name}")
    for directory in ("examples", "integration"):
        examples = ROOT / "docs/assets" / directory
        for source in examples.rglob("*"):
            if source.is_file():
                relative = source.relative_to(examples)
                target = site / "assets" / ("generated/integration" if directory == "integration" else "examples") / relative
                expected = source.read_bytes().replace(SDK_VERSION_TOKEN, version.encode())
                if not target.is_file() or target.read_bytes() != expected:
                    errors.append(f"Example download differs from source: {source.relative_to(ROOT / 'docs')}")
    diagram_count = 0
    for path, page in pages.items():
        for diagram in page.mermaid:
            diagram_count += 1
            if not diagram.strip() or "--8<--" in diagram:
                errors.append(f"Unexpanded Mermaid diagram: {path.relative_to(site)}")
            if "accTitle:" not in diagram or "accDescr:" not in diagram:
                errors.append(f"Mermaid diagram lacks accessible description: {path.relative_to(site)}")
            if "\ufffd" in diagram or "\u00e2\u2020" in diagram:
                errors.append(f"Corrupted text encoding in diagram: {path.relative_to(site)}")
    authored_count = sum(len(re.findall(r"^```mermaid\s*$", p.read_text(encoding="utf-8"), re.M))
                         for p in (ROOT / "docs").rglob("*.md"))
    if diagram_count != authored_count:
        errors.append(f"Expected {authored_count} Mermaid embeds, found {diagram_count}")

    # Public ABI declarations and canonical models are the shared source for
    # every language. A name check catches omitted operations/models; behavior
    # and usability still need a source-based review.
    native_reference = (ROOT / "docs/reference/native-api.md").read_text(encoding="utf-8")
    records = (ROOT / "docs/reference/data-records.md").read_text(encoding="utf-8")
    abi_functions = set()
    models = set()
    for name in (ROOT / "cmake/public-headers.txt").read_text().splitlines():
        if not name.strip():
            continue
        header = (ROOT / "include/overwolf_hook" / name.strip()).read_text(encoding="utf-8")
        abi_functions.update(re.findall(r"OWH_CLIENT\s+\w+\s+__cdecl\s+(owh_\w+)\s*\(", header))
        models.update(re.findall(r"model=(\w+)", header))
    for name in sorted(abi_functions):
        if not re.search(r"\b" + re.escape(name) + r"\b", native_reference):
            errors.append(f"Public ABI operation missing from C reference: {name}")
    for name in sorted(models):
        if not re.search(r"\b" + re.escape(name) + r"\b", records):
            errors.append(f"Public model missing from data records: {name}")
    # Check the rendered declarations, not just names in prose. This catches a
    # changed return type/parameter/layout even when its old name still appears.
    native_page, _ = markdown_page("reference/native-api.md")
    records_page, _ = markdown_page("reference/data-records.md")
    constants_page, _ = markdown_page("reference/constants.md")
    record_text = re.sub(r"\s+", "", "".join(pages[records_page].text))
    for header, name, declaration in declarations():
        domain = Path(header).stem.replace("_", "-")
        operation_page, _ = markdown_page(f"reference/c/{domain}.md")
        operation = pages.get(operation_page)
        if name not in pages[native_page].ids:
            errors.append(f"ABI operation missing from operation index: {name}")
        if operation is None or name not in operation.ids:
            errors.append(f"ABI operation lacks a domain heading: {name}")
        native_text = re.sub(r"\s+", "", "".join(operation.text)) if operation else ""
        if re.sub(r"\s+", "", declaration) not in native_text:
            errors.append(f"Rendered C declaration differs from public header: {name}")
    constants = set()
    for path, header in public_headers():
        if not path.startswith("types/"):
            continue
        constants.update(name for name, _ in enum_constants(header))
        for name, _, declaration in record_declarations(header):
            if name not in pages[records_page].ids or re.sub(r"\s+", "", declaration) not in record_text:
                errors.append(f"Rendered record differs from public header: {name}")
    for name in sorted(constants):
        if name.lower() not in pages[constants_page].ids:
            errors.append(f"Public constant lacks an indexed explanation: {name}")
    report = {"html_pages": len(files), "local_links_checked": count,
              "mermaid_diagrams_checked": diagram_count,
              "abi_operations_checked": len(abi_functions), "api_models_checked": len(models),
              "enum_constants_checked": len(constants),
              "redirects_checked": len(redirects), "errors": errors}
    destination = args.report or site.parent / "link-report.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
