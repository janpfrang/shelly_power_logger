# Risk Assessment — Shelly / ESP32 Power Logger

**Document number:** RA-SHELLY-ESP32-001  
**Version:** 3.0  
**Date:** 01.06.2026  
**Author:** Jan Pfrang  
**Organisation:** De'Longhi Group  
**Contact:** jan.pfrang@delonghigroup.com  
**Device type:** Portable 230 V power logger (internal R&D use)  
**Quantity:** 10–20 units  
**Deployment:** Laboratory / engineers' home offices (work use only)  
**Classification:** Internal use — not placed on market (Art. 2(3) RED)

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 01.06.2026 | Jan Pfrang | Initial issue. Basic regulatory framework, 8 risk items, actions table, sign-off. |
| 2.0 | 01.06.2026 | Jan Pfrang | Added supercap buffer stage (R09, ACT-11, ACT-12). Confirmed ESP32 shield RED standalone certification. Updated EMC compliance route. Added Section 8 Gefährdungsbeurteilung. |
| 3.0 | 01.06.2026 | Jan Pfrang | Enclosure updated: DIN EN 60670 (VDE 0606) replaces UL 94 V-0 reference throughout. ACT-01 revised to EN 60670 DoC retention. R05 mitigation updated with glow-wire test rationale. Risk table restructured: explicit post-mitigation rating column added to 4.2; cross-reference between mitigations and ACT items clarified. |

---

## 1. Scope and Purpose

This document is the Gefährdungsbeurteilung (risk assessment) and Technical Safety File for the Shelly / ESP32 Power Logger, a portable data-logging device for internal R&D use at De'Longhi Group. It covers electrical safety, fire risk, electromagnetic compatibility (EMC), and the supercap buffer stage for 10–20 units deployed in laboratory environments and at engineers' home offices (work use only).

The device is not placed on the market. It is manufactured and used exclusively within the organisation, triggering the internal-use exemption under Article 2(3) RED. This document satisfies the Gefährdungsbeurteilung requirement of BetrSichV §3. The structured assessment in Section 8 follows the tabular format required by German occupational safety practice.

---

## 2. Device Description

### 2.1 Function

The logger measures AC voltage, current, and active power via a Shelly Plug S MTR Gen3 smart plug. The Shelly pushes JSON data over Wi-Fi to a CE/RED-certified ESP32 shield every 1 second. The ESP32 buffers data in RAM, writes to a microSD card as CSV, and serves a live web interface. A supercap buffer stage provides approximately 5–6 seconds of holdover during PSU undervoltage events.

### 2.2 Power Rail Architecture

- Mains 230 V AC → CE-certified PSU → 9 V DC at barrel jack
- 9 V DC → SMBJ9.0A TVS → 0.5 A slow-blow fuse (type T) → EPCOS B57364 NTC (10 Ω) → 2× supercap 5.5 V/1 F series (11 V rated, 100 kΩ balancing per cell) → Traco TSR 1-2450
- TSR 1-2450 → 5 V → ESP32 shield (3.3 V internal LDO) and SD card reader (3.3 V)

### 2.3 Component Inventory

| Component | Function | Certification | Notes |
|-----------|----------|---------------|-------|
| Shelly Plug S MTR Gen3 | Mains smart plug | CE / RED certified | 230 V AC, max 12 A / 2500 W — mains interface; not modified |
| 230 V → 9 V DC PSU | Mains adapter | CE certified (purchased) | Provides 9 V DC to enclosure; mains conversion internal to PSU |
| EPCOS B57364S0100M000 NTC | Inrush limiter | EPCOS / TDK component | 10 Ω cold / 7.5 A rated. First component after barrel jack. Limits supercap inrush to ~900 mA peak; returns to full cold resistance each power cycle |
| 2× supercap 5.5 V / 1 F (series) | Undervoltage buffer | RoHS compliant EDLC | Series: 11 V combined rating vs 9 V rail (2 V / 22% headroom). 100 kΩ balancing per cell. Holdover ~5–6 s at nominal load. ~30 mJ stored energy at 9 V |
| 2× 100 kΩ balancing resistors | Supercap voltage balance | Standard component | One across each cap. 90 µA balancing current at 9 V ensures equal 4.5 V per cell at steady state |
| 0.5 A slow-blow fuse (type T) | Overcurrent protection | Standard — verify type T | After NTC, before supercaps. Type T (slow-blow) mandatory — type F (fast-blow) will trip on 900 mA NTC inrush peak. See ACT-11 |
| Traco TSR 1-2450 | DC-DC step-down | UL / CE certified | 9 V → 5 V, 1 A, input 4.75–36 V. Hiccup-mode OCP/SCP. ~30% load at nominal current |
| ESP32-WROOM-32 shield | Wi-Fi MCU | CE / RED certified standalone | 3.3 V rail. RED compliance confirmed as standalone — integration preserves antenna clearance zone |
| MicroSD card reader | SPI SD interface | RoHS compliant | 3.3 V rail, SPI at 4 MHz. 33 Ω series resistors on CLK/MOSI — see ACT-02 |
| ABS enclosure 100×100×40 mm | Housing | DIN EN 60670 (VDE 0606) | Product-certified enclosure. Glow-wire tested per EN 60695-2-11. EN 60670 DoC to be retained in Technical File (ACT-01). Note any post-certification cutouts in Technical File |
| 5.5×2.1 mm barrel jack | DC input connector | Standard DC connector | 9 V DC input. Label: 'INPUT: 9 V DC / 0.5 A'. SMBJ9.0A TVS diode across jack — see ACT-03 |

---

## 3. Regulatory Framework

LVD does not apply — all internal rails are below 50 V AC. RED is the central directive; the ESP32 shield is CE/RED certified as a standalone module and integration does not alter the radio environment. DIN EN 60670 replaces UL 94 V-0 as the enclosure fire reference — it is a product-level certification more relevant to internal fault scenarios than a material-only rating. BetrSichV requires this risk assessment, satisfied by this document and Section 8.

| Directive / Regulation | Status | Assessment |
|------------------------|--------|------------|
| RED 2014/53/EU | ⚠️ APPLIES | ESP32-WROOM-32 shield is CE/RED certified standalone. Technical File references module DoC; documents that integration preserves antenna environment. EMC Directive 2014/30/EU absorbed by RED for radio equipment. |
| RoHS 2011/65/EU | ⚠️ APPLIES | All electronic assemblies. EDLC supercaps (activated-carbon, no hazardous heavy metals), NTC, Traco, ESP32, SD reader are RoHS compliant. Verify certificates at procurement. |
| BetrSichV (Germany) | ⚠️ APPLIES | Requires Gefährdungsbeurteilung before deployment. This document and Section 8 satisfy that requirement for lab and home-office use by employees. |
| GPSR EU 2023/988 | 🔴 MONITOR | Applies if device reaches consumers or is used at home in non-work context. Home deployment by lab engineers is grey area — document work-use justification per unit. EHS/legal approval required before home deployment. |
| LVD 2014/35/EU | ✅ EXCLUDED | Applies 50–1000 V AC. All internal rails ≤9 V DC. Supercap series stack 9 V — below 75 V DC threshold. PSU holds its own CE/LVD declaration. |
| EMC Directive 2014/30/EU | ✅ EXCLUDED | Absorbed by RED for radio-containing equipment. Not a separate obligation. |
| CE marking | ⚠️ OPTIONAL* | *Art. 2(3) RED: devices not placed on the market and used only within the organisation are excluded. Document exemption. Any transfer outside the company mandates full CE marking. |
| WEEE 2012/19/EU | ✅ LIGHT | EDLC supercaps contain no hazardous heavy metals — standard e-waste route. Register with national WEEE scheme or certified contractor. Low burden for <20 units. |

### 3.1 Internal-Use Exemption (Art. 2(3) RED)

These devices are: built in-house by De'Longhi engineering staff; used exclusively for internal R&D power measurement; never sold, transferred, or made available to third parties. Home use by engineers is work-related use, not consumer use — document per unit in deployment records.

> **Note:** If any unit is transferred outside the company, full RED compliance including CE marking becomes mandatory.

### 3.2 ESP32 RED Compliance Route

The ESP32-WROOM-32 shield carries its own CE/RED Declaration of Conformity as a standalone module, covering EN 300 328 v2.2.2 and EN 301 489-1/-17. Compliance route for the assembled device: (1) retain module DoC in Technical File; (2) document that PCB layout preserves the module's antenna clearance zone with no copper pour under the antenna; (3) document that the power supply is within the module's specified input range. No additional radio or EMC testing is required provided these conditions are met and documented in the Technical File (ACT-05).

### 3.3 Enclosure — DIN EN 60670 vs. UL 94

The ABS enclosure is certified to DIN EN 60670-1 (VDE 0606), a harmonised European product standard for enclosures used with electrical accessories. It includes a glow-wire test per EN 60695-2-11 at 850 °C, which simulates an internal overheating fault. This is more directly relevant to the device's internal fault scenario than UL 94, which is a material-level flammability test using an open external flame. The EN 60670 certificate applies to the enclosure as a product and carries a CE-relevant Declaration of Conformity. Any post-certification modifications (cutouts for barrel jack, cable entries) are noted in the Technical File with a statement that they do not increase internal power dissipation or reduce structural integrity in load-bearing areas (ACT-01).

### 3.4 Harmonised Standards

| Standard | Scope | Relevance | Compliance Route |
|----------|-------|-----------|-----------------|
| EN 300 328 v2.2.2 | RED — radio | 2.4 GHz Wi-Fi emissions | Inherited from ESP32 shield standalone CE/RED certificate |
| EN 301 489-1 / -17 | RED — EMC | EMC for radio equipment | Inherited from ESP32 shield standalone CE/RED certificate |
| EN 62368-1 | RED — safety | Audio/video and IT equipment safety | Relevant for self-assessment; reference in Technical File |
| DIN EN 60670-1 (VDE 0606) | Enclosure | Enclosures for electrical accessories | Product certification held by enclosure manufacturer. Glow-wire test per EN 60695-2-11 at 850 °C. DoC retained in Technical File (ACT-01) |
| IEC 60479 | Reference | Effects of current on human body | Basis for 60 V DC / 50 V AC shock threshold used in R01 |
| IEC 62391-1 | Reference | Fixed electric double-layer capacitors | Design reference for EDLC voltage derating and safe operating area |

---

## 4. Risk Assessment

### 4.1 Risk Matrix Legend

- **Severity:** High = serious injury or significant property damage. Med = minor injury or moderate equipment damage. Low = negligible injury potential or minor damage.
- **Likelihood:** High = probable under normal use. Med = possible under foreseeable misuse. Low = unlikely, requires multiple simultaneous failures.
- **Risk** = combined judgement of severity and likelihood.

> **Important:** The "Before" columns assume no protective measures are in place. The "After" rating assumes all listed measures — including all linked ACT items in Section 5 — are fully implemented. Section 5 is the closure checklist that must be completed to achieve the "After" ratings.

### 4.2 Hazard and Risk Table

| ID | Hazard / Cause | Sev | Lik | Risk (before) | Mitigation measures | Risk (after) | Open actions |
|----|---------------|-----|-----|---------------|---------------------|--------------|--------------|
| R01 | **Electric shock — DC rails**<br>Contact with 9 V / 5 V / 3.3 V pins, traces, or supercap terminals | 🟢 Low | 🟢 Low | 🟢 Low | All rails ≤9 V DC — below IEC 60479 physiological threshold (60 V DC). ABS enclosure encloses all PCB; barrel jack is the only external metal contact. Supercap stack at 9 V total — no shock hazard; ~30 mJ stored energy causes arcing risk during rework only. | 🟢 Low | ACT-12 |
| R02 | **Electric shock — 230 V mains**<br>Contact with mains side of PSU or Shelly internals | 🔴 High | 🟡 Med | 🔴 High | PSU is a CE-certified enclosed mains adapter — no user-accessible mains terminals. Shelly Plug S MTR Gen3 is CE/RED certified with no accessible mains conductors. User instructions and warning label: do not open PSU or Shelly. | 🟢 Low | ACT-04, ACT-07 |
| R03 | **Fire — DC power stage fault**<br>Overload, short circuit, or thermal fault in DC wiring or TSR 1-2450 | 🟡 Med | 🟢 Low | 🟢 Low | Traco TSR 1-2450: industrial-certified, hiccup-mode OCP/SCP — no sustained fault current. Actual load ~300 mA ≈ 30% of rated 1 A. 0.5 A slow-blow fuse upstream of supercap stage. TSR fed from supercap-buffered rail; supercap ESR limits surge to TSR input. | 🟢 Low | ACT-11 |
| R04 | **Fire — wrong PSU voltage**<br>User connects PSU with incorrect output voltage (e.g. 24 V, 48 V) | 🟡 Med | 🟢 Low | 🟢 Low | TSR 1-2450 accepts 4.75–36 V input — operates normally up to 36 V. Supercap series stack rated 11 V: voltages above 11 V cause electrolyte decomposition and venting within seconds. Fuse and TVS do NOT protect against sustained DC overvoltage — prevention is entirely procedural. Label: 'INPUT: 9 V DC / 0.5 A MAX'. PSU spec in user instructions. | 🟢 Low | ACT-03, ACT-04, ACT-07 |
| R05 | **Fire — enclosure under fault conditions**<br>Internal heat source in enclosure | 🟢 Low | 🟢 Low | 🟢 Low | Enclosure certified to DIN EN 60670 (VDE 0606) — product-level standard including glow-wire test per EN 60695-2-11 at 850 °C. More relevant to internal fault scenario than a UL 94 material-only rating. Total normal dissipation ≤0.5 W. EN 60670 DoC retained in Technical File; any post-certification cutouts noted. | 🟢 Low | ACT-01 |
| R06 | **EMC — radiated emissions**<br>ESP32 Wi-Fi + SPI bus interfering with nearby equipment | 🟢 Low | 🟢 Low | 🟢 Low | ESP32-WROOM-32 shield CE/RED certified standalone — EN 300 328 and EN 301 489 covered. Integration preserves antenna clearance zone. SPI at 4 MHz: 33 Ω series resistors on CLK and MOSI. SPI traces <50 mm. ABS enclosure — no shielding, acceptable for internal use. | 🟢 Low | ACT-02, ACT-05 |
| R07 | **EMC — Wi-Fi self-interference**<br>SPI harmonics degrading Shelly–ESP32 Wi-Fi link | 🟢 Low | 🟢 Low | 🟢 Low | SPI 4 MHz harmonics not in 2.4 GHz band at meaningful amplitude. Series resistors on SPI lines further reduce harmonic energy. Monitor Wi-Fi link quality during integration testing. | 🟢 Low | ACT-09 |
| R08 | **Mechanical — enclosure integrity**<br>Drop, impact, or liquid ingress exposing PCB | 🟢 Low | 🟢 Low | 🟢 Low | EN 60670 certified enclosure includes mechanical impact and screw-torque testing as part of product qualification. Not rated for outdoor/wet environments — labelled accordingly. Barrel jack strain relief recommended. | 🟢 Low | ACT-04, ACT-08 |
| R09 | **Supercap — inrush, overvoltage, stored energy**<br>Incorrect series voltage, fuse type, wrong PSU, or assembly arc | 🟡 Med | 🟢 Low | 🟢 Low | **Overvoltage:** 11 V series rating vs. 9 V rail — 22% headroom; each cell at 4.5 V steady state. RESOLVED. **Balancing:** 100 kΩ per cap — 90 µA overcomes EDLC leakage mismatch. RESOLVED. **Inrush:** EPCOS B57364 NTC (10 Ω cold) limits peak to ~900 mA; stays cool at 300 mA operating current. RESOLVED. **Fuse:** slow-blow type T mandatory — type F will trip on inrush pulse. **Holdover:** ~14.6 J usable, ~5–6 s at nominal load. **Assembly arc:** 30 mJ — discharge via 1 kΩ before rework. | 🟢 Low | ACT-11, ACT-12 |

> ⚠️ **R04 — Wrong PSU / supercap overvoltage note:** If a 24 V PSU were connected, each supercap cell would charge to ~12 V (218% of 5.5 V rating) within seconds. The NTC limits inrush to ~2.4 A; the slow-blow fuse trips in ~1–10 s — too slow to prevent cell destruction. Electrolyte decomposition produces flammable/toxic gas; the pressure vent opens; electrolyte spray and vapour are released. In the closed ABS enclosure, pressure builds until the lid is ejected or the enclosure cracks. No circuit-level protection prevents this failure — mitigation is entirely procedural (label, user instruction, pre-connection voltage check).

---

## 5. Required and Recommended Actions

This table is the closure checklist for the mitigations referenced in Section 4.2. The post-mitigation risk ratings in the "Risk (after)" column of Section 4.2 are only valid once all IMMEDIATE actions below are signed off.

| ID | Priority | Action | Owner | Timing |
|----|----------|--------|-------|--------|
| ACT-01 | 🔴 IMMEDIATE | Retain DIN EN 60670 (VDE 0606) Declaration of Conformity for enclosure in Technical File. Confirm glow-wire test at ≥850 °C on certificate. Note any post-certification cutouts (barrel jack, cable entry) in Technical File. | Regulatory / HW | Before deployment |
| ACT-02 | 🔴 IMMEDIATE | Add 33 Ω series resistors on SD SPI CLK and MOSI lines to reduce edge rates and harmonic emissions. | PCB layout / HW | Before first build |
| ACT-03 | 🟡 RECOMMENDED | Add SMBJ9.0A TVS diode across barrel jack for transient/ESD clamping. Does not protect against sustained DC overvoltage — procedural controls (ACT-04/07) remain primary. | PCB layout / HW | Before first build |
| ACT-04 | 🔴 IMMEDIATE | Label each unit: 'INPUT: 9 V DC / 0.5 A — Indoor lab use only — Do not open PSU or Shelly'. Label must be adjacent to barrel jack. | Assembly | Each unit |
| ACT-05 | 🔴 IMMEDIATE | Document internal-use exemption (Art. 2(3) RED) in Technical File. Attach ESP32 shield standalone DoC. Document that PCB layout preserves module antenna clearance zone. | Regulatory / EHS | Before deployment |
| ACT-06 | 🔴 IMMEDIATE | Inform EHS / legal before any home deployment. Record work-use justification per unit in deployment log. | EHS / Management | Before home deploy |
| ACT-07 | 🟡 RECOMMENDED | Write user instruction card: 9 V DC PSU only, verify PSU voltage before connecting, do not open Shelly or PSU, indoor lab use only. Include assembly note on supercap discharge. | Technical author | Before deployment |
| ACT-08 | 🟡 RECOMMENDED | Add strain relief or barrel jack recess to enclosure design to prevent connector leverage on PCB. | Mechanical design | Before first build |
| ACT-09 | 🟢 MONITOR | Check Wi-Fi link quality (RSSI, push drop rate) during integration testing. If dropouts observed, add ferrite bead on SD card reader VCC line. | FW / HW test | Integration test |
| ACT-10 | 🟢 MONITOR | Register devices with WEEE scheme or plan certified e-waste disposal route at end of life. | EHS | End of life |
| ACT-11 | 🔴 IMMEDIATE | Verify fuse is slow-blow type T (e.g. Littelfuse 218 0.5 A T or equivalent). Fast-blow type F WILL trip on ~900 mA NTC inrush pulse at power-on. Check procurement order confirmation. | Procurement / HW | Before first build |
| ACT-12 | 🔴 IMMEDIATE | Add to assembly SOP: discharge supercaps via 1 kΩ resistor for minimum 5 s before any rework on the 9 V rail. ~30 mJ stored energy causes visible arc if terminals are accidentally shorted. | Assembly SOP | Assembly procedure |

---

## 6. Conclusions and Overall Risk Rating

All post-mitigation ratings assume the ACT items in Section 5 are completed. IMMEDIATE actions must be closed before first deployment.

| Risk domain | Post-mitigation rating | Key condition / open action |
|-------------|----------------------|----------------------------|
| Electrical / shock — DC | 🟢 Low | All rails ≤9 V; 30 mJ stored energy — assembly discharge SOP required (ACT-12) |
| Electrical / shock — mains | 🟢 Low | Mains owned by CE-certified PSU and Shelly; label and user instruction (ACT-04, ACT-07) |
| Fire — power stage | 🟢 Low | Traco TSR OCP + slow-blow fuse; EN 60670 enclosure (ACT-01, ACT-11 critical) |
| Fire — wrong PSU | 🟢 Low | No circuit protection above 11 V — procedural controls only (ACT-04, ACT-07) |
| Supercap stage | 🟢 Low | Series 11 V rating; balancing; NTC; slow-blow fuse type T (ACT-11 critical) |
| EMC — emissions | 🟢 Low | ESP32 shield CE/RED standalone DoC in Technical File (ACT-02, ACT-05) |
| Enclosure fire resistance | 🟢 Low | DIN EN 60670 product certificate — glow-wire tested (ACT-01) |
| Regulatory compliance | 🟢 Low | Internal-use exemption documented; BetrSichV satisfied (ACT-05, ACT-06) |

**Overall: LOW risk profile for intended use.** The single most critical open action is ACT-11 (slow-blow fuse type T). The single most important procedural control is the PSU voltage check before connection, which is the only protection against supercap destruction from a wrong PSU.

---

## 7. Review and Sign-off

| Role | Name | Signature | Date |
|------|------|-----------|------|
| Author / Assessor | Jan Pfrang | | 01.06.2026 |
| EHS Review | | | |
| Technical Review | | | |
| Management Approval | | | |

*This document shall be reviewed whenever the hardware design changes, a new deployment location is added, or at minimum annually.*

---

## 8. Gefährdungsbeurteilung (Structured Work Equipment Assessment)

This section presents the Gefährdungsbeurteilung in the structured tabular format required by BetrSichV §3 and DGUV guidance for work equipment. The 'Additional measures required' column references the open ACT items from Section 5 that must be closed before deployment. Written in English as the working language of the responsible team.

**Legal basis:** BetrSichV §3; ArbSchG §5; DGUV Vorschrift 3 (electrical equipment).

### 8.1 General Information

| Field | Value |
|-------|-------|
| Work equipment designation | Shelly / ESP32 Power Logger |
| Inventory number / serial | To be assigned per unit at build |
| Location of use | Laboratory (primary) / engineer's home office (work use only, with EHS approval) |
| Type of use | Intermittent — powered during measurements; unplugged when not in use |
| Users | Trained laboratory engineers (De'Longhi R&D) |
| Employer / responsible person | Jan Pfrang, De'Longhi Group |
| Assessment date | 01.06.2026 |
| Next review due | Annual review or upon any hardware design change |
| Legal basis | BetrSichV §3; ArbSchG §5; DGUV Vorschrift 3 |

### 8.2 Hazard Assessment Table

*Residual risk after all measures: Low = acceptable, no further action beyond listed measures. All residual risks are Low provided the referenced ACT items are completed.*

| Hazard area | Activity / situation | Person at risk | Hazard description | Existing measures | Additional measures required | Residual risk | Responsible |
|-------------|---------------------|----------------|-------------------|-------------------|------------------------------|---------------|-------------|
| Electrical — DC rails | Operation, handling, assembly, rework | Operator, technician | Contact with 9 V / 5 V / 3.3 V rails. Supercap stored energy ~30 mJ — arcing risk during rework. | All rails ≤9 V DC (below IEC 60479 threshold). ABS enclosure. Barrel jack only external metal contact. | Assembly SOP: discharge supercaps via 1 kΩ / 5 s before rework on 9 V rail (ACT-12). | 🟢 Low | Jan Pfrang |
| Electrical — 230 V mains | Connecting PSU and Shelly; any handling of mains components | Operator, technician | Mains voltage inside CE-marked PSU and Shelly Plug. No mains voltage inside ESP32 enclosure. | PSU and Shelly CE certified, fully enclosed. No user-accessible mains terminals. | Warning label on device. User instruction card: do not open PSU or Shelly (ACT-04, ACT-07). | 🟢 Low | Jan Pfrang |
| Fire — DC power stage | Normal operation, fault conditions | Operator, adjacent personnel, property | Overcurrent or thermal fault in NTC, supercaps, fuse, or DC-DC converter. | Traco TSR OCP/SCP. 0.5 A slow-blow fuse. NTC inrush limiter. EN 60670 enclosure (glow-wire tested). | Confirm slow-blow fuse type T (ACT-11). Retain EN 60670 DoC in Technical File (ACT-01). | 🟢 Low | Jan Pfrang |
| Supercap — overvoltage / wrong PSU | Connection of PSU; assembly | Technician, operator | PSU voltage >11 V causes rapid cell destruction: electrolyte decomposition, gas generation, vent opening, electrolyte spray/vapour, possible enclosure pressurisation. | Label: 9 V DC / 0.5 A. Series rating 11 V vs 9 V rail. Balancing resistors. NTC. Slow-blow fuse. | Instruction card: verify PSU voltage before connecting (ACT-07). Pre-connection voltage check procedure. | 🟢 Low | Jan Pfrang |
| EMC — radio emissions | Normal operation (Wi-Fi active) | Occupants of lab / home office; nearby sensitive equipment | 2.4 GHz Wi-Fi and SPI harmonics may interfere with nearby equipment. | ESP32 shield CE/RED certified standalone — EN 300 328 and EN 301 489 covered. Module DoC in Technical File. | 33 Ω SPI series resistors (ACT-02). Integration antenna clearance documented (ACT-05). Monitor during testing (ACT-09). | 🟢 Low | Jan Pfrang |
| Mechanical — enclosure failure | Transport, drop, bench use | Operator | Drop or impact may expose PCB. No liquid ingress protection. | EN 60670 certified enclosure includes mechanical impact testing. Screw closure. | Strain relief on barrel jack (ACT-08). Label: indoor lab use only, not waterproof (ACT-04). | 🟢 Low | Jan Pfrang |
| Home-office deployment | Device used at engineer's private residence for work | Engineer, household members | Non-occupational persons (family, children) may encounter device. GPSR obligations may arise. | Device is work equipment. No mains voltage in enclosure. Shelly and PSU are CE certified. | EHS / legal approval before home deployment. Work-use documented per unit (ACT-06). Instruction card (ACT-07). | 🟢 Low | Jan Pfrang |

### 8.3 Instruction and Training Requirements

- All users must be briefed on the correct PSU specification (9 V DC, max 0.5 A) and the mandatory pre-connection voltage check before first use.
- Users must be instructed not to open the Shelly Plug or PSU under any circumstances.
- Technicians performing rework on the 9 V rail must follow the supercap discharge procedure: connect 1 kΩ resistor across the supercap terminals for minimum 5 seconds before soldering.
- Home-office users must confirm work-use purpose in deployment log and store device out of reach of children.
- Instruction card (ACT-07) must accompany each deployed unit.

### 8.4 Protective Measures Hierarchy (STOP Principle)

- **S — Substitution:** Mains measurement delegated entirely to CE-certified Shelly Plug. No mains voltage inside the ESP32 enclosure.
- **T — Technical measures:** NTC inrush limiter; slow-blow fuse type T; TVS diode; Traco TSR OCP/SCP; series supercap configuration with 22% voltage headroom; passive balancing resistors; DIN EN 60670 glow-wire tested enclosure; 33 Ω SPI series resistors.
- **O — Organisational measures:** Work-use-only deployment policy; EHS approval for home deployment; pre-connection PSU voltage check; annual document review.
- **P — Personal protective measures:** User instruction card; assembly SOP for rework; warning labels on device.

### 8.5 Assessment Result

All identified hazards are rated Low after application of the specified measures. The device is assessed as safe to deploy for its intended use (internal R&D, trained engineers, laboratory and work-related home-office environments) provided all IMMEDIATE actions in Section 5 are completed and signed off before first deployment.

*This Gefährdungsbeurteilung is valid from the date of management sign-off in Section 7.*

---

*RA-SHELLY-ESP32-001 v3.0 — Confidential — Internal use only — De'Longhi Group*
