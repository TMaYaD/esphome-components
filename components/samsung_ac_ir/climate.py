"""Samsung AC IR climate platform.

Wires a ``climate.Climate`` instance to a ``remote_transmitter`` for TX
and (optionally) a ``remote_receiver`` for RX, and pulls in the
``crankyoldgit/IRremoteESP8266`` library — but only for state
encoding/decoding (``IRSamsungAc`` class). The IR timing emission and
capture both go through ESPHome's ``remote_base`` so the bridge keeps
exactly one owner of each GPIO.

Multiple ``climate: - platform: samsung_ac_ir`` entries on a single
device are intentionally supported: each instance is autonomous, shares
the device's TX/RX, and decides on its own whether an incoming frame is
addressed to it (by attempting the decode + checksum). That mirrors the
physical reality of multiple ACs in IR range — each one receives every
signal and decides independently whether to obey.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, remote_transmitter, remote_receiver
from esphome.core import CORE

from . import SamsungAcClimate

DEPENDENCIES = ["remote_transmitter"]
AUTO_LOAD = ["remote_receiver"]

CONF_TRANSMITTER_ID = "transmitter_id"
CONF_RECEIVER_ID = "receiver_id"

# climate.climate_schema() (ESPHome 2024.x+) already includes GenerateID for us.
CONFIG_SCHEMA = climate.climate_schema(SamsungAcClimate).extend(
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

    # Pull in IRremoteESP8266 for the SAMSUNG_AC codec. We use IRSamsungAc for
    # state field manipulation + checksum only; TX and RX timings travel
    # through ESPHome's remote_base, not IRsend/IRrecv. Same library + pin
    # voltas_ac already uses, so devices that load both pay the dep once.
    cg.add_library("crankyoldgit/IRremoteESP8266", "^2.8.6")

    # IRremoteESP8266's IRutils.cpp calls Serial.print() in a debug helper
    # guarded only by #ifdef ARDUINO. ESPHome strips the global Serial
    # object on ESP8266 by default (-DNO_GLOBAL_SERIAL, saves 32 bytes).
    # Opt back in so the library links.
    if CORE.is_esp8266:
        from esphome.components.esp8266.const import enable_serial
        enable_serial()
