#!/usr/bin/env python3
"""
compare_iv.py — Análise Estatística Comparativa de Curvas IV de MOSFET
=======================================================================
Compara um dataset de REFERÊNCIA (formato B2902B) contra um dataset MEDIDO
(formato CSV do firmware ESP32), calculando métricas estatísticas por curva VDS.

Métricas por curva (por VDS):
  • RMSE    — Root Mean Square Error (erro médio quadrático)
  • R²      — Coeficiente de Determinação (qualidade do ajuste)
  • χ²      — Qui-Quadrado reduzido (consistência com ruído)
  • Var     — Variância dos resíduos
  • σ       — Desvio Padrão dos resíduos

Uso:
  python compare_iv.py <referencia.csv> <medida.csv> [--output saida.pdf] [--log] [--no-show]

Exemplo:
  python compare_iv.py "IVG-Triodo - 1 BS170 - 1.csv" "triodoBS170_noOversampling.csv"
"""

import argparse
import sys
import os
import warnings
import numpy as np
import pandas as pd

# Força backend não-interativo se não há display disponível
import matplotlib
if not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY"):
    matplotlib.use("Agg")

import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.ticker import LogLocator, LogFormatter
from scipy.interpolate import interp1d

# ─── Constantes ──────────────────────────────────────────────────────────────

VDS_TOL        = 0.015        # Tolerância para match de VDS entre datasets (V)
EPS            = 1e-15        # Evita divisão por zero no chi-quadrado
CURRENT_UNIT   = "A"          # Unidade de corrente
VDS_ROUND      = 3            # Casas decimais para comparar VDS

# Paleta de cores
COLOR_REF  = "#2196F3"        # Azul  → referência
COLOR_MEAS = "#F44336"        # Vermelho → medida

# ─── Parsers de Dados ─────────────────────────────────────────────────────────

def parse_b2902b(path: str) -> pd.DataFrame:
    """
    Lê um arquivo CSV no formato proprietário B2902B (Keysight).
    Procura linhas DataName e DataValue, extrai colunas Vg (VGS), Vd (VDS), Id (IDS).

    Retorna DataFrame com colunas: vgs [V], vds [V], ids [A]
    """
    header = None
    rows = []

    with open(path, encoding="utf-8-sig", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n\r")
            if line.startswith("DataName"):
                parts = [x.strip() for x in line.split(",")]
                header = parts[1:]          # remove 'DataName' tag
            elif line.startswith("DataValue"):
                if header is None:
                    continue
                parts = [x.strip() for x in line.split(",")]
                values = parts[1:]
                d = dict(zip(header, values))
                try:
                    rows.append({
                        "vgs": float(d["Vg"]),
                        "vds": float(d["Vd"]),
                        "ids": float(d["Id"]),
                    })
                except (KeyError, ValueError):
                    continue

    if not rows:
        raise ValueError(f"Nenhum dado DataValue encontrado em: {path}")

    df = pd.DataFrame(rows)
    print(f"[B2902B] Carregado: {len(df)} pontos | "
          f"VDS: {sorted(df['vds'].round(VDS_ROUND).unique())} V")
    return df


def parse_esp32_csv(path: str) -> pd.DataFrame:
    """
    Lê um arquivo CSV gerado pelo firmware ESP32.
    Ignora linhas de comentário (#) e usa a primeira linha sem # como cabeçalho.
    Espera colunas: timestamp, vds, vgs, vsh, ids  (ou subset com vds, vgs, ids).

    Retorna DataFrame com colunas: vgs [V], vds [V], ids [A]
    """
    rows = []
    header = None

    with open(path, encoding="utf-8-sig", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n\r")
            if not line or line.startswith("#"):
                continue
            parts = [x.strip() for x in line.split(",")]
            if header is None:
                header = [h.lower() for h in parts]
                continue
            if len(parts) < len(header):
                continue
            d = dict(zip(header, parts))
            try:
                rows.append({
                    "vgs": float(d["vgs"]),
                    "vds": float(d["vds"]),
                    "ids": float(d["ids"]),
                })
            except (KeyError, ValueError):
                continue

    if not rows:
        raise ValueError(f"Nenhum dado encontrado em: {path}")

    df = pd.DataFrame(rows)
    print(f"[ESP32 ] Carregado: {len(df)} pontos | "
          f"VDS: {sorted(df['vds'].round(VDS_ROUND).unique())} V")
    return df


def auto_detect_format(path: str) -> pd.DataFrame:
    """Detecta automaticamente o formato do arquivo e chama o parser correto."""
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        first_lines = [f.readline() for _ in range(5)]

    content = "".join(first_lines)
    if "DataName" in content or "DataValue" in content or "SetupTitle" in content:
        return parse_b2902b(path)
    elif "MOSFET Characterization" in content or "vds" in content.lower():
        return parse_esp32_csv(path)
    else:
        # Tenta B2902B, depois ESP32
        try:
            return parse_b2902b(path)
        except ValueError:
            return parse_esp32_csv(path)


# ─── Métricas Estatísticas ────────────────────────────────────────────────────

def compute_statistics(ref_ids: np.ndarray, meas_ids: np.ndarray) -> dict:
    """
    Calcula métricas estatísticas entre correntes de referência e medida.

    Parâmetros
    ----------
    ref_ids  : array de correntes da referência (valores "verdadeiros")
    meas_ids : array de correntes da medida (valores interpolados nos pontos da ref)

    Retorna dicionário com:
        rmse  — Root Mean Square Error [A]
        r2    — Coeficiente de determinação [adimensional]
        chi2  — Qui-quadrado reduzido [adimensional]
        var   — Variância dos resíduos [A²]
        std   — Desvio padrão dos resíduos [A]
        n     — Número de pontos usados
        mean_residual — Média dos resíduos [A]
    """
    n = len(ref_ids)
    if n < 2:
        return dict(rmse=np.nan, r2=np.nan, chi2=np.nan, var=np.nan, std=np.nan,
                    n=n, mean_residual=np.nan)

    residuals = ref_ids - meas_ids
    mean_ref  = np.mean(ref_ids)

    # RMSE
    rmse = np.sqrt(np.mean(residuals**2))

    # R²
    ss_res = np.sum(residuals**2)
    ss_tot = np.sum((ref_ids - mean_ref)**2)
    r2 = 1.0 - (ss_res / ss_tot) if ss_tot > EPS else np.nan

    # Chi² reduzido  (normalizado por |meas|² para evitar escala de corrente)
    denominator = np.maximum(np.abs(meas_ids)**2, EPS)
    chi2 = np.mean(residuals**2 / denominator)

    # Variância e Desvio Padrão dos resíduos
    var = np.var(residuals, ddof=1)
    std = np.std(residuals, ddof=1)

    # Média dos resíduos (bias)
    mean_residual = np.mean(residuals)

    return dict(rmse=rmse, r2=r2, chi2=chi2, var=var, std=std,
                n=n, mean_residual=mean_residual)


# ─── Função de Formatação de Unidade ─────────────────────────────────────────

def fmt_current(val: float, decimals: int = 3) -> str:
    """Formata valor de corrente com prefixo SI apropriado."""
    if np.isnan(val):
        return "N/A"
    abs_val = abs(val)
    if abs_val == 0:
        return "0 A"
    elif abs_val >= 1e-3:
        return f"{val*1e3:.{decimals}f} mA"
    elif abs_val >= 1e-6:
        return f"{val*1e6:.{decimals}f} µA"
    elif abs_val >= 1e-9:
        return f"{val*1e9:.{decimals}f} nA"
    elif abs_val >= 1e-12:
        return f"{val*1e12:.{decimals}f} pA"
    else:
        return f"{val:.2e} A"


def fmt_current_sq(val: float) -> str:
    """Formata A² para variância."""
    if np.isnan(val):
        return "N/A"
    abs_val = abs(val)
    if abs_val >= 1e-6:
        return f"{val*1e6:.3e} µA²"
    elif abs_val >= 1e-12:
        return f"{val*1e12:.3e} pA²"
    else:
        return f"{val:.2e} A²"


# ─── Plotagem ─────────────────────────────────────────────────────────────────

def plot_vds_panel(ax: plt.Axes,
                   vds_val: float,
                   ref_sub: pd.DataFrame,
                   meas_sub: pd.DataFrame,
                   meas_interp: np.ndarray,
                   stats: dict,
                   use_log: bool,
                   ref_label: str,
                   meas_label: str) -> None:
    """
    Plota um painel individual para um valor de VDS:
      - Curvas sobrepostas IDS × VGS (escala log ou linear)
      - Tabela de métricas anotada dentro do gráfico
    """
    # ── Curvas ──
    ax.plot(ref_sub["vgs"].values,
            np.abs(ref_sub["ids"].values),
            color=COLOR_REF,
            linewidth=1.8,
            label=ref_label,
            zorder=3)

    ax.plot(meas_sub["vgs"].values,
            np.abs(meas_sub["ids"].values),
            color=COLOR_MEAS,
            linewidth=1.5,
            linestyle="--",
            alpha=0.9,
            label=meas_label,
            zorder=3)

    # Eixos
    vgs_min  = min(ref_sub["vgs"].min(), meas_sub["vgs"].min())
    vgs_max  = max(ref_sub["vgs"].max(), meas_sub["vgs"].max())

    ax.set_xlim(vgs_min, vgs_max)
    ax.set_xlabel("V$_{GS}$ (V)", fontsize=8)
    ax.set_ylabel("|I$_{DS}$| (A)", fontsize=8)
    ax.set_title(f"V$_{{DS}}$ = {vds_val:.3f} V", fontsize=9, fontweight="bold", pad=4)
    ax.tick_params(labelsize=7)
    ax.grid(True, which="both", linestyle="--", linewidth=0.4, alpha=0.5)
    ax.legend(fontsize=6, loc="upper left", framealpha=0.7)

    if use_log:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            ax.set_yscale("log")

    # ── Tabela de Métricas ──
    r2_str   = f"{stats['r2']:.5f}"   if not np.isnan(stats['r2'])   else "N/A"
    chi2_str = f"{stats['chi2']:.4e}" if not np.isnan(stats['chi2']) else "N/A"

    metrics_text = (
        f"N = {stats['n']}\n"
        f"RMSE = {fmt_current(stats['rmse'])}\n"
        f"R²   = {r2_str}\n"
        f"χ²   = {chi2_str}\n"
        f"Var  = {fmt_current_sq(stats['var'])}\n"
        f"σ    = {fmt_current(stats['std'])}\n"
        f"Bias = {fmt_current(stats['mean_residual'])}"
    )

    ax.text(0.98, 0.02, metrics_text,
            transform=ax.transAxes,
            fontsize=6.5,
            verticalalignment="bottom",
            horizontalalignment="right",
            fontfamily="monospace",
            bbox=dict(boxstyle="round,pad=0.4", facecolor="#F5F5F5",
                      edgecolor="#BDBDBD", alpha=0.92))


# ─── Main ─────────────────────────────────────────────────────────────────────

def find_common_vds(ref_df: pd.DataFrame,
                    meas_df: pd.DataFrame,
                    tol: float = VDS_TOL) -> list[float]:
    """
    Encontra valores de VDS presentes em ambos os datasets (com tolerância).
    Retorna lista de VDS da referência que têm correspondência na medida.
    """
    ref_vds  = sorted(ref_df["vds"].unique())
    meas_vds = sorted(meas_df["vds"].unique())

    common = []
    for rv in ref_vds:
        for mv in meas_vds:
            if abs(rv - mv) <= tol:
                common.append(rv)
                break

    return common


def get_meas_subset_for_vds(meas_df: pd.DataFrame,
                              ref_vds: float,
                              tol: float = VDS_TOL) -> pd.DataFrame:
    """Retorna subset da medida para um VDS da referência (com tolerância)."""
    mask = np.abs(meas_df["vds"] - ref_vds) <= tol
    return meas_df[mask].copy().sort_values("vgs").reset_index(drop=True)


def main():
    parser = argparse.ArgumentParser(
        description="Análise estatística comparativa de curvas IV de MOSFET"
    )
    parser.add_argument("referencia",  help="Arquivo CSV de referência (B2902B ou ESP32)")
    parser.add_argument("medida",      help="Arquivo CSV da medida (B2902B ou ESP32)")
    parser.add_argument("--output",    default=None,
                        help="Arquivo de saída (PDF/PNG). Ex: resultado.pdf")
    parser.add_argument("--no-show",   action="store_true",
                        help="Não exibir janela interativa")
    parser.add_argument("--log",       action="store_true",
                        help="Usar escala logarítmica no eixo de corrente")
    parser.add_argument("--no-log",    action="store_true",
                        help="Forçar escala linear (sobrepõe --log)")
    args = parser.parse_args()

    use_log = args.log and not args.no_log

    print("\n" + "═"*60)
    print(" ANÁLISE ESTATÍSTICA — CURVAS IV MOSFET")
    print("═"*60)
    print(f" Referência : {args.referencia}")
    print(f" Medida     : {args.medida}")
    print(f" Escala Y   : {'logarítmica' if use_log else 'linear'}")
    print("─"*60 + "\n")

    # ── Carregamento ──
    ref_df  = auto_detect_format(args.referencia)
    meas_df = auto_detect_format(args.medida)

    # ── VDS comuns ──
    common_vds = find_common_vds(ref_df, meas_df)
    if not common_vds:
        print("[ERRO] Nenhum VDS em comum entre os arquivos (tolerância ±"
              f"{VDS_TOL} V).")
        print(f"  Referência : {sorted(ref_df['vds'].round(3).unique())}")
        print(f"  Medida     : {sorted(meas_df['vds'].round(3).unique())}")
        sys.exit(1)

    print(f"\n✔ VDS comuns encontrados: {[round(v,3) for v in common_vds]}\n")

    # ── Layout da figura ──
    ncols = 3
    nrows = int(np.ceil(len(common_vds) / ncols))
    fig_w = ncols * 5.0
    fig_h = nrows * 4.2

    fig = plt.figure(figsize=(fig_w, fig_h), facecolor="#FAFAFA")
    fig.suptitle(
        "Comparação Estatística: Curvas I$_{DS}$ × V$_{GS}$ por V$_{DS}$\n"
        f"Referência: {args.referencia.split('/')[-1]}   ●   "
        f"Medida: {args.medida.split('/')[-1]}",
        fontsize=11, fontweight="bold", y=0.995
    )

    gs = gridspec.GridSpec(nrows, ncols, figure=fig,
                           hspace=0.55, wspace=0.38,
                           left=0.06, right=0.98,
                           top=0.94, bottom=0.05)

    # Nomes curtos para a legenda
    ref_label  = "Ref. B2902B"
    meas_label = "ESP32"

    # ── Tabela de resumo no terminal ──
    print(f"{'VDS (V)':>8} | {'N':>5} | {'RMSE':>12} | {'R²':>8} | "
          f"{'χ²':>10} | {'Var':>12} | {'σ':>12}")
    print("─"*85)

    all_stats = []

    for idx, vds_val in enumerate(common_vds):
        row = idx // ncols
        col = idx % ncols
        ax  = fig.add_subplot(gs[row, col])

        ref_sub  = ref_df[np.abs(ref_df["vds"] - vds_val) <= VDS_TOL].copy()
        ref_sub  = ref_sub.sort_values("vgs").reset_index(drop=True)

        meas_sub = get_meas_subset_for_vds(meas_df, vds_val)

        # Interpolação da medida nos pontos VGS da referência
        vgs_ref   = ref_sub["vgs"].values
        vgs_meas  = meas_sub["vgs"].values
        ids_meas  = meas_sub["ids"].values

        vgs_min_common = max(vgs_ref.min(), vgs_meas.min())
        vgs_max_common = min(vgs_ref.max(), vgs_meas.max())

        mask_ref = (vgs_ref >= vgs_min_common) & (vgs_ref <= vgs_max_common)

        if mask_ref.sum() < 2 or len(vgs_meas) < 2:
            print(f"{vds_val:>8.3f} |     — | [dados insuficientes para interpolacao]")
            ax.set_title(f"V$_{{DS}}$ = {vds_val:.3f} V — sem dados", fontsize=9)
            ax.axis("off")
            continue

        try:
            interp_fn   = interp1d(vgs_meas, ids_meas,
                                   kind="linear",
                                   bounds_error=False,
                                   fill_value="extrapolate")
            meas_interp = interp_fn(vgs_ref[mask_ref])
        except Exception as e:
            print(f"[AVISO] Interpolacao falhou para VDS={vds_val:.3f}: {e}")
            meas_interp = np.interp(vgs_ref[mask_ref], vgs_meas, ids_meas)

        stats = compute_statistics(ref_sub["ids"].values[mask_ref], meas_interp)
        all_stats.append({"vds": vds_val, **stats})

        plot_vds_panel(ax, vds_val,
                       ref_sub, meas_sub,
                       meas_interp, stats,
                       use_log,
                       ref_label, meas_label)

        # Linha do terminal — extrair strings antes do print p/ evitar f-string aninhada
        r2_str_t   = f"{stats['r2']:.5f}"   if not np.isnan(stats['r2'])   else "N/A"
        chi2_str_t = f"{stats['chi2']:.3e}" if not np.isnan(stats['chi2']) else "N/A"
        print(f"{vds_val:>8.3f} | {stats['n']:>5d} | "
              f"{fmt_current(stats['rmse']):>12} | "
              f"{r2_str_t:>8} | "
              f"{chi2_str_t:>10} | "
              f"{fmt_current(stats['var']):>12} | "
              f"{fmt_current(stats['std']):>12}")

    print("─"*85 + "\n")

    # ── Ocultar painéis extras ──
    total_panels = nrows * ncols
    for extra_idx in range(len(common_vds), total_panels):
        row_e = extra_idx // ncols
        col_e = extra_idx % ncols
        ax_e  = fig.add_subplot(gs[row_e, col_e])
        ax_e.axis("off")

    # ── Salvar / Exibir ──
    if args.output:
        fig.savefig(args.output, dpi=200, bbox_inches="tight")
        print(f"✔ Figura salva em: {args.output}\n")

    if not args.no_show:
        plt.show()
    else:
        plt.close(fig)


if __name__ == "__main__":
    main()
