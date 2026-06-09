"""Samsung AC IR ESPHome external component.

Published from ``github://tmayad/esphome-components``. Device configs
consume it via ``external_components`` and can drop any local override.

The climate platform lives in ``climate.py`` and is referenced as
``climate: - platform: samsung_ac_ir`` from device configs.

Naming note: this is the **IR**-controlled Samsung A/C codec. There is a
separate, unrelated community component ``samsung_ac`` (omerfaruk-aran)
that speaks the wired F1/F2 NASA/NonNASA serial bus — they're entirely
different control surfaces, so we suffix ``_ir`` to keep them
distinguishable if a future device ever needs both.

First-pass scope: mode / target temp / fan / swing on the 14-byte
standard SAMSUNG_AC frame. Auxiliary toggles (Quiet, Powerful, Breeze /
WindFree, Econo, Display, Clean, Beep, Sleep) and the 21-byte extended
frame (on/off/sleep timers, explicit power-change packets) are not
exposed yet; they'll land as a follow-up sibling switch platform the
same way Sleep/Turbo/Saver/Lamp did for ``voltas_ac``.
"""

import esphome.codegen as cg
from esphome.components import climate

CODEOWNERS = ["@tmayad"]

samsung_ac_ir_ns = cg.esphome_ns.namespace("samsung_ac_ir")
SamsungAcClimate = samsung_ac_ir_ns.class_(
    "SamsungAcClimate", climate.Climate, cg.Component
)
