# Evidence ledger

Source-to-requirement and requirement-to-test traceability for the AK4522 DAC emulator. AGENTS.md states the intended behavior; this file records where each hardware fact comes from, how confident the project is in it, and which test will check it.

## Source manifest

| Key  | Document                                                       | Pages | SHA-256                                                            | Location                        |
| ---- | -------------------------------------------------------------- | ----: | ------------------------------------------------------------------ | ------------------------------- |
| [DS] | Asahi Kasei AK4522, 20Bit Stereo ADC & DAC, M0020-E-01, 1998/10 |    18 | `0db2cd2fe7a989570f01610ea13249576371f7d5fff84864d8a95f7014b0adec` | `reference/AK4522.PDF` (local) |

Printed page numbers match PDF page numbers. The PDF is not committed; see [reference/README.md](../reference/README.md).

**Verification record.** Every numeric hardware constant in AGENTS.md sections 4 to 10 was checked against [DS] on 2026-10-15. All values match except the items listed under [Discrepancies](#discrepancies). Entries below that are not in AGENTS.md are marked *new*.

**Owner spot-check, 2026-10-16.** The project owner checked 14 items against the PDF: the discrepancies, serial modes and Fig.4 framing, DEM and CMODE mappings, mute, reset, DAC filter values, filter test conditions, and serial timing. Two corrections resulted: DISC-04 was withdrawn (the value is in the typ column), and DISC-01 now quotes the p.16 wording exactly. Table column placement was then confirmed from PDF text coordinates (`pdftotext -bbox`) rather than the page image.

## Classification

| Class | Meaning                                                                        |
| ----- | ------------------------------------------------------------------------------ |
| D     | Documented in [DS], with page, table, figure, or note                          |
| I     | Inferred from documented facts; derivation given                               |
| P     | Project policy chosen for deterministic software behavior; see `assumptions.md` |
| M     | Measured; requires capture ID, conditions, calibration, and uncertainty        |

Confidence: **high** (explicit text or table value), **medium** (figure reading or single-source statement), **low** (ambiguous or conflicting).

Test column names the Catch2 tag or CTest label that checks the fact. Status values: *planned*, *implemented*, *passing*, *failing*, *deviation*, *not modeled*, *informational*. *passing* means the linked test exists and passes where [features.md](features.md) records its availability. *not modeled* means the fact is recorded but deliberately has no software counterpart: it needs hardware data, describes a part of the chip outside the model, or would invent behavior. *informational* marks context with nothing to implement.

## 1. Structure and operating conditions

| ID   | Fact                                                                              | Class | Source                  | Conf.  | Requirement     | Test        | Status  |
| ---- | --------------------------------------------------------------------------------- | ----- | ----------------------- | ------ | --------------- | ----------- | ------- |
| D-01 | Stereo 20-bit delta-sigma DAC, 128x oversampling                                  | D     | p.1 Features            | high   | REQ-SCOPE-01    | `[pcm]` | passing (20-bit stereo); 128x modulator not modeled (equivalent baseband, REQ-MOD-01) |
| D-02 | 8x interpolator precedes each DAC delta-sigma modulator                           | D     | p.1 block diagram       | high   | REQ-FILTER-01   | `[filter]`  | passing |
| D-03 | DAC analog filtering is 2nd-order SCF plus 2nd-order CTF                          | D     | p.1 Features; p.16 s.4  | high   | REQ-FILTER-02   | `[filter]`  | passing |
| D-04 | DAC sample rate 16 to 48 kHz; LRCK typical 44.1 kHz                               | D     | p.1; p.7 LRCK fsn       | high   | REQ-CLOCK-01    | `[clock]`   | passing |
| D-05 | MCLK ratio 256fs, 384fs, or 512fs                                                 | D     | p.1; p.3 pin 21; p.9    | high   | REQ-CLOCK-01    | `[clock]`   | passing |
| D-06 | VA 4.5 to 5.5 V; VD 2.7 V min, 5.0 V typ, VA max                                  | D     | p.4 recommended         | high   | REQ-ANALOG-01   | n/a         | not modeled (no supply model) |
| D-07 | DAC outputs single-ended, centered on VCOM; VCOM = VA/2                           | D     | p.3 pin 24; p.16 s.4    | high   | REQ-ANALOG-01   | `[analog]`  | passing |
| D-08 | *new* VREFH sets the output range; normally connected to VA                       | D     | p.3 pin 1; p.15 s.2     | high   | REQ-ANALOG-01   | `[analog]`  | passing |
| D-09 | *new* SCF and CTF remove most modulator noise beyond the audio passband           | D     | p.16 s.4                | high   | REQ-MOD-01      | n/a         | not modeled (no modulator noise to remove; REQ-MOD-01) |
| D-10 | AK4522VF: 24-pin VSOP, rated -10 to +70 degrees C                                 | D     | p.2 ordering guide      | high   | n/a             | n/a         | informational |

## 2. DAC digital filter and combined response

Conditions: Ta = 25 degrees C, VA 4.5 to 5.5 V, VD 2.7 to 5.5 V, fs = 44.1 kHz, DEM0 = 1, DEM1 = 0 (de-emphasis OFF) [DS p.6].

| ID    | Fact                                                         | Value              | Class | Source              | Conf.  | Requirement    | Test                 | Status  |
| ----- | ------------------------------------------------------------ | ------------------ | ----- | ------------------- | ------ | -------------- | -------------------- | ------- |
| D-F01 | Passband edge at -0.06 dB (max column)                       | 20.0 kHz           | D     | p.6 table           | high   | REQ-FILTER-01  | `[filter]`           | passing |
| D-F02 | -6.0 dB point (max column)                                   | 22.05 kHz          | D     | p.6 table           | high   | REQ-FILTER-01  | `[filter]`           | passing |
| D-F03 | Stopband begins (min)                                        | 24.1 kHz           | D     | p.6 table           | high   | REQ-FILTER-01  | `[filter]`           | passing |
| D-F04 | Passband ripple (max)                                        | +/-0.06 dB         | D     | p.6 table           | high   | REQ-FILTER-01  | `[filter]`           | passing |
| D-F05 | Stopband attenuation (min)                                   | 43 dB              | D     | p.6 table           | high   | REQ-FILTER-01  | `[filter]`           | passing |
| D-F06 | Group delay (typ)                                            | 14.7 / fs          | D     | p.6 table, note 11  | high   | REQ-LAT-02     | `[filter][latency]`  | passing |
| D-F07 | Digital filter plus analog filter, 0 to 20.0 kHz (typ)       | +/-0.2 dB          | D     | p.6 table           | high   | REQ-FILTER-02  | `[filter]`           | passing |
| D-F08 | Passband and stopband frequencies scale with fs              | n/a                | D     | p.6 note 10         | high   | REQ-FILTER-01  | `[filter]`           | passing |
| D-F09 | Response reference frequency, under the table's fs = 44.1 kHz conditions | 1 kHz | D | p.6 note 10 | high | REQ-FILTER-01 | `[filter]` | passing |
| D-F10 | DAC delay runs from loading both channels' 20-bit input register to analog output | n/a | D | p.6 note 11 | medium | REQ-LAT-02 | `[latency]` | passing |

### Inferred filter values

| ID    | Value                                                          | Derivation                                         | Class |
| ----- | -------------------------------------------------------------- | -------------------------------------------------- | ----- |
| I-F01 | Passband edge `20/44.1 * fs`; 21.7687 kHz at 48 kHz             | D-F01 scaled by D-F08                              | I     |
| I-F02 | -6 dB point `0.5 * fs`; 24.0 kHz at 48 kHz                      | 22.05/44.1 = 0.5 exactly, scaled by D-F08          | I     |
| I-F03 | Stopband edge `24.1/44.1 * fs`; 26.2313 kHz at 48 kHz           | D-F03 scaled by D-F08                              | I     |
| I-F04 | Stopband edge lies above base-rate Nyquist; verify at 8x rate  | 24.1/44.1 = 0.5465 > 0.5                           | I     |
| I-F05 | Group delay 333.333 us at 44.1 kHz; 306.25 us at 48 kHz        | 14.7 / fs                                          | I     |
| I-F06 | Combined response at rates other than 44.1 kHz is unspecified  | p.6 conditions fix fs = 44.1 kHz                   | I     |
| I-F07 | *withdrawn* Response reference scaling with fs (1000 x fs / 44100) | Withdrawn by the project owner on 2026-10-16. [DS] p.6 note 10 states the reference is 1 kHz, and the filter table's conditions fix fs = 44.1 kHz. The datasheet values are therefore checked at 44.1 kHz against 1 kHz (D-F09); at other rates the datasheet makes no ripple or edge claim, and measurements there are informational | I |

## 3. DAC analog characteristics

Conditions: Ta = 25 degrees C, VA = VD = 5 V, VREFH = VA, fs = 44.1 kHz, SCLK = 64fs, 1 kHz signal, 20-bit data, measurement band 10 Hz to 20 kHz [DS p.5].

| ID    | Quantity                                 | Min      | Typ             | Max      | Class | Source        | Conf. | Requirement    | Test       | Status  |
| ----- | ---------------------------------------- | -------- | --------------- | -------- | ----- | ------------- | ----- | -------------- | ---------- | ------- |
| D-A01 | S/(N+D)                                  | 80 dB    | 90 dB           |          | D     | p.5           | high  | REQ-DIST-01    | `[analog]` | passing |
| D-A02 | Dynamic range, -60 dB output, A-weighted | 95 dB    | 100 dB          |          | D     | p.5, note 5   | high  | REQ-NOISE-01   | `[analog]` | passing |
| D-A03 | S/N, A-weighted                          | 95 dB    | 100 dB          |          | D     | p.5, notes 6,9 | high | REQ-NOISE-01   | `[analog]` | passing |
| D-A04 | Interchannel isolation                   | 90 dB    | 110 dB          |          | D     | p.5           | high  | REQ-CHANNEL-01 | `[analog]` | passing |
| D-A05 | Interchannel gain mismatch               |          | 0.2 dB          | 0.5 dB   | D     | p.5           | high  | REQ-CHANNEL-01 | `[analog]` | passing |
| D-A06 | Gain drift                               |          | 20 ppm/degree C |          | D     | p.5           | high  | n/a (not modeled) | n/a     | not modeled |
| D-A07 | Output voltage (labeled AOUT = 0.6 x VREFH) | 2.65 Vpp | 2.9 Vpp      | 3.15 Vpp | D     | p.5           | high  | REQ-ANALOG-01  | `[analog]` | passing |
| D-A08 | Load resistance                          | 5 kohm   |                 |          | D     | p.5           | high  | n/a (not modeled) | n/a     | not modeled |
| D-A09 | Load capacitance                         |          |                 | 25 pF    | D     | p.5           | high  | n/a (not modeled) | n/a     | not modeled |
| D-A10 | Power-supply rejection                   |          | 50 dB           |          | D     | p.5, note 8   | high  | n/a (not modeled) | n/a     | not modeled |
| D-A11 | 16-bit DAC dynamic range                 |          | 98 dB           |          | D     | p.5 note 5    | high  | REQ-NOISE-01   | `[analog]` | passing |
| D-A12 | Zero-input S/N, any resolution           |          | 100 dB          |          | D     | p.5 note 9    | high  | REQ-NOISE-01   | `[analog]` | passing |
| D-A13 | S/N, CCIR-ARM weighted, each converter   |          | 96 dB           |          | D     | p.5 note 6    | high  | REQ-NOISE-01   | `[analog]` | deviation |
| D-A14 | PSR stimulus: 1 kHz, 50 mVpp on VA and VD; VREFH held constant | |     |          | D     | p.5 note 8    | high  | n/a            | n/a        | not modeled |
| D-A15 | Physical DC offset at outputs: "a few mV"; removed by AC coupling |   |    |          | D     | p.16 s.4      | medium | REQ-ANALOG-02 | `[analog]` | passing |
| D-A16 | *new* Output noise level while soft-muted is shown as -100 dB | |       |          | D     | p.11 Fig.5    | medium | REQ-NOISE-02  | `[mute][analog]` | passing |

D-A16 is a figure annotation, not a table value. It agrees with D-A12 and supports keeping analog noise audible after digital mute.

## 4. PCM data mapping

| ID    | Fact                                                                 | Class | Source               | Conf. | Requirement | Test    | Status  |
| ----- | -------------------------------------------------------------------- | ----- | -------------------- | ----- | ----------- | ------- | ------- |
| D-P01 | DAC input is two's complement, MSB first, in all modes               | D     | p.9; p.16 s.4        | high  | REQ-PCM-01  | `[pcm]` | passing |
| D-P02 | 7FFFFh is positive full scale; 80000h is negative full scale (20-bit) | D    | p.16 s.4             | high  | REQ-PCM-01  | `[pcm]` | passing |
| D-P03 | 00000h ideally produces VCOM at the output                           | D     | p.16 s.4             | high  | REQ-PCM-01  | `[pcm]` | passing |
| I-P01 | Normalized value `code / 524288.0`; +FS is `1 - 2^-19`               | I     | D-P02 with 20-bit range | high | REQ-PCM-01 | `[pcm]` | passing |
| I-P02 | 16-bit words map to 20-bit scale by multiplication by 16             | I     | Mode 0 and 32fs I2S carry the top 16 bits; MSB weight is preserved | high | REQ-PCM-02 | `[pcm]` | passing |
| I-P03 | 24-bit words keep their top 20 bits; the low 4 bits are dropped (floor), not rounded | I | The serial interface takes 20 bits MSB first and ignores the bits after them (D-S08, D-S11; p.9) | high | REQ-API-01 | `[stream]` | passing |

## 5. Serial interface

| ID    | Fact                                                                    | Class | Source                 | Conf.  | Requirement     | Test       | Status  |
| ----- | ----------------------------------------------------------------------- | ----- | ---------------------- | ------ | --------------- | ---------- | ------- |
| D-S01 | Mode 0 (DIF1=0, DIF0=0): 16-bit LSB justified, LRCK H = left, SCLK >= 32fs | D  | p.9 Table 2, Fig.1     | high   | REQ-SERIAL-01   | `[serial]` | passing |
| D-S02 | Mode 1 (0,1): 20-bit LSB justified, LRCK H = left, SCLK >= 40fs          | D     | p.9 Table 2; p.10 Fig.2 | high  | REQ-SERIAL-01   | `[serial]` | passing |
| D-S03 | Mode 2 (1,0): 20-bit MSB justified, LRCK H = left, SCLK >= 40fs          | D     | p.9 Table 2; p.10 Fig.3 | high  | REQ-SERIAL-01   | `[serial]` | passing |
| D-S04 | Mode 3 (1,1): I2S, LRCK L = left, SCLK >= 32fs or 40fs                   | D     | p.9 Table 2; p.10 Fig.4 | high  | REQ-SERIAL-01   | `[serial]` | passing |
| D-S05 | SDTI latched on SCLK rising edge                                         | D     | p.9                    | high   | REQ-SERIAL-01   | `[serial]` | passing |
| D-S06 | Mode 3 at 32fs truncates the low bits                                    | D     | p.9                    | high   | REQ-SERIAL-01   | `[serial]` | passing |
| D-S07 | Mode 3 at 32fs carries bits 19 to 4; bit 4 occupies the first SCLK slot after the LRCK edge | D | p.10 Fig.4 | medium | REQ-SERIAL-01 | `[serial]` | passing |
| D-S08 | Mode 3 at 64fs: bit 19 in the slot after the LRCK edge (slot 1), bit 0 in slot 20, then don't care | D | p.10 Fig.4 | medium | REQ-SERIAL-01   | `[serial]` | passing |
| D-S09 | Mode 0 at 32fs: bits 15 to 0 fill the channel; at 64fs, don't care then bits 15 to 0 | D | p.9 Fig.1   | medium | REQ-SERIAL-01   | `[serial]` | passing |
| D-S10 | Mode 1 at 64fs: don't care, then bits 19 to 0 ending at the channel boundary | D  | p.10 Fig.2             | medium | REQ-SERIAL-01   | `[serial]` | passing |
| D-S11 | Mode 2 at 64fs: bits 19 to 0 from the channel boundary, then don't care  | D     | p.10 Fig.3             | medium | REQ-SERIAL-01   | `[serial]` | passing |
| D-S12 | SDTO (ADC output) changes on SCLK falling edge; produced by `SerialEncoder` (M7) | D     | p.9                    | high   | REQ-SCOPE-01    | `[sdto]` | passing |

### Serial timing (slave mode)

| ID    | Parameter                              | Limit      | Class | Source          | Conf. | Requirement    | Test       | Status  |
| ----- | -------------------------------------- | ---------- | ----- | --------------- | ----- | -------------- | ---------- | ------- |
| D-T01 | SCLK period tSCK                       | >= 160 ns  | D     | p.7             | high  | REQ-SERIAL-02  | `[serial]` | passing |
| D-T02 | SCLK low and high widths tSCKL, tSCKH  | >= 65 ns   | D     | p.7             | high  | REQ-SERIAL-02  | `[serial]` | passing |
| D-T03 | LRCK edge to SCLK rise tLRS            | >= 45 ns   | D     | p.7, note 13    | high  | REQ-SERIAL-02  | `[serial]` | passing |
| D-T04 | SCLK rise to LRCK edge tSLR            | >= 45 ns   | D     | p.7, note 13    | high  | REQ-SERIAL-02  | `[serial]` | passing |
| D-T05 | SDTI hold tSDH                         | >= 40 ns   | D     | p.7             | high  | REQ-SERIAL-02  | `[serial]` | passing |
| D-T06 | SDTI setup tSDS                        | >= 25 ns   | D     | p.7             | high  | REQ-SERIAL-02  | `[serial]` | passing |
| D-T07 | LRCK duty cycle                        | 45 to 55 % | D     | p.7             | high  | REQ-SERIAL-02  | `[serial]` | passing |
| D-T08 | *new* SCLK rising edge must not coincide with an LRCK edge | n/a | D | p.7 note 13 | high  | REQ-SERIAL-02  | `[serial]` | passing |
| I-T01 | *new* Maximum SCLK frequency; 128fs is valid at every supported rate | <= 6.25 MHz (130fs at 48 kHz) | I | 1 / 160 ns; 128 x 48 kHz = 6.144 MHz | high | REQ-SERIAL-02 | `[serial]` | passing |

## 6. Clocks

| ID    | Fact                                                                    | Class | Source            | Conf. | Requirement   | Test      | Status  |
| ----- | ----------------------------------------------------------------------- | ----- | ----------------- | ----- | ------------- | --------- | ------- |
| D-C01 | CMODE L = 256fs, H = 384fs, not connected = 512fs (internally biased)    | D     | p.3 pin 21; p.9   | high  | REQ-CLOCK-01  | `[clock]` | passing |
| D-C02 | MCLK 256fs: 4.096 to 12.288 MHz; low/high width >= 27 ns                 | D     | p.7               | high  | REQ-CLOCK-01  | `[clock]` | passing |
| D-C03 | MCLK 384fs: 6.144 to 18.432 MHz; low/high width >= 20 ns                 | D     | p.7               | high  | REQ-CLOCK-01  | `[clock]` | passing |
| D-C04 | MCLK 512fs: 8.192 to 24.576 MHz; low/high width >= 15 ns                 | D     | p.7               | high  | REQ-CLOCK-01  | `[clock]` | passing |
| D-C05 | LRCK must be synchronized with MCLK; phase is not critical               | D     | p.9               | high  | REQ-CLOCK-01  | `[clock]` | passing |
| D-C06 | Internal timing synchronizes to LRCK at power-up                         | D     | p.9               | high  | REQ-RESET-02  | `[serial]` | passing |
| D-C07 | All external clocks must be present unless PD is low; otherwise abnormal operation | D | p.9         | high  | REQ-CLOCK-01  | `[clock]` | passing |
| I-C01 | At 44.1 kHz: MCLK 11.2896, 16.9344, 22.5792 MHz (Table 1 prints 22.579)  | I     | ratio x fs        | high  | REQ-CLOCK-01  | `[clock]` | passing |
| I-C02 | MCLK ranges equal ratio x 16 kHz to ratio x 48 kHz                      | I     | D-C02 to D-C04 vs D-04 | high | REQ-CLOCK-01 | `[clock]` | passing |

## 7. De-emphasis

| ID    | Fact                                                                    | Class | Source         | Conf. | Requirement    | Test       | Status  |
| ----- | ----------------------------------------------------------------------- | ----- | -------------- | ----- | -------------- | ---------- | ------- |
| D-E01 | Digital IIR de-emphasis, time constants 50/15 us                        | D     | p.11           | high  | REQ-DEEM-01    | `[deemph]` | passing |
| D-E02 | DEM1,DEM0: 00 = 44.1 kHz, 01 = OFF, 10 = 48 kHz, 11 = 32 kHz            | D     | p.11 Table 3   | high  | REQ-DEEM-01    | `[deemph]` | passing |
| D-E03 | Filter applies to input audio data                                      | D     | p.11           | high  | REQ-ARCH-03    | `[deemph]` | passing |
| I-E01 | Reference `H(s) = (1 + s*15e-6) / (1 + s*50e-6)`; unity DC, HF asymptote 0.3 (-10.4576 dB) | I | Standard 50/15 us shelf from D-E01 | high | REQ-DEEM-01 | `[deemph]` | passing |
| I-E02 | Coefficients, switching transients, and state retention are unpublished | I     | Absent from p.11 | high | REQ-DEEM-02, REQ-DEEM-03 | `[deemph]` | passing (fitted sets and switching policy, P-60 to P-64) |

## 8. Soft mute

| ID    | Fact                                                                    | Class | Source            | Conf.  | Requirement   | Test     | Status  |
| ----- | ----------------------------------------------------------------------- | ----- | ----------------- | ------ | ------------- | -------- | ------- |
| D-M01 | Soft mute operates in the digital domain                                | D     | p.11              | high   | REQ-MUTE-01   | `[mute]` | passing |
| D-M02 | SMUTE H attenuates to -infinity over 1024 LRCK cycles                   | D     | p.11; Fig.5 note 1 | high  | REQ-MUTE-01   | `[mute]` | passing |
| D-M03 | SMUTE L returns to 0 dB over 1024 LRCK cycles                           | D     | p.11; Fig.5       | high   | REQ-MUTE-01   | `[mute]` | passing |
| D-M04 | Cancelling within 1024 cycles discontinues attenuation and returns to 0 dB | D  | p.11; Fig.5 note 3 | high  | REQ-MUTE-02   | `[mute]` | passing |
| D-M05 | Analog output follows the digital attenuation after the group delay     | D     | p.11 Fig.5 note 2 | high   | REQ-MUTE-02   | `[mute]` | passing |
| D-M06 | Signal transmission continues during mute                               | D     | p.11              | high   | REQ-MUTE-02   | `[mute]` | passing |
| I-M01 | Full transition durations: 32 ms at 32 kHz, 23.21995 ms at 44.1 kHz, 21.33333 ms at 48 kHz | I | 1024 / fs | high | REQ-MUTE-01 | `[mute]` | passing |
| I-M02 | Ramp law and partial-reversal slope are not numerically specified; Fig.5 (3) draws the reversal with a slope similar to the full ramp | I | p.11 Fig.5 | low | REQ-MUTE-02 | `[mute]` | passing (policy P-50, P-51) |

## 9. Reset and startup

| ID    | Fact                                                                    | Class | Source                | Conf.  | Requirement    | Test      | Status  |
| ----- | ----------------------------------------------------------------------- | ----- | --------------------- | ------ | -------------- | --------- | ------- |
| D-R01 | PD is active low and resets both ADC and DAC                            | D     | p.3 pin 20; p.12      | high   | REQ-RESET-01   | `[reset]` | passing |
| D-R02 | Reset should always be performed after power-up                         | D     | p.12                  | high   | REQ-RESET-02   | `[control]` | passing (P-70) |
| D-R03 | Minimum PD low pulse tPD                                                | D     | p.7 (150 ns)          | high   | REQ-RESET-01   | `[reset]` | passing |
| D-R04 | Clocks may be stopped while PD is low                                   | D     | p.12 Fig.6            | high   | REQ-RESET-01   | `[reset]` | passing |
| D-R05 | DAC returns to normal operation at PD release; the 516-cycle initialization does not affect the DAC | D | p.12 text and Fig.6 | high | REQ-RESET-01 | `[reset]` | passing |
| D-R06 | Click noise occurs at PD edges; external analog mute recommended        | D     | p.12 Fig.6 notes 5,6  | high   | REQ-RESET-02   | `[reset]` | not modeled (no click synthesized without captures, REQ-RESET-02) |
| I-R01 | DAC analog output during reset is drawn flat (no signal)                | I     | p.12 Fig.6 DAC Out    | medium | REQ-RESET-02   | `[reset]` | passing |

## 10. Analog output scaling and external circuit

| ID    | Fact                                                                    | Class | Source       | Conf.  | Requirement   | Test       | Status  |
| ----- | ----------------------------------------------------------------------- | ----- | ------------ | ------ | ------------- | ---------- | ------- |
| D-O01 | Analog Outputs section: "The input signal range scales with the supply voltage and nominally 0.58 x VREFH Vpp"; see DISC-01 | D | p.16 s.4 | medium | REQ-ANALOG-01 | `[analog]` | passing |
| D-O02 | Fig.11 example: 27 uF input coupling, 10 kohm input, 18 kohm feedback, inverting NJM5532, 10 uF output coupling into 27 kohm; 2.9 Vpp in, 5.2 Vpp out; labeled 6 dB gain | D | p.16 Fig.11 | high | REQ-ANALOG-02 | `[circuit]` | passing |
| I-O01 | Nominal output 2.9 Vpp at VREFH = 5 V, scaling with VREFH; peak `0.29 x VREFH`; `Vout = VCOM + 1.45 x` at 5 V | I | D-A07 typ, D-O01, Fig.11 (AOUTL 2.9 Vpp) | high | REQ-ANALOG-01 | `[analog]` | passing |
| I-O02 | Fig.11 gain is -1.8 (5.1 dB), not 6 dB                                  | I     | 18k / 10k; 2.9 x 1.8 = 5.22 Vpp | high | REQ-ANALOG-02 | `[circuit]` | passing |
| I-O03 | Fig.11 high-pass corners: 27 uF into the 10 kohm input resistor (virtual ground) and 10 uF into 27 kohm, each 1 / (2 pi R C) = 0.589 Hz; an external load in parallel with 27 kohm raises the second | I | D-O02 component values | high | REQ-ANALOG-02 | `[circuit]` | passing |

## 11. ADC behavior excluded from the DAC path

ADC facts MUST NOT appear in the DAC model [AGENTS.md section 3]. They are recorded in section 12 and modeled only in `AdcCore` (M7). The placeholder rows below predate M7 and now point to the section 12 entries.

| ID    | ADC fact                                                     | Source           | Now      |
| ----- | ------------------------------------------------------------ | ---------------- | -------- |
| X-01  | Digital HPF, 0.9 Hz at 44.1 kHz, scales with fs              | p.6; p.10        | AD-06    |
| X-02  | Decimation filter group delay 29.3 / fs                      | p.6              | AD-05    |
| X-03  | Decimation stopband attenuation 80 dB; ripple +/-0.005 dB    | p.6              | AD-04    |
| X-04  | 516 LRCK-cycle initialization after PD release (tPDV)        | p.7 note 14; p.12 | AD-10   |
| X-05  | ADC zero group-delay distortion                              | p.6              | AD-05    |
| X-06  | ADC 64x oversampling, input impedance, input voltage 0.6 x VREFH | p.1; p.5; p.15 | AD-01, AD-A07, AD-A08 |

## 12. ADC (M7)

Verified against the [DS] text layer on 2026-10-16. Table values were placed in their min, typ, or max column from PDF word coordinates (`pdftotext -bbox`), not from page images: on p.5, p.6, and p.7 the min, typ, and max columns start at x of about 348, 402, and 454. IDs AD-01 to AD-12 and AD-A01 to AD-A09 are those of AGENTS.md section 17; AD-13 onward and AD-A10 onward are added here.

### 12.1 Facts

Filter values at fs = 44.1 kHz; frequencies scale with fs; response reference 1 kHz [DS p.6 note 10].

| ID    | Fact                                                                     | Class | Source              | Conf.  | Requirement | Test    | Status  |
| ----- | ------------------------------------------------------------------------ | ----- | ------------------- | ------ | ----------- | ------- | ------- |
| AD-01 | Stereo 20-bit delta-sigma ADC, 64x oversampling, 16 to 48 kHz            | D     | p.1 Features; p.5   | high   | REQ-ADC-01  | `[adc]` | passing |
| AD-02 | Inputs are differential (AINx+, AINx-), internally biased to the common voltage VA/2 with 30 kohm typical; a signal may be applied to either input alone (single-ended) | D | p.3 pins 2 to 5; p.15 s.3 | high | REQ-ADC-05 | `[adc]` | passing |
| AD-03 | Decimation passband, all in the max column: -0.005 dB at 19.76 kHz, -0.02 dB at 20.02 kHz (0.454 fs), -0.06 dB at 20.20 kHz, -6.0 dB at 22.05 kHz | D | p.6; note 10 | high | REQ-ADC-02 | `[adc]` | passing |
| AD-04 | Decimation stopband from 24.34 kHz (min column); passband ripple +/-0.005 dB (max); stopband attenuation 80 dB (min) | D | p.6 | high | REQ-ADC-02 | `[adc]` | passing |
| AD-05 | Group delay 29.3 / fs (typ), from analog input to both channels' 20-bit data in the output register; group delay distortion 0 us (typ) | D | p.6; note 11 | high | REQ-ADC-02 | `[adc]` | passing |
| AD-06 | Digital HPF for DC offset cancellation: -3 dB at 0.9 Hz, -0.5 dB at 2.7 Hz, -0.1 dB at 6.0 Hz (typ) at 44.1 kHz; the cutoff scales with fs | D | p.6; p.10; p.15 | high | REQ-ADC-02 | `[adc]` | passing |
| AD-07 | Passband and stopband frequencies scale with fs; the response reference is 1 kHz | D | p.6 note 10 | high | REQ-ADC-02 | `[adc]` | passing |
| AD-08 | The inputs are sampled at 64fs; the digital filter rejects noise above the stopband except near multiples of 64fs; a simple RC filter (fc 150 kHz) may attenuate noise around 64fs | D | p.15 s.3 | high | REQ-ADC-02 | n/a | not modeled |
| AD-09 | Over-range input gives 7FFFFh above positive full scale and 80000h below negative full scale; two's complement; ideal code 00000h with no input; DC offset removed by the HPF | D | p.15 s.3 | high | REQ-ADC-05 | `[adc]` | passing |
| AD-10 | After PD is released an analog initialization cycle runs; SDTO becomes available after 516 LRCK cycles (p.12 text; tPDV 516 / fs in the typ column, counted in LRCK rising edges from PD rising); output is "0" during reset; a small click occurs at the end of initialization | D | p.7 note 14; p.12 Fig.6 notes 3, 4 | high | REQ-ADC-03 | `[adc]` | passing |
| AD-11 | SDTO: 20-bit MSB justified in modes 0, 1, 2; I2S in mode 3; MSB first; changes on the SCLK falling edge; at 32fs the low bits are truncated (modes 0 and 3, AD-16, DISC-10) | D | p.9 Table 2; pp.9-10 Figs.1 to 4 | high | REQ-ADC-04 | `[sdto]` | passing |
| AD-12 | SDTO timing: LRCK edge to SDTO MSB at most 40 ns; SCLK falling to SDTO at most 50 ns (max column) | D | p.7 | high | REQ-ADC-04 | n/a | not modeled |
| AD-13 | Block diagram order per channel: modulator, decimation filter, HPF | D     | p.1 block diagram   | high   | REQ-ADC-02  | `[adc]` | passing |
| AD-14 | Input range: differential nominally 0.3 x VREFH Vpp on each input; single-ended nominally 0.6 x VREFH Vpp on one input; inputs accept AGND to VA | D | p.15 s.3 | high | REQ-ADC-05 | `[adc]` | passing |
| AD-15 | SDTO at 64fs: modes 0, 1, 2 carry bits 19 to 0 in the 20 bit clocks from the LRCK edge, then idle; mode 3 carries bit 19 in the slot after the LRCK edge through bit 0 in slot 20 | D | p.9 Fig.1; p.10 Figs.2 to 4 | medium | REQ-ADC-04 | `[sdto]` | passing |
| AD-16 | SDTO at 32fs carries bits 19 to 4: mode 0 in slots 0 to 15 (Fig.1); mode 3 from slot 1, with bit 4 in slot 0 of the next half-frame (Fig.4) | D | p.9 Fig.1; p.10 Fig.4 | medium | REQ-ADC-04 | `[sdto]` | passing |
| AD-17 | Fig.7 single-ended connection: 470 ohm and 2.2 nF on the driven input (154 kHz), the other input AC-grounded through 4.7 uF and 0.1 uF | D | p.13 Fig.7 | medium | REQ-ADC-05 | n/a | informational |
| AD-18 | Fig.10 differential buffer example: NJM2100, 1.5 Vpp on each input, and 330 ohm and 1.5 nF per input | D | p.15 Fig.10 | medium | REQ-ADC-05 | n/a | informational |
| AD-19 | The ADC initialization does not affect DAC operation; clocks may stop while PD is low | D | p.12 | high | REQ-ADC-03, REQ-SCOPE-04 | `[adc]` | passing |

| ID     | Value                                                                  | Derivation                                                          | Class |
| ------ | ---------------------------------------------------------------------- | ------------------------------------------------------------------- | ----- |
| I-AD01 | Normalized edges: 0.4481, 0.4540, 0.4580, 0.5000 fs (passband points); 0.5519 fs (stopband) | AD-03, AD-04 divided by 44.1 kHz | I |
| I-AD02 | The -0.005 dB edge equals fs minus the stopband edge (44.1 - 24.34 = 19.76 kHz), so everything that aliases into the passband comes from the 80 dB stopband | AD-03, AD-04 | I |
| I-AD03 | The HPF points fit a first-order high-pass at 0.9 Hz: it predicts -0.46 dB at 2.7 Hz and -0.097 dB at 6.0 Hz against the printed -0.5 and -0.1; a second-order Butterworth at 0.9 Hz would give -0.05 dB at 2.7 Hz | AD-06, magnitude of s / (s + wc) | I |
| I-AD04 | Full scale is 3.0 Vpp at VREFH = 5 V in both input modes: across the two inputs when differential (1.5 Vpp each, in antiphase), on one input when single-ended | AD-14, AD-A07, AD-A13 | I |

### 12.2 Analog characteristics

Conditions as section 3, with differential inputs and 470 ohm source impedance [DS p.5].

| ID     | ADC quantity                              | Min      | Typ             | Max      | Class | Source        | Conf. | Requirement    | Test       | Status  |
| ------ | ----------------------------------------- | -------: | --------------: | -------: | ----- | ------------- | ----- | -------------- | ---------- | ------- |
| AD-A01 | S/(N+D), -0.5 dB input                    | 84 dB    | 92 dB           |          | D     | p.5           | high  | REQ-ADC-07     | `[adc]`    | passing |
| AD-A02 | Dynamic range, -60 dB input, A-weighted   | 94 dB    | 100 dB          |          | D     | p.5, note 5   | high  | REQ-ADC-07     | `[adc]`    | passing |
| AD-A03 | S/N, A-weighted                           | 94 dB    | 100 dB          |          | D     | p.5, notes 5, 6 | high | REQ-ADC-07    | `[adc]`    | passing |
| AD-A04 | Interchannel isolation                    | 90 dB    | 110 dB          |          | D     | p.5           | high  | REQ-ADC-07     | `[adc]`    | passing |
| AD-A05 | Interchannel gain mismatch                |          | 0.1 dB          | 0.3 dB   | D     | p.5           | high  | REQ-ADC-07     | `[adc]`    | passing |
| AD-A06 | Gain drift                                |          | 20 ppm/degree C |          | D     | p.5           | high  | n/a            | n/a        | not modeled |
| AD-A07 | Input voltage (AIN = 0.6 x VREFH)         | 2.85 Vpp | 3.0 Vpp         | 3.15 Vpp | D     | p.5, note 7   | high  | REQ-ADC-05     | `[adc]`    | passing |
| AD-A08 | Input resistance                          | 20 kohm  | 30 kohm         |          | D     | p.5           | high  | n/a            | n/a        | not modeled |
| AD-A09 | Power-supply rejection                    |          | 50 dB           |          | D     | p.5, note 8   | high  | n/a            | n/a        | not modeled |
| AD-A10 | Single-ended input S/(N+D): 80 dB typical at VA = 5 V |  |                 |          | D     | p.5 note 4    | high  | REQ-ADC-05     | `[adc]`    | passing |
| AD-A11 | 16-bit ADC DR and S/N: 98 dB              |          |                 |          | D     | p.5 note 5    | high  | REQ-ADC-07     | `[adc]`    | planned (16-bit output occurs only as SDTO at 32fs; not yet measured) |
| AD-A12 | CCIR-ARM S/N: 96 dB for each converter; 94 dB for ADC-to-DAC loopback | |    |          | D     | p.5 note 6    | high  | REQ-ADC-06     | `[adc]`    | deviation per converter (97.3 dB, DISC-08); loopback passing (94.2 dB) |
| AD-A13 | Full-scale input on each AIN+ and AIN- pin in differential mode: 1.5 Vpp | |     |          | D     | p.5 note 7    | high  | REQ-ADC-05     | `[adc]`    | passing |

## Discrepancies

| ID      | Description                                                                                                                                                                       | Source           | Resolution                                                                                                                         | Status |
| ------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------- | ---------------------------------------------------------------------------------------------------------------------------------- | ------ |
| DISC-01 | p.5 labels the output voltage `AOUT = 0.6 x VREFH` (typ 2.9 Vpp). p.16 section 4 "Analog Outputs" says "the input signal range scales with the supply voltage and nominally 0.58 x VREFH Vpp", repeating the wording of section 3 "Analog Inputs" (0.6 x VREFH) with a different number. The DAC has no analog input, so the sentence is read as describing the output; 0.58 x 5 V = 2.9 Vpp agrees with the p.5 typical and the Fig.11 label "AOUTL 2.9Vpp". | p.5; p.15 s.3; p.16 s.4, Fig.11 | Use 2.9 Vpp at 5 V, scaling with VREFH (ratio 0.58), for nominal output (AGENTS.md section 4.3). The p.16 sentence alone is not relied on. | open |
| DISC-02 | Fig.3 labels the first right-channel SDTI bits "23 22"; Table 2 and every other label specify 20 bits.                                                                            | p.10 Fig.3       | Treat as a diagram error; MSB is bit 19 for both channels (AGENTS.md section 5.1).                                                 | open   |
| DISC-03 | The 14.7/fs delay is listed under "DAC Digital Filter", but note 11 defines it from input register to analog output.                                                              | p.6 table, note 11 | Keep the ambiguity in the latency report; do not add 14.7 frames on top of a filter that already provides it (AGENTS.md section 4.2). | open |
| DISC-04 | *withdrawn* Originally recorded the +/-0.2 dB combined response as printed in the min column. The owner's spot-check found it in the typ column, confirmed by PDF text coordinates (x 399.8 to 417.0, same as the typ header and the 14.7 group delay). The error came from reading the page image. | p.6 table | D-F07 is typical, as AGENTS.md states. The AGENTS.md note added for this item was reverted. | withdrawn |
| DISC-05 | Fig.11 is labeled 6 dB gain, but its 10 kohm / 18 kohm network gives 5.1 dB; the drawn 5.2 Vpp output agrees with 5.1 dB.                                                        | p.16 Fig.11      | Calculate from components if the example circuit is modeled (AGENTS.md section 11).                                                | open   |
| DISC-06 | *new* Pin table marks AOUTL and AOUTR (pins 22, 23) as I/O type "I"; they are outputs per p.1, p.13, and p.16.                                                                   | p.3              | Treat as outputs. No model impact.                                                                                                  | open   |
| DISC-07 | *new* p.3 says all inputs "except pull-down pins" must not float; p.13 says all inputs "except CMODE pin". CMODE has internal pull-up and pull-down (p.6 note 12).               | p.3; p.13; p.6   | Only CMODE may float, and floating selects 512fs. No other model impact.                                                            | open   |
| DISC-08 | *new* For flat (white) noise, an A-weighted S/N of 100 dB implies a CCIR-ARM S/N of 97.3 dB, but [DS] gives 96 dB typical (a 1.3 dB difference). Not explained by band limits (10 Hz to 20 kHz vs 22 Hz to 22 kHz: 0.07 dB), 1 kHz calibration (the ARM curve is 0 dB at 2 kHz by definition), noise crest factor or type (at most 0.36 dB), sample rate or filter warping (none: exact curves per FFT bin), or level. No rising tilt helps: an f^2 rise, as a delta-sigma modulator would give, raises the CCIR-ARM figure (to 98.5 dB at 3 kHz corner). Matching both would need a peak of about 8 dB near 7 kHz (Q 0.7), where CCIR-ARM exceeds A-weighting by up to 6.9 dB; nothing in [DS] suggests one. The figures are also mutually inconsistent: two independent converters at 96 dB give 93.0 dB in loopback, not the 94 dB typical (AD-A12) (`tools/noise_shape_study.py`) | p.5 notes 6, 9 | White noise calibrated to the A-weighted figure (owner agreement, 2026-10-16); CCIR-ARM reported as a disclosed deviation. A shaped-noise option fitted to the figures was considered and declined by the owner on 2026-10-17: it would be curve-fitting with no physical basis, and no shape meets all three figures. The noise spectrum is unknown (U-09) | open; the ADC shows the same 97.3 dB per converter, while the DAC-to-ADC loopback measures 94.2 dB against 94 dB typical (AD-A12) |
| DISC-09 | *new* p.1 General Description says "Signal inputs and outputs are single-ended", but the pin table names differential inputs AINx+ and AINx- (p.3) and p.15 describes them as differential, usable single-ended. | p.1; p.3; p.15 s.3 | Model differential inputs with a single-ended option (AD-02); the p.1 sentence describes the DAC outputs and the single-ended input option. |
| DISC-10 | *new* p.9 text says only that mode 3 at 32fs truncates the least significant bits; Fig.1 also shows mode 0 at 32fs carrying SDTO bits 19 to 4. Mode 0 allows 32fs (Table 2). | p.9 text, Fig.1, Table 2 | Follow the figures: at 32fs, modes 0 and 3 carry 16 bits (AD-16). |

## Policies referenced by this ledger

Values classified P are recorded in [assumptions.md](assumptions.md), including the mute ramp law, mute and de-emphasis ordering, reset initialization, de-emphasis behavior at non-matching rates, float input quantization, and fitted filter parameters.
