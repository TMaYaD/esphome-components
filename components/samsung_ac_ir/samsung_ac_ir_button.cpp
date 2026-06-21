#include "samsung_ac_ir_button.h"
#include "esphome/core/log.h"

namespace esphome {
namespace samsung_ac_ir {

static const char *const TAG = "samsung_ac_ir.button";

static const char *kind_name(SamsungAcButtonKind k) {
  switch (k) {
    case BeepTap:  return "beep";
    case CleanTap: return "clean";
  }
  return "?";
}

void SamsungAcButton::dump_config() {
  LOG_BUTTON("", "Samsung AC IR Button", this);
  ESP_LOGCONFIG(TAG, "  Kind: %s (stored-mode toggle)", kind_name(this->kind_));
  ESP_LOGCONFIG(TAG, "  Parent climate bound: %s",
                this->parent_ != nullptr ? "yes" : "no");
}

void SamsungAcButton::press_action() {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "press on %s with no parent — dropping",
             kind_name(this->kind_));
    return;
  }
  this->parent_->tap_button(this->kind_);
}

}  // namespace samsung_ac_ir
}  // namespace esphome
