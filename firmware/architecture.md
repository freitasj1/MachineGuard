# MachineGuard — architecture.md

## 1. Objetivo

Documentar como o MachineGuard está organizado: estrutura do firmware,
comunicação entre componentes, responsabilidades dos módulos e decisões
arquiteturais que não devem ser quebradas.

---

## 2. Visão Geral do Projeto

| | |
|---|---|
| Projeto | MachineGuard |
| Objetivo | Manutenção preditiva para motores rotativos via análise de vibração |
| MCU | ESP32-S3 N16R8 (dual-core LX7, 16 MB flash, 8 MB PSRAM OPI) |
| Processamento | Edge/local; telemetria opcional via Wi-Fi/MQTT |
| Framework | ESP-IDF |
| Prazo | FETIN — 25/09/2026 |

---

## 3. Filosofia do Projeto

- Arquitetura limpa e modular.
- Baixo acoplamento entre componentes.
- Determinismo no fluxo de aquisição, DSP e decisão.
- Simplicidade sobre generalidade.
- ESP-IDF puro, sem Arduino framework.
- Cada componente possui uma responsabilidade clara.
- `task_system` é o mestre do estado, baseline e decisões do sistema.
- O DSP processa sinais, mas não decide o estado da máquina.
- Consumidores não acessam diretamente buffers privados de outros componentes.
- Hardware e I/O ficam fora do fluxo determinístico de decisão sempre que possível.
- Falhas de telemetria não podem impedir a operação local do MachineGuard.

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

O processamento principal permanece local no ESP32-S3. ThingsBoard não participa
das decisões de detecção: sua função é visualização e telemetria.

### Core ownership

| Core | Responsabilidade |
|---|---|
| Core 0 | Aquisição + DSP + processamento de decisão determinístico |
| Core 1 | HMI, Telemetry, Sensors, DAC e demais I/O |

O `task_system` pertence ao fluxo de processamento de decisão. Ele não deve
executar acesso direto a hardware, MQTT ou renderização do LCD. Sua função é
processar resultados, controlar o estado da máquina e distribuir os dados
necessários aos consumidores.

### Infraestrutura compartilhada

| Recurso | Status |
|---|---|
| SPI2 | Implementado |
| SPI3 | Futuro / HMI, se necessário |
| I2C | Futuro / MCP4725 e sensores |
| Wi-Fi | Testado com sucesso no Wi-Fi do INATEL |
| MQTT/TLS | Testado com sucesso com ThingsBoard Cloud |

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
   ├────────→ HMI
   │
   ├────────→ DAC
   │
   └────────→ Telemetry → MQTT → ThingsBoard
```

### Fluxo dos sensores

```text
task_sensors
   │
   │ sensor_result
   ▼
queue_sensor_to_system
   │
   ▼
task_system
   │
   ├────────→ HMI
   └────────→ Telemetry
```

### Comando da HMI para o System

```text
Button
   │
   ▼
task_hmi
   │
   │ comando de reset do warm-up
   ▼
queue_hmi_to_system
   │
   ▼
task_system
```

O clique curto do botão é tratado localmente pela HMI e altera somente a tela
atual. O clique longo gera um comando para o `task_system`, solicitando um novo
warm-up/baseline.

---

## 6. Organização dos Componentes

Componentes previstos:

- `main`
- `app_context`
- `accelerometer`
- `dsp_pipeline`
- `system`
- `hmi`
- `telemetry`
- `sensors`
- `dac`

O componente `storage`/SD foi substituído arquiteturalmente por `telemetry`.
O armazenamento em cartão SD não faz parte da arquitetura atual.

> `rpm_counter` foi removido do projeto. RPM é estimado a partir da frequência
> do pico espectral associado ao componente 1×RPM da FFT. A validação do RPM
> estimado será realizada externamente com um tacômetro digital.

### main

| | |
|---|---|
| Responsabilidade | Inicialização de infraestrutura e criação de tasks |
| Dependências | Todos os componentes |
| Interface | `app_main` |

`main` não contém lógica de processamento ou decisão do sistema.

### app_context

| | |
|---|---|
| Responsabilidade | Contexto compartilhado entre tasks |
| Dependências | Nenhuma |
| Interface | `app_context_init(app_context_t *ctx)` |
| Regra | Um dado → um dono; buffers privados permanecem dentro do componente |
| Recursos | Queues e mutexes compartilhados |

Queues previstas:

- `queue_accel_block_to_dsp`
- `queue_dsp_to_system`
- `queue_sensor_to_system`
- `queue_system_to_hmi`
- `queue_hmi_to_system`
- `queue_system_to_dac`
- `queue_system_to_telemetry`

As queues de saída do `task_system` representam o estado/resultado mais recente
e podem utilizar tamanho 1 com `xQueueOverwrite()` quando apropriado.

A comunicação entre DSP e System deve preservar a sequência necessária para o
warm-up e para a análise de decisão.

### accelerometer

| | |
|---|---|
| Responsabilidade | LSM6DS3TR-C, SPI, DMA, FIFO, ping-pong, seleção de eixo e envio de blocos |
| Não faz | FFT, estatísticas, decisões, HMI, Telemetry ou DAC |
| Dependências | SPI2, `mutex_spi2` |
| Fluxo | Aquisição → seleção de eixo → 2048 amostras → `queue_accel_block_to_dsp` |

O acelerômetro continua adquirindo os três eixos, mas somente um eixo é
selecionado para o pipeline DSP.

### dsp_pipeline

| | |
|---|---|
| Responsabilidade | Processamento do sinal no domínio do tempo e frequência |
| Dependências | `queue_accel_block_to_dsp`, ESP-DSP |
| Interface | `task_dsp(void *arg)` |
| Saída | `dsp_result_t` |

O DSP implementa:

- RMS
- StdDev
- Min
- Max
- Peak-to-Peak
- Crest Factor
- Kurtosis
- FFT
- busca e interpolação do pico espectral
- estimativa de RPM

O `task_dsp` não possui responsabilidade sobre:

- estado da máquina;
- Z-score;
- threshold;
- votação 2/3;
- persistência temporal;
- geração de `HEALTHY`/`ALARM`.

### system

| | |
|---|---|
| Responsabilidade | Mestre do estado, baseline e decisão |
| Dependências | `queue_dsp_to_system`, `queue_sensor_to_system`, `queue_hmi_to_system` |
| Interface | `task_system(void *arg)` |

Responsabilidades:

1. Controlar o estado da máquina.
2. Controlar o warm-up.
3. Construir o baseline.
4. Calcular Z-scores.
5. Aplicar threshold.
6. Avaliar a evidência 2/3.
7. Controlar persistência temporal.
8. Determinar `HEALTHY` ou `ALARM`.
9. Distribuir os resultados aos consumidores.

O baseline é construído durante 600 avaliações saudáveis e permanece fixo após
o warm-up, até que um novo warm-up seja solicitado.

### hmi

| | |
|---|---|
| Responsabilidade | Interface local com LCD TFT e botão |
| Dependências | `queue_system_to_hmi`, `queue_hmi_to_system` |
| Hardware | LCD TFT 3.5" SPI + botão |

A HMI deverá apresentar, no mínimo:

- temperatura;
- RPM;
- status (`HEALTHY`/`ALARM`, além de `WARMUP` quando aplicável);
- gráfico FFT, preferencialmente em tela própria.

Indicadores adicionais em avaliação:

- RMS;
- Kurtosis;
- amplitude 1×RPM/frequência;
- Crest Factor.

O botão terá:

- clique curto: troca para a próxima tela;
- clique longo: solicita novo warm-up.

O tratamento físico do botão deve ser isolado da lógica de tela. A implementação
pode utilizar ISR para detectar o evento e uma comunicação ISR → `task_hmi`;
debounce e tempo de clique longo permanecem parte do contrato de implementação
da HMI.

### telemetry

| | |
|---|---|
| Responsabilidade | Publicar telemetria via MQTT |
| Dependências | Wi-Fi, MQTT/TLS, `queue_system_to_telemetry` |
| Backend | ThingsBoard Cloud |

Dados candidatos à telemetria:

- estado da máquina;
- temperatura;
- RMS;
- Kurtosis;
- Crest Factor;
- amplitude 1×RPM;
- RPM;
- frequência;
- Z-scores;
- progresso do warm-up.

A telemetria é uma saída secundária. Uma falha de Wi-Fi ou MQTT não deve
interromper aquisição, DSP ou decisão local.

O acesso ao broker `mqtt.thingsboard.cloud:8883` com TLS já foi validado no
ESP32-S3 utilizando o Wi-Fi do INATEL.

### sensors

| | |
|---|---|
| Responsabilidade | Aquisição de sensores adicionais |
| Dependências | Hardware dos sensores |
| Interface | `task_sensors(void *arg)` |

O DS18B20 fornece a temperatura utilizada pela HMI e pela Telemetry.

Os resultados dos sensores são enviados ao `task_system`, que decide como eles
serão distribuídos.

### dac

| | |
|---|---|
| Responsabilidade | Saída analógica para osciloscópio |
| Hardware | MCP4725 |
| Dependências | `queue_system_to_dac` |

O DAC deverá reproduzir um sinal temporal representativo da vibração adquirida,
com todas as medidas temporais necessárias para a demonstração.

A task do DAC é responsável pelo acesso físico ao MCP4725. O contrato de dados,
taxa de atualização, quantidade de amostras, escalonamento e ownership do buffer
devem ser definidos antes da implementação final.

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

- RMS
- StdDev
- Min
- Max
- Peak-to-Peak
- Crest Factor
- Kurtosis

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

O `dsp_result_t` pode conter a magnitude FFT necessária para a HMI. O eixo de
frequência pode ser reconstruído a partir de `bin_width_hz` enquanto a taxa de
amostragem e o tamanho da FFT permanecerem fixos.

A necessidade de transportar a FFT completa para cada consumidor deve ser
reavaliada antes da integração final, devido ao impacto de RAM e cópias.

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

Cada avaliação corresponde a um resultado produzido pelo DSP para um bloco de
2048 amostras.

Durante o warm-up:

- os dados são utilizados para construir o baseline;
- nenhuma condição de `ALARM` é declarada;
- a HMI pode apresentar o progresso.

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

- RMS;
- Kurtosis;
- amplitude 1×RPM.

Para cada feature:

```text
μ = média
σ = desvio padrão
```

As estatísticas podem ser calculadas incrementalmente, sem armazenar as 600
avaliações completas.

O baseline permanece fixo após o warm-up.

### 8.3 Z-score

Para cada avaliação:

```text
Z = (x - μ) / σ
```

São calculados:

- `zscore_rms`
- `zscore_kurtosis`
- `zscore_1x_rpm`

Se o desvio padrão for insuficiente, a feature não deve gerar uma decisão
estatística indefinida.

### 8.4 Threshold

O threshold inicial é configurável e deve ser validado com dados reais.

Conceitualmente:

```text
|Z| <= threshold → normal
|Z| >  threshold → anormal
```

O comportamento definitivo e o valor do threshold devem ser confirmados
experimentalmente.

### 8.5 Evidência 2/3

As três features de decisão são:

```text
RMS
Kurtosis
Amplitude 1×RPM
```

Cada uma é classificada como `NORMAL` ou `ANORMAL`.

A avaliação é considerada anormal quando pelo menos 2 das 3 features são
anormais.

Crest Factor permanece disponível como feature do DSP e como possível indicador
da HMI/Telemetry, mas não participa da decisão inicial.

### 8.6 Persistência temporal

Uma única avaliação anormal não é suficiente para declarar `ALARM`.

A implementação atual utiliza:

- 5 avaliações anormais consecutivas para `HEALTHY → ALARM`;
- qualquer avaliação normal interrompe a sequência de entrada em `ALARM`;
- 5 avaliações normais consecutivas para `ALARM → HEALTHY`;
- qualquer avaliação anormal interrompe a sequência de recuperação.

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

---

## 10. Comunicação entre Componentes

| Mecanismo | Uso |
|---|---|
| `queue_accel_block_to_dsp` | Bloco de 2048 amostras |
| `queue_dsp_to_system` | Resultados do DSP |
| `queue_sensor_to_system` | Resultados dos sensores |
| `queue_system_to_hmi` | Estado e dados para HMI |
| `queue_hmi_to_system` | Comandos da HMI para System |
| `queue_system_to_dac` | Dados para saída analógica |
| `queue_system_to_telemetry` | Dados para MQTT |

### Regra de ownership

**Um dado tem um único dono/escritor. Consumidores somente leem os dados
recebidos por suas interfaces.**

O `task_dsp` é responsável pelos resultados do processamento do sinal.

O `task_system` é responsável pelo estado, baseline e decisões.

HMI, DAC e Telemetry não acessam diretamente acelerômetro, buffers privados do
DSP ou estado interno do System.

---

## 11. Recursos Compartilhados

| Recurso | Dono | Consumidores | Sincronização |
|---|---|---|---|
| SPI2 | `main` (init) | Accelerometer | `mutex_spi2` |
| SPI3 | `main` (futuro) | HMI | Isolado |
| I2C | `main` (futuro) | DAC / sensores | Conforme implementação |
| `queue_dsp_to_system` | DSP | System | Queue |
| `queue_system_to_hmi` | System | HMI | Queue |
| `queue_hmi_to_system` | HMI | System | Queue |
| `queue_system_to_dac` | System | DAC | Queue |
| `queue_system_to_telemetry` | System | Telemetry | Queue |

Nenhuma task consumidora acessa diretamente buffers privados de outro componente.

---

## 12. Memória e Transporte de Dados

O `dsp_result_t` atualmente pode ser grande devido principalmente a:

- `magnitude[1024]`;
- `waveform[2048]`.

Antes da integração final, deve ser definido quais dados cada consumidor
realmente necessita.

Objetivos:

- evitar múltiplas cópias de aproximadamente 12 KB;
- evitar transportar dados desnecessários;
- definir ownership dos buffers;
- evitar condições de corrida;
- manter o fluxo DSP → System determinístico.

As interfaces de HMI, DAC e Telemetry podem utilizar estruturas de saída
menores que o resultado interno completo do DSP.

---

## 13. Convenções de Código

| | |
|---|---|
| Linguagem | C |
| Framework | ESP-IDF |
| Branch | `feat_<feature>` / `bugfix_<algo>` |
| Commits | Conventional Commits |
| Colunas | 100 |

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

Código legado pode utilizar `snake_case`; não é necessário reescrever módulos
existentes apenas por causa da convenção.

---

## 14. Limitações Conhecidas

- Apenas um eixo é processado no DSP; os três eixos continuam sendo adquiridos.
- FFT real.
- RPM é estimado pelo pico espectral, sem sensor de RPM integrado.
- Validação do RPM será realizada com referência externa.
- Baseline não é persistido em flash/NVS.
- Baseline permanece fixo após o warm-up.
- O sistema detecta mudança de condição, mas não classifica o tipo de falha.
- Threshold de Z-score ainda precisa de validação experimental.
- Parâmetros de persistência ainda precisam de validação com dados reais.
- A magnitude FFT completa pode ser grande demais para ser copiada
  indiscriminadamente entre tasks.
- A taxa efetiva do MCP4725 para reprodução da waveform ainda precisa ser
  validada.
- Telemetry depende de Wi-Fi e MQTT, mas sua indisponibilidade não deve afetar
  a decisão local.
- Dashboard ThingsBoard é uma interface de supervisão, não parte do loop de
  controle/detecção.
- O novo baseline solicitado pela HMI ainda precisa ter seu mecanismo de
  comunicação e reset implementado.

---

## 15. Pendências Arquiteturais

### Infraestrutura

- [ ] Registrar todas as tasks em `main.c`.
- [ ] Implementar/validar `app_context_init()`.
- [ ] Criar as queues definitivas.
- [ ] Definir prioridades e afinidade de Core.
- [ ] Definir tamanhos das queues.
- [ ] Definir ownership dos buffers.

### System

- [ ] Consolidar contratos de saída.
- [ ] Implementar comando HMI → System para novo warm-up.
- [ ] Validar comportamento de reset do baseline.
- [ ] Avaliar parâmetros estatísticos com dados reais.
- [ ] Avaliar `SYSTEM_ZSCORE_THRESHOLD`.
- [ ] Validar decisão 2/3.
- [ ] Validar 5 avaliações consecutivas.
- [ ] Verificar representatividade do desvio padrão.
- [ ] Verificar comportamento com pouca e alta variabilidade.

### HMI

- [ ] Definir telas finais.
- [ ] Definir dados obrigatórios/opcionais.
- [ ] Definir frequência de atualização.
- [ ] Definir tratamento do botão.
- [ ] Definir ISR/debounce/clique longo.
- [ ] Implementar LCD TFT.
- [ ] Implementar navegação.
- [ ] Implementar comando de novo warm-up.

### DAC

- [ ] Definir waveform.
- [ ] Definir taxa de atualização.
- [ ] Definir quantidade de amostras.
- [ ] Definir escalonamento.
- [ ] Definir formato e ownership do buffer.
- [ ] Implementar MCP4725.
- [ ] Validar sinal no osciloscópio.

### Telemetry

- [ ] Definir payload MQTT.
- [ ] Definir tópico.
- [ ] Definir frequência de publicação.
- [ ] Definir autenticação/token.
- [ ] Implementar task Telemetry.
- [ ] Implementar tratamento de desconexão.
- [ ] Integrar ThingsBoard.
- [ ] Criar dashboard.

### Sensores

- [ ] Implementar DS18B20.
- [ ] Integrar temperatura ao System.
- [ ] Integrar temperatura à HMI.
- [ ] Integrar temperatura à Telemetry.

### Testes

- [ ] Testes sistemáticos do System sem motor.
- [ ] Testar 1/3 features anormais.
- [ ] Testar 2/3 features anormais.
- [ ] Testar 3/3 features anormais.
- [ ] Testar HEALTHY → ALARM → HEALTHY.
- [ ] Testar interrupção da sequência.
- [ ] Testar warm-up novamente via comando HMI.
- [ ] Testar fluxo completo ACCEL → DSP → SYSTEM → OUTPUTS.
- [ ] Testar motor saudável.
- [ ] Testar condição anormal controlada.
- [ ] Validar RPM com tacômetro Minipa.

---

## 16. Regras para IA

- Nunca alterar a arquitetura sem discutir previamente.
- Nunca criar componentes novos sem autorização.
- Nunca adicionar bibliotecas por conta própria.
- Nunca alterar a responsabilidade de um módulo sem discutir previamente.
- Sempre justificar decisões arquiteturais propostas.
- `architecture.md` é a fonte de verdade da arquitetura do firmware.
- `task_dsp` processa sinais e retorna features; não decide o estado da máquina.
- `task_system` é o mestre do estado, baseline e decisão.
- HMI, DAC e Telemetry não acessam diretamente buffers privados de outros
  componentes.
- O processamento e a decisão permanecem locais no ESP32-S3.
- MQTT/ThingsBoard é telemetria e visualização, não parte da lógica de detecção.
- Falhas de telemetria não devem bloquear o processamento local.
- O baseline atual é construído durante 600 avaliações e permanece fixo até
  um novo warm-up.
- Não implementar EMA adaptativo sem decisão arquitetural explícita.
- Não substituir a análise estatística por algoritmos mais complexos sem
  evidência experimental.
- Threshold e persistência devem ser calibrados experimentalmente.