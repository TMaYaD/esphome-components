# AGENTS.md

Context for AI coding agents picking up work in this repo. Read before touching code.

## What this repo is

A small library of ESPHome external components consumed by
`tmayad/esphome-device-configs`. Each component is a self-contained directory
under `components/<name>/` exposing one or more ESPHome platforms (`climate`,
`switch`, `sensor`, etc.).

## Hard rules

1. **Don't break existing consumers.** Every component is referenced from at least one device YAML in `tmayad/esphome-device-configs`. Schema changes need migration notes; ABI changes (renamed methods on the C++ class) bump the version tag.
2. **Pin ownership stays with ESPHome.** Components delegate every GPIO to ESPHome's existing abstractions — `remote_transmitter`, `remote_receiver`, `light`, `output`, etc. We do **not** instantiate `IRsend` / `IRrecv` / equivalent with real pin numbers even when the upstream Arduino library makes it convenient. Two reasons: (a) composability — `dump:` listeners and other protocol decoders share the same hardware; (b) ESP32 portability — ESPHome's RMT integration is incompatible with bit-banging libraries on the same pin.
3. **Vendor libraries are codec-only.** When pulling in a third-party Arduino library (e.g. IRremoteESP8266), use it for protocol encoding/decoding only. Feed the resulting timings into ESPHome's `remote_base` for actual TX/RX. The upstream library's `send()`/`recv()` paths are off-limits.
4. **Decoder lists are additive.** If a component supports multiple protocol variants, each variant is a separate `try_decode_X(frame, &state)` block in the receive path. New variants add without touching existing ones. Avoid central dialect selectors — they create coupling and the autonomous-decode pattern handles multi-remote reality correctly (every variant tries; first match wins).
5. **Cross-check pin assumptions against source.** GPIO mappings derived from external references (Tasmota templates, vendor PDFs, forum threads) must be validated against the original encoding macro / source enum before committing. This bit us once — a Tasmota template decode was off by one for several iterations because the agent counted the enum visually instead of looking up the `AGPIO` macro (`function << 5`). Always check.

## Per-component layout

```
components/<name>/
├── __init__.py        # CODEOWNERS, namespace docstring, no logic
├── <platform>.py      # ESPHome schema + to_code()
├── <name>.h           # C++ header
└── <name>.cpp         # C++ implementation
```

For platform components (climate, sensor, switch, …), name the schema file after the platform: `climate.py`, `sensor.py`. ESPHome resolves `<platform>: { platform: <name> }` by importing `components/<name>/<platform>.py`.

## ESPHome API gotchas

- Use `climate.climate_schema(ClassObj)` / `sensor.sensor_schema(...)` — the older `*_SCHEMA` module constants were removed around ESPHome 2024.x.
- Use `await climate.new_climate(config)` to build + register a climate entity in one shot.
- If your library references ESP8266's `Serial` global (e.g. for debug prints), opt back into linking it:
  ```python
  from esphome.core import CORE
  if CORE.is_esp8266:
      from esphome.components.esp8266.const import enable_serial
      enable_serial()
  ```
  ESPHome strips the `Serial` object by default (`-DNO_GLOBAL_SERIAL`, saves 32 bytes) and otherwise the link fails.
- `cg.add_library("ns/lib", "^X.Y.Z")` — always pin to a SemVer range, never `latest`.

## Adding a component

1. Create `components/<name>/` with the four files above.
2. Set `CODEOWNERS = ["@<your-handle>"]` in `__init__.py`.
3. Add a row to `README.md` under "Components" describing what hardware it covers and its status.
4. Validate from a device config in the consumer repo:
   ```
   cd ../esphome-device-configs
   esphome config <device>.yaml      # schema check
   esphome compile <device>.yaml     # full C++ compile, surfaces ABI issues
   ```
   For iterative work, drop a local override in the device YAML:
   ```yaml
   external_components:
     - source: { type: local, path: ../esphome-components/components }
   ```
   Remove before committing the device YAML — it bypasses version pinning.
5. Once compile-clean, flash and validate behaviour on the real device. Then commit + push the component repo, then commit + push the device YAML separately so the two repos move atomically per change.

## What we deliberately don't do

- **`climate_ir::ClimateIR` base class.** Fine for upstream-supported IR vendors, but it assumes the codec is from ESPHome's own remote protocol set. Our vendors come from third-party Arduino libraries, so the lighter pattern — subclass `climate::Climate` directly and register as `RemoteReceiverListener` — is cleaner. Cost: ~30 lines of bit-loop. Benefit: no hidden upstream behaviors, full control over the receive contract.
- **`ir_rf_proxy` (HA 2026.4 Infrared integration).** It's the right tool for "be a generic IR pipe, let HA encode vendor protocols," but our integrations are vendor-specific climates with on-device authority. Different design point.

## CI

None today. A single-job matrix that checks out a known-good device config from the consumer repo and runs `esphome compile <device>.yaml` against each component is the obvious starting point. Worth adding when there's a second component.
