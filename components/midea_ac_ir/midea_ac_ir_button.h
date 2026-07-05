#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/component.h"
#include "midea_ac_ir.h"

namespace esphome {
namespace midea_ac_ir {

// Sibling button entity parented to a MideaAcClimate. One press = one IR
// toggle word (see MideaAcButtonKind for the per-kind wire quirks). These
// are stateless on purpose: the AC tracks the resulting mode internally
// and never echoes it over IR, so a switch entity would be a lie.
class MideaAcButton : public button::Button, public Component {
 public:
  void dump_config() override;

  void set_parent(MideaAcClimate *parent) { this->parent_ = parent; }
  void set_kind(MideaAcButtonKind kind) { this->kind_ = kind; }
  MideaAcButtonKind get_kind() const { return this->kind_; }

 protected:
  void press_action() override;

  MideaAcClimate *parent_{nullptr};
  MideaAcButtonKind kind_{MideaAcButtonKind::Direct};
};

}  // namespace midea_ac_ir
}  // namespace esphome
