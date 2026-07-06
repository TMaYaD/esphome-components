"""Voltas AC climate platform.

Wires a ``climate.Climate`` instance to a ``remote_transmitter`` for TX
and (optionally) a ``remote_receiver`` for RX, and pulls in the
``crankyoldgit/IRremoteESP8266`` library — but only for state
encoding/decoding (``IRVoltas`` class). The IR timing emission and
capture both go through ESPHome's ``remote_base`` so the bridge keeps
exactly one owner of each GPIO.

Multiple ``climate: - platform: voltas_ac`` entries on a single device
are intentionally supported: each instance is autonomous, shares the
device's TX/RX, and decides on its own whether an incoming frame is
addressed to it (by trying its decoder list and checksumming). That
mirrors the physical reality of multiple ACs in IR range — each one
receives every signal and decides independently whether to obey.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, remote_transmitter, remote_receiver
from esphome.core import CORE

from . import VoltasClimate

DEPENDENCIES = ["remote_transmitter"]
AUTO_LOAD = ["remote_receiver"]

CONF_TRANSMITTER_ID = "transmitter_id"
CONF_RECEIVER_ID = "receiver_id"
# Opt-in: some Voltas remotes/units in the 122LZF timing family carry a
# horizontal-swing command in byte 0 (SwingHChange marker + direction
# bit) — upstream IRVoltas actually fingerprints those as a *different*
# model and no-ops setSwingH() for 122LZF. Verified live on the guest
# unit; the master's AC-133B never moves byte 0. Off by default so
# existing devices keep byte 0 at the 0x33 no-change signature.
CONF_HORIZONTAL_SWING = "horizontal_swing"

# climate.climate_schema() (ESPHome 2024.x+) already includes GenerateID for us.
CONFIG_SCHEMA = climate.climate_schema(VoltasClimate).extend(
    {
        cv.Required(CONF_TRANSMITTER_ID): cv.use_id(
            remote_transmitter.RemoteTransmitterComponent
        ),
        cv.Optional(CONF_RECEIVER_ID): cv.use_id(
            remote_receiver.RemoteReceiverComponent
        ),
        cv.Optional(CONF_HORIZONTAL_SWING, default=False): cv.boolean,
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

    if config[CONF_HORIZONTAL_SWING]:
        cg.add(var.set_horizontal_swing(True))

    # Pull in IRremoteESP8266 for the Voltas codec. We use IRVoltas for
    # state field manipulation + checksum only; TX and RX timings travel
    # through ESPHome's remote_base, not IRsend/IRrecv.
    cg.add_library("crankyoldgit/IRremoteESP8266", "^2.8.6")

    # IRremoteESP8266's IRutils.cpp calls Serial.print() in a debug helper
    # guarded only by #ifdef ARDUINO. ESPHome strips the global Serial
    # object on ESP8266 by default (-DNO_GLOBAL_SERIAL, saves 32 bytes).
    # Opt back in so the library links.
    if CORE.is_esp8266:
        from esphome.components.esp8266.const import enable_serial
        enable_serial()
