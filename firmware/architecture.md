# MachineGuard — architecture.md

## 1. Objetivo

Documentar **como** o MachineGuard está organizado: estrutura do firmware, comunicação entre componentes, responsabilidades dos módulos, fluxo de dados, estados do sistema, decisões de detecção e regras arquiteturais que não devem ser quebradas.

Este documento é a **fonte de verdade da arquitetura do firmware**.

O objetivo é manter o projeto modular, previsível e suficientemente simples para a demonstração do protótipo na FETIN 2026, evitando expansão de escopo sem justificativa técnica.

---

## 2. Visão Geral do Projeto

|               |                                                                     |
| ------------- | ------------------------------------------------------------------- |
| Projeto       | MachineGuard                                                        |
| Objetivo      | Manutenção preditiva para motores rotativos via análise de vibração |
| MCU           | ESP32-S3 N16R8 (dual-core LX7, 16 MB flash, 8 MB PSRAM OPI)         |
| Processamento | Edge/local; telemetria opcional via Wi-Fi/MQTT                      |
| Framework     | ESP-IDF                                                             |
| Aplicação     | Monitoramento de condição de motores rotativos                      |
| Prazo         | FETIN — 25/09/2026                                                  |

O MachineGuard realiza a aquisição de vibração, processamento digital do sinal, extração de características, construção de um baseline de operação saudável e detecção de alterações de condição.

A decisão de condição da máquina ocorre **localmente no ESP32-S3**.

A nuvem não participa da decisão de `HEALTHY` ou `ALARM`.

---

## 3. Escopo Atual

O escopo atual para a FETIN contempla:

* aquisição de vibração com LSM6DS3TR-C;
* aquisição via SPI/FIFO;
* processamento de blocos de 2048 amostras;
* cálculo de features no domínio do tempo;
* FFT;
* identificação do componente 1×RPM;
* estimativa de RPM;
* validação externa do RPM;
* construção estatística de baseline;
* detecção por Z-score;
* margem de segurança de 10%;
* votação 2/3 entre features;
* persistência temporal;
* detecção de ausência de vibração/motor parado;
* interface local HMI;
* leitura de temperatura com DS18B20;
* saída analógica planejada via MCP4725;
* telemetria via Wi-Fi/MQTT/TLS;
* visualização em ThingsBoard.

### Fora do escopo atual

Os seguintes itens **não fazem parte do escopo da FETIN**:

* armazenamento em cartão SD;
* registro histórico local de waveforms;
* classificação específica de falhas como desalinhamento, desbalanceamento etc.;
* sensor Hall dedicado para RPM;
* PCNT para medição de RPM;
* modelos de Machine Learning complexos;
* EMA adaptativo;
* classificação multiclasse de falhas;
* expansão para múltiplos eixos no pipeline de decisão.

O sistema atualmente detecta **mudança de condição**, mas não identifica automaticamente o tipo físico da falha.

---

## 4. Filosofia do Projeto

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
* Alterações arquiteturais devem ser justificadas antes de serem implementadas.
* O projeto deve priorizar estabilidade e validação do escopo existente em vez de adicionar novas funcionalidades próximo à FETIN.

---

## 5. Arquitetura Geral

`main.c` é responsável por:

1. Inicializar a infraestrutura global necessária.
2. Inicializar o `app_context`.
3. Inicializar os componentes.
4. Criar e configurar as tasks.

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

ThingsBoard possui função exclusivamente de:

* telemetria;
* supervisão;
* visualização;
* acompanhamento dos indicadores.

ThingsBoard **não participa das decisões de detecção**.

---

## 6. Distribuição entre os Cores

| Core   | Responsabilidade                                |
| ------ | ----------------------------------------------- |
| Core 0 | DSP + System                                    |
| Core 1 | Accelerometer + Sensors + HMI + Telemetry + DAC |

### Core 0

O Core 0 concentra o fluxo determinístico:

```text
DSP → SYSTEM
```

O `task_system` realiza:

* processamento dos resultados do DSP;
* atualização do baseline;
* cálculo estatístico;
* avaliação das features;
* decisão de estado;
* controle das transições;
* distribuição dos resultados.

O Core 0 não deve executar I/O pesado, renderização de LCD, MQTT ou outras operações que possam bloquear o fluxo de decisão.

### Core 1

O Core 1 concentra periféricos e interfaces:

* aquisição do acelerômetro;
* sensores adicionais;
* HMI;
* comunicação MQTT;
* DAC.

A arquitetura busca evitar que operações externas interfiram na cadeia:

```text
Aquisição → DSP → System → Decisão
```

---

## 7. Infraestrutura Compartilhada

| Recurso         | Status                                     |
| --------------- | ------------------------------------------ |
| SPI2            | Implementado e utilizado pelo acelerômetro |
| SPI3            | Implementado/utilizado pela HMI            |
| I2C             | Infraestrutura destinada ao MCP4725        |
| Wi-Fi           | Validado                                   |
| MQTT            | Validado                                   |
| MQTT/TLS        | Validado                                   |
| ThingsBoard     | Recebendo telemetria                       |
| FreeRTOS Queues | Implementadas                              |
| FreeRTOS Tasks  | Implementadas                              |
| Mutex SPI2      | Utilizado conforme necessidade             |

---

## 8. Fluxo de Dados

### 8.1 Fluxo principal

```text
LSM6DS3TR-C
      │
      │ SPI + FIFO
      ▼
FIFO
      │
      ▼
Acquisition
      │
      ▼
Bloco de 2048 amostras
      │
      ▼
queue_accel_block_to_dsp
      │
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
      └────────→ telemetry_data_t → Telemetry
                                      │
                                      ▼
                                    MQTT
                                      │
                                      ▼
                                 ThingsBoard
```

### 8.2 Fluxo dos sensores

```text
DS18B20
   │
   ▼
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

O `task_system` recebe a temperatura e a distribui aos consumidores.

### 8.3 Fluxo da HMI

```text
Button
   │
   ▼
task_hmi
   │
   │ system_command_t
   ▼
queue_hmi_to_system
   │
   ▼
task_system
```

O clique curto é tratado pela HMI para navegação entre telas.

O clique longo solicita um novo warm-up/baseline através do `task_system`.

---

## 9. Organização dos Componentes

Componentes atuais:

* `main`
* `app_context`
* `accelerometer`
* `dsp_pipeline`
* `system`
* `hmi`
* `telemetry`
* `sensors`
* `dac`

O componente `storage`/SD **não faz mais parte da arquitetura atual**.

O armazenamento local em cartão SD está fora do escopo da FETIN.

> `rpm_counter` foi removido do projeto. RPM é estimado a partir da frequência do pico espectral associado ao componente 1×RPM da FFT. Não existe sensor Hall, PCNT ou medição de RPM dedicada em hardware.

---

# 10. Componentes

## 10.1 main

|                  |                                                     |
| ---------------- | --------------------------------------------------- |
| Responsabilidade | Inicialização da infraestrutura e criação das tasks |
| Dependências     | Todos os componentes                                |
| Interface        | `app_main()`                                        |
| Status           | Implementado                                        |

`main` não contém lógica de processamento ou decisão do sistema.

---

## 10.2 app_context

|                  |                                        |
| ---------------- | -------------------------------------- |
| Responsabilidade | Contexto compartilhado entre tasks     |
| Dependências     | Nenhuma                                |
| Interface        | `app_context_init(app_context_t *ctx)` |
| Status           | Implementado                           |

Regra principal:

> Um dado possui um único dono/escritor.

Buffers privados permanecem dentro dos componentes.

### Queues atuais

* `queue_accel_block_to_dsp`
* `queue_dsp_to_system`
* `queue_sensors_to_system`
* `queue_system_to_hmi`
* `queue_hmi_to_system`
* `queue_system_to_dac`
* `queue_system_to_telemetry`

Queues que representam o estado ou resultado mais recente devem utilizar tamanho 1 e `xQueueOverwrite()` quando apropriado.

A comunicação entre DSP e System deve preservar a sequência necessária para:

* warm-up;
* baseline;
* avaliação;
* detecção;
* persistência.

### Estruturas compartilhadas

As principais estruturas incluem:

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

---

## 10.3 accelerometer

|                  |                                                     |
| ---------------- | --------------------------------------------------- |
| Responsabilidade | LSM6DS3TR-C, SPI, FIFO, aquisição e envio de blocos |
| Hardware         | LSM6DS3TR-C                                         |
| Interface        | SPI2                                                |
| Saída            | `queue_accel_block_to_dsp`                          |
| Status           | Concluído                                           |

Responsabilidades:

* configuração do LSM6DS3TR-C;
* aquisição via FIFO;
* leitura dos dados;
* recuperação local de falhas de SPI/FIFO;
* seleção do eixo utilizado pelo DSP;
* formação de blocos de 2048 amostras;
* envio para o DSP.

O acelerômetro continua adquirindo os três eixos, porém o pipeline DSP atual processa somente o eixo selecionado.

### Não faz

* FFT;
* RMS;
* Kurtosis;
* Z-score;
* threshold;
* decisão de estado;
* HMI;
* MQTT;
* DAC.

---

## 10.4 dsp_pipeline

|                  |                                                         |
| ---------------- | ------------------------------------------------------- |
| Responsabilidade | Processamento do sinal no domínio do tempo e frequência |
| Entrada          | `accel_block_t`                                         |
| Saída            | `dsp_result_t`                                          |
| Dependências     | ESP-DSP                                                 |
| Status           | Concluído                                               |

O DSP trabalha com:

```text
ACCEL_BLOCK_SIZE = 2048
ACCEL_SAMPLE_RATE_HZ ≈ 6660 Hz
```

### Features calculadas

* RMS;
* StdDev;
* Min;
* Max;
* Peak-to-Peak;
* Crest Factor;
* Kurtosis.

### Análise espectral

* janela de Hann;
* FFT;
* magnitude;
* normalização;
* eixo de frequência;
* busca do pico;
* interpolação parabólica;
* amplitude do componente 1×RPM;
* estimativa de frequência;
* estimativa de RPM.

### RPM

A estimativa é realizada por:

```text
RPM = f_peak × 60
```

onde `f_peak` corresponde ao componente espectral associado ao 1×RPM.

O RPM já foi comparado com referência externa/tacômetro e está considerado **validado para o escopo atual**.

### Ausência de vibração

O DSP utiliza `peak_valid` para indicar se foi identificado um componente vibracional válido dentro da condição de busca configurada.

Quando não há vibração válida, o DSP registra a condição correspondente e o System utiliza `peak_valid` para controlar o estado `NO_MOTOR`.

### Não faz

O DSP não decide:

* `HEALTHY`;
* `ALARM`;
* `NO_MOTOR`;
* Z-score;
* threshold;
* votação 2/3;
* persistência temporal.

Sua responsabilidade termina na produção de um `dsp_result_t`.

---

## 10.5 system

|                  |                                      |
| ---------------- | ------------------------------------ |
| Responsabilidade | Mestre do estado, baseline e decisão |
| Entrada          | DSP, sensores e comandos da HMI      |
| Saída            | HMI, DAC e Telemetry                 |
| Interface        | `task_system(void *arg)`             |
| Status           | Implementado                         |

O `task_system` é o **único proprietário do estado da máquina**.

### Responsabilidades

1. Controlar o estado da máquina.
2. Controlar o warm-up.
3. Construir o baseline.
4. Validar o baseline.
5. Calcular Z-scores.
6. Aplicar threshold.
7. Avaliar evidência 2/3.
8. Controlar persistência temporal.
9. Detectar ausência de vibração.
10. Controlar `NO_MOTOR`.
11. Retomar monitoramento após retorno da vibração.
12. Incorporar dados dos sensores.
13. Distribuir os resultados aos consumidores.

---

# 11. Pipeline de Detecção

A decisão ocorre exclusivamente no `task_system`.

```text
                         dsp_result_t
                              │
                              ▼
                       Motor presente?
                         /          \
                       não          sim
                       │              │
                       ▼              ▼
                   NO_MOTOR        WARMUP?
                                     /   \
                                   sim   não
                                    │      │
                                    ▼      ▼
                                 Baseline  Z-score
                                    │      │
                                    │      ▼
                                    │   Threshold
                                    │      │
                                    │      ▼
                                    │   Evidência
                                    │     2/3
                                    │      │
                                    └──────►│
                                           ▼
                                      Persistência
                                           │
                                           ▼
                                         Estado
```

---

## 11.1 Warm-up

O baseline é construído utilizando:

```text
600 avaliações válidas
```

Cada avaliação corresponde a um resultado válido produzido pelo DSP para um bloco de 2048 amostras.

Uma avaliação com:

```text
peak_valid = false
```

não é utilizada como observação válida para construção do baseline.

Durante o warm-up:

* as avaliações válidas alimentam as estatísticas;
* avaliações inválidas de vibração são ignoradas;
* nenhuma condição `ALARM` é declarada;
* a HMI pode apresentar o progresso;
* a contagem representa avaliações válidas utilizadas na construção do baseline.

Após as 600 avaliações válidas:

```text
WARMUP
   ↓
baseline validado
   ↓
HEALTHY
```

---

## 11.2 Baseline

O baseline atual utiliza três features:

```text
RMS
Kurtosis
Amplitude 1×RPM
```

Para cada feature são calculados incrementalmente:

```text
μ = média
σ = desvio padrão
```

O algoritmo utilizado é baseado em estatística online, evitando armazenar todas as 600 observações.

O baseline permanece fixo após sua construção.

### Validação do baseline

O baseline somente é considerado válido quando todas as condições necessárias são atendidas, incluindo quantidade mínima de observações válidas do componente 1×RPM.

Caso o baseline seja considerado inválido ao final do warm-up:

```text
baseline inválido
      ↓
reset das estatísticas de aquisição
      ↓
novo WARMUP
```

O objetivo é impedir que um conjunto parcialmente inválido seja reutilizado como baseline.

---

# 12. Detecção de Ausência de Motor — NO_MOTOR

O sistema possui um estado específico:

```text
SYSTEM_STATE_NO_MOTOR
```

Esse estado representa ausência persistente de vibração válida compatível com a condição de operação monitorada.

### Critério atual

São consideradas as avaliações consecutivas em que:

```text
peak_valid == false
```

Após:

```text
15 avaliações consecutivas inválidas
```

o sistema entra em:

```text
NO_MOTOR
```

### Comportamento

```text
HEALTHY / ALARM / WARMUP
            │
            │ 15 avaliações inválidas
            ▼
         NO_MOTOR
```

Ao entrar em `NO_MOTOR`, o contexto de monitoramento atual é resetado:

* contagem de warm-up;
* contagem de bins válidos;
* estatísticas online;
* diagnósticos;
* contadores de persistência.

O baseline previamente validado **não é apagado**.

Isso é importante porque o desligamento temporário do motor não deve obrigar a reconstrução de um baseline já válido.

### Retorno do motor

Enquanto estiver em `NO_MOTOR`:

```text
peak_valid == false
        ↓
permanece NO_MOTOR
```

Quando surgir uma avaliação válida:

```text
peak_valid == true
```

há duas possibilidades.

### Baseline já válido

```text
NO_MOTOR
   ↓
peak_valid = true
   ↓
baseline válido
   ↓
HEALTHY
   ↓
retoma monitoramento imediatamente
```

O sistema não executa um novo warm-up.

### Baseline inválido

```text
NO_MOTOR
   ↓
peak_valid = true
   ↓
baseline inválido
   ↓
WARMUP
   ↓
novo baseline
```

Essa abordagem evita recalibração desnecessária quando o sistema já possui um baseline válido.

### Parâmetro

```c
SYSTEM_NO_MOTOR_CONSECUTIVE_COUNT = 15
```

Com:

```text
Fs ≈ 6660 Hz
N = 2048
```

cada bloco representa aproximadamente:

```text
2048 / 6660 ≈ 0,307 s
```

Portanto, 15 avaliações correspondem aproximadamente a:

```text
4,6 segundos
```

de ausência contínua de vibração válida.

---

# 13. Z-score

Para cada avaliação após o baseline:

```text
Z = (x - μ) / σ
```

São calculados:

* `zscore_rms`;
* `zscore_kurtosis`;
* `zscore_1x_rpm`.

Quando o desvio padrão não fornece uma condição estatística válida, a feature não deve gerar uma decisão estatística indefinida.

---

# 14. Threshold

O threshold atual é:

```c
SYSTEM_ZSCORE_THRESHOLD = 3.0f
```

Uma feature é considerada anormal quando atende ao critério estatístico implementado.

A implementação atual utiliza:

```text
Z > threshold
```

com margem adicional baseada no baseline:

```text
valor > baseline_mean × 1.10
```

Portanto, a feature precisa apresentar simultaneamente:

```text
Z-score > 3.0
```

e

```text
valor > média do baseline × 1,10
```

O detector atual é unilateral.

Os parâmetros devem continuar sendo validados experimentalmente com dados reais do motor.

---

# 15. Evidência 2/3

As três features utilizadas na decisão são:

```text
RMS
Kurtosis
Amplitude 1×RPM
```

Cada feature é classificada como:

```text
NORMAL
```

ou:

```text
ABNORMAL
```

Uma avaliação é considerada anormal quando:

```text
2 de 3 features = ABNORMAL
```

Crest Factor permanece disponível como indicador do DSP e pode ser apresentado na HMI/Telemetry, mas **não participa da decisão atual 2/3**.

---

# 16. Persistência Temporal

Uma única avaliação anormal não é suficiente para declarar `ALARM`.

A implementação atual utiliza:

```text
5 avaliações anormais consecutivas
```

para:

```text
HEALTHY → ALARM
```

Qualquer avaliação normal interrompe a sequência de entrada no alarme.

Para recuperação:

```text
5 avaliações normais consecutivas
```

são necessárias para:

```text
ALARM → HEALTHY
```

Qualquer avaliação anormal interrompe a sequência de recuperação.

Os parâmetros de persistência devem ser validados durante os testes finais no motor.

---

# 17. Estados do Sistema

O `task_system` é o único proprietário do estado.

Estados atuais:

```text
INIT
WARMUP
HEALTHY
ALARM
NO_MOTOR
```

### Fluxo principal

```text
INIT
  ↓
WARMUP
  ↓
HEALTHY
  ↓
ALARM
```

Com `NO_MOTOR` como estado transversal de ausência de vibração válida:

```text
HEALTHY ────────┐
ALARM ──────────┤
WARMUP ─────────┼──→ NO_MOTOR
                │
                └── 15 avaliações inválidas
```

---

## 17.1 INIT

Estado inicial da lógica do sistema.

A HMI não precisa apresentar `INIT` como uma tela específica.

---

## 17.2 WARMUP

Construção do baseline.

```text
600 avaliações válidas
```

Nenhuma condição `ALARM` é declarada durante essa fase.

---

## 17.3 HEALTHY

Indica:

* baseline disponível;
* vibração válida;
* nenhuma condição anormal persistente detectada.

---

## 17.4 ALARM

Indica que uma condição anormal persistente foi detectada.

Critério atual:

```text
2/3 features anormais
+
5 avaliações consecutivas
```

---

## 17.5 NO_MOTOR

Indica que não foi detectada vibração válida por 15 avaliações consecutivas.

Esse estado não significa necessariamente uma falha do motor.

Pode representar:

* motor desligado;
* motor parado;
* ausência temporária de vibração;
* condição fora da faixa de detecção configurada.

Ao retornar uma vibração válida, o sistema retoma o monitoramento.

---

# 18. Transições de Estado

```text
                 ┌──────────────┐
                 │     INIT     │
                 └──────┬───────┘
                        │
                        ▼
                 ┌──────────────┐
                 │    WARMUP    │
                 └──────┬───────┘
                        │
                  600 válidas
                        │
                        ▼
                 ┌──────────────┐
                 │   HEALTHY    │
                 └───┬──────┬───┘
                     │      │
           5 anormais│      │15 inválidas
                     │      │
                     ▼      ▼
              ┌──────────┐  ┌────────────┐
              │  ALARM   │  │  NO_MOTOR  │
              └────┬─────┘  └──────┬─────┘
                   │                │
           5 normais                │
                   │         peak_valid=true
                   │                │
                   └───────┬────────┘
                           ▼
                       HEALTHY
```

### Comando de novo warm-up

```text
SYSTEM_COMMAND_RESET_WARMUP
```

solicita um novo processo de aquisição de baseline sem reboot do ESP32.

---

# 19. HMI

|                  |                                              |
| ---------------- | -------------------------------------------- |
| Responsabilidade | Interface local                              |
| Hardware         | LCD TFT 3.5" SPI                             |
| Entrada          | Botão                                        |
| Interface        | `queue_system_to_hmi`, `queue_hmi_to_system` |
| Status           | Funcional, refinamento final pendente        |

A HMI recebe dados através de `hmi_data_t`.

Ela não acessa diretamente:

* acelerômetro;
* buffers privados do DSP;
* estado interno do System;
* MQTT.

### Telas atuais

1. Status.
2. FFT.
3. Diagnóstico.

---

## 19.1 Tela Status

Informações principais:

* estado;
* temperatura;
* RPM;
* frequência;
* RMS;
* demais indicadores definidos visualmente.

Estados:

```text
WARMUP
HEALTHY
ALARM
NO_MOTOR
```

### Representação

```text
WARMUP   → amarelo
HEALTHY  → verde
ALARM    → vermelho
NO_MOTOR → amarelo
```

---

## 19.2 Tela FFT

A HMI apresenta:

```text
5 Hz → 250 Hz
```

A implementação atual utiliza 75 bins nativos:

```text
bins 2 → 76
```

Com:

```text
Fs ≈ 6660 Hz
N = 2048
```

a resolução aproximada é:

```text
Δf ≈ 3,25 Hz
```

Os bins utilizados representam aproximadamente:

```text
6,5 Hz → 247,9 Hz
```

Essa faixa cobre adequadamente a região visual de 5–250 Hz sem necessidade de reamostragem adicional.

A HMI reconstrói o eixo de frequência a partir da configuração conhecida do DSP.

---

## 19.3 Tela Diagnóstico

Apresenta as três features utilizadas pela decisão:

* RMS;
* Kurtosis;
* amplitude 1×RPM.

Quando aplicável, apresenta:

* valor atual;
* Z-score;
* classificação `NORMAL`/`ABNORMAL`.

O objetivo é tornar visualmente explicável a decisão 2/3.

---

## 19.4 Botão

Comportamento desejado:

* clique curto → próxima tela;
* clique longo → novo warm-up.

O processamento do botão permanece isolado da lógica de decisão.

### Estado atual

A lógica de HMI está funcional, porém existe uma limitação física no botão que ainda precisa ser corrigida/validada no hardware final.

Esse problema não deve alterar a arquitetura do System.

---

# 20. Sensors

|                  |                                  |
| ---------------- | -------------------------------- |
| Responsabilidade | Aquisição de sensores adicionais |
| Sensor atual     | DS18B20                          |
| Saída            | `sensor_result_t`                |
| Status           | Implementado e validado          |

O DS18B20 fornece:

```text
Temperatura
```

A temperatura é enviada:

```text
Sensors
   ↓
System
   ├──→ HMI
   └──→ Telemetry
```

A leitura foi validada no firmware e integrada ao fluxo de apresentação/telemetria.

---

# 21. DAC

|                  |                                   |
| ---------------- | --------------------------------- |
| Responsabilidade | Saída analógica para osciloscópio |
| Hardware         | MCP4725                           |
| Interface        | I2C                               |
| Entrada          | `queue_system_to_dac`             |
| Status           | Pendente                          |

O DAC deverá reproduzir um sinal temporal representativo da vibração adquirida.

O contrato previsto utiliza uma waveform de:

```text
2048 amostras
```

com aproximadamente:

```text
2048 × 4 bytes = 8192 bytes
```

A queue dedicada ao DAC pode transportar essa estrutura.

Esse consumo é aceitável para o ESP32-S3 atual, mas o consumo total de RAM deve continuar sendo monitorado.

### Pendências

* implementação do driver MCP4725;
* definição da taxa efetiva de atualização;
* definição da quantidade de amostras reproduzidas;
* escalonamento;
* offset;
* limites do DAC;
* validação no osciloscópio;
* comparação entre waveform adquirida e waveform reproduzida.

O DAC é atualmente o principal bloco funcional ainda não concluído do hardware.

---

# 22. Telemetry

|                  |                               |
| ---------------- | ----------------------------- |
| Responsabilidade | Telemetria via Wi-Fi/MQTT/TLS |
| Backend          | ThingsBoard Cloud             |
| Status           | Funcional                     |

O fluxo é:

```text
System
   │
   ▼
telemetry_data_t
   │
   ▼
Telemetry
   │
   ▼
Wi-Fi
   │
   ▼
MQTT/TLS
   │
   ▼
ThingsBoard
```

### Dados enviados

Podem incluir:

* estado;
* temperatura;
* RMS;
* Kurtosis;
* Crest Factor;
* amplitude 1×RPM;
* RPM;
* frequência;
* Z-scores;
* classificação das features;
* informações de warm-up;
* diagnósticos necessários à supervisão.

A telemetria é uma **saída secundária**.

Uma falha de:

* Wi-Fi;
* MQTT;
* TLS;
* ThingsBoard;

não pode interromper:

```text
Accelerometer
      ↓
DSP
      ↓
System
      ↓
Decisão local
```

---

# 23. ThingsBoard

ThingsBoard é utilizado como camada de:

* visualização;
* telemetria;
* acompanhamento dos indicadores;
* demonstração remota.

O dashboard está funcional.

O estado atual é de **refinamento visual**, não de implementação fundamental.

### Indicadores relevantes

* estado da máquina;
* temperatura;
* RMS;
* RPM;
* frequência;
* features;
* Z-scores;
* condição de alarme.

O dashboard não executa a lógica de detecção.

---

# 24. Segurança e Credenciais

Credenciais de infraestrutura **não fazem parte da arquitetura funcional** e não devem ser armazenadas diretamente em código-fonte versionado.

Isso inclui:

* SSID;
* senha de Wi-Fi;
* tokens de acesso;
* credenciais MQTT;
* credenciais de ThingsBoard;
* certificados ou chaves privadas sensíveis.

### Situação atual

Foi identificado que credenciais reais de Wi-Fi/ThingsBoard foram anteriormente expostas em código versionado no GitHub.

Isso deve ser tratado como um **problema de segurança do repositório**, independentemente de o firmware continuar funcionando.

### Regra

Nunca colocar credenciais reais diretamente em:

```text
*.c
*.h
README
architecture.md
logs
scripts públicos
```

### Mitigação necessária

1. Revogar/regenerar o token de acesso exposto.
2. Remover credenciais ativas do código rastreado.
3. Utilizar configuração local ignorada pelo Git ou mecanismo apropriado do ESP-IDF.
4. Garantir que arquivos contendo credenciais estejam no `.gitignore`.
5. Verificar o histórico do Git para evitar considerar a simples remoção do arquivo atual como suficiente.
6. Substituir credenciais reais por placeholders em documentação.

### Impacto na FETIN

A correção das credenciais não precisa necessariamente bloquear a demonstração física do protótipo caso a infraestrutura atual continue funcionando e seja utilizada apenas em ambiente controlado.

Entretanto, **o repositório não deve ser considerado seguro enquanto credenciais válidas permanecerem expostas no histórico**.

Esse item deve ser resolvido antes de publicação/compartilhamento público do projeto.

---

# 25. Pipeline DSP Detalhado

```text
Bloco de 2048 amostras
        │
        ├─────────────────────┐
        │                     │
        ▼                     ▼
Features temporais          Hann
        │                     │
        │                     ▼
        │                    FFT
        │                     │
        │                     ▼
        │                  Magnitude
        │                     │
        │                     ▼
        │                 Normalização
        │                     │
        │                     ▼
        │                 Busca de pico
        │                     │
        │                     ▼
        │             Interpolação parabólica
        │                     │
        │                     ▼
        │                RPM estimado
        │
        └─────────────────────┐
                              ▼
                         dsp_result_t
                              │
                              ▼
                         task_system
```

### Features temporais

```text
RMS
StdDev
Min
Max
Peak-to-Peak
Crest Factor
Kurtosis
```

### Features utilizadas na decisão

```text
RMS
Kurtosis
Amplitude 1×RPM
```

---

# 26. Memória e Transporte de Dados

O `dsp_result_t` é uma estrutura relativamente grande devido principalmente a:

```text
magnitude[1024]
waveform[2048]
```

O resultado completo do DSP é transportado somente na interface:

```text
DSP → System
```

Os consumidores recebem estruturas específicas.

```text
System → HMI

    hmi_data_t
```

```text
System → DAC

    dac_waveform_t
```

```text
System → Telemetry

    telemetry_data_t
```

O objetivo é evitar transportar `dsp_result_t` completo para consumidores que não necessitam de todos os dados.

### Objetivos

* evitar cópias desnecessárias;
* evitar transportar dados desnecessários;
* manter ownership explícito;
* evitar condições de corrida;
* preservar determinismo do DSP → System;
* garantir que Telemetry nunca bloqueie a decisão local.

---

# 27. Comunicação entre Componentes

| Mecanismo                   | Uso                       |
| --------------------------- | ------------------------- |
| `queue_accel_block_to_dsp`  | Bloco de 2048 amostras    |
| `queue_dsp_to_system`       | Resultado completo do DSP |
| `queue_sensors_to_system`   | Resultado dos sensores    |
| `queue_system_to_hmi`       | Dados para HMI            |
| `queue_hmi_to_system`       | Comandos HMI → System     |
| `queue_system_to_dac`       | Waveform para DAC         |
| `queue_system_to_telemetry` | Dados para Telemetry      |

### Regra de ownership

> **Um dado possui um único dono/escritor. Consumidores somente leem os dados recebidos através de suas interfaces.**

O `task_dsp` é responsável pelos resultados do processamento.

O `task_system` é responsável por:

* estado;
* baseline;
* estatísticas;
* decisão.

HMI, DAC e Telemetry não acessam diretamente:

* acelerômetro;
* buffers privados do DSP;
* estado interno do System.

---

# 28. Recursos Compartilhados

| Recurso                     | Dono                           | Consumidores  | Sincronização                  |
| --------------------------- | ------------------------------ | ------------- | ------------------------------ |
| SPI2                        | Infraestrutura / Accelerometer | Accelerometer | `mutex_spi2` quando necessário |
| SPI3                        | Infraestrutura / HMI           | HMI           | Isolado                        |
| I2C                         | Infraestrutura                 | DAC           | Conforme implementação         |
| `queue_accel_block_to_dsp`  | Accelerometer                  | DSP           | Queue                          |
| `queue_dsp_to_system`       | DSP                            | System        | Queue                          |
| `queue_sensors_to_system`   | Sensors                        | System        | Queue                          |
| `queue_system_to_hmi`       | System                         | HMI           | Queue                          |
| `queue_hmi_to_system`       | HMI                            | System        | Queue                          |
| `queue_system_to_dac`       | System                         | DAC           | Queue                          |
| `queue_system_to_telemetry` | System                         | Telemetry     | Queue                          |

Nenhuma task consumidora deve acessar diretamente buffers privados de outro componente.

---

# 29. Limitações Conhecidas

* Apenas um eixo é processado pelo DSP.
* Os três eixos continuam sendo adquiridos.
* FFT possui resolução limitada pelo bloco de 2048 amostras.
* RPM é estimado pelo pico espectral.
* Não existe sensor dedicado de RPM.
* RPM já foi validado externamente, mas sua precisão continua dependente da qualidade do pico espectral detectado.
* Baseline não é persistido em flash/NVS.
* Baseline permanece fixo após sua construção.
* O sistema detecta mudança de condição, mas não classifica automaticamente o tipo de falha.
* Threshold estatístico ainda deve ser validado com dados reais de desequilíbrio.
* Persistência de 5 avaliações ainda deve ser validada no teste final.
* `dsp_result_t` é grande.
* A HMI utiliza apenas a faixa de aproximadamente 5–250 Hz.
* O DAC MCP4725 ainda não foi integrado.
* A taxa efetiva de reprodução do DAC ainda precisa ser validada.
* O botão da HMI possui uma questão física de hardware ainda pendente.
* ThingsBoard depende de conectividade externa.
* A telemetria não pode ser considerada parte da cadeia determinística de decisão.
* Credenciais anteriormente expostas no GitHub exigem tratamento de segurança antes de publicação segura do repositório.

---

# 30. Status Atual do Projeto

| Bloco                            | Status                          |
| -------------------------------- | ------------------------------- |
| LSM6DS3TR-C                      | Concluído                       |
| SPI/FIFO                         | Concluído                       |
| Recuperação SPI/FIFO             | Concluído                       |
| Aquisição 2048 amostras          | Concluído                       |
| DSP temporal                     | Concluído                       |
| RMS                              | Concluído                       |
| Kurtosis                         | Concluído                       |
| Crest Factor                     | Concluído                       |
| Min/Max/PkPk                     | Concluído                       |
| FFT                              | Concluído                       |
| Pico espectral                   | Concluído                       |
| 1×RPM                            | Concluído                       |
| RPM                              | Validado externamente           |
| Baseline 600 avaliações          | Concluído                       |
| Validação do baseline            | Concluído                       |
| Reset de baseline inválido       | Concluído                       |
| Z-score                          | Concluído                       |
| Margem de 10%                    | Concluído                       |
| Votação 2/3                      | Concluído                       |
| Persistência HEALTHY → ALARM     | Implementado                    |
| Persistência ALARM → HEALTHY     | Implementado                    |
| `NO_MOTOR`                       | Implementado e testado          |
| Retorno `NO_MOTOR → HEALTHY`     | Implementado                    |
| DS18B20                          | Implementado e validado         |
| HMI Status                       | Funcional                       |
| HMI FFT                          | Funcional                       |
| HMI Diagnóstico                  | Implementação/refinamento final |
| Botão HMI                        | Questão física pendente         |
| Wi-Fi                            | Funcional                       |
| MQTT                             | Funcional                       |
| MQTT/TLS                         | Funcional                       |
| ThingsBoard                      | Funcional                       |
| Dashboard                        | Funcional; refinamento visual   |
| DAC MCP4725                      | Pendente                        |
| SD Card                          | Fora do escopo                  |
| Robustez final                   | Pendente                        |
| Teste de desbalanceamento        | Pendente                        |
| Teste integrado final            | Pendente                        |
| Limpeza de credenciais do GitHub | Pendente                        |

---

# 31. Pendências Atuais

O projeto está atualmente em fase de **integração final, validação experimental e robustez**, e não mais em fase de desenvolvimento arquitetural principal.

## 31.1 MCP4725 / DAC

* [ ] Implementar driver MCP4725.
* [ ] Integrar I2C.
* [ ] Integrar `queue_system_to_dac`.
* [ ] Definir taxa de atualização.
* [ ] Definir escalonamento.
* [ ] Definir offset.
* [ ] Reproduzir waveform.
* [ ] Validar sinal no osciloscópio.
* [ ] Comparar sinal adquirido e reproduzido.

---

## 31.2 Teste HEALTHY → ALARM → HEALTHY

Realizar teste controlado utilizando o motor real.

Procedimento esperado:

```text
Motor saudável
      ↓
HEALTHY
      ↓
Inserção de condição anormal controlada
      ↓
features alteradas
      ↓
2/3 abnormal
      ↓
5 avaliações consecutivas
      ↓
ALARM
      ↓
remoção da condição anormal
      ↓
5 avaliações normais
      ↓
HEALTHY
```

O teste deve registrar:

* RMS;
* Kurtosis;
* amplitude 1×RPM;
* Z-scores;
* classificação das features;
* contadores de persistência;
* estado final.

A condição de desequilíbrio será induzida de forma controlada utilizando o acoplamento assimétrico planejado para o teste do motor.

---

## 31.3 Robustez

Realizar revisão final dos seguintes pontos:

### Dados

* [ ] Validar `isfinite()` antes de alimentar estatísticas.
* [ ] Garantir ausência de NaN.
* [ ] Garantir ausência de Inf.
* [ ] Verificar comportamento com dados inválidos.
* [ ] Verificar comportamento quando `peak_valid = false`.

### Queues

* [ ] Verificar falha de envio.
* [ ] Verificar overflow.
* [ ] Verificar starvation.
* [ ] Verificar comportamento quando consumidor estiver temporariamente ocupado.
* [ ] Verificar que Telemetry não bloqueie System.

### Tasks

* [ ] Verificar stack high-water mark.
* [ ] Verificar uso de RAM.
* [ ] Verificar CPU.
* [ ] Verificar possíveis bloqueios.
* [ ] Verificar comportamento durante reconexão Wi-Fi/MQTT.

### System

* [x] Validar `NO_MOTOR`.
* [x] Validar retorno do `NO_MOTOR`.
* [x] Resetar contexto de aquisição quando baseline inválido.
* [ ] Validar `HEALTHY → ALARM`.
* [ ] Validar `ALARM → HEALTHY`.
* [ ] Validar novo warm-up via HMI.

---

# 32. Segurança do Repositório

Antes de considerar o repositório pronto para publicação:

* [ ] Remover credenciais reais do código.
* [ ] Revogar/regenerar tokens anteriormente expostos.
* [ ] Remover credenciais de arquivos de configuração rastreados.
* [ ] Adicionar arquivos locais de configuração ao `.gitignore`.
* [ ] Substituir credenciais em documentação por placeholders.
* [ ] Verificar o histórico Git.
* [ ] Garantir que nenhum token válido permaneça acessível no histórico público.
* [ ] Confirmar que o firmware continua funcionando após a mudança para configuração segura.

A limpeza do histórico é uma atividade de **segurança do repositório**, não uma alteração da arquitetura funcional do MachineGuard.

Para a demonstração da FETIN, a prioridade é manter o protótipo funcional; entretanto, credenciais válidas expostas não devem permanecer em um repositório que será disponibilizado publicamente.

---

# 33. Campanha Final de Validação

A campanha integrada final deve seguir aproximadamente:

```text
1. Energizar sistema
        ↓
2. Inicialização
        ↓
3. Motor desligado
        ↓
4. NO_MOTOR
        ↓
5. Ligar motor
        ↓
6. WARMUP ou HEALTHY imediato
        ↓
7. Operação saudável
        ↓
8. Validar features
        ↓
9. Inserir desequilíbrio controlado
        ↓
10. ALARM
        ↓
11. Remover desequilíbrio
        ↓
12. HEALTHY
        ↓
13. Desligar motor
        ↓
14. NO_MOTOR
        ↓
15. Ligar novamente
        ↓
16. Retomar HEALTHY sem novo baseline
```

Durante a campanha devem ser verificadas simultaneamente:

* aquisição;
* DSP;
* RPM;
* System;
* HMI;
* temperatura;
* DAC;
* MQTT;
* ThingsBoard.

---

# 34. Demonstração Final

O comportamento esperado para a demonstração é:

### Motor desligado

```text
NO_MOTOR
```

### Motor ligado sem baseline

```text
WARMUP
   ↓
HEALTHY
```

### Motor ligado com baseline já existente

```text
NO_MOTOR
   ↓
vibração válida
   ↓
HEALTHY
```

sem novo warm-up.

### Condição normal

```text
HEALTHY
```

### Desequilíbrio controlado

```text
HEALTHY
   ↓
2/3 features abnormal
   ↓
5 avaliações
   ↓
ALARM
```

### Desequilíbrio removido

```text
ALARM
   ↓
5 avaliações normais
   ↓
HEALTHY
```

### Telemetria

ThingsBoard deve refletir os principais indicadores e mudanças de estado.

### Falha de conectividade

Caso Wi-Fi/MQTT seja interrompido:

```text
Aquisição → DSP → System → Decisão
```

deve continuar funcionando localmente.

---

# 35. Testes de Sistema

## Warm-up

* [x] Construção do baseline.
* [x] Contagem de 600 avaliações.
* [x] Estatística online.
* [x] Validação de baseline.
* [x] Reinício quando baseline inválido.
* [ ] Repetição via comando da HMI.

## Detecção

* [X] 1/3 features anormais.
* [X] 2/3 features anormais.
* [X] 3/3 features anormais.
* [X] HEALTHY → ALARM.
* [X] ALARM → HEALTHY.
* [X] Interrupção da sequência anormal.
* [X] Interrupção da sequência de recuperação.

## NO_MOTOR

* [x] 15 avaliações inválidas.
* [x] Entrada em `NO_MOTOR`.
* [x] Permanência enquanto inválido.
* [x] Retorno com baseline válido.
* [x] Retorno para warm-up quando baseline inválido.

## Sensores

* [x] DS18B20.
* [x] Temperatura no System.
* [x] Temperatura na HMI.
* [x] Temperatura na Telemetry.

## Telemetry

* [x] Wi-Fi.
* [x] MQTT.
* [x] TLS.
* [x] ThingsBoard.
* [x] Recebimento de dados.
* [X] Refinamento final do dashboard.
* [X] Teste com perda de conectividade.

## DAC

* [ ] Implementação MCP4725.
* [ ] Teste de waveform.
* [ ] Validação no osciloscópio.

---

# 36. Validação do RPM

O RPM é obtido através do componente espectral 1×RPM.

```text
f_peak → RPM
```

A validação deve utilizar uma referência externa.

### Status

A comparação com tacômetro externo já foi realizada e o método foi considerado validado para o escopo atual.

Não existe pendência arquitetural de RPM.

Novas medições somente serão necessárias caso novos testes revelem comportamento inconsistente.

---

# 37. Integração Final

Checklist:

* [x] ACCEL → DSP.
* [x] DSP → SYSTEM.
* [x] Sensors → SYSTEM.
* [x] SYSTEM → HMI.
* [x] SYSTEM → Telemetry.
* [ ] SYSTEM → DAC.
* [x] HMI → SYSTEM.
* [x] Wi-Fi → MQTT → ThingsBoard.
* [ ] Validação integrada final.
* [ ] Teste de perda de conectividade.
* [ ] Teste final de RAM.
* [ ] Teste final de stacks.
* [ ] Teste final de estabilidade.

---

# 38. Regras para IA

As regras abaixo devem ser seguidas por qualquer IA trabalhando no projeto.

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
* O baseline é construído durante 600 avaliações válidas.
* Baseline válido deve ser preservado quando o motor retorna após `NO_MOTOR`.
* `NO_MOTOR` deve ser utilizado para representar ausência persistente de vibração válida.
* Não apagar um baseline válido simplesmente porque o motor foi desligado.
* Quando o baseline for inválido, reiniciar a aquisição do baseline.
* Não implementar EMA adaptativo sem decisão arquitetural explícita.
* Não substituir a análise estatística por algoritmos mais complexos sem evidência experimental.
* Threshold e persistência devem ser calibrados experimentalmente.
* Não otimizar memória prematuramente sem medir o consumo real.
* Não transformar comunicação entre tasks em armazenamento histórico quando somente o dado mais recente é necessário.
* Não adicionar SD/storage ao escopo da FETIN sem decisão explícita.
* Não reintroduzir `rpm_counter`, Hall ou PCNT sem decisão arquitetural explícita.
* Não expandir o sistema para classificação automática de falhas sem decisão arquitetural explícita.
* Nunca inserir credenciais reais em código, documentação ou arquivos versionados.
* Nunca repetir ou expor tokens, senhas, SSIDs ou chaves privadas.
* Antes de publicar o repositório, credenciais anteriormente expostas devem ser revogadas/regeneradas.
* Priorizar estabilidade, validação e integração do escopo atual em vez de novas funcionalidades próximas à FETIN.

---

# 39. Estado Arquitetural Atual

O MachineGuard encontra-se atualmente em uma fase de:

```text
ARQUITETURA PRINCIPAL
        ↓
      FINALIZADA
        ↓
IMPLEMENTAÇÃO PRINCIPAL
        ↓
      FINALIZADA
        ↓
INTEGRAÇÃO + VALIDAÇÃO
        ↓
       ATUAL
        ↓
ROBUSTEZ + TESTE FINAL
        ↓
      FETIN
```

Os principais blocos da arquitetura já estão implementados e validados.

As atividades restantes devem se concentrar em:

1. MCP4725/DAC;
2. teste controlado `HEALTHY → ALARM → HEALTHY`;
3. revisão final de robustez;
4. correção da questão física do botão, se necessária para a demonstração;
5. refinamento do dashboard;
6. limpeza das credenciais expostas;
7. campanha integrada final.

Não devem ser introduzidos novos subsistemas ou expansão significativa de escopo sem justificativa e decisão explícita.
