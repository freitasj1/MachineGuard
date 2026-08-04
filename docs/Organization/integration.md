# MachineGuard
# Hardware/Firmware Interface Specification (HFIS)

> Documento oficial de integração entre Hardware e Firmware  
> Responsável por definir:
> - Alocação de GPIOs
> - Barramentos
> - Restrições elétricas
> - Restrições de boot
> - Ownership de periféricos
> - Regras de integração HW/FW
> - Expansões futuras

---

# 1. Document Information

...

---

# 2. System Overview

## 2.1 Objective

...

## 2.2 Architecture Summary

| Module | Description |
|---|---|
| MCU | ESP32-S3 |
| Sensor Acquisition | SPI + DMA |
| Temperature Monitoring | 1-Wire |
| RPM Measurement | PCNT |
| Display | SPI TFT |
| Storage | SD Card |

---

# 3. GPIO Allocation Table

## 3.1 GPIO Mapping

| GPIO | Signal Name   | Peripheral       | Bus      | Direction | Core | Status   | Description               | Supply | Notes                                       |
| ---- | ------------- | ---------------- | -------- | --------- | ---- | -------- | ------------------------- | ------ | ------------------------------------------- |
| 0    | BTN_BOOT      | Boot Button      | GPIO     | IN        | -    | RESERVED | Boot mode button          | 3.3V   | Strapping pin. Não conectar cargas externas |
| 1    | TFT_DC        | ILI9341 Display  | SPI3     | OUT       | 1    | IN_USE   | Display Data/Command      | 3.3V   | Controle D/C do display                     |
| 2    | GPIO_RESERVED | -                | -        | -         | -    | RESERVED | Reservado para boot       | 3.3V   | Strapping pin. Evitar uso                   |
| 3    | TFT_CS        | ILI9341 Display  | SPI3     | OUT       | 1    | IN_USE   | Chip Select display       | 3.3V   | CS dedicado                                 |
| 4    | TFT_RST       | ILI9341 Display  | GPIO     | OUT       | 1    | IN_USE   | Reset hardware display    | 3.3V   | Reset dedicado                              |
| 5    | TEMP_1WIRE    | DS18B20          | 1-WIRE   | IN/OUT    | 1    | IN_USE   | Sensor temperatura mancal | 3.3V   | Pull-up 4k7 obrigatório                     |
| 6    | RPM_PULSE     | Hall Sensor      | PCNT     | IN        | 1    | IN_USE   | Entrada RPM via PCNT      | 3.3V   | Sem interrupções CPU                        |
| 7    | STATUS_LED    | Status LED       | GPIO     | OUT       | 1    | IN_USE   | Indicador alimentação     | 3.3V   | LED heartbeat                               |
| 8    | LIS3DH_INT1   | LIS3DH           | GPIO     | IN        | 0    | RESERVED | Interrupção Data Ready    | 3.3V   | Reservado para DMA trigger                  |
| 9    | SD_CS         | SD Card          | SPI2     | OUT       | 1    | IN_USE   | Chip Select SD card       | 3.3V   | SPI2 compartilhado                          |
| 10   | LIS3DH_CS     | LIS3DH           | SPI2     | OUT       | 0    | IN_USE   | Chip Select acelerômetro  | 3.3V   | Prioridade máxima Core 0                    |
| 11   | SPI2_MOSI     | LIS3DH + SD      | SPI2     | OUT       | 0    | IN_USE   | SPI2 MOSI                 | 3.3V   | Barramento compartilhado                    |
| 12   | SPI2_SCK      | LIS3DH + SD      | SPI2     | OUT       | 0    | IN_USE   | SPI2 Clock                | 3.3V   | DMA acquisition                             |
| 13   | SPI2_MISO     | LIS3DH + SD      | SPI2     | IN        | 0    | IN_USE   | SPI2 MISO                 | 3.3V   | Barramento compartilhado                    |
| 14   |      | | GPIO     | -         | -    | FREE     |            | 3.3V   |                               |
| 15   | BTN_A         | HMI              | GPIO     | IN        | 1    | IN_USE   | Botão navegação           | 3.3V   | Pull-up interno                             |
| 16   | BTN_B         | HMI              | GPIO     | IN        | 1    | IN_USE   | Botão debug/pause         | 3.3V   | Pull-up interno                             |
| 17   | I2C_SDA       | MCP4725 DAC      | I2C      | IN/OUT    | 1    | IN_USE   | I2C SDA                   | 3.3V   | Pull-up 4k7                                 |
| 18   | I2C_SCL       | MCP4725 DAC      | I2C      | OUT       | 1    | IN_USE   | I2C Clock                 | 3.3V   | 400 kHz                                     |
| 19   | USB_D-        | USB Native       | USB      | IN/OUT    | -    | RESERVED | USB D-                    | 3.3V   | Interno USB                                 |
| 20   | USB_D+        | USB Native       | USB      | IN/OUT    | -    | RESERVED | USB D+                    | 3.3V   | Interno USB                                 |
| 21   |     |  | GPIO     | -         | -    | FREE     |            | 3.3V   |                        |
| 35   | OPI_PSRAM     | Internal PSRAM   | INTERNAL | -         | -    | RESERVED | PSRAM OPI                 | 3.3V   | Não utilizar                                |
| 36   | OPI_PSRAM     | Internal PSRAM   | INTERNAL | -         | -    | RESERVED | PSRAM OPI                 | 3.3V   | Não utilizar                                |
| 37   | OPI_PSRAM     | Internal PSRAM   | INTERNAL | -         | -    | RESERVED | PSRAM OPI                 | 3.3V   | Não utilizar                                |
| 38   | RGB_LED       | Onboard RGB      | GPIO     | OUT       | 1    | IN_USE*  | RGB status LED            | 3.3V   | Confirmar DevKit                            |
| 39   |     | | GPIO     | -         | -    | FREE     |           | 3.3V   | interrupt                          |
| 40   | TFT_SCK       | ILI9341 Display  | SPI3     | OUT       | 1    | IN_USE   | SPI3 Clock                | 3.3V   | Barramento isolado                          |
| 41   | TFT_MOSI      | ILI9341 Display  | SPI3     | OUT       | 1    | IN_USE   | SPI3 MOSI                 | 3.3V   | Display write                               |
| 42   | TFT_MISO      | ILI9341 Display  | SPI3     | IN        | 1    | OPTIONAL | SPI3 MISO                 | 3.3V   | Pode ficar NC                               |
| 43   | UART0_TX      | Debug UART       | UART     | OUT       | -    | RESERVED | Serial TX                 | 3.3V   | USB-UART bridge                             |
| 44   | UART0_RX      | Debug UART       | UART     | IN        | -    | RESERVED | Serial RX                 | 3.3V   | USB-UART bridge                             |
| 45   | GPIO_STRAP    | Future Expansion | GPIO     | IN/OUT    | -    | RESERVED | Strapping pin             | 3.3V   | Usar com extrema cautela                    |
| 46   |      |  | GPIO     |         | -    | FREE     |           | 3.3V   |                        |
| 47   |     |  | GPIO     |     | -    | FREE     |            | 3.3V   |                            |
| 48   |     |  | GPIO     |    | -    | FREE     |            | 3.3V   |                            |

---




## 3.2 GPIO Usage Rules

### Reserved GPIOs

| GPIO | Reason |
|---|---|
| 19 | USB D- |
| 20 | USB D+ |
| 35-37 | OPI PSRAM |
| 43 | UART0 TX |
| 44 | UART0 RX |

---

### Strapping Pins

| GPIO | Constraint |
|---|---|
| 0 | Boot mode selection |
| 2 | Não utilizar saída ativa no boot |
| 45 | Afeta tensão interna no boot |

---

# 4. Bus Architecture

---

# 4.1 SPI2 — Sensor Bus

## Devices
- LIS3DH
- SD Card

## Configuration

| Signal | GPIO |
|---|---|
| MOSI | 11 |
| MISO | 13 |
| SCK | 12 |

## Chip Selects

| Device | GPIO |
|---|---|
| LIS3DH CS | 10 |
| SD Card CS | 9 |

## Firmware Rules

- SPI2 dedicado à aquisição de sensores
- Operação via DMA
- Prioridade máxima no Core 0
- Evitar acessos concorrentes sem mutex
- Clock máximo definido em: ___ MHz

---

# 4.2 SPI3 — Display Bus

## Devices
- ILI9341

## Configuration

| Signal | GPIO |
|---|---|
| MOSI | 41 |
| MISO | 42 |
| SCK | 40 |
| CS | 3 |
| DC | 1 |
| RST | 4 |

## Firmware Rules

- Barramento isolado do SPI2
- Atualização de display executada no Core 1
- SPI display não deve bloquear aquisição

---

# 4.3 I2C Bus

## Devices
- MCP4725

## Configuration

| Signal | GPIO |
|---|---|
| SDA | 17 |
| SCL | 18 |

## Firmware Rules

- Clock: 400 kHz
- Nunca acessar a partir do Core 0
- Mutex obrigatório em acessos compartilhados

---

# 5. Core Responsibilities

| Core | Responsibilities |
|---|---|
| Core 0 | Aquisição crítica, DMA, sensores |
| Core 1 | HMI, display, UI, telemetria |

---

# 6. Interrupt Architecture

| Source | GPIO | Priority | Handler | Notes |
|---|---|---|---|---|
| LIS3DH INT1 | 8 | HIGH | DMA Trigger | Reservado |
| Hall Sensor | 6 | HARDWARE | PCNT | Sem ISR |
| Buttons | 15,16 | LOW | GPIO ISR | Debounce software |

---

# 7. DMA Usage

| Peripheral | DMA | Core | Notes |
|---|---|---|---|
| SPI2 | YES | 0 | Aquisição acelerômetro |
| SPI3 | OPTIONAL | 1 | Display |
| I2C | NO | 1 | Baixa prioridade |

---

# 8. Power & Electrical Constraints

## 8.1 Voltage Levels

| Interface | Voltage |
|---|---|
| GPIO | 3.3V |
| SPI | 3.3V |
| I2C | 3.3V |

---

## 8.2 Pull-ups / Pull-downs

| Signal | Type | Value |
|---|---|---|
| 1-Wire | Pull-up | 4k7 |
| I2C SDA | Pull-up | 4k7 |
| I2C SCL | Pull-up | 4k7 |

---

# 9. Boot Constraints

| GPIO | Restriction | Impact |
|---|---|---|
| GPIO0 | Boot strap | Modo boot |
| GPIO2 | Não dirigir no boot | Boot failure |
| GPIO45 | Strapping | Tensão interna |

---


# 10. Firmware Integration Notes

---

# 11. Hardware Integration Notes

## PCB Requirements

---

# 12. Revision History

| Version | Date | Author | Changes |
|---|---|---|---|
| v1.0 | 20/05/2026 | João Pedro | Initial version |
