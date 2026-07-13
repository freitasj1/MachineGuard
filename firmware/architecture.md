# MachineGuard — architecture.md

## 1. Objetivo

Documentar **como** o MachineGuard está organizado: estrutura do firmware,
comunicação entre componentes, regras de implementação e decisões
arquiteturais que não podem ser quebradas.

---

## 2. Visão Geral do Projeto

| | |
|---|---|
| Projeto | MachineGuard |
| Objetivo | Manutenção preditiva para motores rotativos via análise de vibração |
| MCU | ESP32-S3 N16R8 (dual-core LX7, 16 MB flash, 8 MB PSRAM OPI) |
| Processamento | 100% local (edge), sem nuvem/gateway |
| Framework | ESP-IDF |
| Prazo | FETIN — 25/09/2026 |

---

## 3. Filosofia do Projeto

- Arquitetura limpa e modular
- Baixo acoplamento entre componentes
- Determinismo (Core 0 nunca faz I/O)
- Simplicidade sobre generalidade
- ESP-IDF puro (sem Arduino framework)

---

## 4. Arquitetura Geral

`main.c` é responsável **apenas** por:

1. Inicializar infraestrutura global (barramentos, `app_context`)
2. Criar tasks

Depois disso, `main` não executa mais lógica nenhuma.

**Infraestrutura compartilhada, inicializada no `main`:**

| Recurso | Status |
|---|---|
| SPI2 | implementado |
| SPI3 | futuro |
| I2C | futuro |
| mutexes / queues do `app_context` | pendente (`app_context_init` é stub) |

Cada componente inicializa apenas **seu próprio dispositivo** sobre a
infraestrutura já criada — nunca o barramento em si.

```
main            → inicializa SPI2
accelerometer   → adiciona LSM6DS3TR-C ao barramento SPI2
storage         → adiciona SD Card ao barramento SPI2
```

### Core ownership

| Core | Responsabilidade |
|---|---|
| Core 0 | Aquisição + DSP. Nunca faz I/O. |
| Core 1 | HMI, storage, sensors, DAC. |

---

## 5. Fluxo de Dados

```
LSM6DS3TR-C
   │  SPI + DMA
   ▼
 FIFO (sensor)
   │
   ▼
 DMA
   │
   ▼
 Ping-Pong Buffer  (accelerometer)
   │  Queue (1 eixo selecionado, bloco de 2048 amostras)
   ▼
 DSP Pipeline (dsp_pipeline)
   │  dsp_result_t → queue_dsp_result
   ▼
 app_context
   │
   ├──▶ hmi       (leitura)
   ├──▶ storage    (leitura)
   └──▶ dac         (leitura)
```

Regra: **um dado tem um único dono/escritor**. Consumidores só leem.

---

## 6. Organização dos Componentes

Componentes atuais: `main`, `app_context`, `accelerometer`, `dsp_pipeline`,
`hmi`, `storage`, `sensors`, `dac`.

> `rpm_counter` foi **removido** do projeto. RPM é estimado pelo bin 1xRPM
> da FFT — não existe mais sensor Hall, PCNT ou medição de RPM em hardware.

### main

| | |
|---|---|
| Status | implementado (esqueleto) |
| Responsabilidade | init de infraestrutura, criação de tasks |
| Dependências | todos os componentes |
| Interfaces públicas | — (`app_main`) |
| TODO | registrar `task_accel` via `xTaskCreatePinnedToCore` (falta hoje) |

### app_context

| | |
|---|---|
| Status | header definido, `app_context_init()` é stub |
| Responsabilidade | contexto único compartilhado entre tasks |
| Dependências | nenhuma |
| Interfaces públicas | `app_context_init(app_context_t *ctx)` |
| Regra | um dado → um dono. Não guardar buffers privados de componente dentro do contexto (buffers privados vivem dentro do próprio componente) |
| TODO | implementar criação real de queues/mutexes |

### accelerometer

| | |
|---|---|
| Status | em desenvolvimento (`feat_accelerometer`) |
| Responsabilidade | configurar LSM6DS3TR-C, SPI, DMA, FIFO, ping-pong, seleção de eixo, envio de bloco para o DSP |
| NÃO faz | FFT, RMS, kurtosis, HMI, storage |
| Dependências | SPI2 (criado pelo `main`), `mutex_spi2` |
| Interfaces públicas | `accel_init(app_context_t *ctx)`, `task_accel(void *arg)` |
| Fluxo | ver seção 5 |
| Regra | driver **não inicializa barramento** — só usa a infra já criada pelo `main` |
| TODO | driver real (hoje é stub); definir eixo processado; criar queue de saída para o DSP |

### dsp_pipeline

| | |
|---|---|
| Status | não implementado |
| Responsabilidade | pipeline de indicadores + decisão de estado (seção 7) |
| Dependências | queue de blocos do `accelerometer` |
| Interfaces públicas | `task_dsp(void *arg)` |
| TODO | tudo (ver seção 7) |

### hmi

| | |
|---|---|
| Status | não implementado |
| Responsabilidade | exibir RMS, kurtosis, crest factor, amplitude 1xRPM, estado atual; tela de FFT (faixa útil, ex. 0–300 Hz) |
| NÃO faz | acessar acelerômetro ou SPI diretamente |
| Dependências | `app_context` (somente leitura) |
| TODO | tudo |

### storage

| | |
|---|---|
| Status | não implementado (exceto ownership do `mutex_spi2`) |
| Responsabilidade | salvar indicadores (SD/NVS) |
| Limitação atual | waveforms completas não são salvas ainda |
| Dependências | `mutex_spi2` (compartilhado com `accelerometer`) |
| TODO | tudo |

### sensors

| | |
|---|---|
| Status | não implementado |
| Responsabilidade | DS18B20 (temperatura), leitura de bateria |
| TODO | tudo |

### dac

| | |
|---|---|
| Status | não implementado |
| Responsabilidade | saída analógica (MCP4725) para osciloscópio |
| TODO | tudo |

---

## 7. Pipeline DSP

```
Seleciona eixo
   │
   ▼
2048 amostras
   │
   ├─▶ RMS               (sinal bruto)
   ├─▶ Kurtosis           (sinal bruto)
   └─▶ Crest Factor        (sinal bruto)
   │
   ▼
Janela Hann
   │
   ▼
FFT Real (ESP-DSP)
   │
   ▼
Magnitude Linear
   │
   ▼
Busca de pico na faixa configurada (ex.: 900–3600 RPM)
   │
   ▼
Amplitude do bin 1xRPM
   │
   ▼
EMA ──▶ Z-score ──▶ Threshold ──▶ Votação ──▶ Estado da máquina
```

RMS, Kurtosis e Crest Factor usam o sinal bruto. FFT é usada **apenas**
para análise espectral (localização e amplitude do bin 1xRPM).

### Warm-up (~3 min)

```
executa pipeline completo → calcula média/desvio → armazena baseline
```

Após o warm-up, o mesmo pipeline roda e compara contra o baseline:

```
pipeline → comparação com baseline → Z-score → threshold → votação → estado
```

### Estados

```
BOOT → INIT → WARMUP → HEALTHY → ALARM
```

Futuro: `UNBALANCE`, `MISALIGNMENT`, outras classificações de falha
(hoje o sistema detecta mudança de condição, mas não classifica o tipo).

---

## 8. Comunicação entre Componentes

| Mecanismo | Uso |
|---|---|
| Queue (accelerometer → dsp_pipeline) | bloco de 2048 amostras do eixo selecionado |
| Queue (`queue_dsp_result`, dsp_pipeline → app_context) | `dsp_result_t`, tamanho 1, overwrite |
| `app_context` | ponto único de leitura para `hmi`, `storage`, `dac` |

Regra: **produtor escreve, consumidores leem**. Nenhum componente
consumidor acessa hardware ou buffers privados de outro componente.

---

## 9. Recursos Compartilhados

| Recurso | Dono | Consumidores | Sincronização |
|---|---|---|---|
| SPI2 | `main` (init) | `accelerometer`, `storage` | `mutex_spi2` (herança de prioridade) |
| SPI3 | `main` (futuro) | `hmi` | isolado, sem contenção |
| I2C | `main` (futuro) | `dac` | 400 kHz, sem acesso do Core 0 |
| `queue_dsp_result` | `dsp_pipeline` | `hmi`, `storage`, `dac` | queue tamanho 1, overwrite |

---

## 10. Convenções de Código

| | |
|---|---|
| Linguagem | C |
| Framework | ESP-IDF |
| Estilo | camelCase; `typedef` para structs; `static` para tudo privado |
| Colunas | 100 |
| Branch | `feat_<feature>` / `bugfix_<algo>` |
| Commits | conventional commits |

Organização obrigatória de cada `.c`:

```
Includes
Private constants
Private types
Public variables
Private variables
Private prototypes
Public implementations
Private implementations
```

> Nota: código legado (`app_context`, `accelerometer` atuais) usa
> snake_case. Migrar gradualmente para camelCase nos novos módulos;
> não é necessário reescrever o que já existe só por causa da convenção.

---

## 11. Limitações Conhecidas

- Apenas um eixo é processado (todos os 3 continuam sendo adquiridos)
- FFT real (não complexa)
- Apenas indicadores são armazenados (sem waveform completa)
- HMI mostra apenas a faixa útil do espectro (ex. 0–300 Hz)
- Sistema detecta mudança de condição, mas não classifica o tipo de falha

---

## 12. Pendências Arquiteturais (TODO)

- Registrar `task_accel` em `main.c`
- Implementar `app_context_init()` (queues/mutexes reais)
- Implementar driver real do LSM6DS3TR-C (FIFO, DMA, ping-pong, seleção de eixo)
- Definir queue de saída `accelerometer → dsp_pipeline`
- Implementar `dsp_pipeline` por completo
- Fixar versão do ESP-DSP
- Calibrar alpha do EMA com motor real (depende de driver + DSP prontos)
- Atualizar `docs/integration.md` e diagramas (`docs/diagrams/*`) removendo `rpm_counter`/PCNT/Hall

---

## 13. Regras para IA

- Nunca alterar a arquitetura sem perguntar.
- Nunca criar componentes novos sem autorização.
- Nunca adicionar bibliotecas por conta própria.
- Nunca alterar a responsabilidade de um módulo.
- Sempre justificar decisões arquiteturais propostas.
- Sempre perguntar antes de mudanças estruturais.
- Este documento é a fonte de verdade do projeto — segui-lo estritamente.
