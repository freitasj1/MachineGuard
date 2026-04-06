# MachineGuard

Monitor de saúde de máquinas rotativas com análise de vibração embarcada (FFT, kurtosis, baseline adaptativo) no ESP32-S3.

## Status do Projeto
🚧 Em desenvolvimento para FETIN 2026

## Hardware
- ESP32-S3 MON16R8
- Acelerômetro LIS3DH (SPI, 5.3 kHz)
- Display TFT ILI9341 2.8"
- Motor RS-550 / RS-775

## Firmware
- Framework: ESP-IDF,  FreeRTOS
- Bibliotecas: TFT_eSPI, ESP-DSP
- Indicadores: Kurtosis, RMS, 1×RPM, Z-score

## Licença
MIT

## Autores
João Pedro Job - Engenharia de Telecomunicaões - INATEL
João Pedro Maciel - Engenharia de Computação - INATEL
Núbia - Engenharia Biomédica - INATEL
