from pathlib import Path
import gzip


WEB_DIR = Path.cwd() / "data" / "web"
EXTENSIONS = {".css", ".js", ".json", ".svg", ".txt"}
MIN_SIZE = 10 * 1024


def gzip_asset(path: Path) -> None:
    raw = path.read_bytes()
    gzip_path = path.with_name(path.name + ".gz")
    compressed = gzip.compress(raw, compresslevel=9, mtime=0)

    if len(raw) < MIN_SIZE or len(compressed) >= len(raw):
        if gzip_path.exists():
            gzip_path.unlink()
        return

    if gzip_path.exists() and gzip_path.read_bytes() == compressed:
        return

    gzip_path.write_bytes(compressed)


def main() -> None:
    if not WEB_DIR.exists():
        return

    for path in WEB_DIR.rglob("*"):
        if path.is_file() and path.suffix in EXTENSIONS and not path.name.endswith(".gz"):
            gzip_asset(path)


main()
