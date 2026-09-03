# MachineGuard — architecture.md

## 1. Objetivo

Documentar como o MachineGuard está organizado: estrutura do firmware, comunicação entre componentes, responsabilidades dos módulos e decisões arquiteturais que não devem ser quebradas.

---

## 2. Visão Geral do Projeto

|               |                                                                     |
| ------------- | ------------------------------------------------------------------- |
| Projeto       | MachineGuard                                                        |
| Objetivo      | Manutenção preditiva para motores rotativos via análise de vibração |
| MCU           | ESP32-S3 N16R8 (dual-core LX7, 16 MB flash, 8 MB PSRAM OPI)         |
| Processamento | Edge/local; telemetria opcional via Wi-Fi/MQTT                      |
| Framework     | ESP-IDF                                                             |
| Prazo         | FETIN — 25/09/2026                                                  |

---

## 3. Filosofia do Projeto

* Arquitetura limpa e modular.
* Baixo acoplamento entre componentes.
* Determinismo no fluxo de aquisição, DSP e decisão.
* Simplicidade sobre generalidade.
* ESP-IDF puro, sem Arduino framework.
* Cada componente possui uma responsabilidade clara.
* `task_system` é o mestre do estado, baseline e decisões do sistema.
* O DSP processa sinais, mas não decide o estado da máquina.
* Consumidores não acessam diretamente buffers privados de outros componentes.
* Hardware e I/O ficam fora do fluxo determinístico de decisão sempre que possível.
* Falhas de telemetria não podem impedir a operação local do MachineGuard.
* Cada consumidor recebe somente os dados necessários à sua função.
* Comunicação entre tasks deve privilegiar o dado mais recente quando não houver necessidade de histórico.
* Simplicidade deve ser priorizada sobre otimizações prematuras.

---

## 4. Arquitetura Geral

`main.c` é responsável apenas por:

1. Inicializar a infraestrutura global necessária.
2. Inicializar o `app_context`.
3. Criar e configurar as tasks.

Depois disso, `main` não executa lógica de aplicação.

### Arquitetura de alto nível

```text
                         ┌─────────────────────┐
                         │         HMI         │
                         │   LCD TFT + Button  │
                         └──────────▲───┬──────┘
                                    │   │
                              dados │   │ comando
                                    │   │
Accelerometer → DSP → SYSTEM ───────┤   │
                    │       │        │   │
                    │       ├────────┘   │
                    │       │            │
                    │       ├────────→ DAC → Oscilloscope
                    │       │
                    │       └────────→ TELEMETRY
                    │                         │
                    │                         ↓
                    │                       MQTT
                    │                         │
                    │                         ↓
                    │                    ThingsBoard
                    │
                    └──── Sensors → SYSTEM
```

O processamento principal permanece local no ESP32-S3.

ThingsBoard não participa das decisões de detecção. Sua função é exclusivamente telemetria e visualização.

### Core ownership

| Core   | Responsabilidade                                          |
| ------ | --------------------------------------------------------- |
| Core 0 | Aquisição + DSP + processamento de decisão determinístico |
| Core 1 | HMI, Telemetry, Sensors, DAC e demais I/O                 |

O `task_system` pertence ao fluxo de processamento de decisão. Ele não deve executar acesso direto a hardware, MQTT ou renderização do LCD.

Sua função é:

* processar resultados do DSP;
* incorporar dados dos sensores;
* controlar o estado da máquina;
* controlar o baseline;
* realizar as decisões;
* distribuir os resultados aos consumidores.

### Infraestrutura compartilhada

| Recurso  | Status                                     |
| -------- | ------------------------------------------ |
| SPI2     | Implementado                               |
| SPI3     | HMI                                        |
| I2C      | MCP4725 / sensores, conforme implementação |
| Wi-Fi    | Testado com sucesso no Wi-Fi do INATEL     |
| MQTT/TLS | Testado com sucesso com ThingsBoard Cloud  |

---

## 5. Fluxo de Dados

### Fluxo principal

```text
LSM6DS3TR-C
   │
   │ SPI + DMA
   ▼
FIFO
   │
   ▼
DMA
   │
   ▼
Ping-Pong Buffer
   │
   │ bloco de 2048 amostras
   ▼
task_dsp
   │
   │ dsp_result_t
   ▼
queue_dsp_to_system
   │
   ▼
task_system
   │
   ├────────→ hmi_data_t → HMI
   │
   ├────────→ dac_waveform_t → DAC
   │
   └────────→ telemetry_data_t → MQTT → ThingsBoard
```

### Fluxo dos sensores

```text
task_sensors
   │
   │ sensor_result_t
   ▼
queue_sensors_to_system
   │
   ▼
task_system
   │
   ├────────→ HMI
   │
   └────────→ Telemetry
```

O `task_system` é responsável por distribuir a temperatura aos consumidores.

### Comando da HMI para o System

```text
Button
   │
   ▼
task_hmi
   │
   │ SYSTEM_COMMAND_RESET_WARMUP
   ▼
queue_hmi_to_system
   │
   ▼
task_system
```

O clique curto do botão é tratado localmente pela HMI e altera somente a tela atual.

O clique longo gera um comando para o `task_system`, solicitando um novo warm-up/baseline.

---

## 6. Organização dos Componentes

Componentes previstos:

* `main`
* `app_context`
* `accelerometer`
* `dsp_pipeline`
* `system`
* `hmi`
* `telemetry`
* `sensors`
* `dac`

O componente `storage`/SD foi substituído arquiteturalmente por `telemetry`.

O armazenamento em cartão SD não faz parte da arquitetura atual.

> `rpm_counter` foi removido do projeto. RPM é estimado a partir da frequência do pico espectral associado ao componente 1×RPM da FFT. A validação do RPM estimado será realizada externamente com um tacômetro digital.

### main

|                  |                                                    |
| ---------------- | -------------------------------------------------- |
| Responsabilidade | Inicialização de infraestrutura e criação de tasks |
| Dependências     | Todos os componentes                               |
| Interface        | `app_main`                                         |

`main` não contém lógica de processamento ou decisão do sistema.

### app_context

|                  |                                                                     |
| ---------------- | ------------------------------------------------------------------- |
| Responsabilidade | Contexto compartilhado entre tasks                                  |
| Dependências     | Nenhuma                                                             |
| Interface        | `app_context_init(app_context_t *ctx)`                              |
| Regra            | Um dado → um dono; buffers privados permanecem dentro do componente |
| Recursos         | Queues e mutexes compartilhados                                     |

Queues atuais:

* `queue_accel_block_to_dsp`
* `queue_dsp_to_system`
* `queue_sensors_to_system`
* `queue_system_to_hmi`
* `queue_hmi_to_system`
* `queue_system_to_dac`
* `queue_system_to_telemetry`

As queues de dados que representam o estado ou resultado mais recente devem utilizar tamanho 1 e `xQueueOverwrite()` quando apropriado.

A comunicação entre DSP e System deve preservar a sequência necessária para o warm-up e para a análise de decisão.

As estruturas compartilhadas atualmente definidas no `app_context` incluem:

* `accel_block_t`
* `dsp_result_t`
* `sensor_result_t`
* `system_state_t`
* `system_state_output_t`
* `system_features_t`
* `system_diagnostics_t`
* `system_warmup_t`
* `system_command_t`
* `hmi_data_t`
* `dac_waveform_t`
* `telemetry_data_t`

### accelerometer

|                  |                                                                           |
| ---------------- | ------------------------------------------------------------------------- |
| Responsabilidade | LSM6DS3TR-C, SPI, DMA, FIFO, ping-pong, seleção de eixo e envio de blocos |
| Não faz          | FFT, estatísticas, decisões, HMI, Telemetry ou DAC                        |
| Dependências     | SPI2, `mutex_spi2`                                                        |
| Fluxo            | Aquisição → seleção de eixo → 2048 amostras → `queue_accel_block_to_dsp`  |

O acelerômetro continua adquirindo os três eixos, mas somente um eixo é selecionado para o pipeline DSP.

### dsp_pipeline

|                  |                                                         |
| ---------------- | ------------------------------------------------------- |
| Responsabilidade | Processamento do sinal no domínio do tempo e frequência |
| Dependências     | `queue_accel_block_to_dsp`, ESP-DSP                     |
| Interface        | `task_dsp(void *arg)`                                   |
| Saída            | `dsp_result_t`                                          |

O DSP implementa:

* RMS
* StdDev
* Min
* Max
* Peak-to-Peak
* Crest Factor
* Kurtosis
* FFT
* busca e interpolação do pico espectral
* estimativa de RPM

O `task_dsp` não possui responsabilidade sobre:

* estado da máquina;
* Z-score;
* threshold;
* votação 2/3;
* persistência temporal;
* geração de `HEALTHY`/`ALARM`.

### system

|                  |                                                                         |
| ---------------- | ----------------------------------------------------------------------- |
| Responsabilidade | Mestre do estado, baseline e decisão                                    |
| Dependências     | `queue_dsp_to_system`, `queue_sensors_to_system`, `queue_hmi_to_system` |
| Interface        | `task_system(void *arg)`                                                |

Responsabilidades:

1. Controlar o estado da máquina.
2. Controlar o warm-up.
3. Construir o baseline.
4. Calcular Z-scores.
5. Aplicar threshold.
6. Avaliar a evidência 2/3.
7. Controlar persistência temporal.
8. Determinar `HEALTHY` ou `ALARM`.
9. Incorporar dados dos sensores.
10. Distribuir os resultados aos consumidores.

O baseline é construído durante 600 avaliações saudáveis e permanece fixo após o warm-up, até que um novo warm-up seja solicitado.

O `task_system` é o único proprietário do estado da máquina.

### hmi

|                  |                                              |
| ---------------- | -------------------------------------------- |
| Responsabilidade | Interface local com LCD TFT e botão          |
| Dependências     | `queue_system_to_hmi`, `queue_hmi_to_system` |
| Hardware         | LCD TFT 3.5" SPI + botão                     |

A HMI trabalha sempre com o resultado mais recente disponível.

A HMI não mantém histórico de resultados recebidos.

A frequência de atualização da HMI pode ser menor que a frequência de produção de resultados pelo DSP.

A HMI possui três telas principais:

1. Status.
2. FFT.
3. Diagnóstico.

#### Tela Status

Deve apresentar os principais indicadores da condição atual da máquina.

Informações previstas:

* estado;
* temperatura;
* RPM;
* frequência;
* RMS;
* demais indicadores definidos durante a implementação visual.

Durante `WARMUP`, a tela apresenta o progresso do baseline.

Durante `ALARM`, a condição de alarme deve receber destaque visual.

O estado `INIT` não precisa ser apresentado como uma tela específica. A HMI pode permanecer desligada ou simplesmente ignorar esse estado.

#### Tela FFT

A HMI apresenta uma representação gráfica do espectro na faixa:

```text
5 Hz → 250 Hz
```

A faixa deve permitir visualizar o componente 1×RPM e suas possíveis harmônicas dentro da região apresentada.

A HMI não precisa apresentar a FFT completa de 0 Hz até Nyquist.

A HMI recebe 75 pontos nativos da FFT: bins 2 a 76, inclusive. Com a
configuração atual (6,66 kHz e FFT de 2048 pontos), eles representam de
aproximadamente 6,5 Hz a 247,9 Hz e cobrem a faixa visual de 5–250 Hz sem
reamostragem. A HMI reconstrói o eixo de frequência a partir dessa configuração.

#### Tela Diagnóstico

A tela apresenta individualmente as três features utilizadas pela decisão 2/3:

* RMS;
* Kurtosis;
* amplitude 1×RPM.

Para cada feature devem ser apresentados, quando aplicável:

* Z-score;
* classificação `NORMAL`/`ABNORMAL`.

Isso permite visualizar a evidência que levou a uma eventual condição `ALARM`.

#### Botão

* clique curto: próxima tela;
* clique longo: solicita novo warm-up.

O tratamento físico do botão deve ser isolado da lógica de tela.

A implementação pode utilizar ISR para detectar o evento e comunicação ISR → `task_hmi`.

Debounce e tempo mínimo de clique longo fazem parte da implementação da HMI.

### telemetry

|                  |                                              |
| ---------------- | -------------------------------------------- |
| Responsabilidade | Publicar telemetria via MQTT                 |
| Dependências     | Wi-Fi, MQTT/TLS, `queue_system_to_telemetry` |
| Backend          | ThingsBoard Cloud                            |

Dados previstos para telemetria:

* estado da máquina;
* temperatura;
* RMS;
* Kurtosis;
* Crest Factor;
* amplitude 1×RPM;
* RPM;
* frequência;
* Z-scores;
* classificação das três features;
* informações de warm-up quando necessário.

A telemetria é uma saída secundária.

Uma falha de Wi-Fi ou MQTT não deve interromper aquisição, DSP ou decisão local.

O acesso ao broker `mqtt.thingsboard.cloud:8883` com TLS já foi validado no ESP32-S3 utilizando o Wi-Fi do INATEL.

### sensors

|                  |                                  |
| ---------------- | -------------------------------- |
| Responsabilidade | Aquisição de sensores adicionais |
| Dependências     | Hardware dos sensores            |
| Interface        | `task_sensors(void *arg)`        |

O DS18B20 fornece a temperatura utilizada pela HMI e pela Telemetry.

Os resultados dos sensores são enviados ao `task_system`, que decide como eles serão distribuídos.

### dac

|                  |                                   |
| ---------------- | --------------------------------- |
| Responsabilidade | Saída analógica para osciloscópio |
| Hardware         | MCP4725                           |
| Dependências     | `queue_system_to_dac`             |

O DAC deverá reproduzir um sinal temporal representativo da vibração adquirida.

O contrato utiliza uma waveform de 2048 amostras em `float`, equivalente a aproximadamente 8 KB.

A utilização de aproximadamente 8 KB para a queue dedicada ao DAC é aceitável para o ESP32-S3 atual. A queue não representa stack de task; seu armazenamento é alocado como recurso de comunicação.

A task do DAC é responsável pelo acesso físico ao MCP4725.

Ainda devem ser definidos:

* taxa de atualização;
* quantidade de amostras efetivamente reproduzidas;
* escalonamento;
* offset;
* frequência máxima de reprodução;
* comportamento caso a taxa do MCP4725 não permita reproduzir o bloco integralmente.

---

## 7. Pipeline DSP

### 7.1 Processamento

```text
Bloco de 2048 amostras
        │
        ├───────────────┐
        │               │
        ▼               ▼
Time-domain          Hann
features               │
        │               ▼
        │              FFT
        │               │
        │               ▼
        │          Magnitude
        │               │
        │               ▼
        │          Normalização
        │               │
        │               ▼
        │          Busca de pico
        │               │
        │               ▼
        │       Interpolação parabólica
        │               │
        │               ▼
        │          RPM estimado
        │
        └───────────────┬───────────────┘
                        ▼
                   dsp_result_t
                        │
                        ▼
                   task_system
```

### 7.2 Features temporais

O DSP calcula:

* RMS
* StdDev
* Min
* Max
* Peak-to-Peak
* Crest Factor
* Kurtosis

### 7.3 Análise espectral

O pipeline utiliza:

1. Janela de Hann.
2. FFT via ESP-DSP.
3. Magnitude.
4. Normalização.
5. Eixo de frequência.
6. Busca do pico na faixa configurada.
7. Interpolação parabólica.
8. Conversão de frequência para RPM.

```text
RPM = f_peak × 60
```

onde `f_peak` representa a frequência do componente associado ao 1×RPM.

### 7.4 Dados espectrais

O `dsp_result_t` contém atualmente a magnitude FFT completa para permitir o processamento e futuras necessidades de visualização.

A HMI, entretanto, utiliza somente a faixa de:

```text
5–250 Hz
```

A arquitetura de saída da HMI não deve transportar dados espectrais desnecessários.

A representação final da FFT destinada à HMI deve ser definida de acordo com a resolução necessária para o display.

---

## 8. Pipeline de Detecção

A decisão ocorre exclusivamente no `task_system`.

```text
dsp_result_t
     │
     ▼
  WARMUP?
  /     \
sim      não
 │         │
 ▼         ▼
baseline  Z-score
 │         │
 │         ▼
 │      threshold
 │         │
 │         ▼
 │     evidência 2/3
 │         │
 │         ▼
 │    persistência
 │         │
 └────────► estado
```

### 8.1 Warm-up

O warm-up possui:

```text
600 avaliações
```

Cada avaliação corresponde a um resultado produzido pelo DSP para um bloco de 2048 amostras.

Durante o warm-up:

* os dados são utilizados para construir o baseline;
* nenhuma condição de `ALARM` é declarada;
* a HMI pode apresentar o progresso.

Após a avaliação 600:

```text
WARMUP
   ↓
baseline finalizado
   ↓
HEALTHY
```

Um novo warm-up pode ser solicitado pelo clique longo do botão.

### 8.2 Baseline

O baseline atual utiliza:

* RMS;
* Kurtosis;
* amplitude 1×RPM.

Para cada feature:

```text
μ = média
σ = desvio padrão
```

As estatísticas são calculadas incrementalmente, sem armazenar as 600 avaliações completas.

O baseline permanece fixo após o warm-up.

### 8.3 Z-score

Para cada avaliação:

```text
Z = (x - μ) / σ
```

São calculados:

* `zscore_rms`
* `zscore_kurtosis`
* `zscore_1x_rpm`

Se o desvio padrão for insuficiente, a feature não deve gerar uma decisão estatística indefinida.

### 8.4 Threshold

O threshold atual é:

```text
SYSTEM_ZSCORE_THRESHOLD = 3.0
```

A implementação atual considera uma feature anormal quando:

```text
Z > threshold
```

O detector é, portanto, unilateral no momento.

O valor do threshold continua sujeito à validação experimental com dados reais.

### 8.5 Evidência 2/3

As três features de decisão são:

```text
RMS
Kurtosis
Amplitude 1×RPM
```

Cada uma é classificada como `NORMAL` ou `ABNORMAL`.

A avaliação é considerada anormal quando pelo menos 2 das 3 features são anormais.

Crest Factor permanece disponível como feature do DSP e como possível indicador da HMI/Telemetry, mas não participa da decisão inicial.

### 8.6 Persistência temporal

Uma única avaliação anormal não é suficiente para declarar `ALARM`.

A implementação atual utiliza:

* 5 avaliações anormais consecutivas para `HEALTHY → ALARM`;
* qualquer avaliação normal interrompe a sequência de entrada em `ALARM`;
* 5 avaliações normais consecutivas para `ALARM → HEALTHY`;
* qualquer avaliação anormal interrompe a sequência de recuperação.

Os parâmetros continuam sujeitos à validação com dados reais.

---

## 9. Estados do Sistema

O `task_system` é o único proprietário do estado da máquina.

```text
INIT
  ↓
WARMUP
  ↓
HEALTHY
  ↓
ALARM
```

### INIT

Inicialização da lógica do sistema.

A HMI não precisa apresentar `INIT`.

### WARMUP

Construção do baseline com 600 avaliações.

### HEALTHY

Baseline disponível e nenhuma condição anormal persistente detectada.

### ALARM

Condição anormal persistente detectada.

### Transições

```text
WARMUP ───────────────→ HEALTHY
          600 avaliações

HEALTHY ── 5 anormais ─→ ALARM

ALARM ───── 5 normais ─→ HEALTHY
```

Um comando `SYSTEM_COMMAND_RESET_WARMUP` solicita o reinício do processo de baseline.

---

## 10. Comunicação entre Componentes

| Mecanismo                   | Uso                           |
| --------------------------- | ----------------------------- |
| `queue_accel_block_to_dsp`  | Bloco de 2048 amostras        |
| `queue_dsp_to_system`       | Resultado completo do DSP     |
| `queue_sensors_to_system`   | Resultado dos sensores        |
| `queue_system_to_hmi`       | Dados necessários à HMI       |
| `queue_hmi_to_system`       | Comandos da HMI para System   |
| `queue_system_to_dac`       | Waveform para saída analógica |
| `queue_system_to_telemetry` | Dados para MQTT               |

### Regra de ownership

**Um dado tem um único dono/escritor. Consumidores somente leem os dados recebidos por suas interfaces.**

O `task_dsp` é responsável pelos resultados do processamento do sinal.

O `task_system` é responsável pelo estado, baseline e decisões.

HMI, DAC e Telemetry não acessam diretamente:

* acelerômetro;
* buffers privados do DSP;
* estado interno do System.

Cada saída possui uma estrutura específica para seu contrato.

Atualmente:

```text
System → HMI
    hmi_data_t

System → DAC
    dac_waveform_t

System → Telemetry
    telemetry_data_t
```

As queues de saída representam, quando aplicável, o estado mais recente e não histórico.

---

## 11. Recursos Compartilhados

| Recurso                     | Dono          | Consumidores   | Sincronização          |
| --------------------------- | ------------- | -------------- | ---------------------- |
| SPI2                        | `main` (init) | Accelerometer  | `mutex_spi2`           |
| SPI3                        | `main` (init) | HMI            | Isolado                |
| I2C                         | `main` (init) | DAC / sensores | Conforme implementação |
| `queue_accel_block_to_dsp`  | Accelerometer | DSP            | Queue                  |
| `queue_dsp_to_system`       | DSP           | System         | Queue                  |
| `queue_sensors_to_system`   | Sensors       | System         | Queue                  |
| `queue_system_to_hmi`       | System        | HMI            | Queue                  |
| `queue_hmi_to_system`       | HMI           | System         | Queue                  |
| `queue_system_to_dac`       | System        | DAC            | Queue                  |
| `queue_system_to_telemetry` | System        | Telemetry      | Queue                  |

Nenhuma task consumidora acessa diretamente buffers privados de outro componente.

---

## 12. Memória e Transporte de Dados

O `dsp_result_t` é grande devido principalmente a:

* `magnitude[1024]`;
* `waveform[2048]`.

O resultado completo do DSP é transportado somente na interface:

```text
DSP → System
```

As interfaces de saída utilizam estruturas específicas:

```text
System → HMI
    hmi_data_t

System → DAC
    dac_waveform_t

System → Telemetry
    telemetry_data_t
```

O objetivo é evitar enviar o `dsp_result_t` completo para todos os consumidores.

A waveform do DAC possui aproximadamente:

```text
2048 × 4 bytes = 8192 bytes
```

A alocação de aproximadamente 8 KB para a queue dedicada ao DAC é aceitável no ESP32-S3 N16R8.

Ainda assim, o consumo total de RAM deve ser acompanhado durante a integração.

Objetivos:

* evitar cópias desnecessárias;
* evitar transportar dados que o consumidor não utiliza;
* manter ownership explícito;
* evitar condições de corrida;
* manter o fluxo DSP → System determinístico;
* garantir que Telemetry nunca bloqueie o processamento local.

---

## 13. Convenções de Código

|           |                                       |
| --------- | ------------------------------------- |
| Linguagem | C                                     |
| Framework | ESP-IDF                               |
| Branch    | `feature_<feature>` / `bugfix_<algo>` |
| Commits   | Conventional Commits                  |
| Colunas   | 100                                   |

Organização preferencial de cada `.c`:

```text
Includes

Private constants

Private types

Public variables

Private variables

Private prototypes

Public implementations

Private implementations
```

Código legado pode utilizar `snake_case`; não é necessário reescrever módulos existentes apenas por causa da convenção.

---

## 14. Limitações Conhecidas

* Apenas um eixo é processado no DSP; os três eixos continuam sendo adquiridos.
* FFT real.
* RPM é estimado pelo pico espectral, sem sensor de RPM integrado.
* Validação do RPM será realizada com referência externa.
* Baseline não é persistido em flash/NVS.
* Baseline permanece fixo após o warm-up.
* O sistema detecta mudança de condição, mas não classifica o tipo de falha.
* Threshold de Z-score ainda precisa de validação experimental.
* Parâmetros de persistência ainda precisam de validação com dados reais.
* `dsp_result_t` é grande e contém waveform e magnitude completas.
* A HMI utiliza somente a faixa de 5–250 Hz da FFT.
* A HMI recebe 75 bins nativos para representar a faixa de 5–250 Hz.
* A taxa efetiva do MCP4725 para reprodução da waveform ainda precisa ser validada.
* O DAC recebe atualmente uma waveform de 2048 amostras, aproximadamente 8 KB.
* Telemetry depende de Wi-Fi e MQTT, mas sua indisponibilidade não deve afetar a decisão local.
* Dashboard ThingsBoard é uma interface de supervisão, não parte do loop de controle/detecção.
* O novo baseline solicitado pela HMI ainda precisa ter seu comportamento implementado.
* O DS18B20 ainda precisa ser integrado ao fluxo completo.
* HMI, DAC e Telemetry ainda não estão completamente integrados ao fluxo final.

---

## 15. Pendências e TODO

### 15.1 HMI — foco atual

#### Contrato System → HMI

* [x] Definir conteúdo final de `hmi_data_t`.
* [x] Definir dados obrigatórios.
* [x] Definir dados opcionais.
* [x] Definir representação da FFT de 5–250 Hz: bins nativos 2–76.
* [x] Definir quantidade de pontos da FFT enviada à HMI: 75.
* [ ] Definir frequência de atualização da HMI.

#### Telas

* [ ] Finalizar tela Status.
* [ ] Finalizar tela FFT.
* [ ] Finalizar tela Diagnóstico.
* [ ] Definir comportamento durante `WARMUP`.
* [ ] Definir comportamento durante `ALARM`.
* [ ] Definir comportamento para `INIT`.
* [ ] Implementar navegação entre telas.

#### Botão

* [ ] Definir GPIO.
* [ ] Implementar ISR.
* [ ] Implementar comunicação ISR → `task_hmi`.
* [ ] Implementar debounce.
* [ ] Definir tempo mínimo para clique longo.
* [ ] Implementar clique curto.
* [ ] Implementar clique longo.
* [ ] Implementar `SYSTEM_COMMAND_RESET_WARMUP`.

#### Hardware

* [ ] Implementar/configurar LCD TFT.
* [ ] Validar SPI3.
* [ ] Implementar renderização das telas.
* [ ] Validar atualização sem bloquear o restante do sistema.

---

### 15.2 DAC

#### Contrato

* [ ] Definir conteúdo final de `dac_waveform_t`.
* [ ] Definir waveform utilizada.
* [ ] Definir quantidade de amostras efetivamente reproduzidas.
* [ ] Definir taxa de atualização.
* [ ] Definir escalonamento.
* [ ] Definir offset.
* [ ] Definir limites do MCP4725.

#### Implementação

* [ ] Implementar MCP4725.
* [ ] Implementar task DAC.
* [ ] Integrar `queue_system_to_dac`.
* [ ] Validar taxa de reprodução.
* [ ] Validar sinal no osciloscópio.
* [ ] Comparar sinal reproduzido com waveform adquirida.

---

### 15.3 Telemetry / MQTT

#### Contrato

* [ ] Definir conteúdo final de `telemetry_data_t`.
* [ ] Definir campos obrigatórios.
* [ ] Definir campos opcionais.
* [ ] Definir frequência de publicação.
* [ ] Definir comportamento quando não houver conexão.

#### MQTT

* [ ] Implementar Wi-Fi.
* [ ] Implementar MQTT.
* [ ] Implementar TLS.
* [ ] Configurar autenticação/token.
* [ ] Definir tópico.
* [ ] Definir formato do payload.
* [ ] Implementar publicação.
* [ ] Implementar reconexão.
* [ ] Garantir que falha de MQTT não bloqueie o MachineGuard.

#### ThingsBoard

* [ ] Criar/configurar dispositivo.
* [ ] Configurar autenticação.
* [ ] Publicar telemetria.
* [ ] Validar recebimento.
* [ ] Criar dashboard.
* [ ] Criar indicador de estado.
* [ ] Criar gráfico de RMS.
* [ ] Criar gráfico de temperatura.
* [ ] Criar gráfico de RPM.
* [ ] Avaliar gráficos de features/Z-scores.

---

### 15.4 Sensors

* [ ] Implementar DS18B20.
* [ ] Validar leitura de temperatura.
* [ ] Integrar `sensor_result_t`.
* [ ] Integrar temperatura ao System.
* [ ] Integrar temperatura à HMI.
* [ ] Integrar temperatura à Telemetry.

---

### 15.5 System

* [ ] Implementar processamento do `SYSTEM_COMMAND_RESET_WARMUP`.
* [ ] Validar reinício do baseline sem reboot.
* [ ] Publicar `hmi_data_t`.
* [ ] Publicar `dac_waveform_t`.
* [ ] Publicar `telemetry_data_t`.
* [ ] Integrar temperatura.
* [ ] Gerar diagnósticos individuais das três features para os consumidores.
* [ ] Validar parâmetros estatísticos com dados reais.
* [ ] Avaliar `SYSTEM_ZSCORE_THRESHOLD`.
* [ ] Validar decisão 2/3.
* [ ] Validar persistência de 5 avaliações.
* [ ] Validar recuperação com 5 avaliações normais.

---

### 15.6 Integração

* [ ] Integrar ACCEL → DSP → SYSTEM.
* [ ] Integrar SYSTEM → HMI.
* [ ] Integrar SYSTEM → DAC.
* [ ] Integrar SYSTEM → Telemetry.
* [ ] Integrar Sensors → SYSTEM.
* [ ] Validar fluxo completo.
* [ ] Verificar consumo de RAM.
* [ ] Verificar stacks das tasks.
* [ ] Verificar latência.
* [ ] Verificar ausência de bloqueios.
* [ ] Verificar comportamento com Wi-Fi desconectado.
* [ ] Verificar comportamento com MQTT indisponível.
* [ ] Verificar comportamento com HMI atualizando.
* [ ] Verificar comportamento com DAC reproduzindo waveform.

---

### 15.7 Testes do System

* [ ] Testar warm-up completo.
* [ ] Testar baseline válido.
* [ ] Testar baseline inválido.
* [ ] Testar 1/3 features anormais.
* [ ] Testar 2/3 features anormais.
* [ ] Testar 3/3 features anormais.
* [ ] Testar `HEALTHY → ALARM`.
* [ ] Testar `ALARM → HEALTHY`.
* [ ] Testar interrupção da sequência anormal.
* [ ] Testar interrupção da sequência de recuperação.
* [ ] Testar novo warm-up via HMI.

---

### 15.8 Validação no motor

#### Motor saudável

* [ ] Verificar aquisição.
* [ ] Verificar estatísticas.
* [ ] Verificar FFT.
* [ ] Verificar 1×RPM.
* [ ] Verificar RPM.
* [ ] Verificar temperatura.
* [ ] Verificar baseline.
* [ ] Verificar `HEALTHY`.
* [ ] Verificar HMI.
* [ ] Verificar DAC.
* [ ] Verificar Telemetry.
* [ ] Verificar dashboard ThingsBoard.

#### Condição anormal

* [ ] Inserir condição anormal controlada.
* [ ] Verificar Z-scores.
* [ ] Verificar quantidade de features anormais.
* [ ] Verificar contador de persistência.
* [ ] Verificar transição para `ALARM`.
* [ ] Verificar indicação no HMI.
* [ ] Verificar publicação via MQTT.
* [ ] Verificar dashboard.
* [ ] Verificar recuperação para `HEALTHY`.

---

### 15.9 Validação do RPM

* [ ] Medir RPM real com tacômetro Minipa.
* [ ] Comparar RPM estimado pelo MachineGuard.
* [ ] Calcular erro percentual.
* [ ] Validar frequência fundamental.
* [ ] Validar 1×RPM.
* [ ] Registrar resultados finais.

---

### 15.10 Demonstração e operação

* [ ] Máquina inicia em `WARMUP`.
* [ ] HMI indica progresso do warm-up.
* [ ] Baseline é construído.
* [ ] Sistema entra em `HEALTHY`.
* [ ] Botão permite navegar pelas telas.
* [ ] Clique longo solicita novo warm-up.
* [ ] Novo baseline é construído sem reboot.
* [ ] Condição anormal gera `ALARM`.
* [ ] HMI indica `ALARM`.
* [ ] DAC reproduz sinal temporal.
* [ ] ThingsBoard recebe telemetria.
* [ ] Dashboard apresenta estado da máquina.
* [ ] Sistema continua funcionando caso MQTT seja desconectado.

---

## 16. Regras para IA

* Nunca alterar a arquitetura sem discutir previamente.
* Nunca criar componentes novos sem autorização.
* Nunca adicionar bibliotecas por conta própria.
* Nunca alterar a responsabilidade de um módulo sem discutir previamente.
* Sempre justificar decisões arquiteturais propostas.
* `architecture.md` é a fonte de verdade da arquitetura do firmware.
* `task_dsp` processa sinais e retorna features; não decide o estado da máquina.
* `task_system` é o mestre do estado, baseline e decisão.
* HMI, DAC e Telemetry não acessam diretamente buffers privados de outros componentes.
* Cada consumidor deve receber somente os dados necessários à sua função.
* O processamento e a decisão permanecem locais no ESP32-S3.
* MQTT/ThingsBoard é telemetria e visualização, não parte da lógica de detecção.
* Falhas de telemetria não devem bloquear o processamento local.
* O baseline atual é construído durante 600 avaliações e permanece fixo até um novo warm-up.
* Não implementar EMA adaptativo sem decisão arquitetural explícita.
* Não substituir a análise estatística por algoritmos mais complexos sem evidência experimental.
* Threshold e persistência devem ser calibrados experimentalmente.
* Não otimizar memória prematuramente sem medir o consumo real.
* Não transformar comunicação entre tasks em armazenamento histórico quando somente o dado mais recente é necessário.
