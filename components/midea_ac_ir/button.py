"""Midea AC IR one-shot toggle actions exposed as button entities.

These map to the RG51-generation remote's stateless toggle buttons.
None of them have an observable state over IR — the AC tracks the
resulting mode internally and never echoes it — so ``button`` (not
``switch``) is the honest entity type:

- ``direct``: steps the vertical vane one notch (7 positions, direction
  reverses at the ends; no absolute positioning exists on the wire).
  Wire quirk: sent as a SINGLE Coolix block — a normal double-block
  send would step the vane twice per press.
- ``sleep``:  toggles sleep mode. The physical remote pairs this with a
  state frame (captured behavior), so we do too.
- ``turbo``, ``led``, ``clean``: standalone toggle words, double-block.

``clean`` starts the AC's self-clean cycle; the panel shows "SC" until
the cycle ends or the toggle is sent again.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_TYPE

from . import MideaAcButton, MideaAcButtonKind, MideaAcClimate

# No DEPENDENCIES: ``midea_ac_ir`` is a platform name, not a top-level
# component. The ``cv.use_id(MideaAcClimate)`` below is what enforces
# that a matching climate instance exists in the same config.

CONF_CLIMATE_ID = "climate_id"

BUTTON_KINDS = {
    "direct": MideaAcButtonKind.Direct,
    "sleep":  MideaAcButtonKind.Sleep,
    "turbo":  MideaAcButtonKind.Turbo,
    "led":    MideaAcButtonKind.Led,
    "clean":  MideaAcButtonKind.Clean,
}

CONFIG_SCHEMA = (
    button.button_schema(MideaAcButton)
    .extend(
        {
            cv.Required(CONF_CLIMATE_ID): cv.use_id(MideaAcClimate),
            cv.Required(CONF_TYPE): cv.enum(BUTTON_KINDS, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_CLIMATE_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_kind(config[CONF_TYPE]))
