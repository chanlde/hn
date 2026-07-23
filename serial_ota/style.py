BG = "#f5f7fb"
PANEL = "#ffffff"
PANEL_ALT = "#f8fafc"
BORDER = "#d7dce5"
TEXT = "#1f2937"
TEXT_DIM = "#64748b"
ACCENT = "#2563eb"
ACCENT_HOVER = "#1d4ed8"
SUCCESS = "#16a34a"
DANGER = "#dc2626"
WARNING = "#d97706"

QSS = f"""
QMainWindow, QWidget#central {{
    background: {BG};
    color: {TEXT};
    font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
    font-size: 13px;
}}
QLabel {{
    color: {TEXT};
}}
QLabel[muted="true"] {{
    color: {TEXT_DIM};
}}
QLabel[infoLabel="true"] {{
    color: {TEXT_DIM};
    font-weight: 500;
}}
QLabel[infoValue="true"] {{
    color: {TEXT};
    font-weight: 600;
}}
QFrame[panel="true"] {{
    background: {PANEL};
    border: 1px solid {BORDER};
    border-radius: 8px;
}}
QFrame[softPanel="true"] {{
    background: {PANEL_ALT};
    border: 1px solid {BORDER};
    border-radius: 8px;
}}
QLineEdit, QComboBox, QSpinBox {{
    background: white;
    color: {TEXT};
    border: 1px solid {BORDER};
    border-radius: 6px;
    min-height: 28px;
    padding: 4px 8px;
}}
QLineEdit:read-only {{
    background: {PANEL_ALT};
    color: {TEXT_DIM};
}}
QComboBox::drop-down {{
    width: 24px;
    border: none;
}}
QPushButton {{
    background: white;
    color: {TEXT};
    border: 1px solid {BORDER};
    border-radius: 6px;
    padding: 7px 14px;
    min-height: 22px;
}}
QPushButton:hover {{
    border-color: {ACCENT};
}}
QPushButton:disabled {{
    background: #eef1f6;
    color: #9aa3b2;
}}
QPushButton[primary="true"] {{
    background: {ACCENT};
    color: white;
    border: 1px solid {ACCENT};
    font-weight: 600;
}}
QPushButton[primary="true"]:hover {{
    background: {ACCENT_HOVER};
}}
QPushButton[danger="true"] {{
    background: {DANGER};
    color: white;
    border: 1px solid {DANGER};
    font-weight: 600;
}}
QProgressBar {{
    background: #e8edf5;
    border: 1px solid {BORDER};
    border-radius: 7px;
    text-align: center;
    color: {TEXT};
    min-height: 20px;
}}
QProgressBar::chunk {{
    background: {ACCENT};
    border-radius: 6px;
}}
QPlainTextEdit {{
    background: #0f172a;
    color: #dbeafe;
    border: 1px solid #1e293b;
    border-radius: 8px;
    padding: 8px;
    font-family: Consolas, "Cascadia Mono", monospace;
    font-size: 12px;
}}
QCheckBox {{
    color: {TEXT};
    spacing: 8px;
}}
"""
