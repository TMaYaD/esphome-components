#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "samsung_ac_ir.h"

namespace esphome {
namespace samsung_ac_ir {

// Sibling switch entity parented to a SamsungAcClimate. The parent owns
// the source of truth (cached aux bits, TX/RX, frame encoding); this
// entity is a thin pass-through to one of those bits, identified by
// `kind_`. Toggling it from HA invokes `parent_->set_flag(kind_, state)`,
// which transmits the new full frame and then publishes the confirmed
// state back via the parent's publish_all_aux_flags_() loop.
class SamsungAcSwitch : public switch_::Switch, public Component {
 public:
  void dump_config() override;

  void set_parent(SamsungAcClimate *parent) { this->parent_ = parent; }
  void set_kind(SamsungAcSwitchKind kind) { this->kind_ = kind; }
  SamsungAcSwitchKind get_kind() const { return this->kind_; }

 protected:
  // switch_::Switch override. Delegates to the parent climate.
  void write_state(bool state) override;

  SamsungAcClimate *parent_{nullptr};
  SamsungAcSwitchKind kind_{SamsungAcSwitchKind::Fast};
};

}  // namespace samsung_ac_ir
}  // namespace esphome
