#include "voltas_ac.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace voltas_ac {

static const char *const TAG = "voltas_ac";

// IRVoltas's constructor takes a pin for its internal IRsend, but the pin is
// not claimed until begin() — which we never call. Any value is safe.
static constexpr uint16_t kUnusedIRPin = 255;

// Window after our own TX during which we ignore captured frames. Receivers
// in the same room hear the transmitter's own emission; without suppression
// every TX would re-trigger an RX-driven publish_state().
static constexpr uint32_t kTxEchoSuppressMs = 600;

// Voltas protocol timing constants. These live in the library's TRANSLATION
// UNIT (src/ir_Voltas.cpp) — NOT its public header — because nobody using
// IRsend::sendVoltas() / IRrecv::decodeVoltas() needs them. We talk to
// ESPHome's remote_base directly instead of going through IRsend, so we
// need our own copy.
//
// Source of truth:
//   https://github.com/crankyoldgit/IRremoteESP8266/blob/master/src/ir_Voltas.cpp
// Protocol is marked "STABLE / Working on real device" in the upstream cpp.
// If any of these ever shift, the cg.add_library version pin in climate.py
// is the canary.
static constexpr uint16_t kVoltasBitMarkUs   = 1026;
static constexpr uint16_t kVoltasOneSpaceUs  = 2553;
static constexpr uint16_t kVoltasZeroSpaceUs = 554;
static constexpr uint32_t kVoltasCarrierHz   = 38000;

void VoltasClimate::setup() {
  if (this->receiver_ != nullptr) {
    this->receiver_->register_listener(this);
  }
  // Initial publish so HA shows *something* before any TX/RX activity.
  this->mode = climate::CLIMATE_MODE_OFF;
  this->target_temperature = 25.0f;
  this->fan_mode = climate::CLIMATE_FAN_AUTO;
  this->swing_mode = climate::CLIMATE_SWING_OFF;
  this->publish_state();
}

void VoltasClimate::dump_config() {
  LOG_CLIMATE("", "Voltas AC", this);
  ESP_LOGCONFIG(TAG, "  Transmitter bound: %s",
                this->transmitter_ != nullptr ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Receiver bound:    %s",
                this->receiver_ != nullptr ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  TX echo suppress:  %u ms",
                static_cast<unsigned>(kTxEchoSuppressMs));
}

climate::ClimateTraits VoltasClimate::traits() {
  auto traits = climate::ClimateTraits();
  // ESPHome 2026.x infers current-temperature support from whether one has
  // ever been published. We never publish one (no on-board thermistor here),
  // so nothing to declare.
  // The 122LZF *protocol* has a Heat code (kVoltasHeat = 0b0010), but the
  // split ACs we drive are cool-only — exposing Heat in the UI would let the
  // user issue a command the unit silently ignores. Drop it. If a future
  // Voltas device actually heats, we can flip this on per-instance via a
  // config flag.
  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_DRY,
      climate::CLIMATE_MODE_FAN_ONLY,
  });
  traits.set_visual_min_temperature(static_cast<float>(kVoltasMinTemp));
  traits.set_visual_max_temperature(static_cast<float>(kVoltasMaxTemp));
  traits.set_visual_temperature_step(1.0f);
  traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_AUTO,
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH,
  });
  // Model 1 (122LZF) supports vertical swing only; the enum's comment
  // explicitly says "No SwingH support".
  traits.set_supported_swing_modes({
      climate::CLIMATE_SWING_OFF,
      climate::CLIMATE_SWING_VERTICAL,
  });
  return traits;
}

void VoltasClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value())
    this->mode = *call.get_mode();
  if (call.get_target_temperature().has_value())
    this->target_temperature = *call.get_target_temperature();
  if (call.get_fan_mode().has_value())
    this->fan_mode = *call.get_fan_mode();
  if (call.get_swing_mode().has_value())
    this->swing_mode = *call.get_swing_mode();

  this->transmit_state_();
  this->publish_state();
}

void VoltasClimate::apply_state_to_(IRVoltas &ac) const {
  ac.stateReset();
  ac.setModel(kVoltas122LZF);

  if (this->mode == climate::CLIMATE_MODE_OFF) {
    ac.setPower(false);
  } else {
    ac.setPower(true);
    uint8_t v_mode = kVoltasCool;
    switch (this->mode) {
      case climate::CLIMATE_MODE_COOL:     v_mode = kVoltasCool; break;
      case climate::CLIMATE_MODE_HEAT:     v_mode = kVoltasHeat; break;
      case climate::CLIMATE_MODE_DRY:      v_mode = kVoltasDry; break;
      case climate::CLIMATE_MODE_FAN_ONLY: v_mode = kVoltasFan; break;
      default:                             v_mode = kVoltasCool; break;
    }
    ac.setMode(v_mode);
  }

  float clamped = std::max<float>(static_cast<float>(kVoltasMinTemp),
                  std::min<float>(static_cast<float>(kVoltasMaxTemp),
                                  this->target_temperature));
  ac.setTemp(static_cast<uint8_t>(clamped));

  uint8_t v_fan = kVoltasFanAuto;
  if (this->fan_mode.has_value()) {
    switch (*this->fan_mode) {
      case climate::CLIMATE_FAN_LOW:    v_fan = kVoltasFanLow;  break;
      case climate::CLIMATE_FAN_MEDIUM: v_fan = kVoltasFanMed;  break;
      case climate::CLIMATE_FAN_HIGH:   v_fan = kVoltasFanHigh; break;
      default:                          v_fan = kVoltasFanAuto; break;
    }
  }
  ac.setFan(v_fan);

  ac.setSwingV(this->swing_mode == climate::CLIMATE_SWING_VERTICAL);
}

void VoltasClimate::load_state_from_(IRVoltas &ac) {
  if (!ac.getPower()) {
    this->mode = climate::CLIMATE_MODE_OFF;
  } else {
    switch (ac.getMode()) {
      case kVoltasCool: this->mode = climate::CLIMATE_MODE_COOL;     break;
      case kVoltasHeat: this->mode = climate::CLIMATE_MODE_HEAT;     break;
      case kVoltasDry:  this->mode = climate::CLIMATE_MODE_DRY;      break;
      case kVoltasFan:  this->mode = climate::CLIMATE_MODE_FAN_ONLY; break;
      default:          this->mode = climate::CLIMATE_MODE_COOL;     break;
    }
  }

  this->target_temperature = static_cast<float>(ac.getTemp());

  switch (ac.getFan()) {
    case kVoltasFanLow:  this->fan_mode = climate::CLIMATE_FAN_LOW;    break;
    case kVoltasFanMed:  this->fan_mode = climate::CLIMATE_FAN_MEDIUM; break;
    case kVoltasFanHigh: this->fan_mode = climate::CLIMATE_FAN_HIGH;   break;
    default:             this->fan_mode = climate::CLIMATE_FAN_AUTO;   break;
  }

  this->swing_mode = ac.getSwingV() ? climate::CLIMATE_SWING_VERTICAL
                                    : climate::CLIMATE_SWING_OFF;
}

void VoltasClimate::transmit_state_() {
  if (this->transmitter_ == nullptr) {
    ESP_LOGW(TAG, "No transmitter bound — skipping TX");
    return;
  }

  IRVoltas ac(kUnusedIRPin);
  this->apply_state_to_(ac);
  uint8_t *state = ac.getRaw();  // also computes the checksum byte

  ESP_LOGD(TAG,
           "TX Voltas frame: "
           "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
           state[0], state[1], state[2], state[3], state[4],
           state[5], state[6], state[7], state[8], state[9]);

  auto call = this->transmitter_->transmit();
  auto *data = call.get_data();
  data->set_carrier_frequency(kVoltasCarrierHz);

  // Voltas frame: no header, then 80 bits MSB-first, then a trailing mark.
  for (uint16_t byte_i = 0; byte_i < kVoltasStateLength; byte_i++) {
    for (int bit_i = 7; bit_i >= 0; bit_i--) {
      data->mark(kVoltasBitMarkUs);
      const bool bit = ((state[byte_i] >> bit_i) & 0x01) != 0;
      data->space(bit ? kVoltasOneSpaceUs : kVoltasZeroSpaceUs);
    }
  }
  data->mark(kVoltasBitMarkUs);

  call.perform();
  this->last_tx_ms_ = millis();
}

bool VoltasClimate::on_receive(remote_base::RemoteReceiveData data) {
  // Suppress echoes of our own TX. We're in IR range of ourselves.
  if (millis() - this->last_tx_ms_ < kTxEchoSuppressMs) {
    return false;
  }

  // Pull 80 bits, MSB-first, no header. Any timing mismatch ⇒ not a Voltas
  // frame; return false so other listeners on this receiver still get a turn.
  uint8_t state[kVoltasStateLength] = {0};
  for (uint16_t byte_i = 0; byte_i < kVoltasStateLength; byte_i++) {
    for (int bit_i = 7; bit_i >= 0; bit_i--) {
      if (!data.expect_mark(kVoltasBitMarkUs))
        return false;
      if (data.expect_space(kVoltasOneSpaceUs)) {
        state[byte_i] |= static_cast<uint8_t>(1u << bit_i);
      } else if (data.expect_space(kVoltasZeroSpaceUs)) {
        // bit is 0
      } else {
        return false;
      }
    }
  }
  // Optional trailing mark: don't fail on absence, the gap may have eaten it.
  (void) data.expect_mark(kVoltasBitMarkUs);

  if (!IRVoltas::validChecksum(state)) {
    ESP_LOGV(TAG, "RX: frame matched timings but checksum failed; ignoring");
    return false;
  }

  ESP_LOGD(TAG,
           "RX Voltas frame: "
           "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
           state[0], state[1], state[2], state[3], state[4],
           state[5], state[6], state[7], state[8], state[9]);

  IRVoltas ac(kUnusedIRPin);
  ac.setRaw(state);
  this->load_state_from_(ac);
  this->publish_state();
  return true;
}

}  // namespace voltas_ac
}  // namespace esphome
