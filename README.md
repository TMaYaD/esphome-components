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
| [`samsung_ac_ir`](components/samsung_ac_ir/) | `climate`, `switch`, `button` | Samsung A/C, SAMSUNG_AC 14-byte IR family (post-2018, non-WindFree) | Climate + five aux switches (Fast / Quiet / Beep / Clean / Display) + two stored-mode toggle buttons (Beep / Clean). 21-byte timer frame and remote's trigger / Capacity buttons deferred |

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

Supported climate traits: modes (Off / Cool / Dry / Fan-Only), target temp (16–30 °C step 1 °C), fan (Auto / Low / Medium / High), swing (Off / Vertical — plus Horizontal / Both with `horizontal_swing: true`).

#### Horizontal swing (opt-in)

Some remotes in the 122LZF timing family carry a horizontal-swing command in byte 0: a `SwingHChange` marker (`0b1111100`) plus a direction bit, where steady frames always show the `0x33` "no change" signature. Upstream `IRVoltas` actually fingerprints marker-bearing remotes as a *different model* and no-ops `setSwingH()` for 122LZF — so this component patches byte 0 raw (and re-checksums) when an H change is pending, and emits the marker only on actual changes, mirroring the physical remote.

```yaml
climate:
  - platform: voltas_ac
    name: Guest AC
    id: guest_ac
    transmitter_id: ir_tx
    receiver_id: ir_rx
    horizontal_swing: true   # default false
```

Off by default: units like the master's AC-133B never move byte 0, and enabling H on them would emit frames the AC may not understand. Verified live on the guest unit (captures `0xF8`/`0xF9` in byte 0 = H off/on commands). Note the wire only reports H state on explicit H-command frames — steady frames carry no H info, so the component caches the last commanded/observed value.

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

#### Auxiliary toggles (Fast / Quiet / Beep / Clean / Display)

The five sticky toggle buttons on the Samsung remote map to single boolean setters on `IRSamsungAc`. They're exposed as a sibling `switch` platform parented to the climate id, mirroring `voltas_ac`'s layout. The YAML `type:` values use the **remote's** vocabulary rather than the wire-protocol names (the codec calls `fast` "Powerful").

```yaml
switch:
  - platform: samsung_ac_ir
    name: Office AC Fast
    climate_id: office_ac
    type: fast
  - platform: samsung_ac_ir
    name: Office AC Quiet
    climate_id: office_ac
    type: quiet
  - platform: samsung_ac_ir
    name: Office AC Beep
    climate_id: office_ac
    type: beep
  - platform: samsung_ac_ir
    name: Office AC Clean
    climate_id: office_ac
    type: clean
  - platform: samsung_ac_ir
    name: Office AC Display
    climate_id: office_ac
    type: display
```

`type:` accepts `fast` (Powerful on the wire), `quiet`, `beep`, `clean`, and `display` (front-panel LED). Toggling any one emits a full SAMSUNG_AC frame with the climate's current mode/temp/fan/swing plus all five aux flag bits. The bit value on each outgoing frame is what the AC honors for that one command — switch ON sends the bit as 1, switch OFF sends it as 0. Each switch is optional — opt in only to the toggles you want.

#### Stored-mode toggle buttons (Beep / Clean)

Companion button platform for the two pulse-semantic fields the AC tracks internally. Same `type:` vocabulary as the switches, different job:

```yaml
button:
  - platform: samsung_ac_ir
    name: Office AC Beep Toggle
    climate_id: office_ac
    type: beep
  - platform: samsung_ac_ir
    name: Office AC Clean Toggle
    climate_id: office_ac
    type: clean
```

One press emits a single SAMSUNG_AC frame with the relevant `Toggle` bit set AND a "remote-issued" watermark (byte 2 high nibble, bit 4) — empirically verified by frame captures to be the bit that unlocks the AC's *stored-mode* toggle path. The effect mirrors pressing Beep or Clean on the physical remote: the AC flips the state shown on its panel and honors that mode for chirps on commands from other senders.

#### Switch vs button — why both

The Beep switch and Beep button (same logic for Clean) target genuinely independent observable AC behaviors. Captures show:

| Wire pattern | Effect on AC's stored mode | Audible on this command |
|---|---|---|
| `BeepToggle=0`, no watermark (our climate TX, switch OFF) | no-op | silent |
| `BeepToggle=1`, no watermark (our climate TX, switch ON) | **no-op** | audible |
| `BeepToggle=0`, watermark set (remote temp/mode/fan/swing) | no-op | per AC's stored mode |
| `BeepToggle=1`, watermark set (remote Beep press / our button tap) | **TOGGLE** | audible |

So:
- **Switch** controls whether HA-issued climate commands chirp. It's a per-command instruction the AC honors directly; the AC's stored mode is untouched by it. Switch OFF can silence HA commands even when the AC's stored mode is ON.
- **Button** toggles the AC's stored mode. That mode is what governs chirps on commands issued by the physical remote or other IR devices in range — and what's visible on the AC's front panel.

Both are optional and independent.

> **RX asymmetry.** If you press Beep or Clean on the physical remote, the climate's RX path does NOT update the matching switch state. The bits are pulse-semantic from the remote (set on press only), so trusting them on RX would flicker the switch every time you touched any non-aux button on the remote. HA's switch therefore tracks HA's intent for our TXs only; the AC's actual stored mode is best observed via the AC's panel or by listening for chirps on inbound commands. Quiet, Display and Fast (Powerful) ARE stored state bits and DO sync bidirectionally.

**Deferred.** The remote's trigger-style buttons (Usage, Filter reset) and the 5-step Capacity cycle (40 / 60 / 80 / 100 / 120 %) aren't in `IRSamsungAc`'s public API — they need Tasmota-dump bit reverse-engineering to wire up. The 21-byte extended SAMSUNG_AC frame (on/off/sleep timers, explicit power-change packets) is also a separate pass.

> The same `web_server` climate rendering limitation noted above applies here too. The aux switches and buttons are regular `switch` / `button` entities and render fine in `web_server`.

## License

See [LICENSE](LICENSE).
