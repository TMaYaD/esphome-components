"""Voltas AC auxiliary toggles exposed as individual switch entities.

The 122LZF protocol carries four independent toggle bits — Sleep, Turbo,
Saver (a.k.a. Econo) and Lamp (display Light) — in three different bytes
of the 80-bit frame. The wire format places no mutual-exclusion
constraint on them, and some Voltas firmwares actually couple them
(e.g. Sleep auto-engages Eco), so we surface each as a separate switch
rather than collapsing onto a single ``climate.preset``. Independence is
preserved on both TX and RX.

Each switch is parented to a ``climate: - platform: voltas_ac`` instance
via ``climate_id`` and shares its transmitter / receiver. Toggling a
switch emits a fresh Voltas frame containing the climate's current
mode/temp/fan/swing plus all four flag bits; an incoming frame updates
all four switch states in lock-step with the climate state.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_TYPE

from . import VoltasClimate, VoltasSwitch, VoltasSwitchKind

# No DEPENDENCIES: ``voltas_ac`` is a platform name, not a top-level
# component. The ``cv.use_id(VoltasClimate)`` below is what enforces
# that a matching climate instance exists in the same config.

CONF_CLIMATE_ID = "climate_id"

# Lower-case YAML aliases mapped to the C++ enum values declared in
# voltas_ac.h. ``saver`` and ``lamp`` use the user-facing names from the
# Voltas remote; the wire-protocol names are ``Econo`` and ``Light``.
SWITCH_KINDS = {
    "sleep": VoltasSwitchKind.SLEEP,
    "turbo": VoltasSwitchKind.TURBO,
    "saver": VoltasSwitchKind.ECONO,
    "lamp":  VoltasSwitchKind.LIGHT,
}

CONFIG_SCHEMA = (
    switch.switch_schema(VoltasSwitch)
    .extend(
        {
            cv.Required(CONF_CLIMATE_ID): cv.use_id(VoltasClimate),
            cv.Required(CONF_TYPE): cv.enum(SWITCH_KINDS, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_CLIMATE_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_kind(config[CONF_TYPE]))
    # Register with parent so decoded RX frames can publish the right entity.
    cg.add(parent.register_switch(var, config[CONF_TYPE]))
