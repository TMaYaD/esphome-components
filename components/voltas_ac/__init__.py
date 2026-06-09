"""Voltas AC ESPHome external component.

Published from ``github://tmayad/esphome-components``. Device configs
consume it via ``external_components`` and can drop any local override.

The climate platform lives in ``climate.py`` and is referenced as
``climate: - platform: voltas_ac`` from device configs.

The switch platform lives in ``switch.py`` and exposes the Voltas
protocol's four independent toggle bits — Sleep, Turbo, Saver (Econo)
and Lamp (display Light) — as individual switches parented to a
climate instance. They share the climate's TX/RX path; toggling one
emits a full Voltas frame with the current climate state plus the
modified flag.
"""

import esphome.codegen as cg
from esphome.components import climate, switch

CODEOWNERS = ["@tmayad"]

voltas_ac_ns = cg.esphome_ns.namespace("voltas_ac")
VoltasClimate = voltas_ac_ns.class_(
    "VoltasClimate", climate.Climate, cg.Component
)
VoltasSwitch = voltas_ac_ns.class_(
    "VoltasSwitch", switch.Switch, cg.Component
)
VoltasSwitchKind = voltas_ac_ns.enum("VoltasSwitchKind")
