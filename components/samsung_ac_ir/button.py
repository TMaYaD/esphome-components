"""Samsung AC IR stored-mode toggle actions exposed as button entities.

Companion to the switch platform — same `type:` vocabulary (``beep`` and
``clean``), different job. The switches drive per-command audible
behavior on HA-issued frames (the AC honors the ``BeepToggle`` /
``CleanToggle`` bit value directly on each receipt). The buttons here
emit a one-shot remote-equivalent frame that flips the AC's *stored*
mode for that feature — same effect as pressing Beep or Clean on the
physical remote.

The two surfaces target genuinely independent levers:
- Switch ON  -> outgoing climate frames carry the bit = 1 -> AC chirps
  on each HA command. Doesn't change AC's stored mode.
- Switch OFF -> outgoing climate frames carry the bit = 0 -> AC silent
  on each HA command. Doesn't change AC's stored mode.
- Button press -> emits a single SAMSUNG_AC frame with the bit = 1 AND
  the remote-issued watermark (byte 2 bit 4). The AC interprets this as
  "user pressed the Beep/Clean button" and toggles its stored mode. The
  AC's stored mode is what governs whether the AC chirps on commands
  issued by OTHER senders (physical remote, other IR devices in range).

Both switch and button can coexist on the same climate id; they target
different observable AC behaviors.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_TYPE

from . import SamsungAcButton, SamsungAcButtonKind, SamsungAcClimate

# No DEPENDENCIES: ``samsung_ac_ir`` is a platform name, not a top-level
# component. The ``cv.use_id(SamsungAcClimate)`` below is what enforces
# that a matching climate instance exists in the same config.

CONF_CLIMATE_ID = "climate_id"

# Lower-case YAML aliases mapped to the C++ enum values declared in
# samsung_ac_ir.h. Internal enum names carry the ``Tap`` suffix to avoid
# collision with SamsungAcSwitchKind::Beep / Clean at namespace scope.
# The YAML surface keeps the short names to match the remote's button
# labels.
BUTTON_KINDS = {
    "beep":  SamsungAcButtonKind.BeepTap,
    "clean": SamsungAcButtonKind.CleanTap,
}

CONFIG_SCHEMA = (
    button.button_schema(SamsungAcButton)
    .extend(
        {
            cv.Required(CONF_CLIMATE_ID): cv.use_id(SamsungAcClimate),
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
