

import numpy as np
import struct
import os

# ──────────────────────────────────────────────────────────────────────────────
# CONFIG — ajuste aqui
# ──────────────────────────────────────────────────────────────────────────────

FAULTY          = False     # False = saudável | True = desbalanceado

SAMPLE_RATE     = 6660      # Hz — ODR do LSM6DS3TR-C (6.66 kHz)
N_SAMPLES       = 2048      # tamanho do ping-pong buffer
RPM             = 1800      # rotação do motor
SENSITIVITY     = 0.000061  # g/LSB — escala ±2g

# Motor saudável — ruído de fundo + vibração residual leve
NOISE_STD_G     = 0.003     # g — ruído gaussiano do sensor + ambiente
RESIDUAL_AMP_G  = 0.005     # g — vibração residual (rolamentos, estrutura)

# Motor com falha — amplitude do desbalanceamento em 1xRPM
FAULT_AMP_G     = 0.25      # g — componente 1xRPM; aumente para falha severa
FAULT_HARMONICS = True      # inclui 2xRPM e 3xRPM com amplitude decrescente

# Ruído adicional na condição de falha (aquecimento, folgas)
FAULT_NOISE_EXTRA_G = 0.008

# Semente para reprodutibilidade (None = aleatório a cada execução)
SEED            = 42

# ──────────────────────────────────────────────────────────────────────────────
# GERAÇÃO
# ──────────────────────────────────────────────────────────────────────────────

def g_to_int16(signal_g: np.ndarray) -> np.ndarray:
    """Converte sinal em g para int16 raw (complemento de dois, ±2g)."""
    lsb = signal_g / SENSITIVITY
    # Clamp para evitar overflow int16
    lsb = np.clip(lsb, -32768, 32767)
    return lsb.astype(np.int16)


def generate_buffer(faulty: bool, seed=None) -> dict:
    """
    Gera um buffer de N_SAMPLES amostras simulando leituras brutas do LSM6DS3TR-C.

    Retorna dict com:
        'x', 'y', 'z'  : np.ndarray de int16 (raw ADC)
        'x_g', 'y_g'   : np.ndarray de float (em g, para validação)
        'rpm'           : float
        'freq_1xrpm'    : float (Hz)
        'faulty'        : bool
    """
    rng = np.random.default_rng(seed)
    t = np.arange(N_SAMPLES) / SAMPLE_RATE  # vetor de tempo

    freq_1x = RPM / 60.0   # Hz — 1xRPM = 30 Hz a 1800 RPM
    freq_2x = freq_1x * 2
    freq_3x = freq_1x * 3

    # ── Eixo X (radial principal) ────────────────────────────────────────────
    noise_std = NOISE_STD_G + (FAULT_NOISE_EXTRA_G if faulty else 0)

    # Ruído gaussiano + vibração residual de rolamentos (componente aleatória leve)
    x_g = (rng.normal(0, noise_std, N_SAMPLES)
           + RESIDUAL_AMP_G * np.sin(2 * np.pi * freq_2x * t + rng.uniform(0, 2*np.pi)))

    if faulty:
        # 1xRPM — assinatura de desbalanceamento (dominante)
        x_g += FAULT_AMP_G * np.sin(2 * np.pi * freq_1x * t)

        if FAULT_HARMONICS:
            # 2xRPM — harmônica secundária
            x_g += (FAULT_AMP_G * 0.30) * np.sin(2 * np.pi * freq_2x * t + 0.5)
            # 3xRPM — harmônica terciária
            x_g += (FAULT_AMP_G * 0.12) * np.sin(2 * np.pi * freq_3x * t + 1.1)

    # ── Eixo Y (radial secundário — 90° defasado de X) ───────────────────────
    y_g = (rng.normal(0, noise_std, N_SAMPLES)
           + RESIDUAL_AMP_G * 0.8 * np.sin(2 * np.pi * freq_2x * t + np.pi / 3))

    if faulty:
        # Desbalanceamento aparece em Y defasado ~90° (força centrífuga rotacional)
        y_g += FAULT_AMP_G * 0.85 * np.sin(2 * np.pi * freq_1x * t - np.pi / 2)

        if FAULT_HARMONICS:
            y_g += (FAULT_AMP_G * 0.25) * np.sin(2 * np.pi * freq_2x * t + 0.8)
            y_g += (FAULT_AMP_G * 0.10) * np.sin(2 * np.pi * freq_3x * t + 1.4)

    # ── Eixo Z (axial — menor contribuição para desbalanceamento) ────────────
    # Gravity offset em Z se sensor estiver horizontal (+1g estático)
    # No pipeline DSP você vai querer remover esse offset DC antes da FFT
    z_g = (1.0                                          # componente DC da gravidade
           + rng.normal(0, noise_std * 0.5, N_SAMPLES)  # ruído menor no axial
           + 0.002 * np.sin(2 * np.pi * freq_1x * t))  # acoplamento axial mínimo

    return {
        'x':          g_to_int16(x_g),
        'y':          g_to_int16(y_g),
        'z':          g_to_int16(z_g),
        'x_g':        x_g,
        'y_g':        y_g,
        'rpm':        float(RPM),
        'freq_1xrpm': freq_1x,
        'faulty':     faulty,
    }


# ──────────────────────────────────────────────────────────────────────────────
# SERIALIZAÇÃO — formato compatível com accel_sample_t { int16 x, y, z }
# ──────────────────────────────────────────────────────────────────────────────

def serialize_to_binary(buf: dict, path: str):
    """
    Serializa o buffer no formato binário que o firmware esperaria via SPI DMA:
    N_SAMPLES * struct { int16_t x; int16_t y; int16_t z; } = N_SAMPLES * 6 bytes
    """
    with open(path, 'wb') as f:
        for i in range(N_SAMPLES):
            f.write(struct.pack('<hhh', buf['x'][i], buf['y'][i], buf['z'][i]))
    print(f"Binário salvo: {path} ({os.path.getsize(path)} bytes)")


def serialize_to_csv(buf: dict, path: str):
    """CSV legível para inspeção e plots externos."""
    import csv
    with open(path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['sample', 'raw_x', 'raw_y', 'raw_z', 'g_x', 'g_y'])
        for i in range(N_SAMPLES):
            writer.writerow([
                i,
                buf['x'][i], buf['y'][i], buf['z'][i],
                f"{buf['x_g'][i]:.6f}", f"{buf['y_g'][i]:.6f}",
            ])
    print(f"CSV salvo: {path}")


# ──────────────────────────────────────────────────────────────────────────────
# ANÁLISE — indicadores que o pipeline DSP vai calcular
# ──────────────────────────────────────────────────────────────────────────────

def analyze(buf: dict):
    """Calcula os mesmos indicadores do pipeline DSP para validação cruzada."""
    # Magnitude radial (o que o DSP vai processar)
    mag = np.sqrt(buf['x_g']**2 + buf['y_g']**2)

    rms     = np.sqrt(np.mean(mag**2))
    mean    = np.mean(mag)
    std     = np.std(mag)
    kurt    = np.mean((mag - mean)**4) / (std**4) if std > 0 else 0
    crest   = np.max(np.abs(mag)) / rms if rms > 0 else 0

    # FFT para verificar pico 1xRPM
    window  = np.hanning(N_SAMPLES)
    fft_mag = np.abs(np.fft.rfft(mag * window)) / N_SAMPLES
    freqs   = np.fft.rfftfreq(N_SAMPLES, 1.0 / SAMPLE_RATE)

    freq_1x = buf['freq_1xrpm']
    bin_1x  = np.argmin(np.abs(freqs - freq_1x))
    amp_1x  = fft_mag[bin_1x]

    print(f"\n{'='*50}")
    print(f"  Estado: {'FALHA' if buf['faulty'] else 'SAUDÁVEL'}")
    print(f"  RPM: {buf['rpm']:.0f} | 1xRPM: {freq_1x:.1f} Hz (bin {bin_1x})")
    print(f"{'─'*50}")
    print(f"  RMS magnitude radial : {rms:.6f} g")
    print(f"  Kurtosis             : {kurt:.4f}  (normal ~3, falha >4–5)")
    print(f"  Fator de Crista      : {crest:.4f}")
    print(f"  Amplitude bin 1xRPM  : {amp_1x:.6f} g")
    print(f"{'='*50}\n")

    return {'rms': rms, 'kurtosis': kurt, 'crest_factor': crest, 'amp_1xrpm': amp_1x}


# ──────────────────────────────────────────────────────────────────────────────
# MAIN
# ──────────────────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    label = 'faulty' if FAULTY else 'healthy'

    print(f"Gerando buffer mock: {label.upper()} | {N_SAMPLES} amostras | {SAMPLE_RATE} Hz | {RPM} RPM")

    buf = generate_buffer(faulty=FAULTY, seed=SEED)

    analyze(buf)

    serialize_to_binary(buf, f'mock_{label}.bin')
    serialize_to_csv(buf,    f'mock_{label}.csv')

    # ── Opcional: gera ambos de uma vez para comparação ──────────────────────
    print("Gerando par saudável/falha para comparação...")
    buf_ok    = generate_buffer(faulty=False, seed=SEED)
    buf_fault = generate_buffer(faulty=True,  seed=SEED)

    analyze(buf_ok)
    analyze(buf_fault)

    serialize_to_binary(buf_ok,    '/scripts/mock/mock_healthy.bin')
    serialize_to_binary(buf_fault, '/scripts/mock/mock_faulty.bin')
    serialize_to_csv(buf_ok,       '/scripts/mock/mock_healthy.csv')
    serialize_to_csv(buf_fault,    '/scripts/mock/mock_faulty.csv')
