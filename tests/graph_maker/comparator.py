#!/usr/bin/env python3
"""
MOSFET Curve Comparator 
-----------------------
Lets the user configure N comparing pairs of curves independently and
overlays them on a single matplotlib plot. Each curve can read data from
a distinct CSV file via the UI.

Usage:
    python comparator.py
"""

import sys, os, glob
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Qt5Agg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_qt5agg import (
    FigureCanvasQTAgg as FigureCanvas,
    NavigationToolbar2QT as NavigationToolbar,
)
from matplotlib.mathtext import MathTextParser
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QFormLayout,
    QPushButton, QLabel, QComboBox, QMessageBox, QGroupBox, QSplitter,
    QCheckBox, QLineEdit, QTableWidget, QTableWidgetItem, QHeaderView, QFileDialog,
    QScrollArea
)
from PyQt5.QtGui import QDoubleValidator, QFont
from PyQt5.QtCore import Qt, QSize
from dataclasses import dataclass

COLORS = {
    "Blue": "#1f77b4", "Red": "#d62728", "Green": "#2ca02c",
    "Orange": "#ff7f0e", "Purple": "#9467bd", "Brown": "#8c564b",
    "Pink": "#e377c2", "Gray": "#7f7f7f", "Olive": "#bcbd22",
    "Cyan": "#17becf", "Black": "#000000", "Magenta": "#m"
}

LINE_STYLES = {
    "Continuous (-)": "-",
    "Dashed (--)": "--",
    "Dotted (:)": ":",
    "Dash-Dot (-.)": "-."
}

MARKERS = {
    "None": "None",
    "Circle (o)": "o",
    "Cross (x)": "x",
    "Square (s)": "s",
    "Triangle (^)": "^"
}

# ---------------------------------------------------------------------------
#  State Objects
# ---------------------------------------------------------------------------

@dataclass
class CurveState:
    filepath: str = ""
    df: pd.DataFrame | None = None
    
    show: bool = True
    x_col: str = ""
    y_col: str = ""
    grp_col: str = ""
    val_str: str = ""
    offset: float = 0.0
    
    legend: str = ""
    custom_legend: bool = False
    
    color: str = "Blue"
    linestyle: str = "Continuous (-)"
    marker: str = "None"
    alpha: float = 0.85

class ComparisonPair:
    def __init__(self, name="Pair 1"):
        self.name = name
        self.c1 = CurveState(color="Blue", linestyle="Continuous (-)", marker="None")
        self.c2 = CurveState(color="Red", linestyle="Dashed (--)", marker="Cross (x)")


# ---------------------------------------------------------------------------
#  CSV loaders
# ---------------------------------------------------------------------------

def load_flexible_csv(filepath: str):
    sweep_mode = "VGS"
    try:
        with open(filepath, "r", encoding="utf-8-sig", errors="ignore") as fh:
            head = [fh.readline() for _ in range(20)]

        for line in head:
            if "Sweep Mode: VDS" in line:
                sweep_mode = "VDS"
                break

        is_keysight = any(
            l.strip().startswith(("SetupTitle", "PrimitiveTest", "TestParameter"))
            for l in head
        )

        if is_keysight:
            headers, rows = [], []
            var1_idx = -1
            with open(filepath, "r", encoding="utf-8-sig", errors="ignore") as fh:
                for line in fh:
                    parts = [p.strip() for p in line.split(",")]
                    if len(parts) < 2: continue
                    
                    if parts[0] == "TestParameter":
                        if parts[1] == "Channel.Func":
                            try:
                                var1_idx = parts.index("VAR1")
                            except ValueError: pass
                        elif parts[1] == "Channel.VName" and var1_idx != -1:
                            if var1_idx < len(parts):
                                vname = parts[var1_idx].lower()
                                if "d" in vname:
                                    sweep_mode = "VDS"
                                elif "g" in vname:
                                    sweep_mode = "VGS"
                                    
                    if parts[0] == "DataName":
                        headers = parts[1:]
                    elif parts[0] == "DataValue":
                        vals = []
                        for x in parts[1:]:
                            try:
                                vals.append(float(x))
                            except (ValueError, TypeError):
                                vals.append(float('nan'))
                        if vals:
                            rows.append(vals)
            if headers and rows:
                return pd.DataFrame(rows, columns=headers), sweep_mode
            return None, sweep_mode

        # ESP32-style
        df = pd.read_csv(filepath, comment="#")
        df.columns = df.columns.str.strip()
        # Final fallback: filename search
        if sweep_mode == "VGS" and "idvd" in filepath.lower():
            sweep_mode = "VDS"
        if sweep_mode == "VDS" and "idvg" in filepath.lower():
            sweep_mode = "VGS"

        return df, sweep_mode

    except Exception as exc:
        print(f"[load] {filepath}: {exc}")
        return None, sweep_mode

def _find_col(columns, candidates):
    lower = {c.lower(): c for c in columns}
    for cand in candidates:
        if cand.lower() in lower:
            return lower[cand.lower()]
    return None


# ---------------------------------------------------------------------------
#  Main window
# ---------------------------------------------------------------------------

class CurveComparator(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("MOSFET Curve Comparator - Multiple Pairs")
        self.resize(1300, 850)
        
        self._math_parser = MathTextParser("agg")
        self.statusBar().showMessage("Pronto")
        
        self.pairs = []
        self.current_pair_idx = -1
        self._is_updating_ui = False

        self._build_ui()
        self._sync_state_to_ui()

    # ---- UI ---------------------------------------------------------------

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QHBoxLayout(central)

        # --- Main Splitter (Left Controls | Right Plot) ---
        main_splitter = QSplitter(Qt.Horizontal)
        root.addWidget(main_splitter)

        # --- controls (left) ---
        ctrl_panel = QWidget()
        ctrl_panel.setMinimumWidth(380)
        ctrl_panel_lay = QVBoxLayout(ctrl_panel)
        ctrl_panel_lay.setContentsMargins(0, 0, 0, 0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QScrollArea.NoFrame)
        
        scroll_content = QWidget()
        ctrl_lay = QVBoxLayout(scroll_content)
        scroll.setWidget(scroll_content)

        # Pair manager
        pair_grp = QGroupBox("Comparison Pairs")
        pair_lay = QHBoxLayout()
        self.pair_cb = QComboBox()
        self.pair_cb.currentIndexChanged.connect(self._on_pair_selected)
        
        btn_new = QPushButton("Nova Curva")
        btn_new.clicked.connect(self._add_pair)
        btn_new.setMinimumHeight(30)
        btn_del = QPushButton("Deletar")
        btn_del.clicked.connect(self._delete_pair)
        btn_del.setMinimumHeight(30)
        
        pair_lay.addWidget(self.pair_cb, 1)
        pair_lay.addWidget(btn_new)
        pair_lay.addWidget(btn_del)
        pair_grp.setLayout(pair_lay)
        ctrl_lay.addWidget(pair_grp)

        # Global settings
        global_grp = QGroupBox("Configurações do Gráfico")
        global_lay = QFormLayout()
        
        self.plot_title_le = QLineEdit("MOSFET Curve Comparison")
        self.plot_title_le.editingFinished.connect(self._update_plot)
        
        self.leg_pos_cb = QComboBox()
        self.leg_pos_cb.addItems([
            "Melhor Ajuste (Auto)", "Topo-Esquerda", "Topo-Direita", 
            "Base-Esquerda", "Base-Direita", "Centro", "Centro-Esquerda", "Centro-Direita"
        ])
        self.leg_pos_cb.currentIndexChanged.connect(self._update_plot)
        
        global_lay.addRow("Título do Gráfico:", self.plot_title_le)
        global_lay.addRow("Posição da Legenda:", self.leg_pos_cb)
        global_grp.setLayout(global_lay)
        ctrl_lay.addWidget(global_grp)

        self.file_groups = []
        for idx in (0, 1):
            grp, widgets = self._make_file_group(idx)
            self.file_groups.append(widgets)
            ctrl_lay.addWidget(grp)

        ctrl_lay.addStretch()
        
        ctrl_panel_lay.addWidget(scroll)

        # Bottom buttons (fixed)
        btn_row_widget = QWidget()
        btn_row_widget.setStyleSheet("background: #f0f0f0; border-top: 1px solid #ccc;")
        btn_row = QHBoxLayout(btn_row_widget)
        
        self.lin_btn = QPushButton("Esquerda (Linear)")
        self.lin_btn.setCheckable(True)
        self.lin_btn.setChecked(True)
        self.lin_btn.setMinimumHeight(40)
        self.lin_btn.setStyleSheet("background:#4CAF50;color:white;padding:5px;font-weight:bold;")
        self.lin_btn.clicked.connect(self._toggle_scales)
        btn_row.addWidget(self.lin_btn)
        
        self.log_btn = QPushButton("Direita (Log)")
        self.log_btn.setCheckable(True)
        self.log_btn.setChecked(False)
        self.log_btn.setMinimumHeight(40)
        self.log_btn.setStyleSheet("padding:5px;font-weight:bold;")
        self.log_btn.clicked.connect(self._toggle_scales)
        btn_row.addWidget(self.log_btn)

        ctrl_panel_lay.addWidget(btn_row_widget)
        
        main_splitter.addWidget(ctrl_panel)

        # --- plot & table (right) ---
        right_splitter = QSplitter(Qt.Vertical)
        
        # plot
        self.figure, self.ax = plt.subplots(figsize=(8, 6))
        self.canvas = FigureCanvas(self.figure)
        self.toolbar = NavigationToolbar(self.canvas, self)

        plot_w = QWidget()
        plot_lay = QVBoxLayout(plot_w)
        plot_lay.addWidget(self.toolbar)
        plot_lay.addWidget(self.canvas)
        right_splitter.addWidget(plot_w)
        
        # table
        self.table = QTableWidget()
        self.table.setColumnCount(5)
        self.table.setHorizontalHeaderLabels(
            ["Par de Curvas", "Legenda Curva 1", "Legenda Curva 2", "RMSE", "NRMSE"]
        )
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        right_splitter.addWidget(self.table)
        
        right_splitter.setSizes([600, 200])
        main_splitter.addWidget(right_splitter)
        main_splitter.setStretchFactor(0, 0) # Control panel doesn't stretch by default
        main_splitter.setStretchFactor(1, 1) # Plot stretches
        main_splitter.setSizes([400, 900])

    def _make_file_group(self, idx: int):
        grp = QGroupBox(f"File {idx + 1}")
        lay = QVBoxLayout()

        show_cb = QCheckBox("Visível (Show curve)")
        show_cb.setChecked(True)
        show_cb.stateChanged.connect(self._sync_ui_to_state)
        
        # File selector
        file_lay = QHBoxLayout()
        file_path_le = QLineEdit()
        file_path_le.setReadOnly(True)
        file_btn = QPushButton("Procurar...")
        file_btn.clicked.connect(lambda _, i=idx: self._select_file(i))
        file_lay.addWidget(file_path_le)
        file_lay.addWidget(file_btn)

        # Data combo boxes
        form_lay1 = QFormLayout()
        x_cb     = QComboBox()
        y_cb     = QComboBox()
        grp_cb   = QComboBox()
        val_cb   = QComboBox()
        offset_val = QLineEdit("0.0")
        offset_val.setValidator(QDoubleValidator(-10.0, 10.0, 4))
        
        form_lay1.addRow("Eixo X:", x_cb)
        form_lay1.addRow("Eixo Y:", y_cb)
        form_lay1.addRow("Agrupar por (VDS alvo):", grp_cb)
        form_lay1.addRow("Filtro Curva alvo:", val_cb)
        form_lay1.addRow("Horizontal Offset (V):", offset_val)

        # Styling options
        form_lay2 = QFormLayout()
        legend_le = QLineEdit()
        legend_le.setPlaceholderText("Deixe em branco para legenda automática")
        
        color_cb = QComboBox()
        color_cb.addItems(list(COLORS.keys()))
        
        line_cb = QComboBox()
        line_cb.addItems(list(LINE_STYLES.keys()))
        
        marker_cb = QComboBox()
        marker_cb.addItems(list(MARKERS.keys()))
        
        alpha_cb = QComboBox()
        alpha_cb.addItems(["1.0", "0.9", "0.85", "0.8", "0.7", "0.6", "0.5", "0.4", "0.3", "0.2", "0.1"])
        
        form_lay2.addRow("Legenda Personalizada:", legend_le)
        form_lay2.addRow("Cor:", color_cb)
        form_lay2.addRow("Tipo da Curva:", line_cb)
        form_lay2.addRow("Marcador:", marker_cb)
        form_lay2.addRow("Opacidade (Alpha):", alpha_cb)
        
        # When group changes, repopulate values, then sync
        grp_cb.currentIndexChanged.connect(lambda _, i=idx: self._populate_values_and_sync(i))
        
        # For remaining controls, just sync state and update plot
        for cb in (x_cb, y_cb, val_cb, color_cb, line_cb, marker_cb, alpha_cb):
            cb.currentIndexChanged.connect(self._sync_ui_to_state)
            
        offset_val.textChanged.connect(self._sync_ui_to_state)
        
        # Mark custom_legend flag ONLY when user types manually
        legend_le.textEdited.connect(lambda _, i=idx: self._on_legend_edited(i))
        legend_le.editingFinished.connect(self._sync_ui_to_state)

        lay.addWidget(show_cb)
        lay.addLayout(file_lay)
        lay.addLayout(form_lay1)
        lay.addWidget(QLabel("--- Estilos ---"))
        lay.addLayout(form_lay2)

        grp.setLayout(lay)
        return grp, {
            "group_box": grp,
            "filepath": file_path_le,
            "show": show_cb, "x": x_cb, "y": y_cb, 
            "grp": grp_cb, "val": val_cb, "offset": offset_val,
            "legend": legend_le, "color": color_cb, "line": line_cb, "marker": marker_cb, "alpha": alpha_cb
        }

    # ---- Logic ------------------------------------------------------------

    def _select_file(self, idx: int):
        if self.current_pair_idx < 0: return
        path, _ = QFileDialog.getOpenFileName(self, "Selecionar CSV", "", "CSV Files (*.csv)")
        if not path: return
        
        df, sweep_mode = load_flexible_csv(path)
        if df is None: return
        
        if self.current_pair_idx < 0: return
        pair = self.pairs[self.current_pair_idx]
        state = pair.c1 if idx == 0 else pair.c2
        
        state.filepath = path
        state.df = df
        
        cols = list(df.columns)
        
        # Smart defaults based on sweep mode
        if sweep_mode == "VDS":
            # IdVd Mode
            state.x_col = _find_col(cols, ["vds_true", "vds", "vd"]) or ""
            state.grp_col = _find_col(cols, ["vgs", "vg", "vgs_true"]) or ""
        else:
            # IdVg Mode
            state.x_col = _find_col(cols, ["vgs_true", "vg", "vgs"]) or ""
            state.grp_col = _find_col(cols, ["vd", "vds", "vds_true"]) or ""
            
        state.y_col = _find_col(cols, ["ids", "Id", "id"]) or ""
        state.val_str = ""
        
        # Auto trigger legend reset if it was auto
        if not state.custom_legend:
            state.legend = ""
            
        self.statusBar().showMessage(f"Arquivo carregado. Modo detectado: {sweep_mode}", 5000)
        self._sync_state_to_ui()


    def _populate_values(self, i: int):
        w = self.file_groups[i]
        
        if self.current_pair_idx < 0: return
        pair = self.pairs[self.current_pair_idx]
        df = pair.c1.df if i == 0 else pair.c2.df
        
        col = w["grp"].currentText()
        if df is None or not col: 
            w["val"].blockSignals(True)
            w["val"].clear()
            w["val"].blockSignals(False)
            return

        unique = sorted(df[col].round(3).unique())
        
        w["val"].blockSignals(True)
        w["val"].clear()
        w["val"].addItems([str(v) for v in unique])
        w["val"].blockSignals(False)

    def _populate_values_and_sync(self, i: int):
        if self._is_updating_ui: return
        self._populate_values(i)
        
        w = self.file_groups[i]
        if w["val"].count() > 0:
            w["val"].setCurrentIndex(0)
            
        self._sync_ui_to_state()

    # ---- Pair Management --------------------------------------------------

    def _add_pair(self):
        new_id = len(self.pairs) + 1
        p = ComparisonPair(name=f"Pair {new_id}")
        
        # No smart defaults anymore because there's no pre-loaded global CSV.
        
        self.pairs.append(p)
        
        self._is_updating_ui = True
        self.pair_cb.addItem(p.name)
        self.pair_cb.setCurrentIndex(len(self.pairs)-1)
        self.current_pair_idx = len(self.pairs)-1
        self._is_updating_ui = False
        
        self._sync_state_to_ui()

    def _delete_pair(self):
        if len(self.pairs) <= 1:
            QMessageBox.warning(self, "Warning", "Precisa ter pelo menos um par.")
            return
            
        idx = self.pair_cb.currentIndex()
        if idx >= 0:
            del self.pairs[idx]
            
            self._is_updating_ui = True
            self.pair_cb.removeItem(idx)
            self._is_updating_ui = False
            
            new_idx = max(0, idx - 1)
            self._is_updating_ui = True
            self.pair_cb.setCurrentIndex(new_idx)
            self.current_pair_idx = new_idx
            self._is_updating_ui = False
            
            self._sync_state_to_ui()

    def _on_pair_selected(self, idx):
        if self._is_updating_ui or idx < 0: return
        self.current_pair_idx = idx
        self._sync_state_to_ui()

    def _safe_math(self, text, field_name):
        """Validates math syntax. Returns original if OK, or stripped text if invalid."""
        if not text or "$" not in text:
            return text
        try:
            self._math_parser.parse(text)
            return text
        except Exception as e:
            msg = f"Erro de sintaxe no {field_name}: {str(e).splitlines()[-1]}"
            self.statusBar().showMessage(msg, 10000)
            print(f"[Math Error] {msg}")
            # Return text without $ to prevent matplotlib crash
            return text.replace("$", "")

    # ---- State Synchronization --------------------------------------------

    def _on_legend_edited(self, idx: int):
        if self.current_pair_idx < 0: return
        p = self.pairs[self.current_pair_idx]
        st = p.c1 if idx == 0 else p.c2
        st.custom_legend = True

    def _sync_state_to_ui(self):
        has_pair = (self.current_pair_idx >= 0)
        for i in range(2):
            self.file_groups[i]["group_box"].setEnabled(has_pair)
        
        if not has_pair:
            self.ax.clear()
            self.table.setRowCount(0)
            self.ax.text(0.5, 0.5, "Nenhuma curva criada. Crie uma Nova Curva.", ha="center", va="center", transform=self.ax.transAxes)
            self.canvas.draw()
            return
            
        self._is_updating_ui = True
        pair = self.pairs[self.current_pair_idx]
        states = [pair.c1, pair.c2]
        
        for i in range(2):
            w = self.file_groups[i]
            s = states[i]
            
            w["filepath"].setText(s.filepath)
            w["show"].setChecked(s.show)
            
            # Populate combos if df exists
            w["x"].blockSignals(True)
            w["y"].blockSignals(True)
            w["grp"].blockSignals(True)
            
            w["x"].clear()
            w["y"].clear()
            w["grp"].clear()
            
            if s.df is not None:
                cols = list(s.df.columns)
                w["x"].addItems(cols)
                w["y"].addItems(cols)
                w["grp"].addItems(cols)
                
            w["x"].blockSignals(False)
            w["y"].blockSignals(False)
            w["grp"].blockSignals(False)
            
            if s.x_col: w["x"].setCurrentText(s.x_col)
            if s.y_col: w["y"].setCurrentText(s.y_col)
            if s.grp_col: w["grp"].setCurrentText(s.grp_col)
            
            self._populate_values(i)
            if s.val_str: w["val"].setCurrentText(s.val_str)
            else:
                if w["val"].count() > 0:
                    w["val"].setCurrentIndex(0)
                    s.val_str = w["val"].currentText()
            
            w["offset"].setText(str(s.offset))
            w["legend"].setText(s.legend)
            w["color"].setCurrentText(s.color)
            w["line"].setCurrentText(s.linestyle)
            w["marker"].setCurrentText(s.marker)
            w["alpha"].setCurrentText(str(s.alpha))
            
        self._is_updating_ui = False
        self._sync_ui_to_state()

    def _sync_ui_to_state(self, *args):
        if self._is_updating_ui or self.current_pair_idx < 0: return
        pair = self.pairs[self.current_pair_idx]
        states = [pair.c1, pair.c2]
        
        for i in range(2):
            w = self.file_groups[i]
            s = states[i]
            
            s.show = w["show"].isChecked()
            s.x_col = w["x"].currentText()
            s.y_col = w["y"].currentText()
            s.grp_col = w["grp"].currentText()
            s.val_str = w["val"].currentText()
            try:
                s.offset = float(w["offset"].text().replace(",", "."))
            except:
                s.offset = 0.0
            
            s.color = w["color"].currentText()
            s.linestyle = w["line"].currentText()
            s.marker = w["marker"].currentText()
            try:
                s.alpha = float(w["alpha"].currentText())
            except ValueError:
                s.alpha = 0.85
            
            # --- Auto legend logic ---
            current_legend_text = w["legend"].text()
            if not s.custom_legend and s.filepath and s.x_col and s.y_col and s.grp_col and s.val_str:
                short = os.path.basename(s.filepath)[:20]
                auto_str = f"{short} {s.y_col} vs {s.x_col} (@{s.grp_col}={s.val_str})"
                if s.offset != 0:
                    auto_str += f" [Off: {s.offset:+.3f}V]"
                s.legend = auto_str
                
                self._is_updating_ui = True
                w["legend"].setText(auto_str)
                self._is_updating_ui = False
            else:
                s.legend = current_legend_text
                
        self._update_plot()

    # ---- plotting ---------------------------------------------------------

    def _update_plot(self):
        if self._is_updating_ui: return
        try:
            self._update_plot_impl()
        except Exception as e:
            print(f"[Plot Error] {e}")
            # Optionally show a small message on the plot if it fails
            self.ax.text(0.5, 0.05, f"Erro de renderização: {e}", 
                         color="red", transform=self.ax.transAxes, ha="center")
            self.canvas.draw()

    def _update_plot_impl(self):
        self.figure.clf()
        self.ax = self.figure.add_subplot(111)
        
        show_lin = self.lin_btn.isChecked()
        show_log = self.log_btn.isChecked()
        
        if show_log:
            self.ax2 = self.ax.twinx()
        else:
            self.ax2 = None
            
        self.table.setRowCount(0)
        plotted = False
        
        for r_idx, pair in enumerate(self.pairs):
            states = [pair.c1, pair.c2]
            curve_data = [] 
            
            for i in range(2):
                s = states[i]
                df = s.df
                if df is None or not s.show:
                    curve_data.append(None)
                    continue
                    
                if not (s.x_col and s.y_col and s.grp_col and s.val_str):
                    curve_data.append(None)
                    continue
                    
                try:
                    val = float(s.val_str)
                    mask = df[s.grp_col].round(3) == val
                    sub = df.loc[mask].sort_values(s.x_col)
                    if sub.empty:
                        curve_data.append(None)
                        continue
                        
                    xs = sub[s.x_col].values + s.offset
                    ys = sub[s.y_col].values
                    
                    c  = COLORS.get(s.color, "blue")
                    ls = LINE_STYLES.get(s.linestyle, "-")
                    m  = MARKERS.get(s.marker, "None")
                    safe_legend = self._safe_math(s.legend, f"Par {r_idx+1} (Curva {i+1})")
                    
                    calc_xs = xs
                    calc_ys = ys
                    
                    if show_lin:
                        self.ax.plot(xs, ys, color=c, linestyle=ls, marker=m, 
                                     label=safe_legend, markersize=4, alpha=s.alpha)
                        plotted = True
                                     
                    if show_log:
                        valid_mask = ys > 0
                        xs_log = xs[valid_mask]
                        ys_log = ys[valid_mask]
                        if len(xs_log) > 0:
                            log_legend = safe_legend + " (Log)" if show_lin else safe_legend
                            
                            self.ax2.plot(xs_log, ys_log, color=c, linestyle=ls, marker=m,
                                          label=log_legend, markersize=4, alpha=s.alpha)
                            plotted = True
                            
                            if not show_lin:
                                calc_xs = xs_log
                                calc_ys = ys_log
                    
                    if len(calc_xs) > 0 and (show_lin or show_log):
                        curve_data.append((calc_xs, calc_ys))
                    else:
                        curve_data.append(None)
                except Exception as exc:
                    print(f"[plot] Pair '{pair.name}' curve {i+1}: {exc}")
                    curve_data.append(None)
            
            # --- Metrics for Table ---
            rmse_str, nrmse_str = "-", "-"
            if len(curve_data) == 2 and curve_data[0] is not None and curve_data[1] is not None:
                x1, y1 = curve_data[0]
                x2, y2 = curve_data[1]
    
                x_lo = max(x1.min(), x2.min())
                x_hi = min(x1.max(), x2.max())
    
                if x_lo < x_hi:
                    common_x = np.sort(np.unique(np.concatenate([
                        x1[(x1 >= x_lo) & (x1 <= x_hi)],
                        x2[(x2 >= x_lo) & (x2 <= x_hi)],
                    ])))
    
                    y1_interp = np.interp(common_x, x1, y1)
                    y2_interp = np.interp(common_x, x2, y2)
    
                    diff = y1_interp - y2_interp
                    rmse = np.sqrt(np.mean(diff ** 2))
                    y_range = max(y2_interp.max() - y2_interp.min(), y1_interp.max() - y1_interp.min())
                    nrmse_pct = (rmse / y_range * 100) if y_range > 0 else float('nan')
                    
                    rmse_str = f"{rmse:.4e}"
                    nrmse_str = f"{nrmse_pct:.2f}%"
            
            # Insert into Table
            self.table.insertRow(r_idx)
            self.table.setItem(r_idx, 0, QTableWidgetItem(pair.name))
            l1 = states[0].legend if states[0].show else "(Oculto)"
            l2 = states[1].legend if states[1].show else "(Oculto)"
            self.table.setItem(r_idx, 1, QTableWidgetItem(l1))
            self.table.setItem(r_idx, 2, QTableWidgetItem(l2))
            self.table.setItem(r_idx, 3, QTableWidgetItem(rmse_str))
            self.table.setItem(r_idx, 4, QTableWidgetItem(nrmse_str))

        if plotted:
            title_text = self._safe_math(self.plot_title_le.text(), "Título")
            self.ax.set_title(title_text, fontsize=11)
            self.ax.set_xlabel(r"$V_{GS}$ (V)")
            
            # Map choice to matplotlib 'loc'
            leg_map = {
                "Melhor Ajuste (Auto)": "best",
                "Topo-Esquerda": "upper left",
                "Topo-Direita": "upper right",
                "Base-Esquerda": "lower left",
                "Base-Direita": "lower right",
                "Centro": "center",
                "Centro-Esquerda": "center left",
                "Centro-Direita": "center right"
            }
            leg_loc = leg_map.get(self.leg_pos_cb.currentText(), "best")
            
            if show_lin:
                self.ax.set_ylabel(r"$I_{DS}$ (A) [Linear]")
                self.ax.set_yscale("linear")
                self.ax.ticklabel_format(axis="y", style="sci", scilimits=(0, 0))
                self.ax.grid(True, ls="--", alpha=0.6, which="both")
                self.ax.legend(loc=leg_loc, fontsize=8)
            else:
                self.ax.set_yticks([])
                
            if show_log and self.ax2 is not None:
                self.ax2.set_ylabel(r"$I_{DS}$ (A) [Log]")
                self.ax2.set_yscale("log")
                if not show_lin:
                    self.ax2.grid(True, ls="--", alpha=0.6, which="both")
                    self.ax2.legend(loc=leg_loc, fontsize=8)
                else:
                    # If both are active, and same loc is chosen, they might overlap. 
                    # 'best' usually handles this but separate axes legends are tricky.
                    self.ax2.legend(loc=leg_loc, fontsize=8)

            try:
                self.figure.tight_layout()
            except Exception as e:
                self.statusBar().showMessage(f"Erro no ajuste de layout: {e}", 5000)
        else:
            self.ax.set_yticks([])
            self.ax.set_xticks([])
            self.ax.text(0.5, 0.5, "Nenhum dado válido para exibir. Ajuste os eixos.",
                         ha="center", va="center", transform=self.ax.transAxes)

        self.canvas.draw()

    def _toggle_scales(self):
        if self.lin_btn.isChecked():
            self.lin_btn.setStyleSheet("background:#4CAF50;color:white;padding:10px;font-weight:bold;")
        else:
            self.lin_btn.setStyleSheet("padding:10px;font-weight:bold;")
            
        if self.log_btn.isChecked():
            self.log_btn.setStyleSheet("background:#FF9800;color:white;padding:10px;font-weight:bold;")
        else:
            self.log_btn.setStyleSheet("padding:10px;font-weight:bold;")
        self._update_plot()


# ---------------------------------------------------------------------------
#  Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    # Enable High DPI scaling
    if hasattr(Qt, 'AA_EnableHighDpiScaling'):
        QApplication.setAttribute(Qt.AA_EnableHighDpiScaling, True)
    if hasattr(Qt, 'AA_UseHighDpiPixmaps'):
        QApplication.setAttribute(Qt.AA_UseHighDpiPixmaps, True)

    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    
    # Set a more modern default font for the UI
    font = QFont("Segoe UI", 9) if os.name == 'nt' else QFont("Ubuntu", 9)
    app.setFont(font)

    win = CurveComparator()
    win.show()
    sys.exit(app.exec_())
