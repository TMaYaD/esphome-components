#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "midea_ac_ir.h"

namespace esphome {
namespace midea_ac_ir {

// Sibling switch entity parented to a MideaAcClimate. Only Eco today —
// the one aux feature this remote generation encodes with distinct on/off
// words (Coolix48), making a settable, RX-syncable state possible.
class MideaAcSwitch : public switch_::Switch, public Component {
 public:
  void dump_config() override;

  void set_parent(MideaAcClimate *parent) { this->parent_ = parent; }
  void set_kind(MideaAcSwitchKind kind) { this->kind_ = kind; }
  MideaAcSwitchKind get_kind() const { return this->kind_; }

 protected:
  void write_state(bool state) override;

  MideaAcClimate *parent_{nullptr};
  MideaAcSwitchKind kind_{MideaAcSwitchKind::Eco};
};

}  // namespace midea_ac_ir
}  // namespace esphome
