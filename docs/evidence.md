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

Test column names the planned Catch2 tag from AGENTS.md section 13.1. Status values: *planned*, *implemented*, *passing*, *failing*, *deviation*. *passing* means the linked test exists and passes where [features.md](features.md) records its availability.

## 1. Structure and operating conditions

| ID   | Fact                                                                              | Class | Source                  | Conf.  | Requirement     | Test        | Status  |
| ---- | --------------------------------------------------------------------------------- | ----- | ----------------------- | ------ | --------------- | ----------- | ------- |
| D-01 | Stereo 20-bit delta-sigma DAC, 128x oversampling                                  | D     | p.1 Features            | high   | REQ-SCOPE-01    | n/a         | planned |
| D-02 | 8x interpolator precedes each DAC delta-sigma modulator                           | D     | p.1 block diagram       | high   | REQ-FILTER-01   | `[filter]`  | passing |
| D-03 | DAC analog filtering is 2nd-order SCF plus 2nd-order CTF                          | D     | p.1 Features; p.16 s.4  | high   | REQ-FILTER-02   | `[filter]`  | passing |
| D-04 | DAC sample rate 16 to 48 kHz; LRCK typical 44.1 kHz                               | D     | p.1; p.7 LRCK fsn       | high   | REQ-CLOCK-01    | `[clock]`   | passing |
| D-05 | MCLK ratio 256fs, 384fs, or 512fs                                                 | D     | p.1; p.3 pin 21; p.9    | high   | REQ-CLOCK-01    | `[clock]`   | passing |
| D-06 | VA 4.5 to 5.5 V; VD 2.7 V min, 5.0 V typ, VA max                                  | D     | p.4 recommended         | high   | REQ-ANALOG-01   | n/a         | planned |
| D-07 | DAC outputs single-ended, centered on VCOM; VCOM = VA/2                           | D     | p.3 pin 24; p.16 s.4    | high   | REQ-ANALOG-01   | `[analog]`  | planned |
| D-08 | *new* VREFH sets the output range; normally connected to VA                       | D     | p.3 pin 1; p.15 s.2     | high   | REQ-ANALOG-01   | `[analog]`  | planned |
| D-09 | *new* SCF and CTF remove most modulator noise beyond the audio passband           | D     | p.16 s.4                | high   | REQ-MOD-01      | n/a         | planned |
| D-10 | AK4522VF: 24-pin VSOP, rated -10 to +70 degrees C                                 | D     | p.2 ordering guide      | high   | n/a             | n/a         | planned |

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
| D-A01 | S/(N+D)                                  | 80 dB    | 90 dB           |          | D     | p.5           | high  | REQ-DIST-01    | `[analog]` | planned |
| D-A02 | Dynamic range, -60 dB output, A-weighted | 95 dB    | 100 dB          |          | D     | p.5, note 5   | high  | REQ-NOISE-01   | `[analog]` | planned |
| D-A03 | S/N, A-weighted                          | 95 dB    | 100 dB          |          | D     | p.5, notes 6,9 | high | REQ-NOISE-01   | `[analog]` | planned |
| D-A04 | Interchannel isolation                   | 90 dB    | 110 dB          |          | D     | p.5           | high  | REQ-CHANNEL-01 | `[analog]` | planned |
| D-A05 | Interchannel gain mismatch               |          | 0.2 dB          | 0.5 dB   | D     | p.5           | high  | REQ-CHANNEL-01 | `[analog]` | planned |
| D-A06 | Gain drift                               |          | 20 ppm/degree C |          | D     | p.5           | high  | n/a (not modeled) | n/a     | planned |
| D-A07 | Output voltage (labeled AOUT = 0.6 x VREFH) | 2.65 Vpp | 2.9 Vpp      | 3.15 Vpp | D     | p.5           | high  | REQ-ANALOG-01  | `[analog]` | planned |
| D-A08 | Load resistance                          | 5 kohm   |                 |          | D     | p.5           | high  | n/a (not modeled) | n/a     | planned |
| D-A09 | Load capacitance                         |          |                 | 25 pF    | D     | p.5           | high  | n/a (not modeled) | n/a     | planned |
| D-A10 | Power-supply rejection                   |          | 50 dB           |          | D     | p.5, note 8   | high  | n/a (not modeled) | n/a     | planned |
| D-A11 | 16-bit DAC dynamic range                 |          | 98 dB           |          | D     | p.5 note 5    | high  | REQ-NOISE-01   | `[analog]` | planned |
| D-A12 | Zero-input S/N, any resolution           |          | 100 dB          |          | D     | p.5 note 9    | high  | REQ-NOISE-01   | `[analog]` | planned |
| D-A13 | S/N, CCIR-ARM weighted, each converter   |          | 96 dB           |          | D     | p.5 note 6    | high  | REQ-NOISE-01   | `[analog]` | planned |
| D-A14 | PSR stimulus: 1 kHz, 50 mVpp on VA and VD; VREFH held constant | |     |          | D     | p.5 note 8    | high  | n/a            | n/a        | planned |
| D-A15 | Physical DC offset at outputs: "a few mV"; removed by AC coupling |   |    |          | D     | p.16 s.4      | medium | REQ-ANALOG-02 | `[analog]` | planned |
| D-A16 | *new* Output noise level while soft-muted is shown as -100 dB | |       |          | D     | p.11 Fig.5    | medium | REQ-NOISE-02  | `[mute][analog]` | planned |

D-A16 is a figure annotation, not a table value. It agrees with D-A12 and supports keeping analog noise audible after digital mute.

## 4. PCM data mapping

| ID    | Fact                                                                 | Class | Source               | Conf. | Requirement | Test    | Status  |
| ----- | -------------------------------------------------------------------- | ----- | -------------------- | ----- | ----------- | ------- | ------- |
| D-P01 | DAC input is two's complement, MSB first, in all modes               | D     | p.9; p.16 s.4        | high  | REQ-PCM-01  | `[pcm]` | passing |
| D-P02 | 7FFFFh is positive full scale; 80000h is negative full scale (20-bit) | D    | p.16 s.4             | high  | REQ-PCM-01  | `[pcm]` | passing |
| D-P03 | 00000h ideally produces VCOM at the output                           | D     | p.16 s.4             | high  | REQ-PCM-01  | `[pcm]` | planned |
| I-P01 | Normalized value `code / 524288.0`; +FS is `1 - 2^-19`               | I     | D-P02 with 20-bit range | high | REQ-PCM-01 | `[pcm]` | passing |
| I-P02 | 16-bit words map to 20-bit scale by multiplication by 16             | I     | Mode 0 and 32fs I2S carry the top 16 bits; MSB weight is preserved | high | REQ-PCM-02 | `[pcm]` | passing |

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
| D-S12 | SDTO (ADC output) changes on SCLK falling edge; outside the DAC contract | D     | p.9                    | high   | REQ-SCOPE-01    | n/a        | planned |

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
| D-C06 | Internal timing synchronizes to LRCK at power-up                         | D     | p.9               | high  | REQ-RESET-02  | `[reset]` | planned |
| D-C07 | All external clocks must be present unless PD is low; otherwise abnormal operation | D | p.9         | high  | REQ-CLOCK-01  | `[clock]` | planned |
| I-C01 | At 44.1 kHz: MCLK 11.2896, 16.9344, 22.5792 MHz (Table 1 prints 22.579)  | I     | ratio x fs        | high  | REQ-CLOCK-01  | `[clock]` | passing |
| I-C02 | MCLK ranges equal ratio x 16 kHz to ratio x 48 kHz                      | I     | D-C02 to D-C04 vs D-04 | high | REQ-CLOCK-01 | `[clock]` | passing |

## 7. De-emphasis

| ID    | Fact                                                                    | Class | Source         | Conf. | Requirement    | Test       | Status  |
| ----- | ----------------------------------------------------------------------- | ----- | -------------- | ----- | -------------- | ---------- | ------- |
| D-E01 | Digital IIR de-emphasis, time constants 50/15 us                        | D     | p.11           | high  | REQ-DEEM-01    | `[deemph]` | passing |
| D-E02 | DEM1,DEM0: 00 = 44.1 kHz, 01 = OFF, 10 = 48 kHz, 11 = 32 kHz            | D     | p.11 Table 3   | high  | REQ-DEEM-01    | `[deemph]` | passing |
| D-E03 | Filter applies to input audio data                                      | D     | p.11           | high  | REQ-ARCH-03    | `[deemph]` | passing |
| I-E01 | Reference `H(s) = (1 + s*15e-6) / (1 + s*50e-6)`; unity DC, HF asymptote 0.3 (-10.4576 dB) | I | Standard 50/15 us shelf from D-E01 | high | REQ-DEEM-01 | `[deemph]` | passing |
| I-E02 | Coefficients, switching transients, and state retention are unpublished | I     | Absent from p.11 | high | REQ-DEEM-02, REQ-DEEM-03 | `[deemph]` | planned |

## 8. Soft mute

| ID    | Fact                                                                    | Class | Source            | Conf.  | Requirement   | Test     | Status  |
| ----- | ----------------------------------------------------------------------- | ----- | ----------------- | ------ | ------------- | -------- | ------- |
| D-M01 | Soft mute operates in the digital domain                                | D     | p.11              | high   | REQ-MUTE-01   | `[mute]` | planned |
| D-M02 | SMUTE H attenuates to -infinity over 1024 LRCK cycles                   | D     | p.11; Fig.5 note 1 | high  | REQ-MUTE-01   | `[mute]` | passing |
| D-M03 | SMUTE L returns to 0 dB over 1024 LRCK cycles                           | D     | p.11; Fig.5       | high   | REQ-MUTE-01   | `[mute]` | passing |
| D-M04 | Cancelling within 1024 cycles discontinues attenuation and returns to 0 dB | D  | p.11; Fig.5 note 3 | high  | REQ-MUTE-02   | `[mute]` | passing |
| D-M05 | Analog output follows the digital attenuation after the group delay     | D     | p.11 Fig.5 note 2 | high   | REQ-MUTE-02   | `[mute]` | passing |
| D-M06 | Signal transmission continues during mute                               | D     | p.11              | high   | REQ-MUTE-02   | `[mute]` | passing |
| I-M01 | Full transition durations: 32 ms at 32 kHz, 23.21995 ms at 44.1 kHz, 21.33333 ms at 48 kHz | I | 1024 / fs | high | REQ-MUTE-01 | `[mute]` | passing |
| I-M02 | Ramp law and partial-reversal slope are not numerically specified; Fig.5 (3) draws the reversal with a slope similar to the full ramp | I | p.11 Fig.5 | low | REQ-MUTE-02 | `[mute]` | planned |

## 9. Reset and startup

| ID    | Fact                                                                    | Class | Source                | Conf.  | Requirement    | Test      | Status  |
| ----- | ----------------------------------------------------------------------- | ----- | --------------------- | ------ | -------------- | --------- | ------- |
| D-R01 | PD is active low and resets both ADC and DAC                            | D     | p.3 pin 20; p.12      | high   | REQ-RESET-01   | `[reset]` | passing |
| D-R02 | Reset should always be performed after power-up                         | D     | p.12                  | high   | REQ-RESET-02   | `[reset]` | planned |
| D-R03 | Minimum PD low pulse tPD                                                | D     | p.7 (150 ns)          | high   | REQ-RESET-01   | `[reset]` | passing |
| D-R04 | Clocks may be stopped while PD is low                                   | D     | p.12 Fig.6            | high   | REQ-RESET-01   | `[reset]` | passing |
| D-R05 | DAC returns to normal operation at PD release; the 516-cycle initialization does not affect the DAC | D | p.12 text and Fig.6 | high | REQ-RESET-01 | `[reset]` | passing |
| D-R06 | Click noise occurs at PD edges; external analog mute recommended        | D     | p.12 Fig.6 notes 5,6  | high   | REQ-RESET-02   | `[reset]` | planned |
| I-R01 | DAC analog output during reset is drawn flat (no signal)                | I     | p.12 Fig.6 DAC Out    | medium | REQ-RESET-02   | `[reset]` | passing |

## 10. Analog output scaling and external circuit

| ID    | Fact                                                                    | Class | Source       | Conf.  | Requirement   | Test       | Status  |
| ----- | ----------------------------------------------------------------------- | ----- | ------------ | ------ | ------------- | ---------- | ------- |
| D-O01 | Analog Outputs section: "The input signal range scales with the supply voltage and nominally 0.58 x VREFH Vpp"; see DISC-01 | D | p.16 s.4 | medium | REQ-ANALOG-01 | `[analog]` | planned |
| D-O02 | Fig.11 example: 27 uF input coupling, 10 kohm input, 18 kohm feedback, inverting NJM5532, 10 uF output coupling into 27 kohm; 2.9 Vpp in, 5.2 Vpp out; labeled 6 dB gain | D | p.16 Fig.11 | high | REQ-ANALOG-02 | n/a | planned |
| I-O01 | Nominal output 2.9 Vpp at VREFH = 5 V, scaling with VREFH; peak `0.29 x VREFH`; `Vout = VCOM + 1.45 x` at 5 V | I | D-A07 typ, D-O01, Fig.11 (AOUTL 2.9 Vpp) | high | REQ-ANALOG-01 | `[analog]` | planned |
| I-O02 | Fig.11 gain is -1.8 (5.1 dB), not 6 dB                                  | I     | 18k / 10k; 2.9 x 1.8 = 5.22 Vpp | high | REQ-ANALOG-02 | n/a | planned |

## 11. ADC behavior excluded from the DAC path

These facts are documented but belong to the ADC. They MUST NOT appear in the DAC model [AGENTS.md section 3]. They will be modeled in `AdcCore` (M7, AGENTS.md section 17); full ADC evidence entries are created at the start of M7.

| ID    | ADC fact                                                     | Source           |
| ----- | ------------------------------------------------------------ | ---------------- |
| X-01  | Digital HPF, 0.9 Hz at 44.1 kHz, scales with fs              | p.6; p.10        |
| X-02  | Decimation filter group delay 29.3 / fs                      | p.6              |
| X-03  | Decimation stopband attenuation 80 dB; ripple +/-0.005 dB    | p.6              |
| X-04  | 516 LRCK-cycle initialization after PD release (tPDV)        | p.7 note 14; p.12 |
| X-05  | ADC zero group-delay distortion                              | p.6              |
| X-06  | ADC 64x oversampling, input impedance, input voltage 0.6 x VREFH | p.1; p.5; p.15 |

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

## Policies referenced by this ledger

Values classified P are recorded in [assumptions.md](assumptions.md), including the mute ramp law, mute and de-emphasis ordering, reset initialization, de-emphasis behavior at non-matching rates, float input quantization, and fitted filter parameters.
