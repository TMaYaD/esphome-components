#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/components/remote_receiver/remote_receiver.h"
#include "esphome/components/remote_transmitter/remote_transmitter.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace midea_ac_ir {

// Identifies which one-shot toggle a MideaAcButton fires. Values are
// referenced from Python (cv.enum in button.py) so the names here ARE the
// YAML config surface — don't rename without updating both. Mixed-case per
// the samsung_ac_ir lesson: SHOUTY names risk colliding with #defines from
// transitive SDK headers.
enum MideaAcButtonKind : uint8_t {
  Direct = 0,  // vane step, 0xB20FE0 — MUST go out as a single block
  Sleep  = 1,  // 0xB2E003 — remote pairs it with a state frame; so do we
  Turbo  = 2,  // 0xB5F5A2
  Led    = 3,  // 0xB5F5A5 — panel display on/off
  Clean  = 4,  // 0xB5F5AA — starts self-clean ("SC" on the panel)
};

// Stateful toggles surfaced as switches. Only Eco today: this remote
// generation gives Eco distinct Coolix48 on/off words, so unlike the blind
// toggles above we can force either state and sync from RX.
enum MideaAcSwitchKind : uint8_t {
  Eco = 0,
};

class MideaAcButton;  // defined in midea_ac_ir_button.h
class MideaAcSwitch;  // defined in midea_ac_ir_switch.h

class MideaAcClimate : public climate::Climate,
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

  // Called from switch.py to_code() so RX-decoded eco words can publish
  // the right entity.
  void register_switch(MideaAcSwitch *sw, MideaAcSwitchKind kind);

  // Called from MideaAcButton::press_action(). Emits the matching one-shot
  // toggle word (single-block for Direct, double otherwise; Sleep is
  // preceded by a state frame, mirroring captured remote behavior).
  void tap_button(MideaAcButtonKind kind);

  // Called from MideaAcSwitch::write_state(). Emits the Coolix48 eco
  // on/off word and confirms the state back to the entity.
  void set_eco(bool state);

 protected:
  // RemoteReceiverListener: returns true when the frame was consumed by
  // this instance, false otherwise (so other listeners on the same receiver
  // still get a shot).
  bool on_receive(remote_base::RemoteReceiveData data) override;

  // Encode current climate state as a Coolix state word and transmit it
  // (double block). mode==OFF sends the discrete off word instead — the
  // 24-bit state word has no power bit; any state frame wakes the unit.
  void transmit_state_();

  // Build the 24-bit state word (B2 fan/sensor temp/mode) from the current
  // climate fields. Returns false when mode is OFF (no state word exists).
  bool encode_state_(uint8_t *b1, uint8_t *b2) const;

  // Emit `blocks` Coolix blocks of the given 6 wire bytes through the bound
  // remote_transmitter. For 24-bit words the caller passes byte+complement
  // pairs; Coolix48 (eco) passes its literal 6 bytes.
  void transmit_wire_(const uint8_t wire[6], uint8_t blocks);

  // Convenience: expand a 24-bit word into byte+complement wire framing and
  // transmit it.
  void transmit_word_(uint32_t word, uint8_t blocks);

  remote_transmitter::RemoteTransmitterComponent *transmitter_{nullptr};
  remote_receiver::RemoteReceiverComponent *receiver_{nullptr};

  // millis() at the end of our last TX. Used to self-suppress RX of our
  // own emission (the receiver hangs centimetres from the transmitter).
  uint32_t last_tx_ms_{0};

  // Cached eco state. Source of truth for the switch entity; updated by
  // both set_eco() and RX decode of the Coolix48 words.
  bool eco_{false};

  MideaAcSwitch *eco_switch_{nullptr};
};

}  // namespace midea_ac_ir
}  // namespace esphome
