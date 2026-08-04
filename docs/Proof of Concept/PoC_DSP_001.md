# MachineGuard
## Caderno de Testes – PoC DSP #001
**Título:** Validação da Aquisição e Estatísticas no Domínio do Tempo

---

## Objetivo

Validar o pipeline inicial do DSP:

- Recepção de blocos de 2048 amostras
- Remoção do offset DC
- Cálculo de:
  - Mean
  - RMS
  - Minimum
  - Maximum
  - Peak-to-Peak

O objetivo desta etapa **não é detectar falhas**, mas validar que os indicadores respondem corretamente à vibração do motor.

---

## Configuração

| Item | Valor |
|------|-------|
| Data | |
| Firmware | feature_dsp |
| Commit | |
| ESP-IDF | |
| MCU | ESP32-S3 N16R8 |
| Sensor | LSM6DS3TR-C |
| Escala | ±2 g |
| ODR | 6.66 kHz |
| Eixo analisado | |
| Buffer | 2048 amostras |
| Fixação | Ímã de neodímio |
| Motor | |

---

# Teste 1 — Motor desligado

### Objetivo

Validar ruído de aquisição e estabilidade.

### Procedimento

- Motor desligado.
- Sensor fixado no motor.
- Aguardar estabilização.
- Registrar aproximadamente 10 blocos consecutivos.

### Resultados

| Bloco | Mean | RMS | Min | Max | Peak-to-Peak |
|-------:|-----:|----:|----:|----:|-------------:|
| 1 | | | | | |
| 2 | | | | | |
| 3 | | | | | |
| 4 | | | | | |
| 5 | | | | | |
| 6 | | | | | |
| 7 | | | | | |
| 8 | | | | | |
| 9 | | | | | |
| 10 | | | | | |

### Resultado esperado

- Mean aproximadamente constante.
- RMS baixo.
- Peak-to-Peak baixo.
- Pouca variação entre blocos.

### Resultado obtido

- [ ] Conforme esperado
- [ ] Divergente

Observações:

---

# Teste 2 — Motor ligado

### Objetivo

Validar resposta dos indicadores à vibração.

### Procedimento

- Ligar o motor.
- Não alterar a posição do acelerômetro.
- Registrar aproximadamente 10 blocos consecutivos.

### Resultados

| Bloco | Mean | RMS | Min | Max | Peak-to-Peak |
|-------:|-----:|----:|----:|----:|-------------:|
| 1 | | | | | |
| 2 | | | | | |
| 3 | | | | | |
| 4 | | | | | |
| 5 | | | | | |
| 6 | | | | | |
| 7 | | | | | |
| 8 | | | | | |
| 9 | | | | | |
| 10 | | | | | |

### Resultado esperado

Comparado ao Teste 1:

- Mean permanece aproximadamente constante.
- RMS aumenta.
- Peak-to-Peak aumenta.
- Minimum e Maximum apresentam maior amplitude.

### Resultado obtido

- [ ] Conforme esperado
- [ ] Divergente

Observações:

# Conclusão

## Critérios de aprovação

- [ ] Blocos recebidos continuamente.
- [ ] Mean estável entre blocos.
- [ ] RMS aumenta com o motor ligado.
- [ ] Peak-to-Peak aumenta com o motor ligado.
- [ ] Resultados repetíveis após novo acionamento.

---

## Conclusão Geral

- [ ] PoC aprovada
- [ ] PoC reprovada

Comentários finais:
