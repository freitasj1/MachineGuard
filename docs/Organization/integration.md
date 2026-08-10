# MachineGuard

# Hardware/Firmware Interface Specification (HFIS)

> Documento de apoio para desenvolvimento de Hardware, Mecânica e Firmware.
>
> Define as conexões físicas, GPIOs, barramentos, alimentação, pull-ups e
> interfaces externas necessárias ao funcionamento do MachineGuard.
>
> Este documento deve ser utilizado como referência para:
>
> * desenvolvimento da PCB;
> * integração dos periféricos;
> * desenvolvimento mecânico;
> * implementação do Firmware.
>
> A arquitetura interna do Firmware é documentada separadamente em
> `architecture.md`.

---

# 1. Document Information

| Item      | Informação                                |
| --------- | ----------------------------------------- |
| Projeto   | MachineGuard                              |
| Documento | Hardware/Firmware Interface Specification |
| Versão    | v1.1                                      |
| Data      | 10/08/2026                                |
| MCU       | ESP32-S3-WROOM-1                          |
| Variante  | MON16R8                                   |
| Framework | ESP-IDF                                   |

---

# 2. Hardware Overview

| Periférico      | Dispositivo              | Interface   | Status  |
| --------------- | ------------------------ | ----------- | ------- |
| MCU             | ESP32-S3-WROOM-1 MON16R8 | —           | IN_USE  |
| Acelerômetro    | LSM6DS3TR-C              | SPI2        | IN_USE  |
| Display         | ILI9341                  | SPI3        | PLANNED |
| Temperatura     | DS18B20                  | 1-Wire      | PLANNED |
| Storage         | SD Card                  | SPI2        | PLANNED |
| DAC             | MCP4725                  | I2C         | PLANNED |
| Saída analógica | BNC fêmea                | MCP4725 OUT | PLANNED |
| LED             | LED de status            | GPIO        | IN_USE  |

---

# 3. GPIO Allocation

## 3.1 GPIOs em uso

| GPIO | Sinal      | Função                  | Interface | Status |
| ---: | ---------- | ----------------------- | --------- | ------ |
|   10 | ACCEL_CS   | Chip Select LSM6DS3TR-C | SPI2      | IN_USE |
|   11 | SPI2_MOSI  | MOSI                    | SPI2      | IN_USE |
|   12 | SPI2_SCLK  | Clock                   | SPI2      | IN_USE |
|   13 | SPI2_MISO  | MISO                    | SPI2      | IN_USE |
|   48 | STATUS_LED | LED de status           | GPIO      | IN_USE |

---

## 3.2 GPIOs planejados

| GPIO | Sinal      | Função               | Interface | Status  |
| ---: | ---------- | -------------------- | --------- | ------- |
|    4 | TEMP_1WIRE | DS18B20 DATA         | 1-Wire    | PLANNED |
|   14 | SD_CS      | Chip Select SD Card  | SPI2      | PLANNED |
|   17 | I2C_SDA    | DAC SDA              | I2C       | PLANNED |
|   18 | I2C_SCL    | DAC SCL              | I2C       | PLANNED |
|   21 | TFT_RST    | Reset ILI9341        | GPIO      | PLANNED |
|   38 | TFT_SCK    | Clock ILI9341        | SPI3      | PLANNED |
|   39 | TFT_MOSI   | MOSI ILI9341         | SPI3      | PLANNED |
|   40 | TFT_MISO   | MISO ILI9341         | SPI3      | PLANNED |
|   41 | TFT_CS     | Chip Select ILI9341  | SPI3      | PLANNED |
|   42 | TFT_DC     | Data/Command ILI9341 | GPIO      | PLANNED |

> Os GPIOs planejados devem ser considerados parte da interface proposta,
> mas ainda podem ser alterados antes da fabricação da PCB.

---

# 4. GPIOs que não devem ser utilizados

|  GPIO | Motivo                            |
| ----: | --------------------------------- |
|     0 | Strapping / boot                  |
|     3 | Strapping                         |
|    19 | USB-JTAG                          |
|    20 | USB-JTAG                          |
| 26–32 | Flash/PSRAM                       |
| 33–37 | Flash/PSRAM em configuração Octal |
|    45 | Strapping                         |
|    46 | Strapping                         |

O ESP32-S3 possui GPIO Matrix, portanto os sinais de periféricos podem ser
roteados para diferentes GPIOs. Entretanto, os GPIOs acima possuem restrições
específicas e não devem ser utilizados como primeira opção de expansão.

---

# 5. SPI2 — Acelerômetro + SD Card

SPI2 é compartilhado entre o acelerômetro e o storage.

## 5.1 Sinais do barramento

| Sinal | GPIO |
| ----- | ---: |
| MOSI  |   11 |
| MISO  |   13 |
| SCLK  |   12 |

## 5.2 Chip Select

| Dispositivo |     CS |
| ----------- | -----: |
| LSM6DS3TR-C | GPIO10 |
| SD Card     | GPIO14 |

## 5.3 Prioridade

```text
SPI2
 │
 ├── LSM6DS3TR-C
 │      CRÍTICO
 │
 └── SD Card
        NÃO CRÍTICO
```

A aquisição do acelerômetro possui prioridade sobre o acesso ao SD Card.

O acesso ao storage não deve comprometer a aquisição contínua do
LSM6DS3TR-C. O compartilhamento do SPI deve considerar também a carga
capacitiva e os pull-ups do SD Card.

---

# 6. LSM6DS3TR-C

## 6.1 Conexões

| LSM6DS3TR-C | Conexão |
| ----------- | ------- |
| VDD         | 3.3 V   |
| VDDIO       | 3.3 V   |
| GND         | GND     |
| CS          | GPIO10  |
| SCL/SPC     | GPIO12  |
| SDA/SDI     | GPIO11  |
| SDO         | GPIO13  |

O sensor é utilizado em SPI de 4 fios.

## 6.2 Configuração atual

| Parâmetro       | Valor                    |
| --------------- | ------------------------ |
| Interface       | SPI                      |
| SPI Mode        | 0                        |
| Clock SPI       | 10 MHz                   |
| ODR             | 6.66 kHz                 |
| Full Scale      | ±2 g                     |
| FIFO            | Continuous               |
| DMA             | Sim                      |
| Eixo processado | Configurável no Firmware |

O Firmware atualmente utiliza FIFO e DMA para aquisição dos dados.

## 6.3 Pull-up / estado de CS

O `CS` deve permanecer em estado inativo durante inicialização/reset do
sistema.

Recomenda-se prever:

```text
GPIO10 / ACCEL_CS
        │
       10 kΩ
        │
       3.3 V
```

A necessidade final desse resistor deve ser validada junto ao esquemático
do módulo/sensor utilizado.

## 6.4 Desacoplamento

Prever capacitores de desacoplamento próximos aos pinos de alimentação do
sensor, conforme recomendação do fabricante.

## 6.5 Mecânica

O acelerômetro deve ser rigidamente acoplado à estrutura monitorada.

A montagem deve:

* minimizar folgas;
* evitar movimento relativo entre sensor e máquina;
* manter orientação conhecida dos eixos;
* permitir repetibilidade de montagem;
* permitir acesso ao acoplamento utilizado durante os testes.

---

# 7. SD Card

## 7.1 Conexões

| SD Card     | Conexão            |
| ----------- | ------------------ |
| VCC         | 3.3 V              |
| GND         | GND                |
| SCLK        | SPI2 SCLK / GPIO12 |
| MOSI / CMD  | SPI2 MOSI / GPIO11 |
| MISO / DAT0 | SPI2 MISO / GPIO13 |
| CS / DAT3   | GPIO14             |

Os sinais não utilizados do cartão devem seguir a recomendação do fabricante
do socket/módulo utilizado.

## 7.2 Pull-ups

Prever resistores externos de:

```text
10 kΩ
```

nas linhas SD que requerem estado alto durante inicialização.

Para o modo SPI utilizado pelo projeto, a implementação deve seguir as
recomendações de pull-up da Espressif para SD.

Não adicionar pull-up ao `SCLK` sem necessidade.

## 7.3 Regra de integração

O SD Card é **não crítico**.

```text
LSM6DS3TR-C → prioridade máxima
SD Card      → prioridade secundária
```

O Firmware deve garantir que operações de armazenamento não bloqueiem a
aquisição do acelerômetro.

---

# 8. SPI3 — Display ILI9341

## 8.1 Conexões

| ILI9341 | GPIO / conexão |
| ------- | -------------- |
| VCC     | 3.3 V          |
| GND     | GND            |
| SCK     | GPIO38         |
| MOSI    | GPIO39         |
| MISO    | GPIO40         |
| CS      | GPIO41         |
| DC      | GPIO42         |
| RST     | GPIO21         |

## 8.2 MISO

Se o módulo ILI9341 utilizado não exigir leitura do display, o `MISO` pode
ser deixado não conectado.

Nesse caso:

```text
ILI9341 MISO → NC
```

e o Firmware deve operar somente com escrita.

## 8.3 Backlight

A alimentação do backlight deve seguir o circuito específico do módulo
ILI9341 utilizado.

Não conectar diretamente um LED de backlight ao GPIO do ESP32-S3 sem o
circuito de limitação/acionamento adequado.

## 8.4 Desacoplamento

Prever desacoplamento próximo à alimentação do display.

---

# 9. I2C — MCP4725

## 9.1 Conexões

| MCP4725 | Conexão                     |
| ------- | --------------------------- |
| VDD     | 3.3 V                       |
| VSS/GND | GND                         |
| SDA     | GPIO17                      |
| SCL     | GPIO18                      |
| A0      | GND                         |
| VOUT    | Circuito de saída analógica |

O MCP4725 possui interface I2C open-drain e necessita de pull-ups em SDA e
SCL. O pino A0 define parte do endereço I2C e será fixado em GND nesta
implementação.

## 9.2 Pull-ups

Prever:

```text
GPIO17 / SDA
      │
     4.7 kΩ
      │
     3.3 V
```

e:

```text
GPIO18 / SCL
      │
     4.7 kΩ
      │
     3.3 V
```

Os resistores devem ficar fisicamente próximos ao barramento/dispositivo,
considerando a capacitância total da conexão.

## 9.3 A0

```text
MCP4725 A0
     │
     └── GND
```

Isso fixa o endereço do dispositivo para a configuração correspondente a
`A0 = 0`.

## 9.4 Desacoplamento

Prever capacitor de desacoplamento entre:

```text
VDD ── capacitor ── GND
```

próximo ao MCP4725.

O datasheet da Microchip mostra desacoplamento local de 0,1 µF e capacitor de
reservatório de 10 µF como referência de aplicação. O dimensionamento final
pode ser ajustado conforme a fonte e o projeto da PCB.

---

# 10. Saída Analógica — BNC

O MCP4725 será utilizado para gerar o sinal analógico destinado à visualização
em osciloscópio.

## 10.1 Conexão

```text
MCP4725 VOUT
     │
     ▼
 BNC fêmea
  center
```

e:

```text
GND do sistema
     │
     ▼
 BNC fêmea
  shield
```

Portanto:

| BNC              | Conexão      |
| ---------------- | ------------ |
| Pino central     | MCP4725 VOUT |
| Carcaça / Shield | GND          |

## 10.2 Objetivo

A saída representa o sinal temporal de vibração produzido pelo processamento
do acelerômetro.

Durante a demonstração, o sinal poderá ser observado em um osciloscópio.

Espera-se que uma condição de maior vibração, como um desbalanceamento,
produza aumento observável da amplitude do sinal.

## 10.3 Layout

O caminho:

```text
MCP4725 VOUT → BNC
```

deve ser mantido curto e com referência de GND adequada.

Evitar passar a trilha analógica do DAC junto a sinais digitais de alta
velocidade quando isso puder ser evitado.

---

# 11. 1-Wire — DS18B20

## 11.1 Conexões

| DS18B20 | Conexão |
| ------- | ------- |
| VDD     | 3.3 V   |
| GND     | GND     |
| DATA    | GPIO4   |

## 11.2 Pull-up

Prever:

```text
3.3 V
 │
4.7 kΩ
 │
DATA ───────── GPIO4
```

O resistor deve ser externo.

## 11.3 Desacoplamento

Prever capacitor de desacoplamento próximo ao sensor quando a alimentação
for realizada em modo normal.

---

# 12. LED de Status

## Conexão atual

```text
GPIO48 → LED_STATUS
```

O circuito do LED deve incluir o resistor de limitação de corrente adequado.

O Firmware configura GPIO48 como saída digital.

---

# 13. Alimentação

Todos os periféricos digitais devem utilizar níveis compatíveis com 3.3 V.

## Alimentações principais

```text
3.3 V
 │
 ├── ESP32-S3
 ├── LSM6DS3TR-C
 ├── SD Card
 ├── ILI9341
 ├── MCP4725
 └── DS18B20
```

Todos os dispositivos devem possuir caminho de GND comum adequado.

---

# 14. Pull-ups e Componentes Externos

Resumo dos componentes externos obrigatórios/recomendados:

| Circuito     | Componente     |              Valor | Observação                       |
| ------------ | -------------- | -----------------: | -------------------------------- |
| I2C SDA      | Pull-up        |             4.7 kΩ | 3.3 V → SDA                      |
| I2C SCL      | Pull-up        |             4.7 kΩ | 3.3 V → SCL                      |
| DS18B20 DATA | Pull-up        |             4.7 kΩ | 3.3 V → DATA                     |
| SD Card      | Pull-up        |              10 kΩ | Conforme sinais exigidos pelo SD |
| ACCEL CS     | Pull-up        |              10 kΩ | Manter CS inativo durante boot   |
| MCP4725 VDD  | Desacoplamento |             100 nF | Próximo ao dispositivo           |
| MCP4725 VDD  | Reservatório   |              10 µF | Próximo ao dispositivo           |
| DS18B20      | Desacoplamento | conforme aplicação | Próximo ao sensor                |
| LSM6DS3TR-C  | Desacoplamento | conforme datasheet | Próximo ao sensor                |
| ILI9341      | Desacoplamento |    conforme módulo | Próximo ao display               |

Os valores indicados como recomendação devem ser confirmados no esquemático
final dos componentes/módulos efetivamente utilizados.

---

# 15. Resumo de Conexões

## MCU → Periféricos

```text
ESP32-S3
│
├── SPI2
│   ├── GPIO11 → MOSI ─────────┬── LSM6DS3TR-C
│   ├── GPIO12 → SCLK ─────────┤
│   ├── GPIO13 ← MISO ─────────┤
│   ├── GPIO10 → CS ───────────┘
│   │
│   └── GPIO14 → SD_CS ───────── SD Card
│
├── I2C
│   ├── GPIO17 ↔ SDA ─────────── MCP4725
│   └── GPIO18 ↔ SCL ─────────── MCP4725
│
├── GPIO4 ←→ 1-Wire ──────────── DS18B20
│
├── SPI3
│   ├── GPIO38 → SCK ──────────── ILI9341
│   ├── GPIO39 → MOSI ─────────── ILI9341
│   ├── GPIO40 ← MISO ─────────── ILI9341
│   ├── GPIO41 → CS ───────────── ILI9341
│   └── GPIO42 → DC ───────────── ILI9341
│
├── GPIO21 → RST ──────────────── ILI9341
│
└── GPIO48 → LED_STATUS
```

## DAC → BNC

```text
MCP4725 VOUT ────────── BNC center
GND ─────────────────── BNC shield
```

---

# 16. Regras de Integração

1. GPIOs `IN_USE` não devem ser reutilizados.
2. GPIOs `PLANNED` podem ser alterados antes da fabricação da PCB.
3. Qualquer alteração de GPIO deve ser refletida simultaneamente no Hardware,
   Firmware e HFIS.
4. SPI2 deve priorizar a aquisição do LSM6DS3TR-C.
5. O SD Card é um periférico não crítico.
6. Storage não deve bloquear a aquisição do acelerômetro.
7. HMI, storage e DAC não devem introduzir interferência no caminho crítico de
   aquisição.
8. Pull-ups externos devem ser utilizados quando especificados neste documento.
9. Todos os periféricos devem possuir alimentação e GND adequados.
10. O BNC deve possuir GND comum com o circuito do DAC.
11. O acelerômetro deve possuir acoplamento mecânico rígido e orientação
    conhecida.
12. Não utilizar GPIOs reservados para Flash/PSRAM, USB-JTAG ou strapping sem
    uma revisão específica do projeto.
13. O projeto mecânico deve permitir instalação e remoção consistente do
    conjunto do acelerômetro.
14. O layout deve manter separação adequada entre sinais analógicos do DAC e
    sinais digitais de alta velocidade.

---

# 17. Status da Integração

| Item                      | Status  |
| ------------------------- | ------- |
| ESP32-S3-WROOM-1          | IN_USE  |
| SPI2                      | IN_USE  |
| LSM6DS3TR-C               | IN_USE  |
| LED                       | IN_USE  |
| SD Card                   | PLANNED |
| ILI9341                   | PLANNED |
| DS18B20                   | PLANNED |
| MCP4725                   | PLANNED |
| BNC                       | PLANNED |
| PCB final                 | PLANNED |
| Integração mecânica final | PLANNED |

---

# 18. Revision History

| Version | Date       | Author     | Changes                                                                                                                                                                                      |
| ------- | ---------- | ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| v1.0    | 20/05/2026 | João Pedro | Initial version                                                                                                                                                                              |
| v1.1    | 10/08/2026 | João Pedro | Atualização completa da interface HW/FW; LSM6DS3TR-C; remoção de Hall/PCNT/LIS3DH; novo mapeamento GPIO; conexão física dos periféricos; pull-ups; alimentação; DAC/BNC; integração mecânica |
