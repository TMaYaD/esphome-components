#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/components/remote_receiver/remote_receiver.h"
#include "esphome/components/remote_transmitter/remote_transmitter.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

// IRremoteESP8266 — used only as a TCL112 state codec. The bridge GPIO is
// owned by ESPHome's remote_transmitter / remote_receiver, not by IRsend.
#include <ir_Tcl.h>

namespace esphome {
namespace tcl_ac_ir {

// Identifies which TCL112 toggle bit a TclAcSwitch instance controls. The
// values are referenced from Python (cv.enum in switch.py) so the names
// here ARE the YAML config surface — don't rename without updating both.
// Quiet is deliberately absent: on the wire it lives in special type-2
// messages with tri-state library handling, and the Voltas/TCL remotes
// we mirror never emit those.
enum TclAcSwitchKind : uint8_t {
  ECONO = 0,   // Byte 5, bit 7
  HEALTH = 1,  // Byte 6, bit 4
  LIGHT = 2,   // Byte 5, bit 6 (display)
  TURBO = 3,   // Byte 6, bit 5
};

class TclAcSwitch;  // defined in tcl_ac_ir_switch.h

class TclAcClimate : public climate::Climate,
                     public Component,
                     public remote_base::RemoteReceiverListener {
 public:
  void setup() override;
  void dump_config() override;

  void set_transmitter(remote_transmitter::RemoteTransmitterComponent *t) {
    this->transmitter_ = t;
  }
  void set_receiver(remote_receiver::RemoteReceiverComponent *r) {
    this->receiver_ = r;
  }

  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  // Called from switch.py to_code(). The kind picks one of the four flag
  // slots; subsequent RX-driven publishes will route decoded bits to this
  // switch via publish_flag_().
  void register_switch(TclAcSwitch *sw, TclAcSwitchKind kind);

  // Called from TclAcSwitch::write_state() when the user toggles a flag
  // entity. Updates the cached bit, emits a full TCL112 frame, and echoes
  // the new state back to the bound switch.
  void set_flag(TclAcSwitchKind kind, bool state);

 protected:
  // RemoteReceiverListener: returns true when the frame was consumed by this
  // instance, false otherwise (so other listeners on the same receiver still
  // get a shot).
  bool on_receive(remote_base::RemoteReceiveData data) override;

  // Encode current climate state as a TCL112 frame and push it through the
  // bound remote_transmitter.
  void transmit_state_();

  // Copy this->mode / target_temperature / fan_mode / swing_mode + the four
  // toggle flags into the IRTcl112Ac state struct.
  void apply_state_to_(IRTcl112Ac &ac) const;

  // Update this->mode / target_temperature / fan_mode / swing_mode + the
  // four toggle flags from a decoded IRTcl112Ac state.
  void load_state_from_(IRTcl112Ac &ac);

  // Look up the current cached bit for a kind.
  bool flag_value_(TclAcSwitchKind kind) const;

  // If a TclAcSwitch is registered for this kind, publish its current
  // cached value. Safe to call when no switch is registered (no-op).
  void publish_flag_(TclAcSwitchKind kind);

  // Convenience: publish all four flags. Called after every RX-driven
  // load_state_from_() so the switch entities track the AC's truth.
  void publish_all_flags_();

  remote_transmitter::RemoteTransmitterComponent *transmitter_{nullptr};
  remote_receiver::RemoteReceiverComponent *receiver_{nullptr};

  // millis() at the end of our last TX. Used to self-suppress RX of our
  // own emission (the receiver in the same room WILL hear it).
  uint32_t last_tx_ms_{0};

  // Cached TCL112 toggle bits. Source of truth for both TX encoding and
  // switch-entity state. Updated by both control() paths and by RX decode.
  bool econo_{false};
  bool health_{false};
  bool light_{false};
  bool turbo_{false};

  // Optional pointers to the four switch entities that surface those bits.
  // Any of them may be nullptr — users can opt in to whichever toggles
  // they want to expose to HA.
  TclAcSwitch *econo_switch_{nullptr};
  TclAcSwitch *health_switch_{nullptr};
  TclAcSwitch *light_switch_{nullptr};
  TclAcSwitch *turbo_switch_{nullptr};
};

}  // namespace tcl_ac_ir
}  // namespace esphome
