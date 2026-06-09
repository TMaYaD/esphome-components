#include "voltas_ac_switch.h"
#include "voltas_ac.h"
#include "esphome/core/log.h"

namespace esphome {
namespace voltas_ac {

static const char *const TAG = "voltas_ac.switch";

static const char *kind_name(VoltasSwitchKind k) {
  switch (k) {
    case SLEEP: return "sleep";
    case TURBO: return "turbo";
    case ECONO: return "saver";
    case LIGHT: return "lamp";
  }
  return "?";
}

void VoltasSwitch::dump_config() {
  LOG_SWITCH("", "Voltas AC Switch", this);
  ESP_LOGCONFIG(TAG, "  Kind: %s", kind_name(this->kind_));
  ESP_LOGCONFIG(TAG, "  Parent climate bound: %s",
                this->parent_ != nullptr ? "yes" : "no");
}

void VoltasSwitch::write_state(bool state) {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "write_state(%s) on %s with no parent — dropping",
             ONOFF(state), kind_name(this->kind_));
    return;
  }
  // The parent updates its cached bit, emits a Voltas frame, and calls
  // publish_state() back on us so HA sees the confirmed state. We do NOT
  // call publish_state(state) here — the source of truth is the parent's
  // cache, and we want the entity to flip only when TX actually happens.
  this->parent_->set_flag(this->kind_, state);
}

}  // namespace voltas_ac
}  // namespace esphome
