#include "midea_ac_ir_switch.h"
#include "esphome/core/log.h"

namespace esphome {
namespace midea_ac_ir {

static const char *const TAG = "midea_ac_ir.switch";

void MideaAcSwitch::dump_config() {
  LOG_SWITCH("", "Midea AC IR Switch", this);
  ESP_LOGCONFIG(TAG, "  Kind: eco");
  ESP_LOGCONFIG(TAG, "  Parent climate bound: %s",
                this->parent_ != nullptr ? "yes" : "no");
}

void MideaAcSwitch::write_state(bool state) {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "write_state(%s) on eco with no parent — dropping",
             ONOFF(state));
    return;
  }
  // Parent transmits the Coolix48 word and publishes the confirmed state
  // back to us. We don't publish here — single source of truth.
  this->parent_->set_eco(state);
}

}  // namespace midea_ac_ir
}  // namespace esphome
