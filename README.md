# tmayad/esphome-components

Shared ESPHome external components used by [tmayad/esphome-device-configs](https://github.com/tmayad/esphome-device-configs).

## Usage

In any device YAML (or a shared package like `common/base.yaml`):

```yaml
external_components:
  - source: github://tmayad/esphome-components
```

To pin to a specific revision (recommended for production devices):

```yaml
external_components:
  - source: github://tmayad/esphome-components@<sha-or-tag>
    components: [voltas_ac]
```

## Components

| Name | Platform | Hardware | Status |
|---|---|---|---|
| [`voltas_ac`](components/voltas_ac/) | `climate`, `switch` | Voltas A/C, 122LZF protocol family | Stable. Two-way sync (TX + RX) validated on the AC-133B remote |
| [`samsung_ac_ir`](components/samsung_ac_ir/) | `climate` | Samsung A/C, SAMSUNG_AC 14-byte IR family (post-2018, non-WindFree) | First-pass: climate basics only. TX + RX both implemented; aux toggles (Quiet / Powerful / Breeze / Econo / Display / Clean / Beep / Sleep) and 21-byte timer frame deferred |

### `voltas_ac`

Native ESPHome climate platform for Voltas A/C units. Wraps `IRVoltas` from
[`crankyoldgit/IRremoteESP8266`](https://github.com/crankyoldgit/IRremoteESP8266)
as a state codec only — TX/RX timings flow through ESPHome's `remote_transmitter`
and `remote_receiver` so the bridge composes with other listeners (`dump:`, raw
captures, future codecs). Decoder list is additive: new protocol variants can be
slotted in without touching existing ones.

Minimal device snippet:

```yaml
remote_transmitter:
  id: ir_tx
  pin: GPIO14
  carrier_duty_percent: 50%

remote_receiver:
  id: ir_rx
  pin:
    number: GPIO5
    inverted: true
    mode: { input: true, pullup: true }
  tolerance: 25%
  filter: 4us
  idle: 4ms
  buffer_size: 2kb

climate:
  - platform: voltas_ac
    name: Master AC
    id: master_ac
    transmitter_id: ir_tx
    receiver_id: ir_rx
```

Supported climate traits: modes (Off / Cool / Dry / Fan-Only), target temp (16–30 °C step 1 °C), fan (Auto / Low / Medium / High), swing (Off / Vertical).

#### Auxiliary toggles (Sleep / Turbo / Saver / Lamp)

The 122LZF protocol carries four independent flag bits beyond the core climate state. They're exposed as a sibling `switch` platform parented to the climate id. Wire-protocol-faithful: combinations are allowed (the Voltas firmware itself sometimes auto-couples Sleep with Saver), so we don't collapse them onto a mutually-exclusive `climate.preset`.

```yaml
switch:
  - platform: voltas_ac
    name: Master AC Sleep
    climate_id: master_ac
    type: sleep
  - platform: voltas_ac
    name: Master AC Turbo
    climate_id: master_ac
    type: turbo
  - platform: voltas_ac
    name: Master AC Saver
    climate_id: master_ac
    type: saver
  - platform: voltas_ac
    name: Master AC Lamp
    climate_id: master_ac
    type: lamp
```

`type:` accepts `sleep`, `turbo`, `saver` (a.k.a. Econo on the wire), `lamp` (a.k.a. Light). Toggling any one emits a full Voltas frame with the climate's current mode/temp/fan/swing plus the modified flag. Incoming frames update all four switches in lock-step with the climate. Each switch is optional — opt in only to the toggles you want surfaced.

> **Local web UI limitation.** ESPHome's `web_server` (both v2 and v3, as of 2026.5.2) renders only mode and target temperature for climate entities — fan and swing aren't in the JS bundle. The native API serves them correctly, so Home Assistant shows the full set. For local fan/swing control without HA, add template `select:` entities mirroring the climate's `fan_mode` / `swing_mode`. The auxiliary switches above are regular `switch` entities and render fine in `web_server`.

### `samsung_ac_ir`

Native ESPHome climate platform for IR-controlled Samsung A/C units in the `SAMSUNG_AC` 14-byte frame family (post-2018, non-WindFree). Same architectural shape as `voltas_ac`: wraps `IRSamsungAc` from [`crankyoldgit/IRremoteESP8266`](https://github.com/crankyoldgit/IRremoteESP8266) as a state codec only — TX/RX timings flow through ESPHome's `remote_transmitter` / `remote_receiver`, so the bridge composes with `dump:` listeners, the Voltas codec on a sibling instance, and any future protocol additions.

Naming: the leading-edge `samsung_ac` slot is taken by the (unrelated, third-party) wired F1/F2 NASA/NonNASA bus component. This one is IR-only and suffixes `_ir` to keep both addressable on a single device if anyone ever needs that.

Minimal device snippet:

```yaml
remote_transmitter:
  id: ir_tx
  pin: GPIO14
  carrier_duty_percent: 50%

remote_receiver:
  id: ir_rx
  pin:
    number: GPIO5
    inverted: true
    mode: { input: true, pullup: true }
  tolerance: 25%
  filter: 4us
  idle: 4ms
  buffer_size: 2kb

climate:
  - platform: samsung_ac_ir
    name: Office AC
    id: office_ac
    transmitter_id: ir_tx
    receiver_id: ir_rx
```

Supported climate traits: modes (Off / Auto / Cool / Heat / Dry / Fan-Only), target temp (16–30 °C step 1 °C), fan (Auto / Low / Medium / High), swing (Off / Vertical / Horizontal / Both).

**First-pass scope.** Auxiliary toggles the protocol can carry — Quiet, Powerful (Turbo), Breeze (WindFree-style on supported firmware), Econo, Display (Light), Clean, Beep, Sleep — and the 21-byte extended frame used for on/off/sleep timers and explicit power-change packets are NOT exposed yet. They're the next natural follow-up, modeled on the way `voltas_ac` grew its `switch` platform for Sleep/Turbo/Saver/Lamp.

> The same `web_server` climate rendering limitation noted above applies here too.

## License

See [LICENSE](LICENSE).
