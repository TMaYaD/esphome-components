#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/components/remote_receiver/remote_receiver.h"
#include "esphome/components/remote_transmitter/remote_transmitter.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

// IRremoteESP8266 — used only as a Voltas state codec. The bridge GPIO is
// owned by ESPHome's remote_transmitter / remote_receiver, not by IRsend.
#include <ir_Voltas.h>

namespace esphome {
namespace voltas_ac {

// Identifies which 122LZF toggle bit a VoltasSwitch instance controls. The
// values are referenced from Python (cv.enum in switch.py) so the names
// here ARE the YAML config surface — don't rename without updating both.
enum VoltasSwitchKind : uint8_t {
  SLEEP = 0,  // Byte 2, bit 6
  TURBO = 1,  // Byte 2, bit 5
  ECONO = 2,  // Byte 3, bit 6  (YAML alias: "saver")
  LIGHT = 3,  // Byte 8, bit 5  (YAML alias: "lamp")
};

class VoltasSwitch;  // defined in voltas_ac_switch.h

class VoltasClimate : public climate::Climate,
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
  void set_horizontal_swing(bool enabled) {
    this->horizontal_swing_ = enabled;
  }

  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  // Called from switch.py to_code(). The kind picks one of the four flag
  // slots; subsequent RX-driven publishes will route decoded bits to this
  // switch via publish_flag_().
  void register_switch(VoltasSwitch *sw, VoltasSwitchKind kind);

  // Called from VoltasSwitch::write_state() when the user toggles a flag
  // entity. Updates the cached bit, emits a full Voltas frame, and echoes
  // the new state back to the bound switch.
  void set_flag(VoltasSwitchKind kind, bool state);

 protected:
  // RemoteReceiverListener: returns true when the frame was consumed by this
  // instance, false otherwise (so other listeners on the same receiver still
  // get a shot).
  bool on_receive(remote_base::RemoteReceiveData data) override;

  // Encode current climate state as a Voltas frame and push it through the
  // bound remote_transmitter.
  void transmit_state_();

  // Copy this->mode / target_temperature / fan_mode / swing_mode + the four
  // toggle flags into the IRVoltas state struct.
  void apply_state_to_(IRVoltas &ac) const;

  // Update this->mode / target_temperature / fan_mode / swing_mode + the
  // four toggle flags from a decoded IRVoltas state.
  void load_state_from_(IRVoltas &ac);

  // Look up the current cached bit for a kind.
  bool flag_value_(VoltasSwitchKind kind) const;

  // If a VoltasSwitch is registered for this kind, publish its current
  // cached value. Safe to call when no switch is registered (no-op).
  void publish_flag_(VoltasSwitchKind kind);

  // Convenience: publish all four flags. Called after every RX-driven
  // load_state_from_() so the switch entities track the AC's truth.
  void publish_all_flags_();

  remote_transmitter::RemoteTransmitterComponent *transmitter_{nullptr};
  remote_receiver::RemoteReceiverComponent *receiver_{nullptr};

  // millis() at the end of our last TX. Used to self-suppress RX of our
  // own emission (the receiver in the same room WILL hear it).
  uint32_t last_tx_ms_{0};

  // Horizontal swing (opt-in; see climate.py). The wire carries H state
  // only on explicit H-command frames (byte 0 = SwingHChange marker +
  // direction bit); steady frames say "no change". So we cache the last
  // commanded/observed H state and only emit the marker when it moves —
  // mirroring how the physical remote behaves.
  bool horizontal_swing_{false};
  bool swing_h_{false};
  bool swing_h_change_pending_{false};

  // Cached Voltas-protocol toggle bits. Source of truth for both TX
  // encoding and switch-entity state. Updated by both control() paths and
  // by RX decode.
  bool sleep_{false};
  bool turbo_{false};
  bool econo_{false};
  bool light_{false};

  // Optional pointers to the four switch entities that surface those bits.
  // Any of them may be nullptr — users can opt in to whichever toggles
  // they want to expose to HA.
  VoltasSwitch *sleep_switch_{nullptr};
  VoltasSwitch *turbo_switch_{nullptr};
  VoltasSwitch *econo_switch_{nullptr};
  VoltasSwitch *light_switch_{nullptr};
};

}  // namespace voltas_ac
}  // namespace esphome
