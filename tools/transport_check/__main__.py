"""Entry point, so `python3 tools/transport_check` runs the harness.

The work is in `cli.py` rather than here because `__main__` cannot be imported
by name once something else owns that name — which is exactly what happens when
`selftest.py` drives the harness in-process.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cli import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
