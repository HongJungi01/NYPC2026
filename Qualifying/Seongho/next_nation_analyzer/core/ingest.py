from __future__ import annotations

import hashlib
from datetime import datetime, timezone
from pathlib import Path


def read_input(path: str | Path) -> tuple[Path, str, dict]:
    source = Path(path)
    text = source.read_text(encoding="utf-8", errors="replace")
    digest = hashlib.sha256(text.encode("utf-8", errors="replace")).hexdigest()
    manifest = {
        "source_file": str(source),
        "source_name": source.name,
        "source_size_bytes": source.stat().st_size,
        "source_hash": digest,
        "parsed_at": datetime.now(timezone.utc).isoformat(),
    }
    return source, text, manifest

