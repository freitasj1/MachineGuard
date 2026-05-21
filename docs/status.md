# MachineGuard — Status de Desenvolvimento

> Documento vivo. Atualizar a cada sessão de trabalho.  
> Estados: `[ ]` não iniciado · `[-]` em andamento · `[x]` concluído  
> Referência técnica: DCP v1.5 · Prazo: 25/09/2026 (FETIN)

---

## Responsáveis

| Vertente | Responsável |
|---|---|
| Firmware / DSP / Arquitetura | |
| Mecânica / Hardware físico / 3D | |
| Hardware / Documentação (IC) | |

---

## 1. Firmware

### 1.1 Infraestrutura

| Estado | Tarefa | Observação |
|---|---|---|
| `[x]` | Ambiente ESP-IDF v6.0.1 configurado | VS Code + clangd LLVM + compile_commands.json |
| `[x]` | Estrutura de diretórios do repositório | `firmware/main/`, `firmware/components/` |
| `[x]` | `CMakeLists.txt` raiz e `main/` | Funcionais |
| `[x]` | `sdkconfig.defaults` | Criado; revisar `CONFIG_RPM_FALLBACK` |
| `[x]` | `.clangd` configurado | `CompilationDatabase: build/` |
| `[x]` | Componente `app_context` | `app_context_t`, `dsp_result_t`, `app_context_init()` |
| `[x]` | `main.c` — esqueleto com criação de tasks | `extern` temporários; substituir por `#include` conforme componentes forem criados |
| `[ ]` | Atualizar `fft_magnitudes[1024]` → `[2048]` em `dsp_result_t` | Aguarda criação do `dsp_pipeline` |

### 1.2 Componente `Accelerometer`
| Estado | Tarefa | Observação |
|---|---|---|
| `[ ]` | Criar estrutura do componente (`CMakeLists.txt`, headers) | Pasta já existe em `components/accelerometer/` |
| `[ ]` | Driver SPI DMA — inicialização e configuração de ODR | `lis3dh_regs.h` interno; adaptar para registradores LSM6DS3 |
| `[ ]` | Leitura de buffer DMA — 2048 amostras → PSRAM | `heap_caps_malloc(MALLOC_CAP_SPIRAM)` |
| `[ ]` | Interrupt / trigger por timer de hardware (sem polling) | INT1 no GPIO 8 — reservado no mapa de pinos |
| `[ ]` | Teste unitário com leitura real do sensor | Verificar ODR, ruído de fundo, alinhamento de buffer |

### 1.3 Componente `dsp_pipeline`

| Estado | Tarefa | Observação |
|---|---|---|
| `[ ]` | Criar estrutura do componente | `dsp_fft.c`, `dsp_indicators.c`, `dsp_zscore.c` |
| `[ ]` | Janela de Hanning sobre 2048 amostras | Elimina vazamento espectral |
| `[ ]` | FFT 2048 pts via ESP-DSP SIMD | Buffers `static` + `__attribute__((aligned(16)))` obrigatório; assert de alinhamento no init |
| `[ ]` | Cálculo de Kurtosis | `K = E[(x-μ)⁴]/σ⁴` sobre amostras brutas; normal ~3, defeito >4–5 |
| `[ ]` | Cálculo de RMS | Raiz da média dos quadrados |
| `[ ]` | Cálculo de Fator de Crista | `CF = pico / RMS` |
| `[ ]` | Rastreamento bin 1xRPM | Frequência de rotação via `current_rpm` atômico |
| `[ ]` | EMA adaptativo por indicador | Alpha a calibrar empiricamente com motor real |
| `[ ]` | Z-score + votação 2 de 3 | Threshold 2,5σ; alerta quando kurtosis + RMS + bin 1xRPM simultaneamente |
| `[ ]` | Warm-up (~1800 amostras / ~3 min a 10 Hz) | Independente do alpha; LED azul durante warm-up |
| `[ ]` | Persistência de baseline em NVS flash | Recuperação automática no boot; componente `storage` é dependência |
| `[ ]` | Validação com sinal sintético (sem hardware) | Pré-requisito antes de qualquer teste com sensor real |

### 1.4 Componente `rpm_counter`

| Estado | Tarefa | Observação |
|---|---|---|
| `[ ]` | PCNT hardware — configuração e leitura a cada 100 ms | GPIO 6; zero interrupções de CPU |
| `[ ]` | Escrita em `_Atomic float current_rpm` | Core 1 → Core 0 |
| `[ ]` | `rpm_fallback.c` — detecção por pico FFT | Faixa 26–31 Hz; ativado via `CONFIG_RPM_FALLBACK` |

### 1.5 Componente `hmi`

| Estado | Tarefa | Observação |
|---|---|---|
| `[ ]` | Criar componente; configurar LovyanGFX em `display_lgfx.cpp` | Único arquivo C++ do projeto; interface `extern "C"` |
| `[ ]` | Sprite off-screen via PSRAM | Evita flickering; `heap_caps_malloc(MALLOC_CAP_SPIRAM)` |
| `[ ]` | Tela 1 — Dashboard | Kurtosis, RMS, bin 1xRPM, Z-score, RPM, Temp, Status, LED virtual |
| `[ ]` | Tela 2 — Espectro FFT | Barras 0–500 Hz; marcadores 1xRPM e 2xRPM |
| `[ ]` | Tela 3 — Forma de onda | 100 amostras brutas; modo RAW / FILTRADO |
| `[ ]` | Tela 4 — Histórico SD | Z-score max, contagem de alertas, temp, fonte, tempo, arquivo CSV |
| `[ ]` | Navegação por botões A/B | GPIO 15 (próx. tela) e GPIO 16 (pause/debug); pull-up interno |
| `[ ]` | LED RGB GPIO 38 | Verde=OK, Amarelo=atenção, Vermelho=alerta, Azul=warm-up |
| `[ ]` | Buzzer| Aviso sonoro para caso dê problema |

### 1.6 Componente `storage`

| Estado | Tarefa | Observação |
|---|---|---|
| `[ ]` | SD card — log CSV | Colunas: timestamp, kurtosis, RMS, CF, bin_1xRPM, z_score, rpm, temp, status |
| `[ ]` | NVS flash — persistência do baseline EMA | Leitura no boot; escrita periódica |
| `[ ]` | `mutex_spi2` — criação e ownership | `xSemaphoreCreateMutex()` com herança de prioridade; compartilhado com `lis3dh` |

### 1.7 Componente `sensors`

| Estado | Tarefa | Observação |
|---|---|---|
| `[ ]` | DS18B20 1-Wire — leitura periódica | GPIO 5; Core 1; temperatura de mancal |
| `[ ]` | ADC bateria — divisor resistivo | Baixa prioridade; aguarda decisão do subsistema de bateria |

### 1.8 Componente `dac_output`

| Estado | Tarefa | Observação |
|---|---|---|
| `[ ]` | MCP4725 I2C 12-bit — inicialização | GPIO 17 (SDA) / 18 (SCL); 400 kHz; exclusivo Core 1 |
| `[ ]` | Filtro FIR passa-banda centrado em 1xRPM | Aplicado no domínio DSP antes de escrever no DAC |
| `[ ]` | Saída analógica para osciloscópio via BNC | Motor saudável: amplitude baixa; desbalanceado: senoidal crescente |

### 1.9 Watchdog e Recovery

| Estado | Tarefa | Observação |
|---|---|---|
| `[ ]` | TWDT habilitado no Core 0 | Timeout acima do tempo máximo de um ciclo DSP |
| `[ ]` | `esp_task_wdt_add(NULL)` + `esp_task_wdt_reset()` em cada task | Convenção estabelecida; aplicar em todas as tasks |
| `[ ]` | Verificar stack sizes com `uxTaskGetStackHighWaterMark` | Após testes reais; valores atuais são estimativas |

---

## 2. Hardware

### 2.1 BOM — Adquirido

| Estado | Item | Observação |
|---|---|---|
| `[x]` | LSM6DS3TR | Acelerômetro principal; SPI DMA 5 kHz |
| `[x]` | LIS3DH | Reserva / testes comparativos |
| `[x]` | Kit cabos JST 2.54mm 6 vias | Conexões SPI |
| `[x]` | BNC fêmea | Saída osciloscópio — DAC |
| `[x]` | Módulo SD card SPI 3.3V | Compartilha SPI2 com acelerômetro |
| `[x]` | Sensor Hall breakout (A3144) | GPIO 6 / PCNT; 1 PPR |
| `[x]` | TP4056 | Carregador 18650 |
| `[x]` | Suporte 18650 duplo (1S2P) | |
| `[x]` | Células 18650 genéricas | |
| `[x]` | Cabos Dupont | Testes em protoboard |
| `[x]` | Protoboard | |

### 2.2 BOM — Carrinho Silício (pendente chegada)

| Estado | Item | Observação |
|---|---|---|
| `[ ]` | Display TFT | Tamanho a definir (>2.4"); ILI9341 SPI3 exclusivo |
| `[ ]` | MCP4725 | DAC I2C 12-bit; saída para osciloscópio |
| `[ ]` | DS18B20 impermeável | 1-Wire; temperatura de mancal |
| `[ ]` | Ímã escareado | Fixação do acelerômetro no protótipo |
| `[ ]` | Ímã pequeno 6×3mm | Neodímio; acoplamento jaw coupling → sensor Hall |

### 2.3 BOM — Pendente de decisão / compra

| Estado | Item | Observação |
|---|---|---|
| `[ ]` | MT3608 boost 5V | Baixa prioridade; subsistema bateria |
| `[ ]` | Resistores pull-up I2C (4.7kΩ) | Para MCP4725 a 400 kHz; podem ir direto na PCB |
| `[ ]` | Capacitores de desacoplamento (100nF + 10µF) | Perto do LSM6DS3 e MCP4725 |
| `[ ]` | LED RGB 5mm externo | Decidir: usar só GPIO38 da placa ou LED externo no enclosure |
| `[ ]` | Cabo BNC para osciloscópio do lab | Verificar disponibilidade no INATEL |

### 2.4 PCB

| Estado | Tarefa | Observação |
|---|---|---|
| `[ ]` | Esquemático  | Depende de decisões finais de BOM (LED, bateria) |
| `[ ]` | Layout 2 camadas |  |
| `[ ]` |  |  |

---

## 3. Mecânica

| Estado | Tarefa | Responsável | Observação |
|---|---|---|---|
| `[ ]` | Acoplamento 3D — furo 19mm + chaveta 6×6mm |  | Compatível com eixo do Equacional EA2-80-B3/4 |
| `[ ]` | Disco de falha PLA ø100mm — furos M6 a 35–40mm |  | ~10g de massa excêntrica; reversível |
| `[ ]` | Alojamento ímã 6×3mm no acoplamento |  | Para sensor Hall; posicionamento repetível |
| `[ ]` | Enclosure final — impressão 3D |  | Abrigar ESP32-S3, display, PCB, LEDs, botões |
| `[ ]` | Fixação do acelerômetro na carcaça do motor (versão FETIN) |  | Parafuso M2 ou abraçadeira metálica; furação/rosqueamento na carcaça |
| `[ ]` | Validação do acoplamento com VFD do INATEL |  | Confirmar compatibilidade jaw coupling lado VFD |

---

## 4. Documentação

| Estado | Tarefa | Observação |
|---|---|---|
| `[-]` | DCP v1.5 | Documento interno de referência; atualizar conforme decisões técnicas |
| `[-]` | `docs/pin_map.md` | Existente; manter sincronizado com mapa de pinos do DCP |
| `[ ]` | `docs/STATUS.md` | Este documento |
| `[ ]` | `docs/architecture.md` | Decisões de arquitetura firmware — inter-core, componentes, convenções |
| `[ ]` | `docs/dsp_pipeline.md` | Especificação técnica do pipeline — fórmulas, parâmetros, justificativas |
| `[ ]` | Diagrama de hardware atualizado | Refletir LSM6DS3 (substituiu LIS3DH) e motor trifásico Equacional |
| `[ ]` | Diagrama de firmware atualizado | Refletir FFT 2048 pts |
| `[ ]` | README.md — seção Documentação com links para `docs/` | Linkar pin_map, STATUS, architecture, dsp_pipeline |
| `[ ]` | Script Python pós-processamento CSV | Análise offline dos logs do SD para calibração do alpha EMA |

---

## 5. Tarefas Multi-vertente

| Estado | Tarefa | Vertentes | Observação |
|---|---|---|---|
| `[ ]` | Validação do pipeline DSP com sinal sintético | Firmware | Pré-requisito para qualquer teste com hardware; sem dependência de sensor físico |
| `[ ]` | Primeiro teste integrado LSM6DS3 + pipeline DSP | Firmware + Hardware | Requer acelerômetro conectado via SPI; verificar ODR, buffer DMA, FFT com vibração real |
| `[ ]` | Calibração do alpha EMA com motor real | Firmware + Hardware + Mecânica | Motor + acoplamento + acelerômetro + pipeline todos funcionais; script Python recomendado |
| `[ ]` | Roteiro de demonstração completo (4 estados) | Firmware + Hardware + Mecânica | Saudável → injeção de falha → variação RPM → recuperação; tudo integrado |
| `[ ]` | Fixação final do acelerômetro + validação de repetibilidade | Hardware + Mecânica | Ímã escareado (protótipo) ou parafuso M2 (FETIN); medir variação de leitura entre fixações |
| `[ ]` | Revisão final antes do pedido da PCB | Firmware + Hardware | GPIO map confirmado, todos os componentes testados em protoboard |
