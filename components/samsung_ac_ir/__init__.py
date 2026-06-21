"""Samsung AC IR ESPHome external component.

Published from ``github://tmayad/esphome-components``. Device configs
consume it via ``external_components`` and can drop any local override.

The climate platform lives in ``climate.py`` and is referenced as
``climate: - platform: samsung_ac_ir`` from device configs.

The switch platform lives in ``switch.py`` and exposes the five sticky
auxiliary toggles surfaced on the user-facing AC-1xxxx-style Samsung
remote — Fast, Quiet, Beep, Clean, Display — as individual switches
parented to a climate instance. They share the climate's TX/RX path;
toggling one emits a full SAMSUNG_AC frame with the current climate
state plus the modified flag.

The button platform lives in ``button.py`` and exposes momentary
mode-toggle actions for the two pulse-semantic fields the AC tracks
internally — Beep and Clean. These are separate concerns from the
same-named switches: the switches drive per-command audible behavior on
HA-issued frames (which the AC honors directly); the buttons send a
one-shot remote-equivalent frame that flips the AC's *stored* mode (the
state visible on the AC's panel, which the remote also flips with its
Beep / Clean buttons). Both can coexist on a device — they control
genuinely different things.

Naming note: this is the **IR**-controlled Samsung A/C codec. There is a
separate, unrelated community component ``samsung_ac`` (omerfaruk-aran)
that speaks the wired F1/F2 NASA/NonNASA serial bus — they're entirely
different control surfaces, so we suffix ``_ir`` to keep them
distinguishable if a future device ever needs both.

Not yet surfaced (defer to a follow-up pass): the trigger-style buttons
on the Samsung remote (Usage, Filter reset) and the 5-step Capacity
cycle (40/60/80/100/120%). None of those map cleanly to public methods
on ``IRSamsungAc`` — they'll need bit-level reverse-engineering from
Tasmota captures. The 21-byte extended SAMSUNG_AC frame (used for the
explicit power-change and on/off/sleep timer packets) is also deferred.
"""

import esphome.codegen as cg
from esphome.components import button, climate, switch

CODEOWNERS = ["@tmayad"]

samsung_ac_ir_ns = cg.esphome_ns.namespace("samsung_ac_ir")
SamsungAcClimate = samsung_ac_ir_ns.class_(
    "SamsungAcClimate", climate.Climate, cg.Component
)
SamsungAcSwitch = samsung_ac_ir_ns.class_(
    "SamsungAcSwitch", switch.Switch, cg.Component
)
SamsungAcSwitchKind = samsung_ac_ir_ns.enum("SamsungAcSwitchKind")
SamsungAcButton = samsung_ac_ir_ns.class_(
    "SamsungAcButton", button.Button, cg.Component
)
SamsungAcButtonKind = samsung_ac_ir_ns.enum("SamsungAcButtonKind")
