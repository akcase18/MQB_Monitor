"""Re-exports tools/monitor's theme so both host tools look like one family.

The palette lives in exactly one place (tools/monitor/mqb_monitor/theme.py);
retuning it there retunes this app too. The sys.path insert is what lets
that import work without either tool being installed as a package -- the
same reason run.py inserts its own directory.
"""
from __future__ import annotations

import sys
from pathlib import Path

# tools/log_export/mqb_logs/theme.py -> tools/ -> tools/monitor
_MONITOR_DIR = Path(__file__).resolve().parents[2] / "monitor"
if str(_MONITOR_DIR) not in sys.path:
    sys.path.insert(0, str(_MONITOR_DIR))

from mqb_monitor.theme import (  # noqa: E402  (must follow the path insert)
    ACCENT,
    AMBER,
    BORDER,
    GREEN,
    MONO_FONT,
    PANEL,
    RED,
    TEXT,
    TEXT_DIM,
    apply_theme,
    mark_primary,
)

__all__ = [
    "ACCENT", "AMBER", "BORDER", "GREEN", "MONO_FONT", "PANEL", "RED",
    "TEXT", "TEXT_DIM", "apply_theme", "mark_primary",
]
