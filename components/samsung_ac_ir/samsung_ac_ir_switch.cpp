#include "samsung_ac_ir_switch.h"
#include "esphome/core/log.h"

namespace esphome {
namespace samsung_ac_ir {

static const char *const TAG = "samsung_ac_ir.switch";

static const char *kind_name(SamsungAcSwitchKind k) {
  switch (k) {
    case Fast:    return "fast";
    case Quiet:   return "quiet";
    case Beep:    return "beep";
    case Clean:   return "clean";
    case Display: return "display";
  }
  return "?";
}

void SamsungAcSwitch::dump_config() {
  LOG_SWITCH("", "Samsung AC IR Switch", this);
  ESP_LOGCONFIG(TAG, "  Kind: %s", kind_name(this->kind_));
  ESP_LOGCONFIG(TAG, "  Parent climate bound: %s",
                this->parent_ != nullptr ? "yes" : "no");
}

void SamsungAcSwitch::write_state(bool state) {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "write_state(%s) on %s with no parent — dropping",
             ONOFF(state), kind_name(this->kind_));
    return;
  }
  // Parent updates its cached bit, emits a SAMSUNG_AC frame, and publishes
  // ALL five aux states back so HA sees the confirmed value (including any
  // mutex-induced clears that would happen on the wire if Breeze/Econo were
  // ever exposed alongside Fast). We do NOT publish_state(state) here — the
  // source of truth is the parent's cache.
  this->parent_->set_flag(this->kind_, state);
}

}  // namespace samsung_ac_ir
}  // namespace esphome
