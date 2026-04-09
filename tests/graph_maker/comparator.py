#!/usr/bin/env python3
"""
MOSFET Curve Comparator
-----------------------
Loads exactly two CSV files from a given directory (ESP32 or Keysight format),
lets the user choose X / Y / grouping columns independently for each file,
and overlays the selected curves on a single matplotlib plot.

Usage:
    python comparator.py <directory_with_2_csvs>
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
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QComboBox, QMessageBox, QGroupBox, QSplitter,
    QCheckBox,
)
from PyQt5.QtCore import Qt


# ---------------------------------------------------------------------------
#  CSV loaders
# ---------------------------------------------------------------------------

def load_flexible_csv(filepath: str) -> pd.DataFrame | None:
    """Return a DataFrame from either an ESP32 CSV or a Keysight B2900 CSV."""
    try:
        with open(filepath, "r", encoding="utf-8-sig", errors="ignore") as fh:
            head = [fh.readline() for _ in range(20)]

        is_keysight = any(
            l.strip().startswith(("SetupTitle", "PrimitiveTest", "TestParameter"))
            for l in head
        )

        if is_keysight:
            headers, rows = [], []
            with open(filepath, "r", encoding="utf-8-sig", errors="ignore") as fh:
                for line in fh:
                    parts = [p.strip() for p in line.split(",")]
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
                return pd.DataFrame(rows, columns=headers)
            return None

        # ESP32-style CSV (lines starting with # are comments)
        df = pd.read_csv(filepath, comment="#")
        df.columns = df.columns.str.strip()
        return df

    except Exception as exc:
        print(f"[load] {filepath}: {exc}")
        return None


# ---------------------------------------------------------------------------
#  Helper: guess sensible default column indices
# ---------------------------------------------------------------------------

def _find_col(columns, candidates):
    """Return the first column whose name matches any candidate (case-insensitive)."""
    lower = {c.lower(): c for c in columns}
    for cand in candidates:
        if cand.lower() in lower:
            return lower[cand.lower()]
    return None


# ---------------------------------------------------------------------------
#  Main window
# ---------------------------------------------------------------------------

class CurveComparator(QMainWindow):
    def __init__(self, directory: str):
        super().__init__()
        self.setWindowTitle("MOSFET Curve Comparator")
        self.resize(1200, 750)

        self.dfs = [None, None]
        self.names = ["", ""]
        self.directory = directory

        self._build_ui()
        self._load_files()

    # ---- UI ---------------------------------------------------------------

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QHBoxLayout(central)

        # --- controls (left) ---
        ctrl = QWidget()
        ctrl.setMaximumWidth(420)
        ctrl_lay = QVBoxLayout(ctrl)

        self.file_groups = []
        for idx in (0, 1):
            grp, widgets = self._make_file_group(idx)
            self.file_groups.append(widgets)
            ctrl_lay.addWidget(grp)

        ctrl_lay.addStretch()

        btn_row = QHBoxLayout()
        btn = QPushButton("Update Plot")
        btn.setStyleSheet("background:#4CAF50;color:white;padding:10px;font-weight:bold;")
        btn.clicked.connect(self._update_plot)
        btn_row.addWidget(btn)

        self.log_btn = QPushButton("Log Y")
        self.log_btn.setCheckable(True)
        self.log_btn.setStyleSheet(
            "padding:10px;font-weight:bold;"
        )
        self.log_btn.clicked.connect(self._toggle_log)
        btn_row.addWidget(self.log_btn)

        ctrl_lay.addLayout(btn_row)

        root.addWidget(ctrl)

        # --- plot (right) ---
        self.figure, self.ax = plt.subplots(figsize=(8, 6))
        self.canvas = FigureCanvas(self.figure)
        self.toolbar = NavigationToolbar(self.canvas, self)

        plot_w = QWidget()
        plot_lay = QVBoxLayout(plot_w)
        plot_lay.addWidget(self.toolbar)
        plot_lay.addWidget(self.canvas)
        root.addWidget(plot_w)

    def _make_file_group(self, idx: int):
        grp = QGroupBox(f"File {idx + 1}")
        lay = QVBoxLayout()

        show_cb = QCheckBox("Show curve")
        show_cb.setChecked(True)
        show_cb.stateChanged.connect(self._update_plot)

        lbl = QLabel("…")
        lbl.setWordWrap(True)

        x_cb   = QComboBox()
        y_cb   = QComboBox()
        grp_cb = QComboBox()
        val_cb = QComboBox()

        # When the "group by" column changes, repopulate unique values
        grp_cb.currentIndexChanged.connect(lambda _, i=idx: self._populate_values(i))
        # Auto-update plot on any combobox change
        for cb in (x_cb, y_cb, val_cb):
            cb.currentIndexChanged.connect(self._update_plot)

        lay.addWidget(show_cb)
        lay.addWidget(lbl)
        for title, cb in [("X Axis:", x_cb), ("Y Axis:", y_cb),
                          ("Group by (constant VDS col):", grp_cb),
                          ("Select curve:", val_cb)]:
            lay.addWidget(QLabel(title))
            lay.addWidget(cb)

        grp.setLayout(lay)
        return grp, {"label": lbl, "show": show_cb, "x": x_cb, "y": y_cb, "grp": grp_cb, "val": val_cb}

    # ---- data loading -----------------------------------------------------

    def _load_files(self):
        csvs = sorted(glob.glob(os.path.join(self.directory, "*.csv")))
        if len(csvs) != 2:
            QMessageBox.critical(self, "Error",
                f"Expected 2 CSV files in\n{self.directory}\nFound {len(csvs)}.")
            return

        for i, path in enumerate(csvs):
            self.names[i] = os.path.basename(path)
            self.dfs[i] = load_flexible_csv(path)
            if self.dfs[i] is None:
                QMessageBox.critical(self, "Error", f"Could not load:\n{path}")
                return

        for i in range(2):
            w = self.file_groups[i]
            cols = list(self.dfs[i].columns)
            w["label"].setText(self.names[i])

            # Block signals while populating to avoid premature updates
            for cb in (w["x"], w["y"], w["grp"]):
                cb.blockSignals(True)
                cb.clear()
                cb.addItems(cols)

            # Smart defaults
            x_def = _find_col(cols, ["vgs_true", "Vg", "vg"])
            y_def = _find_col(cols, ["ids", "Id", "id"])
            g_def = _find_col(cols, ["vd", "Vd", "vds_true"])   # prefer target col

            if x_def: w["x"].setCurrentText(x_def)
            if y_def: w["y"].setCurrentText(y_def)
            if g_def: w["grp"].setCurrentText(g_def)

            for cb in (w["x"], w["y"], w["grp"]):
                cb.blockSignals(False)

            self._populate_values(i)

        self._update_plot()

    def _populate_values(self, idx: int):
        w  = self.file_groups[idx]
        df = self.dfs[idx]
        col = w["grp"].currentText()
        if df is None or not col:
            return

        unique = sorted(df[col].round(3).unique())

        w["val"].blockSignals(True)
        w["val"].clear()
        w["val"].addItems([str(v) for v in unique])
        w["val"].blockSignals(False)

    # ---- plotting ---------------------------------------------------------

    def _update_plot(self):
        self.ax.clear()
        colors  = ["#1f77b4", "#d62728"]
        markers = ["o-", "x--"]
        msizes  = [3, 4]
        plotted = False
        curve_data = []          # will hold (x_array, y_array) per visible curve

        for i in range(2):
            w  = self.file_groups[i]
            df = self.dfs[i]
            if df is None or not w["show"].isChecked():
                curve_data.append(None)
                continue

            x_col   = w["x"].currentText()
            y_col   = w["y"].currentText()
            grp_col = w["grp"].currentText()
            val_str = w["val"].currentText()

            if not (x_col and y_col and grp_col and val_str):
                curve_data.append(None)
                continue

            try:
                val = float(val_str)
                mask = df[grp_col].round(3) == val
                sub = df.loc[mask].sort_values(x_col)
                if sub.empty:
                    curve_data.append(None)
                    continue

                xs = sub[x_col].values
                ys = sub[y_col].values
                curve_data.append((xs, ys))

                short = self.names[i][:20]
                label = f"{short}  {y_col} vs {x_col} (@{grp_col}={val_str})"
                self.ax.plot(xs, ys, markers[i],
                             label=label, markersize=msizes[i],
                             alpha=0.9, color=colors[i])
                plotted = True
            except Exception as exc:
                print(f"[plot] file {i+1}: {exc}")
                curve_data.append(None)

        # --- Compute RMSE / MAE when both curves are visible ---------------
        metrics_text = ""
        if len(curve_data) == 2 and curve_data[0] is not None and curve_data[1] is not None:
            x1, y1 = curve_data[0]
            x2, y2 = curve_data[1]

            # Overlapping x-range
            x_lo = max(x1.min(), x2.min())
            x_hi = min(x1.max(), x2.max())

            if x_lo < x_hi:
                # Common grid: union of both x-values inside the overlap
                common_x = np.sort(np.unique(np.concatenate([
                    x1[(x1 >= x_lo) & (x1 <= x_hi)],
                    x2[(x2 >= x_lo) & (x2 <= x_hi)],
                ])))

                # Interpolate both curves onto the common grid
                y1_interp = np.interp(common_x, x1, y1)
                y2_interp = np.interp(common_x, x2, y2)

                diff = y1_interp - y2_interp
                rmse = np.sqrt(np.mean(diff ** 2))
                mae  = np.mean(np.abs(diff))

                # NRMSE: normalized by the Y range of the reference (file 2)
                y_range = max(y2_interp.max() - y2_interp.min(), y1_interp.max() - y1_interp.min())
                nrmse_pct = (rmse / y_range * 100) if y_range > 0 else float('nan')

                metrics_text = (f"RMSE = {rmse:.4e}   |   MAE = {mae:.4e}   |   "
                                f"NRMSE = {nrmse_pct:.2f}%   (N={len(common_x)} pts)")

        if plotted:
            title = "MOSFET Curve Comparison"
            if metrics_text:
                title += f"\n{metrics_text}"
            self.ax.set_title(title, fontsize=10)
            self.ax.set_xlabel("VGS (V)")
            self.ax.set_ylabel("Ids (A)")
            self.ax.grid(True, ls="--", alpha=0.6, which="both")
            self.ax.legend(fontsize=8)

            if self.log_btn.isChecked():
                self.ax.set_yscale("log")
            else:
                self.ax.set_yscale("linear")
                self.ax.ticklabel_format(axis="y", style="sci", scilimits=(0, 0))

            self.figure.tight_layout()
        else:
            self.ax.text(0.5, 0.5, "No data – configure axes",
                         ha="center", va="center", transform=self.ax.transAxes)

        self.canvas.draw()

    def _toggle_log(self):
        """Toggle log/linear scale and update button style."""
        if self.log_btn.isChecked():
            self.log_btn.setStyleSheet(
                "background:#FF9800;color:white;padding:10px;font-weight:bold;"
            )
            self.log_btn.setText("Linear Y")
        else:
            self.log_btn.setStyleSheet(
                "padding:10px;font-weight:bold;"
            )
            self.log_btn.setText("Log Y")
        self._update_plot()


# ---------------------------------------------------------------------------
#  Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python comparator.py <directory_with_2_csvs>")
        sys.exit(1)

    d = sys.argv[1]
    if not os.path.isdir(d):
        print(f"Error: {d} is not a directory"); sys.exit(1)

    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    win = CurveComparator(d)
    win.show()
    sys.exit(app.exec_())
