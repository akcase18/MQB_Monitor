"""Dark, blue-accent theme for the MQB Monitor.

Two layers, matching how Qt actually resolves style:
  * a QPalette (base colors Qt's Fusion style renders controls with -- menus,
    dialogs, scrollbars-by-default, disabled-state grays);
  * a QSS stylesheet on top for the specifics a palette alone can't reach
    (the sidebar's selected-item accent bar, table headers, progress-bar
    chunks, focus/hover borders).

Both read from the same color constants below, so retuning the palette means
editing this file only -- nothing downstream (gui.py) references hex codes
directly except MONO_FONT, which is a font choice, not a color.
"""
from __future__ import annotations

from PySide6.QtGui import QColor, QFont, QPalette
from PySide6.QtWidgets import QApplication, QStyleFactory

BG = "#1b1d22"  # window background
PANEL = "#212328"  # cards / top bar / sidebar / table background
PANEL_ALT = "#26282e"  # alternate table row, hover, input fields
BORDER = "#33353c"
TEXT = "#e7e8ea"
TEXT_DIM = "#9aa0ab"
ACCENT = "#4c8dff"
ACCENT_HOVER = "#6aa1ff"
ACCENT_DIM = "rgba(76, 141, 255, 0.16)"
ACCENT_TEXT_ON = "#0c1116"  # text color drawn on top of a solid ACCENT fill
GREEN = "#3ddc84"
AMBER = "#f5a623"
RED = "#ef4444"

# Applied to table cells holding IDs/hex/numbers -- reads better tabular than
# the UI's proportional font, and matches how the values line up with each
# other. Menlo ships on every Mac; Qt silently falls back if it's ever absent.
MONO_FONT = QFont("Menlo", 11)

STYLESHEET = f"""
QWidget#Root {{ background: {BG}; }}
QDialog {{ background: {BG}; }}
QLabel {{ color: {TEXT}; }}

QFrame#TopBar {{
    background: {PANEL};
    border-bottom: 1px solid {BORDER};
}}
QLabel#AppTitle {{ color: {TEXT}; font-size: 14px; font-weight: 600; }}
QLabel#AppSubtitle {{ color: {TEXT_DIM}; font-size: 10px; }}
QLabel#FieldLabel {{ color: {TEXT_DIM}; font-size: 11px; }}
QLabel#StatusDot {{ font-size: 13px; }}
QLabel#StatsLabel {{ color: {TEXT_DIM}; font-size: 11px; }}

QListWidget#Sidebar {{
    background: {PANEL};
    border: none;
    border-right: 1px solid {BORDER};
    padding: 10px 0;
    outline: none;
    font-size: 13px;
}}
QListWidget#Sidebar::item {{
    padding: 9px 18px;
    margin: 1px 8px;
    border-radius: 6px;
    border-left: 3px solid transparent;
    color: {TEXT_DIM};
}}
QListWidget#Sidebar::item:hover {{
    background: {PANEL_ALT};
    color: {TEXT};
}}
QListWidget#Sidebar::item:selected {{
    background: {ACCENT_DIM};
    color: {TEXT};
    font-weight: 600;
    border-left: 3px solid {ACCENT};
}}

QPushButton {{
    background: {PANEL_ALT};
    border: 1px solid {BORDER};
    border-radius: 6px;
    padding: 5px 14px;
    color: {TEXT};
}}
QPushButton:hover {{ border-color: {ACCENT}; }}
QPushButton:pressed {{ background: {BORDER}; }}
QPushButton:disabled {{ color: {TEXT_DIM}; border-color: {BORDER}; }}
QPushButton[primary="true"] {{
    background: {ACCENT};
    border: 1px solid {ACCENT};
    color: {ACCENT_TEXT_ON};
    font-weight: 600;
}}
QPushButton[primary="true"]:hover {{ background: {ACCENT_HOVER}; }}
QPushButton[primary="true"]:disabled {{
    background: {PANEL_ALT}; color: {TEXT_DIM}; border-color: {BORDER};
}}

QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {{
    background: {PANEL_ALT};
    border: 1px solid {BORDER};
    border-radius: 6px;
    padding: 4px 8px;
    color: {TEXT};
    selection-background-color: {ACCENT};
}}
QComboBox:hover, QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover {{
    border-color: {ACCENT};
}}
QComboBox:disabled {{ color: {TEXT_DIM}; }}
QComboBox::drop-down {{ border: none; width: 20px; }}
QComboBox QAbstractItemView {{
    background: {PANEL};
    border: 1px solid {BORDER};
    selection-background-color: {ACCENT_DIM};
    color: {TEXT};
    outline: none;
}}

QCheckBox {{ color: {TEXT_DIM}; spacing: 6px; }}
QCheckBox:disabled {{ color: {BORDER}; }}
QCheckBox::indicator {{
    width: 14px; height: 14px;
    border: 1px solid {BORDER};
    border-radius: 3px;
    background: {PANEL_ALT};
}}
QCheckBox::indicator:checked {{ background: {ACCENT}; border-color: {ACCENT}; }}
QCheckBox::indicator:disabled {{ background: {PANEL}; border-color: {BORDER}; }}

QTableWidget {{
    background: {PANEL};
    alternate-background-color: {PANEL_ALT};
    gridline-color: {BORDER};
    border: 1px solid {BORDER};
    border-radius: 6px;
    color: {TEXT};
    selection-background-color: {ACCENT_DIM};
    selection-color: {TEXT};
}}
QTableWidget::item {{ padding: 3px 6px; border: none; }}
QHeaderView::section {{
    background: {PANEL_ALT};
    color: {TEXT_DIM};
    padding: 6px 8px;
    border: none;
    border-bottom: 1px solid {BORDER};
    font-weight: 600;
    font-size: 10px;
}}
QTableCornerButton::section {{ background: {PANEL_ALT}; border: none; }}

QListWidget {{
    background: {PANEL};
    border: 1px solid {BORDER};
    border-radius: 6px;
    color: {TEXT};
    outline: none;
}}
QListWidget::item {{ padding: 5px 8px; }}
QListWidget::item:selected {{ background: {ACCENT_DIM}; color: {TEXT}; }}
QListWidget::item:hover {{ background: {PANEL_ALT}; }}

QProgressBar {{
    background: {PANEL_ALT};
    border: 1px solid {BORDER};
    border-radius: 4px;
}}
QProgressBar::chunk {{ background: {ACCENT}; border-radius: 3px; }}

QScrollBar:vertical {{ background: {PANEL}; width: 12px; margin: 0; }}
QScrollBar::handle:vertical {{
    background: {BORDER}; border-radius: 5px; min-height: 24px;
}}
QScrollBar::handle:vertical:hover {{ background: {ACCENT}; }}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{ height: 0; }}
QScrollBar:horizontal {{ background: {PANEL}; height: 12px; margin: 0; }}
QScrollBar::handle:horizontal {{
    background: {BORDER}; border-radius: 5px; min-width: 24px;
}}
QScrollBar::handle:horizontal:hover {{ background: {ACCENT}; }}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {{ width: 0; }}

QToolTip {{
    background: {PANEL_ALT};
    color: {TEXT};
    border: 1px solid {BORDER};
    padding: 4px 6px;
}}

QMessageBox {{ background: {BG}; }}
"""


def apply_theme(app: QApplication) -> None:
    """Applies the Fusion style + dark palette + QSS stylesheet. Call once,
    right after constructing the QApplication and before building any
    windows (so every widget picks it up on first paint)."""
    fusion = QStyleFactory.create("Fusion")
    if fusion is not None:
        app.setStyle(fusion)

    pal = QPalette()
    pal.setColor(QPalette.Window, QColor(BG))
    pal.setColor(QPalette.WindowText, QColor(TEXT))
    pal.setColor(QPalette.Base, QColor(PANEL))
    pal.setColor(QPalette.AlternateBase, QColor(PANEL_ALT))
    pal.setColor(QPalette.ToolTipBase, QColor(PANEL_ALT))
    pal.setColor(QPalette.ToolTipText, QColor(TEXT))
    pal.setColor(QPalette.Text, QColor(TEXT))
    pal.setColor(QPalette.Button, QColor(PANEL_ALT))
    pal.setColor(QPalette.ButtonText, QColor(TEXT))
    pal.setColor(QPalette.BrightText, QColor(RED))
    pal.setColor(QPalette.Highlight, QColor(ACCENT))
    pal.setColor(QPalette.HighlightedText, QColor(ACCENT_TEXT_ON))
    pal.setColor(QPalette.Link, QColor(ACCENT))
    pal.setColor(QPalette.Disabled, QPalette.Text, QColor(TEXT_DIM))
    pal.setColor(QPalette.Disabled, QPalette.ButtonText, QColor(TEXT_DIM))
    pal.setColor(QPalette.Disabled, QPalette.WindowText, QColor(TEXT_DIM))
    app.setPalette(pal)

    app.setStyleSheet(STYLESHEET)


def mark_primary(button) -> None:
    """Flags a QPushButton for the accent-filled 'primary action' style
    (QPushButton[primary="true"] above). Must be called after the widget is
    constructed but works whether or not the stylesheet is already applied."""
    button.setProperty("primary", True)
