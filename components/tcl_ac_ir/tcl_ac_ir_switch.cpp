#include "tcl_ac_ir_switch.h"
#include "tcl_ac_ir.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tcl_ac_ir {

static const char *const TAG = "tcl_ac_ir.switch";

static const char *kind_name(TclAcSwitchKind k) {
  switch (k) {
    case ECONO:  return "econo";
    case HEALTH: return "health";
    case LIGHT:  return "light";
    case TURBO:  return "turbo";
  }
  return "?";
}

void TclAcSwitch::dump_config() {
  LOG_SWITCH("", "TCL AC Switch", this);
  ESP_LOGCONFIG(TAG, "  Kind: %s", kind_name(this->kind_));
  ESP_LOGCONFIG(TAG, "  Parent climate bound: %s",
                this->parent_ != nullptr ? "yes" : "no");
}

void TclAcSwitch::write_state(bool state) {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "write_state(%s) on %s with no parent — dropping",
             ONOFF(state), kind_name(this->kind_));
    return;
  }
  // The parent updates its cached bit, emits a TCL112 frame, and calls
  // publish_state() back on us so HA sees the confirmed state. We do NOT
  // call publish_state(state) here — the source of truth is the parent's
  // cache, and we want the entity to flip only when TX actually happens.
  this->parent_->set_flag(this->kind_, state);
}

}  // namespace tcl_ac_ir
}  // namespace esphome
