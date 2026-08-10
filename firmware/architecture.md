# MachineGuard — architecture.md

## 1. Objetivo

Documentar **como o MachineGuard está organizado**: estrutura do firmware,
comunicação entre componentes, regras de implementação e decisões
arquiteturais que não podem ser quebradas.

---

## 2. Visão Geral do Projeto

|               |                                                                     |
| ------------- | ------------------------------------------------------------------- |
| Projeto       | MachineGuard                                                        |
| Objetivo      | Manutenção preditiva para motores rotativos via análise de vibração |
| MCU           | ESP32-S3 N16R8 (dual-core LX7, 16 MB flash, 8 MB PSRAM OPI)         |
| Processamento | 100% local (edge), sem nuvem/gateway                                |
| Framework     | ESP-IDF                                                             |
| Prazo         | FETIN — 25/09/2026                                                  |

---

## 3. Filosofia do Projeto

* Arquitetura limpa e modular
* Baixo acoplamento entre componentes
* Determinismo (Core 0 nunca faz I/O)
* Simplicidade sobre generalidade
* ESP-IDF puro (sem Arduino framework)
* Cada componente possui uma responsabilidade clara
* `task_system` é o mestre do estado e das decisões do sistema
* O DSP processa sinais, mas não decide o estado da máquina
* Consumidores não acessam diretamente buffers privados de outros componentes

---

## 4. Arquitetura Geral

`main.c` é responsável **apenas** por:

1. Inicializar infraestrutura global (barramentos, `app_context`)
2. Criar tasks

Depois disso, `main` não executa mais lógica nenhuma.

**Infraestrutura compartilhada, inicializada no `main`:**

| Recurso                           | Status                               |
| --------------------------------- | ------------------------------------ |
| SPI2                              | implementado                         |
| SPI3                              | futuro                               |
| I2C                               | futuro                               |
| mutexes / queues do `app_context` | pendente (`app_context_init` é stub) |

Cada componente inicializa apenas **seu próprio dispositivo** sobre a
infraestrutura já criada — nunca o barramento em si.

```text
main            → inicializa SPI2
accelerometer   → adiciona LSM6DS3TR-C ao barramento SPI2
storage         → adiciona SD Card ao barramento SPI2
```

### Core ownership

| Core   | Responsabilidade                                          |
| ------ | --------------------------------------------------------- |
| Core 0 | Aquisição + DSP + processamento de decisão determinístico |
| Core 1 | HMI, storage, sensors, DAC                                |

O `task_system` pertence ao fluxo de processamento determinístico e não deve
executar operações de I/O diretamente. Sua responsabilidade é processar os
resultados recebidos, controlar o estado da máquina e distribuir os resultados
para as tasks consumidoras.

---

## 5. Fluxo de Dados

Fluxo principal de aquisição e processamento:

```text
LSM6DS3TR-C
   │  SPI + DMA
   ▼
 FIFO (sensor)
   │
   ▼
 DMA
   │
   ▼
 Ping-Pong Buffer (accelerometer)
   │
   │ Queue
   │ 1 eixo selecionado
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
   ├──▶ queue_system_to_hmi
   ├──▶ queue_system_to_storage
   └──▶ queue_system_to_dac
```

Fluxo dos sensores:

```text
task_sensor
   │
   │ sensor_result
   ▼
queue_sensor_to_system
   │
   ▼
task_system
   │
   ├──▶ HMI
   ├──▶ storage
   └──▶ DAC
```

### Regra de ownership

**Um dado tem um único dono/escritor. Consumidores somente leem os dados
recebidos por suas respectivas filas.**

O `task_dsp` é responsável pelos resultados do processamento do sinal.

O `task_system` é responsável pelo estado, baseline e decisões do sistema.

HMI, storage e DAC não acessam diretamente acelerômetro, buffers do DSP ou
estado interno do `task_system`.

---

## 6. Organização dos Componentes

Componentes atuais:

* `main`
* `app_context`
* `accelerometer`
* `dsp_pipeline`
* `system`
* `hmi`
* `storage`
* `sensors`
* `dac`

> `rpm_counter` foi removido do projeto. RPM é estimado a partir da frequência
> do pico espectral associado ao componente 1×RPM da FFT. A validação do RPM
> estimado poderá ser realizada externamente com um tacômetro digital durante
> a apresentação, sem necessidade de implementar um componente de medição de
> RPM no firmware.

---

### main

|                     |                                                                    |
| ------------------- | ------------------------------------------------------------------ |
| Status              | implementado (esqueleto)                                           |
| Responsabilidade    | init de infraestrutura e criação de tasks                          |
| Dependências        | todos os componentes                                               |
| Interfaces públicas | `app_main`                                                         |
| TODO                | registrar tasks via `xTaskCreatePinnedToCore` conforme arquitetura |

`main` não contém lógica de processamento ou decisão do sistema.

---

### app_context

|                     |                                                                     |
| ------------------- | ------------------------------------------------------------------- |
| Status              | implementado                                                        |
| Responsabilidade    | contexto compartilhado entre tasks                                  |
| Dependências        | nenhuma                                                             |
| Interfaces públicas | `app_context_init(app_context_t *ctx)`                              |
| Regra               | um dado → um dono; buffers privados permanecem dentro do componente |
| Recursos            | queues de comunicação e `mutex_spi2`                                |

Queues previstas:

* `queue_accel_block_to_dsp`
* `queue_dsp_to_system`
* `queue_sensor_to_system`
* `queue_system_to_hmi`
* `queue_system_to_storage`
* `queue_system_to_dac`

As queues de saída do `task_system` representam o **estado/resultado mais
recente** e podem utilizar tamanho 1 com `xQueueOverwrite()`.

A comunicação entre `task_dsp` e `task_system` deve preservar a sequência dos
resultados necessários para o warm-up e para a análise temporal.

---

### accelerometer

|                     |                                                                                               |
| ------------------- | --------------------------------------------------------------------------------------------- |
| Status              | Concluído (`feat_accelerometer`)                                                              |
| Responsabilidade    | configurar LSM6DS3TR-C, SPI, DMA, FIFO, ping-pong, seleção de eixo e envio de blocos para DSP |
| NÃO faz             | FFT, RMS, kurtosis, decisões de estado, HMI, storage                                          |
| Dependências        | SPI2, `mutex_spi2`                                                                            |
| Interfaces públicas | `accel_init(app_context_t *ctx)`, `task_accel(void *arg)`                                     |
| Fluxo               | aquisição → seleção de eixo → bloco de 2048 amostras → `queue_accel_block_to_dsp`             |
| Regra               | driver não inicializa barramento — utiliza a infraestrutura criada pelo `main`                |

O acelerômetro continua adquirindo os três eixos, mas somente um eixo é
selecionado para o pipeline DSP.

---

### dsp_pipeline

|                     |                                                                                                           |
| ------------------- | --------------------------------------------------------------------------------------------------------- |
| Status              | Em desenvolvimento (`feature_dsp`)                                                                        |
| Responsabilidade    | processamento do sinal no domínio do tempo e da frequência                                                |
| Dependências        | `queue_accel_block_to_dsp`, ESP-DSP                                                                       |
| Interfaces públicas | `task_dsp(void *arg)`                                                                                     |
| Implementado        | RMS, StdDev, Min, Max, Peak-to-Peak, Crest Factor, Kurtosis e FFT                                         |
| Análise espectral   | janela de Hann, FFT, magnitude, normalização, eixo de frequência, busca de pico e interpolação parabólica |
| RPM                 | estimado a partir da frequência do pico espectral                                                         |
| Validação           | estatísticas temporais/RMS validadas; frequência/RPM ainda em validação experimental                      |
| TODO                | consolidar `dsp_result_t`, integração com `task_system` e validação do componente 1×RPM                   |

O `task_dsp` **não possui responsabilidade sobre o estado da máquina**.

Ele recebe um bloco de aquisição, processa o sinal e retorna um `dsp_result_t`
contendo as features calculadas.

O DSP não sabe se o sistema está em:

* `WARMUP`
* `HEALTHY`
* `ALARM`

Também não calcula:

* Z-score
* threshold de anomalia
* persistência temporal
* votação
* estado da máquina

---

### system

|                     |                                                                                               |
| ------------------- | --------------------------------------------------------------------------------------------- |
| Status              | A implementar                                                                                 |
| Responsabilidade    | mestre do estado e das decisões do sistema                                                    |
| Dependências        | `queue_dsp_to_system`, `queue_sensor_to_system`                                               |
| Interfaces públicas | `task_system(void *arg)`                                                                      |
| Responsabilidades   | warm-up, baseline, Z-score, threshold, evidência multivariada, persistência temporal e estado |
| NÃO faz             | aquisição, FFT, acesso direto a hardware, renderização HMI, storage ou DAC                    |

O `task_system` é o **orquestrador central do comportamento do MachineGuard**.

Ele recebe os resultados produzidos pelo DSP e pelos sensores e determina o
estado atual da máquina.

### Responsabilidades do `task_system`

1. Controlar o estado da máquina
2. Controlar o warm-up
3. Contabilizar as 600 avaliações do baseline
4. Construir o baseline estatístico
5. Manter o baseline fixo após o warm-up
6. Calcular Z-scores
7. Aplicar o threshold de anomalia
8. Determinar a evidência de anomalia de cada avaliação
9. Avaliar a persistência temporal através de uma janela
10. Determinar `HEALTHY` ou `ALARM`
11. Distribuir os resultados para HMI, storage e DAC

O `task_system` não atualiza o baseline durante o monitoramento normal.

Para iniciar um novo warm-up, o sistema poderá ser reinicializado. A
persistência do baseline em flash/NVS não faz parte da implementação atual.

---

### hmi

|                  |                                       |
| ---------------- | ------------------------------------- |
| Status           | não implementado                      |
| Responsabilidade | exibir estado, indicadores e espectro |
| Dependências     | `queue_system_to_hmi`                 |
| TODO             | implementação da interface LCD        |

A HMI deverá exibir, entre outros:

* estado da máquina
* progresso do warm-up
* RMS
* kurtosis
* crest factor
* amplitude 1×RPM
* RPM estimado
* Z-scores
* estado da detecção
* espectro FFT

### Espectro

A HMI deverá permitir visualizar uma faixa configurável do espectro, por
exemplo:

```text
0–400 Hz
```

O `task_dsp` disponibiliza a magnitude FFT necessária para essa visualização.

A HMI não acessa diretamente os buffers privados do DSP.

---

### storage

|                  |                                         |
| ---------------- | --------------------------------------- |
| Status           | não implementado                        |
| Responsabilidade | salvar resultados e indicadores         |
| Dependências     | `queue_system_to_storage`, `mutex_spi2` |
| Limitação atual  | waveforms completas não são armazenadas |
| TODO             | implementação                           |

---

### sensors

|                  |                                                       |
| ---------------- | ----------------------------------------------------- |
| Status           | não implementado                                      |
| Responsabilidade | DS18B20, leitura de bateria e outros sensores futuros |
| Dependências     | definidas conforme hardware                           |
| TODO             | implementação                                         |

Os resultados dos sensores são enviados para o `task_system`, que decide como
esses dados serão utilizados e distribuídos.

---

### dac

|                  |                                   |
| ---------------- | --------------------------------- |
| Status           | não implementado                  |
| Responsabilidade | saída analógica para osciloscópio |
| Dependências     | `queue_system_to_dac`             |
| TODO             | implementação                     |

O DAC deverá receber um sinal temporal apropriado para visualização no
osciloscópio.

A intenção atual é representar a vibração periódica detectada pelo
acelerômetro. Em uma condição de desbalanceamento, espera-se observar aumento
da amplitude do sinal.

O tratamento necessário para gerar esse sinal é responsabilidade do pipeline
DSP, enquanto o acesso físico ao DAC permanece responsabilidade da task do
DAC.

---

## 7. Pipeline DSP

### 7.1 Processamento do sinal

```text
Seleciona eixo
   │
   ▼
2048 amostras
   │
   ├───────────────┐
   │               │
   ▼               ▼
Time-domain       Hann
features           │
   │               ▼
   │              FFT Real
   │               │
   │               ▼
   │          Magnitude Linear
   │               │
   │               ▼
   │          Busca de pico
   │               │
   │               ▼
   │          Frequência do pico
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

### 7.2 Features no domínio do tempo

O DSP calcula:

* RMS
* StdDev
* Min
* Max
* Peak-to-Peak
* Crest Factor
* Kurtosis

Essas métricas são calculadas sobre o sinal de vibração processado.

### 7.3 Análise espectral

O pipeline utiliza:

1. Janela de Hann
2. FFT Real via ESP-DSP
3. Magnitude linear
4. Normalização
5. Eixo de frequência
6. Busca do pico na faixa de operação configurada
7. Interpolação parabólica
8. Conversão da frequência estimada para RPM

A estimativa é:

[
RPM = f_{peak} \times 60
]

onde `f_peak` é a frequência do componente espectral associado ao 1×RPM.

A validação do RPM estimado será realizada experimentalmente, podendo utilizar
um tacômetro digital externo durante a feira.

### 7.4 Dados espectrais

O `dsp_result_t` poderá transportar a magnitude FFT para permitir:

* visualização do espectro pela HMI;
* processamento posterior;
* outras saídas que necessitem da informação espectral.

O eixo de frequência pode ser reconstruído a partir de `bin_width_hz` enquanto
a frequência de amostragem e o tamanho da FFT permanecerem fixos.

---

## 8. Pipeline de Detecção

O processo de decisão ocorre exclusivamente no `task_system`.

```text
dsp_result_t
      │
      ▼
   WARMUP?
   /     \
 sim      não
  │         │
  ▼         ▼
baseline   Z-score
  │         │
  │         ▼
  │      threshold
  │         │
  │         ▼
  │    feature evidence
  │         │
  │         ▼
  │   temporal window
  │         │
  │         ▼
  └──────► machine state
```

---

### 8.1 Warm-up

O warm-up possui exatamente:

```text
600 avaliações
```

Cada avaliação corresponde a um resultado produzido pelo `task_dsp` para um
bloco de 2048 amostras.

A HMI poderá apresentar, por exemplo:

```text
WARM-UP

Coletando dados
438 / 600
```

Durante o warm-up:

* o sistema considera que o motor está saudável;
* os resultados do DSP são utilizados para construir o baseline;
* nenhuma decisão de `ALARM` é tomada;
* o baseline ainda não é utilizado para detecção.

Após a avaliação 600:

```text
WARMUP
   ↓
baseline finalizado
   ↓
HEALTHY
```

O baseline permanece fixo durante aquela execução do sistema.

Para iniciar um novo baseline, a implementação atual poderá reiniciar o
dispositivo e executar novamente o warm-up.

---

### 8.2 Baseline

O baseline é calculado separadamente para cada indicador utilizado na
detecção:

```text
RMS
Kurtosis
Amplitude 1×RPM
```

Para cada indicador são determinados:

```text
μ = média durante o warm-up
σ = desvio padrão durante o warm-up
```

O baseline é calculado a partir das 600 avaliações consideradas saudáveis e
estáveis.

O armazenamento das 600 avaliações completas não é necessário. As estatísticas
podem ser calculadas incrementalmente durante o warm-up.

O baseline é congelado após o warm-up.

Não existe atualização adaptativa do baseline durante o monitoramento na
implementação atual.

---

### 8.3 Z-score

Para cada nova avaliação após o warm-up:

[
Z = \frac{x-\mu}{\sigma}
]

onde:

* `x` = valor atual produzido pelo DSP;
* `μ` = média do indicador durante o warm-up;
* `σ` = desvio padrão do indicador durante o warm-up.

São calculados três Z-scores:

```text
zscore_rms
zscore_kurtosis
zscore_bin
```

O Z-score representa quantos desvios padrão o valor atual está acima ou abaixo
do comportamento considerado normal durante o warm-up.

Se o desvio padrão de uma feature for insuficiente para produzir um Z-score
confiável, a feature não deve gerar uma decisão de anomalia baseada em um
valor estatisticamente indefinido.

---

### 8.4 Threshold

Cada Z-score é comparado com um threshold configurável.

O threshold inicial ainda será determinado experimentalmente.

Conceitualmente:

```text
Z <= threshold
    → normal

Z > threshold
    → anormal
```

O objetivo do threshold é identificar uma alteração estatisticamente
significativa em relação ao baseline saudável.

O valor definitivo não deve ser considerado fixo antes da validação experimental.

---

### 8.5 Evidência por avaliação

Cada avaliação produz três indicadores de evidência:

```text
RMS
Kurtosis
Amplitude 1×RPM
```

Cada indicador pode ser classificado como:

```text
NORMAL
ANORMAL
```

A avaliação pode então ser representada por uma quantidade de indicadores
anormais:

```text
0 → nenhum indicador anormal
1 → um indicador anormal
2 → dois indicadores anormais
3 → três indicadores anormais
```

A condição de evidência multivariada utilizada inicialmente é:

```text
2 de 3 indicadores anormais
```

O `Crest Factor`, StdDev, Min, Max e Peak-to-Peak permanecem disponíveis como
features do DSP, mas não participam da decisão inicial de estado.

---

### 8.6 Persistência temporal

Uma única avaliação anormal não deve ser suficiente para declarar uma falha.

Após o threshold, a evidência de cada avaliação é armazenada em uma janela
temporal.

Conceitualmente:

```text
Avaliação 1 → 2/3
Avaliação 2 → 2/3
Avaliação 3 → 0/3
Avaliação 4 → 2/3
Avaliação 5 → 2/3
```

O `task_system` analisa a quantidade de avaliações anormais dentro da janela.

A decisão final depende da persistência da condição anormal ao longo da janela,
e não de uma única leitura.

Os seguintes parâmetros ainda serão calibrados experimentalmente:

* tamanho da janela;
* quantidade mínima de avaliações anormais;
* threshold de Z-score.

A implementação inicial utilizará uma janela temporal explícita, evitando
EMA ou outros mecanismos adaptativos.

---

## 9. Estados do Sistema

O `task_system` é o único proprietário do estado da máquina.

Estados atuais:

```text
BOOT
  ↓
INIT
  ↓
WARMUP
  ↓
HEALTHY
  ↓
ALARM
```

### BOOT

Estado inicial após inicialização do dispositivo.

### INIT

Inicialização e validação dos recursos necessários para operação.

### WARMUP

Coleta das 600 avaliações utilizadas para construir o baseline.

Nenhum alarme é gerado durante o warm-up.

### HEALTHY

Baseline disponível e nenhuma condição anormal persistente detectada.

### ALARM

A análise estatística e temporal indica uma alteração persistente na condição
da máquina.

O `task_system` publica o estado e os dados relevantes para os consumidores.

---

## 10. Comunicação entre Componentes

| Mecanismo                  | Uso                                        |
| -------------------------- | ------------------------------------------ |
| `queue_accel_block_to_dsp` | bloco de 2048 amostras do eixo selecionado |
| `queue_dsp_to_system`      | resultados produzidos pelo DSP             |
| `queue_sensor_to_system`   | resultados dos sensores                    |
| `queue_system_to_hmi`      | estado e dados para HMI                    |
| `queue_system_to_storage`  | dados para armazenamento                   |
| `queue_system_to_dac`      | dados para saída analógica                 |

### Fluxo principal

```text
accelerometer
      │
      ▼
queue_accel_block_to_dsp
      │
      ▼
task_dsp
      │
      ▼
queue_dsp_to_system
      │
      ▼
task_system
      │
      ├──────► queue_system_to_hmi
      │
      ├──────► queue_system_to_storage
      │
      └──────► queue_system_to_dac
```

### Regra das queues

A queue entre `accelerometer` e `dsp_pipeline` transporta blocos de aquisição.

A queue entre `dsp_pipeline` e `task_system` transporta resultados que precisam
ser processados pelo sistema em sequência, principalmente durante o warm-up.

As queues de saída do `task_system` representam o resultado mais recente e podem
ser implementadas com tamanho 1 e `xQueueOverwrite()` quando apropriado.

---

## 11. Recursos Compartilhados

| Recurso                   | Dono            | Consumidores               | Sincronização           |
| ------------------------- | --------------- | -------------------------- | ----------------------- |
| SPI2                      | `main` (init)   | `accelerometer`, `storage` | `mutex_spi2`            |
| SPI3                      | `main` (futuro) | `hmi`                      | isolado                 |
| I2C                       | `main` (futuro) | `dac`                      | sem acesso do Core 0    |
| `queue_dsp_to_system`     | `dsp_pipeline`  | `system`                   | sequência de resultados |
| `queue_system_to_hmi`     | `system`        | `hmi`                      | queue de resultado      |
| `queue_system_to_storage` | `system`        | `storage`                  | queue de resultado      |
| `queue_system_to_dac`     | `system`        | `dac`                      | queue de resultado      |

Nenhuma task consumidora acessa diretamente os buffers privados de outro
componente.

---

## 12. Convenções de Código

|           |                                                               |
| --------- | ------------------------------------------------------------- |
| Linguagem | C                                                             |
| Framework | ESP-IDF                                                       |
| Estilo    | camelCase; `typedef` para structs; `static` para tudo privado |
| Colunas   | 100                                                           |
| Branch    | `feat_<feature>` / `bugfix_<algo>`                            |
| Commits   | conventional commits                                          |

Organização obrigatória de cada `.c`:

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

> Código legado (`app_context`, `accelerometer` atuais) usa snake_case.
> Migrar gradualmente para camelCase nos novos módulos; não é necessário
> reescrever o que já existe apenas por causa da convenção.

---

## 13. Limitações Conhecidas

* Apenas um eixo é processado no DSP; os três eixos continuam sendo adquiridos
* FFT real
* RPM é estimado a partir do pico espectral, sem sensor de RPM integrado
* Validação do RPM será realizada experimentalmente com referência externa
* Baseline não é persistido em flash/NVS
* Baseline é fixo após o warm-up
* O sistema inicialmente detecta mudança de condição, mas não classifica o tipo
  de falha
* O tamanho da janela temporal ainda precisa ser calibrado
* O threshold de Z-score ainda precisa ser calibrado
* O número mínimo de avaliações anormais dentro da janela ainda precisa ser
  calibrado
* Waveforms completas ainda não são armazenadas

---

## 14. Pendências Arquiteturais (TODO)

### Infraestrutura

* Registrar todas as tasks em `main.c`
* Implementar `app_context_init()`
* Criar as queues definitivas no `app_context`
* Definir prioridades e afinidade de Core das tasks

### DSP

* Consolidar `dsp_result_t`
* Definir transporte da magnitude FFT
* Definir saída temporal destinada ao DAC
* Validar frequência/RPM com sinal conhecido
* Validar frequência/RPM com motor real
* Validar amplitude do componente 1×RPM
* Fixar versão do ESP-DSP

### System

* Criar `system.h`
* Criar `system.c`
* Implementar `task_system`
* Implementar máquina de estados
* Implementar warm-up de 600 avaliações
* Implementar cálculo incremental do baseline
* Implementar média e desvio padrão por feature
* Implementar Z-score
* Implementar threshold
* Implementar evidência 2 de 3
* Implementar janela temporal
* Calibrar tamanho da janela
* Calibrar quantidade mínima de avaliações anormais
* Calibrar threshold

### Outputs

* Implementar HMI
* Implementar storage
* Implementar DAC
* Definir formato de dados distribuído pelo `task_system`

### Documentação

* Atualizar `docs/integration.md`
* Atualizar diagramas em `docs/diagrams/*`
* Remover referências antigas a `rpm_counter`, PCNT e Hall
* Documentar fluxo `accelerometer → dsp_pipeline → system → outputs`

---

## 15. Regras para IA

* Nunca alterar a arquitetura sem perguntar.
* Nunca criar componentes novos sem autorização.
* Nunca adicionar bibliotecas por conta própria.
* Nunca alterar a responsabilidade de um módulo sem discutir previamente.
* Sempre justificar decisões arquiteturais propostas.
* Sempre perguntar antes de mudanças estruturais.
* O `architecture.md` é a fonte de verdade do firmware e deve ser seguido
  estritamente.
* `task_dsp` processa sinais e retorna features; não toma decisões sobre o
  estado da máquina.
* `task_system` é o mestre do estado, baseline e decisão do sistema.
* HMI, storage e DAC não acessam diretamente hardware ou buffers privados de
  outros componentes.
* O baseline atual é construído durante 600 avaliações saudáveis e permanece
  fixo durante a execução.
* Não implementar EMA adaptativo sem decisão arquitetural explícita.
* Não substituir a análise estatística por algoritmos mais complexos sem
  evidência experimental que justifique a mudança.
* Parâmetros de threshold e persistência devem ser calibrados experimentalmente,
  não escolhidos arbitrariamente como valores definitivos.
