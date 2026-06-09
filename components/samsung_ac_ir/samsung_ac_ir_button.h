#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/component.h"
#include "samsung_ac_ir.h"

namespace esphome {
namespace samsung_ac_ir {

// Sibling button entity parented to a SamsungAcClimate. The parent owns
// the source of truth (climate state cache, TX path, frame encoding); this
// entity is a thin pass-through. Pressing the button calls
// parent_->tap_button(kind_), which emits a single one-shot SAMSUNG_AC
// frame mirroring the structural recipe of a physical-remote Beep/Clean
// press — toggling the AC's stored mode for that feature.
class SamsungAcButton : public button::Button, public Component {
 public:
  void dump_config() override;

  void set_parent(SamsungAcClimate *parent) { this->parent_ = parent; }
  void set_kind(SamsungAcButtonKind kind) { this->kind_ = kind; }
  SamsungAcButtonKind get_kind() const { return this->kind_; }

 protected:
  // button::Button override. Delegates to the parent climate.
  void press_action() override;

  SamsungAcClimate *parent_{nullptr};
  SamsungAcButtonKind kind_{SamsungAcButtonKind::BeepTap};
};

}  // namespace samsung_ac_ir
}  // namespace esphome
