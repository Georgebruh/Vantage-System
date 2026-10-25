# Change log

Hardware revisions, newest first. Each release is tagged `rev-<letter>`.

## Rev A — 2026-10-25

First release in this repository. It consolidates the earlier, unreleased design iterations.

### Sensor board

- ESP32-S3 dev board carrier, 110 × 30 mm, single copper layer for toner transfer.
- Tracks 0.6 mm (signals) and 1.0 mm (supplies, ground), gaps 0.5 mm, 0.8 mm from the edge; ground fill with
  thermal reliefs and none under the dev board antenna.
- Two wire links (L1, L2) on the component side complete the ground.
- Interfaces: J1 power board (JST XH 6-way side entry), J2 AJ-SR04M, J3 BME280, J4 rain gauge, J5 TF-Luna.
- JP1 (3V3 FEED) and JP2 (BENCH 5V) select field mode or bench mode.
- Protection: TVS and RC filter on the rain input, series resistors on the sensor lines, PTC on the TF-Luna
  supply, BME280 supplied from a GPIO through 100 Ω, 5 V echo divided to about 3.2 V.

### Power board

- Schematic only; the PCB layout is not started.
- 18650 cell with 1S protection module, 1.5 A PTC and BAT_ISO jumper.
- Solar input with TVS and Schottky protection, CN3791 MPPT charger.
- INA226 current monitors for the solar input (0x40) and the battery (0x44).
- 3.3 V from a TPS63020 buck-boost module through an LM66100 ideal diode.
- Sensor 5 V from a boost module switched by a TPS2051B (SENS_EN, 10 kΩ pull-down).

### Documents

- VNT-001 Vantage — Sensor board — Fabrication, assembly and test, first edition.
- VNT-002 Vantage — Power board — Description and test, first edition.
