#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"

#include "tcl_ac_ir.h"  // for TclAcSwitchKind + TclAcClimate forward decl

namespace esphome {
namespace tcl_ac_ir {

// One of these per exposed toggle (econo / health / light / turbo). The
// platform schema in switch.py wires set_parent() + set_kind() at codegen
// time; runtime writes flow back through the parent climate so that a
// toggle emits a full TCL112 frame with the rest of the cached state
// preserved.
class TclAcSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(TclAcClimate *parent) { this->parent_ = parent; }
  void set_kind(TclAcSwitchKind kind) { this->kind_ = kind; }
  TclAcSwitchKind get_kind() const { return this->kind_; }

  void dump_config() override;

 protected:
  void write_state(bool state) override;

  TclAcClimate *parent_{nullptr};
  TclAcSwitchKind kind_{ECONO};
};

}  // namespace tcl_ac_ir
}  // namespace esphome
