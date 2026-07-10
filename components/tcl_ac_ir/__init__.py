"""TCL 112-bit AC ESPHome external component (GZ/Teknopoint timing).

Published from ``github://tmayad/esphome-components``. Device configs
consume it via ``external_components``.

Built for Voltas-branded inverter splits that are rebadged TCL hardware:
they speak the TCL112AC 14-byte frame format, but at the GZ055BE1 /
Teknopoint timing variant (3.6 ms header, ~530 us zero-space) rather
than TCL's nominal timing — which is also why ESPHome's stock ``tcl112``
climate can't decode their remotes at any safe receiver tolerance.

The climate platform lives in ``climate.py`` and is referenced as
``climate: - platform: tcl_ac_ir`` from device configs.

The switch platform lives in ``switch.py`` and exposes the protocol's
independent toggle bits — Econo, Health, Light (display) and Turbo — as
individual switches parented to a climate instance. They share the
climate's TX/RX path; toggling one emits a full frame with the current
climate state plus the modified flag.
"""

import esphome.codegen as cg
from esphome.components import climate, switch

CODEOWNERS = ["@tmayad"]

tcl_ac_ir_ns = cg.esphome_ns.namespace("tcl_ac_ir")
TclAcClimate = tcl_ac_ir_ns.class_(
    "TclAcClimate", climate.Climate, cg.Component
)
TclAcSwitch = tcl_ac_ir_ns.class_(
    "TclAcSwitch", switch.Switch, cg.Component
)
TclAcSwitchKind = tcl_ac_ir_ns.enum("TclAcSwitchKind")
