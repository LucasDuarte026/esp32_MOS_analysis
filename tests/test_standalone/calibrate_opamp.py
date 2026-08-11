import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy.stats import linregress
import sys
import os
import re

def calibrate_opamp(csv_path):
    print(f"--- Calibração de Amp Op: {os.path.basename(csv_path)} ---")
    
    # 1. Tentar extrair metadados do cabeçalho
    original_gain = 31.455823 # Default teórico
    original_offset = 0.0
    
    try:
        with open(csv_path, 'r') as f:
            for line in f:
                if "LM358_gain=" in line:
                    match_gain = re.search(r"LM358_gain=([0-9.]+)", line)
                    match_offset = re.search(r"A3_DC_offset=([0-9.-]+)V", line)
                    if match_gain:
                        original_gain = float(match_gain.group(1))
                    if match_offset:
                        original_offset = float(match_offset.group(1))
                    break
    except Exception as e:
        print(f"Aviso: Não foi possível ler metadados do cabeçalho: {e}")

    # 2. Carregar dados
    try:
        df = pd.read_csv(csv_path, comment='#')
    except Exception as e:
        print(f"Erro ao ler CSV: {e}")
        return

    if 'vsh_precise' not in df.columns or 'vsh' not in df.columns:
        print("ERRO: Colunas 'vsh' ou 'vsh_precise' não encontradas.")
        return

    # 3. Reconstruir V_raw_A3 (tensão no pino do ADC)
    # No firmware atual, vsh_precise = (raw_a3 - offset) / gain
    # Logo, raw_a3 = (vsh_precise * gain) + offset
    
    # Se os valores de vsh_precise forem muito baixos (ex: max < 1V), 
    # é sinal que já foram divididos pelo ganho (casos de IdVgs).
    if df['vsh_precise'].max() < 2.0:
        print(f"Detectado: Dados de 'vsh_precise' parecem já estar divididos pelo ganho ({original_gain:.2f}x).")
        df['v_raw_a3'] = (df['vsh_precise'] * original_gain) + original_offset
    else:
        print("Detectado: Dados de 'vsh_precise' parecem ser tensões brutas no pino.")
        df['v_raw_a3'] = df['vsh_precise']

    # 4. Limpeza de dados:
    # - Shunt direto (A0) deve ser confiável (> 2mV)
    # - Canal amplificado (A3) não deve estar saturado (< 3.65V para LM358 em 5V)
    # - Também remover o ruído do "piso" do operacional (Vsh > 5mV costuma ser seguro)
    mask = (df['vsh'] > 0.005) & (df['v_raw_a3'] < 3.65)
    data = df[mask]

    if len(data) < 5:
        print("ERRO: Poucos pontos válidos (não saturados) para calibração.")
        # Se falhou, talvez o canal A3 esteja sempre baixo?
        print(f"Máximo V_raw_A3 detectado: {df['v_raw_a3'].max():.3f}V")
        return

    # 5. Regressão Linear: V_raw_A3 = Actual_Gain * Vsh_Direct + Actual_Offset
    x = data['vsh']
    y = data['v_raw_a3']
    
    res = linregress(x, y)
    
    actual_gain = res.slope
    actual_offset = res.intercept
    r_squared = res.rvalue**2

    print(f"\nResultados da Calibração Real (Hardware):")
    print(f"------------------------------------------")
    print(f"Ganho do Hardware (m):     {actual_gain:.6f}")
    print(f"Offset no Pino A3 (c):     {actual_offset:.6f} V ({actual_offset*1000:.2f} mV)")
    print(f"Qualidade do Ajuste (R²):  {r_squared:.6f}")
    print(f"------------------------------------------")

    # 6. Sugestão de correção para o firmware
    print(f"\nSugestão para 'include/hardware_hal.h':")
    print(f"constexpr float SHUNT_AMP_GAIN_INV = 1.0f / {actual_gain:.6f}f;")
    print(f"constexpr float SHUNT_AMP_A3_OFFSET_V = {actual_offset:.6f}f;")

    # 7. Plot para visualização
    plt.figure(figsize=(10, 6))
    
    # Todos os pontos em cinza (incluindo saturados)
    plt.scatter(df['vsh'] * 1000, df['v_raw_a3'] * 1000, color='gray', alpha=0.2, label='Pontos Saturados/Ruído')
    
    # Pontos usados no ajuste em azul
    plt.scatter(x * 1000, y * 1000, alpha=0.6, color='tab:blue', label='Pontos Válidos (Ajuste)')
    
    # Linha de tendência
    x_plot = np.linspace(0, df['vsh'].max(), 100)
    plt.plot(x_plot * 1000, (actual_gain * x_plot + actual_offset) * 1000, color='red', 
             linewidth=2, label=f'Ajuste Linear (G={actual_gain:.2f})')
    
    plt.axhline(y=3650, color='orange', linestyle='--', alpha=0.5, label='Limite Saturação (3.65V)')
    
    plt.xlabel('Vshunt (Direct A0) [mV]')
    plt.ylabel('V_raw_A3 (ADC Pin) [mV]')
    plt.title(f'Calibração de Ganho Real do LM358\nHardware Gain={actual_gain:.4f}, Offset={actual_offset*1000:.1f}mV')
    plt.legend()
    plt.grid(True, alpha=0.3)
    
    plot_path = csv_path.replace('.csv', '_calibration_v2.png')
    plt.savefig(plot_path)
    print(f"\nNovo gráfico salvo em: {plot_path}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Uso: python calibrate_opamp.py <caminho_do_csv>")
    else:
        calibrate_opamp(sys.argv[1])
