"""TCL 112-bit AC auxiliary toggles exposed as individual switch entities.

The TCL112 protocol carries independent toggle bits — Econo, Health,
Light (panel display) and Turbo — in bytes 5 and 6 of the 14-byte frame.
The wire format places no mutual-exclusion constraint on them, so we
surface each as a separate switch rather than collapsing onto a single
``climate.preset``. Independence is preserved on both TX and RX.

Quiet is deliberately not offered: on the wire it lives in special
type-2 messages with tri-state library handling, and the Voltas/TCL
remotes we mirror never emit those.

Each switch is parented to a ``climate: - platform: tcl_ac_ir`` instance
via ``climate_id`` and shares its transmitter / receiver. Toggling a
switch emits a fresh TCL112 frame containing the climate's current
mode/temp/fan/swing plus all four flag bits; an incoming frame updates
all four switch states in lock-step with the climate state.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_TYPE

from . import TclAcClimate, TclAcSwitch, TclAcSwitchKind

# No DEPENDENCIES: ``tcl_ac_ir`` is a platform name, not a top-level
# component. The ``cv.use_id(TclAcClimate)`` below is what enforces
# that a matching climate instance exists in the same config.

CONF_CLIMATE_ID = "climate_id"

# Lower-case YAML aliases mapped to the C++ enum values declared in
# tcl_ac_ir.h. These use the wire-protocol names; device configs can set
# friendlier entity names per unit.
SWITCH_KINDS = {
    "econo":  TclAcSwitchKind.ECONO,
    "health": TclAcSwitchKind.HEALTH,
    "light":  TclAcSwitchKind.LIGHT,
    "turbo":  TclAcSwitchKind.TURBO,
}

CONFIG_SCHEMA = (
    switch.switch_schema(TclAcSwitch)
    .extend(
        {
            cv.Required(CONF_CLIMATE_ID): cv.use_id(TclAcClimate),
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
