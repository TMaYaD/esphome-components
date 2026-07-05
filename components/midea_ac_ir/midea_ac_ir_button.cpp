#include "midea_ac_ir_button.h"
#include "esphome/core/log.h"

namespace esphome {
namespace midea_ac_ir {

static const char *const TAG = "midea_ac_ir.button";

static const char *kind_name(MideaAcButtonKind k) {
  switch (k) {
    case Direct: return "direct (vane step)";
    case Sleep:  return "sleep";
    case Turbo:  return "turbo";
    case Led:    return "led";
    case Clean:  return "clean";
  }
  return "?";
}

void MideaAcButton::dump_config() {
  LOG_BUTTON("", "Midea AC IR Button", this);
  ESP_LOGCONFIG(TAG, "  Kind: %s", kind_name(this->kind_));
  ESP_LOGCONFIG(TAG, "  Parent climate bound: %s",
                this->parent_ != nullptr ? "yes" : "no");
}

void MideaAcButton::press_action() {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "press on %s with no parent — dropping",
             kind_name(this->kind_));
    return;
  }
  this->parent_->tap_button(this->kind_);
}

}  // namespace midea_ac_ir
}  // namespace esphome
