"""Samsung AC IR auxiliary toggles exposed as individual switch entities.

The user-facing Samsung remote (AC-1xxxx-class) carries five sticky
toggle buttons beyond the climate state: Fast, Quiet, Beep, Clean,
Display. Each maps to a single boolean setter on ``IRSamsungAc``:
``setPowerful``, ``setQuiet``, ``setBeep``, ``setClean``,
``setDisplay``. We surface each as a separate switch rather than
collapsing onto a single ``climate.preset``.

Wire-level note. The SAMSUNG_AC frame's ``fan_special`` byte encodes
Powerful, Breeze (WindFree) and Econo as a mutually-exclusive 3-bit
group — only one can be active at a time. We expose only Powerful (as
``fast``), so the mutex never bites the user-visible surface. Even so,
the climate publishes ALL five aux states after every TX (not just the
one the user touched) — cheap, defensive against future addition of
Breeze / Econo as switches, and matches what the AC actually does on
the wire.

Each switch is parented to a ``climate: - platform: samsung_ac_ir``
instance via ``climate_id`` and shares its transmitter / receiver.
Toggling a switch emits a fresh SAMSUNG_AC frame containing the
climate's current mode/temp/fan/swing plus all aux flag bits; an
incoming frame updates all five switch states in lock-step with the
climate state.

Not surfaced here: the trigger-style buttons (Usage, Filter reset) and
the 5-step Capacity cycle. None of those map to public methods on
``IRSamsungAc``; they'll land once we have Tasmota dumps to reverse
the bit positions from.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_TYPE

from . import SamsungAcClimate, SamsungAcSwitch, SamsungAcSwitchKind

# No DEPENDENCIES: ``samsung_ac_ir`` is a platform name, not a top-level
# component. The ``cv.use_id(SamsungAcClimate)`` below is what enforces
# that a matching climate instance exists in the same config.

CONF_CLIMATE_ID = "climate_id"

# Lower-case YAML aliases mapped to the C++ enum values declared in
# samsung_ac_ir.h. We use the names from the Samsung remote where they
# differ from the wire-protocol names:
#   "fast"    -> Powerful on the wire (Tasmota also calls this Turbo)
#   "display" -> the wire calls it Display; remote calls it Display too
SWITCH_KINDS = {
    "fast":    SamsungAcSwitchKind.Fast,
    "quiet":   SamsungAcSwitchKind.Quiet,
    "beep":    SamsungAcSwitchKind.Beep,
    "clean":   SamsungAcSwitchKind.Clean,
    "display": SamsungAcSwitchKind.Display,
}

CONFIG_SCHEMA = (
    switch.switch_schema(SamsungAcSwitch)
    .extend(
        {
            cv.Required(CONF_CLIMATE_ID): cv.use_id(SamsungAcClimate),
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
