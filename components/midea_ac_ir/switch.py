"""Midea AC IR stateful toggles exposed as switch entities.

Currently only ``eco``. Unlike the blind toggles in ``button.py``, Eco
on the RG51-generation remote uses the 48-bit Coolix48 extension with
DISTINCT on and off words (captured & remote-verified):

    on:  0xB54AF50A8240
    off: 0xB54AF50A8340

Distinct words mean we can force either state — a real switch, no
toggle ambiguity. RX of either word (user pressing Eco on the physical
remote) syncs the entity, so this one stays honest bidirectionally.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_TYPE

from . import MideaAcClimate, MideaAcSwitch, MideaAcSwitchKind

CONF_CLIMATE_ID = "climate_id"

SWITCH_KINDS = {
    "eco": MideaAcSwitchKind.Eco,
}

CONFIG_SCHEMA = (
    switch.switch_schema(MideaAcSwitch)
    .extend(
        {
            cv.Required(CONF_CLIMATE_ID): cv.use_id(MideaAcClimate),
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
    cg.add(parent.register_switch(var, config[CONF_TYPE]))
