# Bill of Materials (BOM)
 
**Title:** Power Monitor and Logger
**Version:** V 1.01
**Date:** 01 June 2026
 
---
 
## Information
 
BOM for improved functionalty including RTC and undervoltage protection and fuse
 
---
 
## Components
 
| ID | Item | Description | Supplier | Supplier ID | Quantity | Price for Required Quantity |
|----|------|-------------|----------|-------------|----------|-----------------------------|
| 1  |  ESP32    |Node MCU / ESP-Wroom-32 (LX6)             | Reichelt         |             |    1      |         7.30 €                    |
| 2  |(micro) SD-Shield      |SPI, best 3.3 without LDO             |Ebay          |             |1          | 4.00 €                            |
| 3  |micro SD-card      |FAT32 < 16 GB             |amazon - Intenso 16 GB          |             |1          | 4.00 €                            |
| 4  |Power supply      |9 V with 5.5-2.1 mm jack, CE marked             |Conrad          |ID002355712             |1          | 8.49 €                             |
| 5  |Shelly Plug S MTR Gen3      |power monitor plug adapter             |Conrad          |             |1          | 17.00 €                             |
| 6  |Housing      |distribution box, 100x100x50 mm          |Hardware store - Bauhaus         |             |1          |  1.99 €                           |
| 7  |Socket      |hollow pin 5.5 - 2.1 mm             |Conrad          |             |   1       | 3.00 €                            | 
| 8  |Real time clock |I2C DS3231 ZS042             |ebay         |             |   1       | 2.53€                            | 
| 9  |R_bal 100 kOhms       |     100 k ohms      |    conrad      |  x 003359192 VitrOhm POS100JT-52-100KAA POS100JT-52-100KAA Metallschicht-Widerstand 100 kΩ THT 0207 1 W 5 % 1 St. CE 003359192 Produkt-ID: 003359192 Menge: 4           |   2       |  0.28 €          | 
| 10  |R1 180 kOhms       |           |          |             |   1       |                          | 
| 11  |R2 47 kOhms       |           |          |             |   1       |                          | 
| 12  |Diode 1N5822      | set of diods purchased (5x)      |          |             |   1       |              3.17 €           | 
| 13  |C1, C2       | Supercap 5.5V/1F           |  conrad        |  x 000457454 Samxon DDL105S05F1JRRDAPZ Doppelschicht-Kondensator 1 F 5.5 V 20 % (L x B x H) 17.5 x 9 x 19.5 mm 1 St.CE 000457454 Menge: 2           |   2       |             5.59 €            | 
| 14  | R_inrushNTC      |  inrush limiterB57364S0100M000  (10 Ω cold)Disc THT 15 mm         |   ebay       |             |   1       |   6.00 €                      | 
| 15  |U1 DC/DC converter       | 000156673 TracoPower TSR 1-2450 DC/DC-Wandler, Print 24 V/DC 5 V/DC 1 A 6 W Anzahl Ausgänge: 1 x Inhalt 1 St.CE 000156673 Produkt-ID: 000156673         |   Conrad       |     000156673        |   1       |  6.49 €                       | 
| 16  |TVS diode       |          |       |            |   1       |  ?.?? €                     | 
| 17  |Current limiting fuse 0.5A slow |  Feinsicherung 5x20mm, träge 0,5A        |    reichelt.de   |           Artikel-Nr.: TR 0,5A Hst.-Teile-Nr.: 522.514 |   1       |  0.48 €                     | 
| 18  |fuse folder      |   Sicherungshalter, 5x20mm, print, mit Abdeckkappe       | reichelt.de      |    Artikel-Nr.: MTA 506000 / Hst.-Teile-Nr.: 506000        |   1       |  0.32 €                     | 
| 19  |surpressor diode 9.4 V      |    Littlefuse P6KE11A      |eu.mouser.com       |     Mouser No:576-P6KE11A       |   1       |  0.46 €                     |
| 17  |empty on purpose      |          |       |            |   1       |  ?.?? €                     | 

---
 
*Total Cost:*
