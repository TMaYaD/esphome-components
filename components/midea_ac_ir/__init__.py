"""Midea AC IR (Coolix protocol) ESPHome external component.

Published from ``github://tmayad/esphome-components``. Device configs
consume it via ``external_components`` and can drop any local override.

Targets Midea split A/C units of the RG51-remote generation (verified
against an RG51Y5/E remote + its unit), which speak the classic 24-bit
Coolix protocol plus a 48-bit Coolix48 extension for Eco (and, per
upstream notes, timers). The protocol map was reverse-verified capture
by capture against the physical remote — every constant in the C++
sources traces to a logged frame.

Unlike ``voltas_ac`` / ``samsung_ac_ir``, this component does NOT wrap
IRremoteESP8266. The Coolix word is 3 bytes with byte-inverse framing
and no checksum; the empirically-verified tables are smaller than the
glue code a codec wrapper would need. Hand-rolling also sidesteps the
library's stateful IRac/IRCoolixAC layer, whose fan-mode encoding bug
(temp-vs-mode ordering, fixed upstream later) is what shipped broken
fan frames from the Tasmota firmware this component replaces.

Platforms:
- ``climate.py``  — mode / target temp / fan; RX-synced from remote use
- ``button.py``   — one-shot toggles: direct (vane step), sleep, turbo,
                    led, clean
- ``switch.py``   — eco: a REAL stateful switch, because this remote
                    generation sends distinct Coolix48 words for on/off
"""

import esphome.codegen as cg
from esphome.components import button, climate, switch

CODEOWNERS = ["@tmayad"]

midea_ac_ir_ns = cg.esphome_ns.namespace("midea_ac_ir")
MideaAcClimate = midea_ac_ir_ns.class_(
    "MideaAcClimate", climate.Climate, cg.Component
)
MideaAcButton = midea_ac_ir_ns.class_(
    "MideaAcButton", button.Button, cg.Component
)
MideaAcButtonKind = midea_ac_ir_ns.enum("MideaAcButtonKind")
MideaAcSwitch = midea_ac_ir_ns.class_(
    "MideaAcSwitch", switch.Switch, cg.Component
)
MideaAcSwitchKind = midea_ac_ir_ns.enum("MideaAcSwitchKind")
