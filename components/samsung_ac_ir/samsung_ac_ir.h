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

 protected:
  // RemoteReceiverListener: returns true when the frame was consumed by this
  // instance, false otherwise (so other listeners on the same receiver still
  // get a shot).
  bool on_receive(remote_base::RemoteReceiveData data) override;

  // Encode current climate state as a SAMSUNG_AC frame and push it through
  // the bound remote_transmitter.
  void transmit_state_();

  // Copy this->mode / target_temperature / fan_mode / swing_mode into the
  // IRSamsungAc state struct.
  void apply_state_to_(IRSamsungAc &ac) const;

  // Update this->mode / target_temperature / fan_mode / swing_mode from a
  // decoded IRSamsungAc state.
  void load_state_from_(IRSamsungAc &ac);

  remote_transmitter::RemoteTransmitterComponent *transmitter_{nullptr};
  remote_receiver::RemoteReceiverComponent *receiver_{nullptr};

  // millis() at the end of our last TX. Used to self-suppress RX of our
  // own emission (the receiver in the same room WILL hear it).
  uint32_t last_tx_ms_{0};
};

}  // namespace samsung_ac_ir
}  // namespace esphome
