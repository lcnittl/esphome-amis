# esphome-amis

This is an [ESPHome](https://esphome.io) component that reads data from an
AMIS smart meter. This meter is used in Upper Austria and provided by Netz OÖ
GmbH. It is actually a Siemens TD-3511/TD-3512, which communicates via
Serial-over-Infrared using AES-128-CBC encrypted M-Bus frames — this is *not*
DLMS/COSEM, so the official
[`dlms_meter`](https://esphome.io/components/sensor/dlms_meter/) component
cannot decode it. This component follows the same configuration architecture
as `dlms_meter`: a hub that owns the UART connection and decryption key, plus
`sensor`/`text_sensor` platforms that map values by OBIS code. Decryption
uses the platform crypto library (PSA/mbedtls on ESP32, BearSSL on ESP8266),
selected the same way as in `dlms_meter`.

An IR read/write head is required (e.g. the AMIS reader by Gottfried
Prandstetter, or the hardware from the
[amis_smartmeter_reader](https://github.com/mgerhard74/amis_smartmeter_reader)
project). The data exchange is entirely driven by the meter: it broadcasts a
search request (SND_NKE) once per minute, and after the component
acknowledges it, the meter pushes an encrypted telegram every second, which
the component acknowledges as well.

## Usage

Requires ESPHome 2026.6.0 or newer (the component reuses the `obis_code`
validator of `dlms_meter`).

You need the AES key ("power grid key") from Netz OÖ GmbH. You can request it
online in the [customer portal](https://eservice.netzooe.at). Copy the
32-character hex key into the configuration as-is.

```yaml
external_components:
  - source: github://lcnittl/esphome-amis
    components: [amis_meter]

# The meter transmits with 9600 baud, 8 data bits, even parity, 1 stop bit
uart:
  id: uart_bus
  tx_pin: 32
  rx_pin: 16
  baud_rate: 9600
  parity: EVEN

amis_meter:
  id: amis_meter_bus
  uart_id: uart_bus
  decryption_key: "1234567890ABCDEF1234567890ABCDEF"

sensor:
  - platform: amis_meter
    amis_meter_id: amis_meter_bus
    obis_code: "1.0.1.8.0.255"
    name: "Energy A+"
    unit_of_measurement: Wh
    device_class: energy
    state_class: total_increasing

text_sensor:
  - platform: amis_meter
    amis_meter_id: amis_meter_bus
    obis_code: "0.0.1.0.0.255"
    name: "AMIS Timestamp"
```

See [example_amis.yaml](example_amis.yaml) for a complete configuration with
all available values.

### Hub configuration variables

- **id** (*Optional*): Manually specify the ID used for code generation.
- **uart_id** (*Optional*): The UART bus to use. TX and RX are both required
  (the meter expects its telegrams to be acknowledged).
- **decryption_key** (**Required**, string): The 32-character hexadecimal AES
  key from the Netz OÖ customer portal. (`power_grid_key` is still accepted
  as a deprecated alias.)
- **receive_timeout** (*Optional*, time): Discard a partially received frame
  if no further bytes arrive within this time. Defaults to `1000ms`.

### Sensor / text sensor configuration variables

- **amis_meter_id** (*Optional*): The hub to read from. Only needed with multiple
  hubs.
- **obis_code** (**Required**, string): Which value to report. The same
  flexible notations as the official `dlms_meter` component are accepted
  (`1-0:1.8.0` or `1.0.1.8.0.255`); `F` defaults to `255` when omitted.
- All other options from [Sensor](https://esphome.io/components/sensor/) /
  [Text Sensor](https://esphome.io/components/text_sensor/). Note that unit,
  device class etc. are not set automatically — see the example config.

### Available OBIS codes

| OBIS code | Register | Description | Unit |
| --- | --- | --- | --- |
| `1.0.1.8.0.255` | 1.8.0 | Active energy A+ (consumed) | Wh |
| `1.0.2.8.0.255` | 2.8.0 | Active energy A- (delivered) | Wh |
| `1.0.3.8.1.255` | 3.8.1 | Reactive energy R+ | varh |
| `1.0.4.8.1.255` | 4.8.1 | Reactive energy R- | varh |
| `1.0.1.7.0.255` | 1.7.0 | Instantaneous active power P+ | W |
| `1.0.2.7.0.255` | 2.7.0 | Instantaneous active power P- | W |
| `1.0.3.7.0.255` | 3.7.0 | Instantaneous reactive power Q+ | var |
| `1.0.4.7.0.255` | 4.7.0 | Instantaneous reactive power Q- | var |
| `1.0.1.128.0.255` | 1.128.0 | Inkassozählwerk (prepayment register, signed) | Wh |
| `0.0.1.0.0.255` | — | Meter date + time | — |

The timestamp is available as a `text_sensor` (ISO 8601, meter-local time)
and as a numeric `sensor` (Unix epoch). Prefer the text sensor: numeric
sensor states are 32-bit floats, which cannot represent current epoch
seconds exactly.

Like `dlms_meter`, only values contained in the telegram are exposed.
Derived values such as the net active power (P+ − P-) can be calculated
with a [template sensor](https://esphome.io/components/sensor/template/) —
see [example_amis.yaml](example_amis.yaml).

If the decryption key is wrong, the telegram is received but fails the
decryption sanity check — the log will show
`Decryption failed, please check your decryption_key`.

## Migrating from the old configuration

The component was renamed from `amis` to `amis_meter` (update the
`external_components` entry and the platform name accordingly), and the key
moved from the sensor platform to the new `amis_meter:` hub:

```yaml
# Old
sensor:
  - platform: amis
    uart_id: uart_bus
    power_grid_key: 1234567890ABCDEF1234567890ABCDEF
    energy_a_positive:
      name: Energy A+

# New
amis_meter:
  uart_id: uart_bus
  decryption_key: "1234567890ABCDEF1234567890ABCDEF"

sensor:
  - platform: amis_meter
    obis_code: "1.0.1.8.0.255"
    name: Energy A+
    unit_of_measurement: Wh
    device_class: energy
    state_class: total_increasing
```

The old named-key sensor schema (`energy_a_positive:` etc. nested under the
platform) still works once the hub is configured, but is deprecated — please
switch to `obis_code`.

## Credits

- Original implementation by [@andyboeh](https://github.com/andyboeh/esphome-amis),
  tested with the AMIS reader by Gottfried Prandstetter
  (<http://www.mitterbaur.at/amis-leser.html>).
- Parts of the parsing are based on the
  [vzlogger](https://github.com/volkszaehler/vzlogger/blob/master/src/protocols/MeterOMS.cpp)
  implementation.
- Frame validation, wrong-key detection, the 1.128.0 register and the
  telegram structure documentation are based on
  [mgerhard74/amis_smartmeter_reader](https://github.com/mgerhard74/amis_smartmeter_reader).
- Configuration architecture modeled after the official ESPHome
  [dlms_meter](https://esphome.io/components/sensor/dlms_meter/) component.
