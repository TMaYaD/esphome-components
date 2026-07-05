"""Midea AC IR (Coolix) climate platform.

Wires a ``climate.Climate`` instance to a ``remote_transmitter`` for TX
and (optionally) a ``remote_receiver`` for RX. All encode/decode is
in-component (see ``__init__.py`` for why there is no IRremoteESP8266
dependency here); TX/RX timings travel through ESPHome's
``remote_base`` so the bridge keeps exactly one owner of each GPIO and
composes with sibling listeners.

Receiver note: the Coolix inter-block gap is 5.2 ms and the header
space is 4.4 ms — the shared ``remote_receiver`` must have ``idle``
comfortably above those (the YTF bridge package uses 25 ms) or frames
arrive split and never decode.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, remote_transmitter, remote_receiver

from . import MideaAcClimate

DEPENDENCIES = ["remote_transmitter"]
AUTO_LOAD = ["remote_receiver"]

CONF_TRANSMITTER_ID = "transmitter_id"
CONF_RECEIVER_ID = "receiver_id"

CONFIG_SCHEMA = climate.climate_schema(MideaAcClimate).extend(
    {
        cv.Required(CONF_TRANSMITTER_ID): cv.use_id(
            remote_transmitter.RemoteTransmitterComponent
        ),
        cv.Optional(CONF_RECEIVER_ID): cv.use_id(
            remote_receiver.RemoteReceiverComponent
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)

    tx = await cg.get_variable(config[CONF_TRANSMITTER_ID])
    cg.add(var.set_transmitter(tx))

    if CONF_RECEIVER_ID in config:
        rx = await cg.get_variable(config[CONF_RECEIVER_ID])
        cg.add(var.set_receiver(rx))
