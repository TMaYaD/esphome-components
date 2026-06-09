#include "samsung_ac_ir.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace samsung_ac_ir {

static const char *const TAG = "samsung_ac_ir";

// IRSamsungAc's constructor takes a pin for its internal IRsend, but the pin
// is not claimed until begin() — which we never call. Any value is safe.
static constexpr uint16_t kUnusedIRPin = 255;

// Window after our own TX during which we ignore captured frames. Receivers
// in the same room hear the transmitter's own emission; without suppression
// every TX would re-trigger an RX-driven publish_state(). A full SAMSUNG_AC
// frame is ~220 ms (header + 2 sections of 56 bits + section footers), so
// 800 ms gives comfortable headroom for IR train + decoder latency.
static constexpr uint32_t kTxEchoSuppressMs = 800;

// SAMSUNG_AC protocol timing constants. Source of truth (verbatim):
//   https://github.com/crankyoldgit/IRremoteESP8266/blob/master/src/ir_Samsung.cpp
// Protocol marked "STABLE / Known to be working" upstream. If any of these
// shift, the cg.add_library version pin in climate.py is the canary.
//
// The 14-byte SAMSUNG_AC frame is sent as a header plus TWO 7-byte sections.
// Each section gets its own SectionMark/Space prefix and a bit-mark + section
// gap as the per-section footer. Bytes are emitted LSB-first within each byte.
static constexpr uint16_t kSamsungAcHdrMarkUs      = 690;
static constexpr uint16_t kSamsungAcHdrSpaceUs     = 17844;
static constexpr uint16_t kSamsungAcSectionMarkUs  = 3086;
static constexpr uint16_t kSamsungAcSectionSpaceUs = 8864;
static constexpr uint16_t kSamsungAcSectionGapUs   = 2886;
static constexpr uint16_t kSamsungAcBitMarkUs      = 586;
static constexpr uint16_t kSamsungAcOneSpaceUs     = 1432;
static constexpr uint16_t kSamsungAcZeroSpaceUs    = 436;
static constexpr uint32_t kSamsungAcCarrierHz      = 38000;
static constexpr uint16_t kSamsungAcSectionBytes   = 7;

void SamsungAcClimate::setup() {
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

void SamsungAcClimate::dump_config() {
  LOG_CLIMATE("", "Samsung AC IR", this);
  ESP_LOGCONFIG(TAG, "  Transmitter bound: %s",
                this->transmitter_ != nullptr ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Receiver bound:    %s",
                this->receiver_ != nullptr ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  TX echo suppress:  %u ms",
                static_cast<unsigned>(kTxEchoSuppressMs));
}

climate::ClimateTraits SamsungAcClimate::traits() {
  auto traits = climate::ClimateTraits();
  // ESPHome 2026.x infers current-temperature support from whether one has
  // ever been published. We never publish one (no on-board thermistor), so
  // nothing to declare.
  //
  // Modes: full SAMSUNG_AC set. Unlike voltas_ac (cool-only firmware), the
  // Samsung 14-byte frame's mode field genuinely covers all five, and most
  // post-2018 Samsung splits in the field heat as well as cool. Including
  // Auto and Heat is honest to the wire protocol — if a specific unit
  // doesn't heat, it'll just ignore the mode-4 frame.
  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_AUTO,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_DRY,
      climate::CLIMATE_MODE_FAN_ONLY,
  });
  traits.set_visual_min_temperature(static_cast<float>(kSamsungAcMinTemp));
  traits.set_visual_max_temperature(static_cast<float>(kSamsungAcMaxTemp));
  traits.set_visual_temperature_step(1.0f);
  // Skip kSamsungAcFanTurbo (7) and kSamsungAcFanAuto2 (6) in v1 — Turbo is
  // arguably better expressed as a switch entity alongside the other aux
  // toggles, and Auto2 only applies inside Mode=Auto. Add later if needed.
  traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_AUTO,
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH,
  });
  // Samsung exposes V, H, and Both as distinct values on the wire, so we
  // surface them all. The IRSamsungAc API splits them into two bools
  // (setSwing for V, setSwingH for H) — we map back across the boundary.
  traits.set_supported_swing_modes({
      climate::CLIMATE_SWING_OFF,
      climate::CLIMATE_SWING_VERTICAL,
      climate::CLIMATE_SWING_HORIZONTAL,
      climate::CLIMATE_SWING_BOTH,
  });
  return traits;
}

void SamsungAcClimate::control(const climate::ClimateCall &call) {
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

void SamsungAcClimate::apply_state_to_(IRSamsungAc &ac) const {
  ac.stateReset();

  if (this->mode == climate::CLIMATE_MODE_OFF) {
    ac.setPower(false);
  } else {
    ac.setPower(true);
    uint8_t s_mode = kSamsungAcCool;
    switch (this->mode) {
      case climate::CLIMATE_MODE_AUTO:     s_mode = kSamsungAcAuto; break;
      case climate::CLIMATE_MODE_COOL:     s_mode = kSamsungAcCool; break;
      case climate::CLIMATE_MODE_HEAT:     s_mode = kSamsungAcHeat; break;
      case climate::CLIMATE_MODE_DRY:      s_mode = kSamsungAcDry;  break;
      case climate::CLIMATE_MODE_FAN_ONLY: s_mode = kSamsungAcFan;  break;
      default:                             s_mode = kSamsungAcCool; break;
    }
    ac.setMode(s_mode);
  }

  float clamped = std::max<float>(static_cast<float>(kSamsungAcMinTemp),
                  std::min<float>(static_cast<float>(kSamsungAcMaxTemp),
                                  this->target_temperature));
  ac.setTemp(static_cast<uint8_t>(clamped));

  uint8_t s_fan = kSamsungAcFanAuto;
  if (this->fan_mode.has_value()) {
    switch (*this->fan_mode) {
      case climate::CLIMATE_FAN_LOW:    s_fan = kSamsungAcFanLow;  break;
      case climate::CLIMATE_FAN_MEDIUM: s_fan = kSamsungAcFanMed;  break;
      case climate::CLIMATE_FAN_HIGH:   s_fan = kSamsungAcFanHigh; break;
      default:                          s_fan = kSamsungAcFanAuto; break;
    }
  }
  ac.setFan(s_fan);

  const bool swing_v = (this->swing_mode == climate::CLIMATE_SWING_VERTICAL ||
                        this->swing_mode == climate::CLIMATE_SWING_BOTH);
  const bool swing_h = (this->swing_mode == climate::CLIMATE_SWING_HORIZONTAL ||
                        this->swing_mode == climate::CLIMATE_SWING_BOTH);
  ac.setSwing(swing_v);
  ac.setSwingH(swing_h);
}

void SamsungAcClimate::load_state_from_(IRSamsungAc &ac) {
  if (!ac.getPower()) {
    this->mode = climate::CLIMATE_MODE_OFF;
  } else {
    switch (ac.getMode()) {
      case kSamsungAcAuto: this->mode = climate::CLIMATE_MODE_AUTO;     break;
      case kSamsungAcCool: this->mode = climate::CLIMATE_MODE_COOL;     break;
      case kSamsungAcHeat: this->mode = climate::CLIMATE_MODE_HEAT;     break;
      case kSamsungAcDry:  this->mode = climate::CLIMATE_MODE_DRY;      break;
      case kSamsungAcFan:  this->mode = climate::CLIMATE_MODE_FAN_ONLY; break;
      default:             this->mode = climate::CLIMATE_MODE_COOL;     break;
    }
  }

  this->target_temperature = static_cast<float>(ac.getTemp());

  switch (ac.getFan()) {
    case kSamsungAcFanLow:   this->fan_mode = climate::CLIMATE_FAN_LOW;    break;
    case kSamsungAcFanMed:   this->fan_mode = climate::CLIMATE_FAN_MEDIUM; break;
    case kSamsungAcFanHigh:  this->fan_mode = climate::CLIMATE_FAN_HIGH;   break;
    // Auto2 and Turbo collapse to AUTO in v1's surface; an aux switch (a la
    // voltas Turbo) is the better home for Turbo when we expand the API.
    default:                 this->fan_mode = climate::CLIMATE_FAN_AUTO;   break;
  }

  const bool sv = ac.getSwing();
  const bool sh = ac.getSwingH();
  if (sv && sh)        this->swing_mode = climate::CLIMATE_SWING_BOTH;
  else if (sv)         this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
  else if (sh)         this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
  else                 this->swing_mode = climate::CLIMATE_SWING_OFF;
}

void SamsungAcClimate::transmit_state_() {
  if (this->transmitter_ == nullptr) {
    ESP_LOGW(TAG, "No transmitter bound — skipping TX");
    return;
  }

  IRSamsungAc ac(kUnusedIRPin);
  this->apply_state_to_(ac);
  uint8_t *state = ac.getRaw();  // also computes the checksum nibbles

  ESP_LOGD(TAG,
           "TX SAMSUNG_AC frame: "
           "%02X %02X %02X %02X %02X %02X %02X "
           "%02X %02X %02X %02X %02X %02X %02X",
           state[0], state[1], state[2], state[3], state[4], state[5], state[6],
           state[7], state[8], state[9], state[10], state[11], state[12], state[13]);

  auto call = this->transmitter_->transmit();
  auto *data = call.get_data();
  data->set_carrier_frequency(kSamsungAcCarrierHz);

  // Header.
  data->mark(kSamsungAcHdrMarkUs);
  data->space(kSamsungAcHdrSpaceUs);

  // Two 7-byte sections. Each: section header (3086us/8864us), then 56 bits
  // LSB-first, then bit-mark + section gap as footer. This matches
  // IRsend::sendSamsungAC()'s sendGeneric() invocation verbatim.
  for (uint16_t sec = 0; sec < kSamsungAcStateLength / kSamsungAcSectionBytes; sec++) {
    data->mark(kSamsungAcSectionMarkUs);
    data->space(kSamsungAcSectionSpaceUs);
    for (uint16_t byte_i = 0; byte_i < kSamsungAcSectionBytes; byte_i++) {
      const uint8_t b = state[sec * kSamsungAcSectionBytes + byte_i];
      for (uint8_t bit_i = 0; bit_i < 8; bit_i++) {  // LSB-first
        data->mark(kSamsungAcBitMarkUs);
        const bool bit = ((b >> bit_i) & 0x01) != 0;
        data->space(bit ? kSamsungAcOneSpaceUs : kSamsungAcZeroSpaceUs);
      }
    }
    data->mark(kSamsungAcBitMarkUs);
    data->space(kSamsungAcSectionGapUs);
  }

  call.perform();
  this->last_tx_ms_ = millis();
}

bool SamsungAcClimate::on_receive(remote_base::RemoteReceiveData data) {
  // Suppress echoes of our own TX. We're in IR range of ourselves.
  if (millis() - this->last_tx_ms_ < kTxEchoSuppressMs) {
    return false;
  }

  // Header.
  if (!data.expect_mark(kSamsungAcHdrMarkUs))   return false;
  if (!data.expect_space(kSamsungAcHdrSpaceUs)) return false;

  uint8_t state[kSamsungAcStateLength] = {0};
  for (uint16_t sec = 0; sec < kSamsungAcStateLength / kSamsungAcSectionBytes; sec++) {
    if (!data.expect_mark(kSamsungAcSectionMarkUs))   return false;
    if (!data.expect_space(kSamsungAcSectionSpaceUs)) return false;
    for (uint16_t byte_i = 0; byte_i < kSamsungAcSectionBytes; byte_i++) {
      for (uint8_t bit_i = 0; bit_i < 8; bit_i++) {  // LSB-first
        if (!data.expect_mark(kSamsungAcBitMarkUs))
          return false;
        if (data.expect_space(kSamsungAcOneSpaceUs)) {
          state[sec * kSamsungAcSectionBytes + byte_i] |=
              static_cast<uint8_t>(1u << bit_i);
        } else if (data.expect_space(kSamsungAcZeroSpaceUs)) {
          // bit is 0
        } else {
          return false;
        }
      }
    }
    if (!data.expect_mark(kSamsungAcBitMarkUs)) return false;
    // Section gap: don't fail on absence of the final section's gap — the
    // receiver may have eaten the trailing silence before delivering the
    // buffer. Required for non-final sections to keep us in sync though.
    const bool is_last = (sec + 1 == kSamsungAcStateLength / kSamsungAcSectionBytes);
    if (!data.expect_space(kSamsungAcSectionGapUs) && !is_last) {
      return false;
    }
  }

  if (!IRSamsungAc::validChecksum(state)) {
    ESP_LOGV(TAG, "RX: frame matched timings but checksum failed; ignoring");
    return false;
  }

  ESP_LOGD(TAG,
           "RX SAMSUNG_AC frame: "
           "%02X %02X %02X %02X %02X %02X %02X "
           "%02X %02X %02X %02X %02X %02X %02X",
           state[0], state[1], state[2], state[3], state[4], state[5], state[6],
           state[7], state[8], state[9], state[10], state[11], state[12], state[13]);

  IRSamsungAc ac(kUnusedIRPin);
  ac.setRaw(state);
  this->load_state_from_(ac);
  this->publish_state();
  return true;
}

}  // namespace samsung_ac_ir
}  // namespace esphome
