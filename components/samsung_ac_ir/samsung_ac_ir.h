#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/components/remote_receiver/remote_receiver.h"
#include "esphome/components/remote_transmitter/remote_transmitter.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

// IRremoteESP8266 — used only as a SAMSUNG_AC state codec. The bridge GPIO is
// owned by ESPHome's remote_transmitter / remote_receiver, not by IRsend.
#include <ir_Samsung.h>

namespace esphome {
namespace samsung_ac_ir {

// Identifies which SAMSUNG_AC aux bit a SamsungAcSwitch instance controls.
// The values are referenced from Python (cv.enum in switch.py) so the
// names here ARE the YAML config surface — don't rename without updating
// both. Naming uses the Samsung remote's button labels where they differ
// from the wire-protocol names: Fast is what the codec calls Powerful.
//
// Mixed-case deliberately. IRremoteESP8266 transitive headers #define
// several shouty-case identifiers (DISPLAY among them); ALL_CAPS enum
// values get preprocessed into garbage and fail to compile. Mixed-case
// dodges that without scoping the enum.
enum SamsungAcSwitchKind : uint8_t {
  Fast    = 0,  // setPowerful — fan_special triplet (mutex with Breeze/Econo)
  Quiet   = 1,  // setQuiet
  Beep    = 2,  // setBeep — per-command audible instruction; AC honors directly
  Clean   = 3,  // setClean — per-command clean signal
  Display = 4,  // setDisplay — front-panel LED
};

// Identifies which SAMSUNG_AC stored-mode toggle a SamsungAcButton triggers
// on press. Named with `Tap` suffix to disambiguate from same-themed values
// in SamsungAcSwitchKind — both enums are unscoped and inject their values
// into this namespace, so bare `Beep` / `Clean` would collide. YAML still
// uses the short forms ("beep" / "clean") via the BUTTON_KINDS mapping in
// button.py.
//
// The wire recipe: build a normal SAMSUNG_AC frame for current climate
// state, set the corresponding Toggle bit (setBeep(true) or setClean(true)),
// patch byte 2 bit 4 to mark the frame as "remote-issued," then recompute
// the section checksum and emit. Frame captures (see commit history)
// confirm byte 2 bit 4 is what unlocks the AC's stored-mode toggle path —
// without it, the same bit pattern only drives per-command audible.
enum SamsungAcButtonKind : uint8_t {
  BeepTap  = 0,  // toggle AC's stored beep mode (physical-remote equivalent)
  CleanTap = 1,  // toggle AC's stored clean cycle (assumed-symmetric to Beep)
};

class SamsungAcSwitch;  // defined in samsung_ac_ir_switch.h
class SamsungAcButton;  // defined in samsung_ac_ir_button.h

class SamsungAcClimate : public climate::Climate,
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

  // Called from switch.py to_code(). The kind picks one of the five flag
  // slots; subsequent RX-driven publishes will route decoded bits to this
  // switch via publish_all_aux_flags_().
  void register_switch(SamsungAcSwitch *sw, SamsungAcSwitchKind kind);

  // Called from SamsungAcSwitch::write_state() when the user toggles a flag
  // entity. Updates the cached bit, emits a full SAMSUNG_AC frame, and
  // publishes ALL five aux states back (the publish-everything-on-TX design
  // chosen to stay correct under the fan_special mutex even if Breeze/Econo
  // are added as switches later).
  void set_flag(SamsungAcSwitchKind kind, bool state);

  // Emit a one-shot SAMSUNG_AC frame that toggles the AC's stored mode for
  // the requested aux feature (Beep or Clean). Climate state on the frame
  // mirrors our cache, so this does not change temp/mode/fan/swing. The
  // outgoing frame matches the structural recipe captured from physical
  // remote Beep presses (the relevant Toggle bit set AND byte 2 bit 4
  // set), which the AC interprets as "toggle the stored mode for this
  // feature." Called by SamsungAcButton::press_action().
  void tap_button(SamsungAcButtonKind kind);

 protected:
  // RemoteReceiverListener: returns true when the frame was consumed by this
  // instance, false otherwise (so other listeners on the same receiver still
  // get a shot).
  bool on_receive(remote_base::RemoteReceiveData data) override;

  // Encode current climate state as a SAMSUNG_AC frame and push it through
  // the bound remote_transmitter.
  void transmit_state_();

  // Common emission path: takes an already-built 14-byte SAMSUNG_AC frame
  // (with checksums valid) and emits it via the bound remote_transmitter,
  // matching the IRsend::sendSamsungAC() timing sequence. Updates
  // last_tx_ms_ so RX echo-suppression covers this emission.
  void emit_raw_frame_(const uint8_t *state);

  // Copy this->mode / target_temperature / fan_mode / swing_mode + the five
  // aux flags into the IRSamsungAc state struct.
  void apply_state_to_(IRSamsungAc &ac) const;

  // Update this->mode / target_temperature / fan_mode / swing_mode + the
  // five aux flags from a decoded IRSamsungAc state.
  void load_state_from_(IRSamsungAc &ac);

  // Look up the current cached bit for a kind.
  bool flag_value_(SamsungAcSwitchKind kind) const;

  // If a SamsungAcSwitch is registered for this kind, publish its current
  // cached value. Safe to call when no switch is registered (no-op).
  void publish_flag_(SamsungAcSwitchKind kind);

  // Convenience: publish all five aux flags. Called after every TX and
  // after every RX-driven load_state_from_() so the switch entities track
  // the AC's truth even when the wire-level mutex moves bits the user
  // didn't touch.
  void publish_all_aux_flags_();

  remote_transmitter::RemoteTransmitterComponent *transmitter_{nullptr};
  remote_receiver::RemoteReceiverComponent *receiver_{nullptr};

  // millis() at the end of our last TX. Used to self-suppress RX of our
  // own emission (the receiver in the same room WILL hear it).
  uint32_t last_tx_ms_{0};

  // Cached SAMSUNG_AC aux toggle bits. Source of truth for both TX
  // encoding and switch-entity state. Updated by both control() / set_flag()
  // and by RX decode.
  bool fast_{false};
  bool quiet_{false};
  bool beep_{false};
  bool clean_{false};
  bool display_{true};  // display defaults ON on most Samsung firmwares

  // Optional pointers to the five switch entities that surface those bits.
  // Any of them may be nullptr — users can opt in to whichever toggles
  // they want to expose to HA.
  SamsungAcSwitch *fast_switch_{nullptr};
  SamsungAcSwitch *quiet_switch_{nullptr};
  SamsungAcSwitch *beep_switch_{nullptr};
  SamsungAcSwitch *clean_switch_{nullptr};
  SamsungAcSwitch *display_switch_{nullptr};
};

}  // namespace samsung_ac_ir
}  // namespace esphome
