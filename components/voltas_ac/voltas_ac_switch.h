#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"

#include "voltas_ac.h"  // for VoltasSwitchKind + VoltasClimate forward decl

namespace esphome {
namespace voltas_ac {

// One of these per exposed toggle (sleep / turbo / saver / lamp). The
// platform schema in switch.py wires set_parent() + set_kind() at codegen
// time; runtime writes flow back through the parent climate so that a
// toggle emits a full Voltas frame with the rest of the cached state
// preserved.
class VoltasSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(VoltasClimate *parent) { this->parent_ = parent; }
  void set_kind(VoltasSwitchKind kind) { this->kind_ = kind; }
  VoltasSwitchKind get_kind() const { return this->kind_; }

  void dump_config() override;

 protected:
  void write_state(bool state) override;

  VoltasClimate *parent_{nullptr};
  VoltasSwitchKind kind_{SLEEP};
};

}  // namespace voltas_ac
}  // namespace esphome
