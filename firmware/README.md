# Vantage — Firmware

Firmware for the ESP32-S3 dev board on the Vantage sensor board (Rev A). It reads every interface of the node,
manages the power budget from the battery and solar monitors, and sends the readings over Wi-Fi to the gateway
computer.

The pin map and the hardware rules it follows are in VNT-001, Annex C and Annex D.

| Interface | Connection | What the firmware does |
|---|---|---|
| BME280 (J3) | I2C on IO41/IO42, supply from IO2 | Powers it only for a forced measurement; probes 0x76 and 0x77 |
| AJ-SR04M (J2) | Trig IO5, Echo IO4 | 9 pings, median, speed of sound corrected with the BME280 temperature |
| TF-Luna (J5) | UART on IO39/IO40, 115 200 baud | Median of 15 frames; only when `LIDAR_FITTED` is true |
| Rain gauge (J4) | IO1, internal pull-up | Counts tips while awake and in deep sleep (wakes on each tip) |
| INA226 solar (0x40), battery (0x44) | I2C on IO38/IO47 | Triggered conversion, then power-down |
| Sensor 5 V | SENS_EN on IO21 | On only while measuring; held low in deep sleep |

## Setup

1. Install [PlatformIO](https://platformio.org/) (VS Code extension or `pip install platformio`).
2. Copy `include/secrets.example.h` to `include/secrets.h` and set the Wi-Fi SSID and password. `secrets.h` is
   not committed.
3. In `include/config.h`, set `NODE_ID`, `GATEWAY_URL` (the gateway computer's address) and, once the node is
   mounted, `MOUNT_HEIGHT_MM` (sensor face to the canal bed). Set `LIDAR_FITTED` to `true` if a TF-Luna is fitted.
4. Build and upload from this folder:

```
pio run -e field -t upload
pio device monitor
```

## Build environments

| Environment | Use |
|---|---|
| `field` | Normal operation: measure, send, deep sleep |
| `bench` | Bench and functional tests (VNT-001, 11.5 and 11.6): I2C scan of both buses at start, then a reading every 5 s on the serial monitor, no Wi-Fi, no sleep, running total of rain tips. Does not need `secrets.h`. |

```
pio run -e bench -t upload && pio device monitor
```

## Operation

Each wake-up reads the battery first and picks a power mode. The sensor 5 V is switched on, the BME280 is read
while the ultrasonic module warms up, then the ultrasonic (and the TF-Luna) is read and the 5 V is switched off.
The reading is queued and the queue is sent to the gateway; the node then sleeps until the next reading.

| Mode | Battery | Interval | Storm interval | Notes |
|---|---|---|---|---|
| normal | ≥ 3.60 V | 5 min | 1 min | |
| save | ≥ 3.45 V | 15 min | 5 min | TF-Luna off |
| low | ≥ 3.30 V | 60 min | 60 min | TF-Luna off |
| critical | < 3.30 V | 3 h | 3 h | Battery and rain only, no Wi-Fi |
| unknown | no battery monitor | 5 min | 1 min | Bench or power board missing |

A mode steps back up only when the battery is 0.05 V above the threshold. Storm mode starts when at least 2 tips
fall in one interval or the water rises 50 mm between readings, and lasts 30 min. Rain tips during sleep can
bring the next reading forward.

Robustness:

- Readings that cannot be sent stay queued in RTC memory (48 readings) and go out with the next successful upload.
- After 3 failed uploads the node tries only every third reading, except in storm mode.
- A brownout reset puts the node straight into critical mode for 3 h without using Wi-Fi.
- A 60 s watchdog resets the node if anything hangs; the queue and rain count survive the reset.
- After a power loss the node starts a new `session`, so the gateway can tell a restart from a lost reading.

All thresholds and intervals are in `include/config.h`.

## Data sent to the gateway

`POST` to `GATEWAY_URL` with a JSON body:

```json
{
  "node": "vantage-01", "fw": "0.1.0", "session": 2846392011, "boots": 412,
  "brownouts": 0, "crashes": 0, "dropped": 0, "rssi": -61,
  "readings": [
    {"seq": 37, "age_s": 0, "mode": "normal", "storm": false,
     "temp_c": 28.41, "rh": 81.2, "p_hpa": 1008.62, "dist_mm": 1532, "lidar_mm": null, "level_mm": null,
     "rain_tips": 0, "rain_mm": 0.00, "vbat": 3.987, "ibat_ma": 41.3, "vsol": 5.512, "isol_ma": 120.4}
  ]
}
```

The node has no clock. `age_s` is how long ago the reading was taken, so the measurement time is the receive time
less `age_s`. `rain_tips` counts the tips since the previous reading. Missing values are `null`. A reading is
identified by `node`, `session` and `seq`; the gateway should ignore repeats, which happen when an upload arrived
but its reply did not.

## Gateway receiver

`tools/receiver.py` is a small receiver for the gateway computer (Python 3, no packages needed). It writes every
reading to a CSV file with its measurement time and skips repeats:

```
python3 tools/receiver.py --port 8080 --out readings.csv
```

Set `GATEWAY_URL` to `http://<gateway IP>:8080/ingest`, and allow the port through the computer's firewall.
