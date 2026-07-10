#include "tcl_ac_ir.h"
#include "tcl_ac_ir_switch.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace tcl_ac_ir {

static const char *const TAG = "tcl_ac_ir";

// IRTcl112Ac's constructor takes a pin for its internal IRsend, but the pin
// is not claimed until begin() — which we never call. Any value is safe.
static constexpr uint16_t kUnusedIRPin = 255;

// Window after our own TX during which we ignore captured frames. Receivers
// in the same room hear the transmitter's own emission; without suppression
// every TX would re-trigger an RX-driven publish_state().
static constexpr uint32_t kTxEchoSuppressMs = 600;

// GZ055BE1 / Teknopoint timing variant of the TCL112 protocol. These are
// upstream's constants from src/ir_Teknopoint.cpp — a TRANSLATION-UNIT
// local there, so we keep our own copy (same situation as voltas_ac).
// Verified against a live capture of the theater Voltas remote
// (hdr 3.66/1.55 ms, one ~1.19 ms, zero ~0.51 ms — all within receiver
// tolerance of these nominals). TCL's own TAC09CHSD timing differs enough
// (zero-space 325 us) that the two can't share decode windows.
static constexpr uint16_t kGzHdrMarkUs    = 3600;
static constexpr uint16_t kGzHdrSpaceUs   = 1600;
static constexpr uint16_t kGzBitMarkUs    = 477;
static constexpr uint16_t kGzOneSpaceUs   = 1200;
static constexpr uint16_t kGzZeroSpaceUs  = 530;
static constexpr uint32_t kGzCarrierHz    = 38000;

// Base frame for TX encoding: the theater Voltas remote's own steady state
// (power on, cool, 24C, fan auto, swing-V on), captured live. Preferred
// over IRTcl112Ac::stateReset() because the library's "known good" state
// carries byte-8 bit 6 (TimerIndicator) set — a bit this remote never
// sends. Starting from the remote's vocabulary keeps our frames
// byte-identical to what the AC is proven to accept; setters below adjust
// only the fields we manage. Byte 12 = 0x00 also means the isTcl model
// bit is clear, i.e. GZ055BE1 — matching the timing variant.
static const uint8_t kBaseState[kTcl112AcStateLength] = {
    0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x07,
    0x38, 0x00, 0x00, 0x00, 0x00, 0x7B};

void TclAcClimate::setup() {
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

void TclAcClimate::dump_config() {
  LOG_CLIMATE("", "TCL AC (GZ timing)", this);
  ESP_LOGCONFIG(TAG, "  Transmitter bound: %s",
                this->transmitter_ != nullptr ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Receiver bound:    %s",
                this->receiver_ != nullptr ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  TX echo suppress:  %u ms",
                static_cast<unsigned>(kTxEchoSuppressMs));
}

climate::ClimateTraits TclAcClimate::traits() {
  auto traits = climate::ClimateTraits();
  // The protocol has a Heat code (kTcl112AcHeat = 1), but the Voltas
  // splits we drive are cool-only — exposing Heat in the UI would let the
  // user issue a command the unit silently ignores (same reasoning as
  // voltas_ac). HEAT_COOL maps to the protocol's Auto (8), which the
  // remote's mode cycle carries.
  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_DRY,
      climate::CLIMATE_MODE_FAN_ONLY,
      climate::CLIMATE_MODE_HEAT_COOL,
  });
  traits.set_visual_min_temperature(kTcl112AcTempMin);
  traits.set_visual_max_temperature(kTcl112AcTempMax);
  // The wire supports 0.5C via the HalfDegree bit, but the physical remote
  // steps whole degrees; offering halves in the UI would let HA command
  // temps the panel can't display. Whole degrees only.
  traits.set_visual_temperature_step(1.0f);
  traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_AUTO,
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH,
  });
  traits.set_supported_swing_modes({
      climate::CLIMATE_SWING_OFF,
      climate::CLIMATE_SWING_VERTICAL,
  });
  return traits;
}

void TclAcClimate::control(const climate::ClimateCall &call) {
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

void TclAcClimate::register_switch(TclAcSwitch *sw, TclAcSwitchKind kind) {
  switch (kind) {
    case ECONO:  this->econo_switch_ = sw; break;
    case HEALTH: this->health_switch_ = sw; break;
    case LIGHT:  this->light_switch_ = sw; break;
    case TURBO:  this->turbo_switch_ = sw; break;
  }
}

bool TclAcClimate::flag_value_(TclAcSwitchKind kind) const {
  switch (kind) {
    case ECONO:  return this->econo_;
    case HEALTH: return this->health_;
    case LIGHT:  return this->light_;
    case TURBO:  return this->turbo_;
  }
  return false;
}

void TclAcClimate::set_flag(TclAcSwitchKind kind, bool state) {
  switch (kind) {
    case ECONO:  this->econo_ = state; break;
    case HEALTH: this->health_ = state; break;
    case LIGHT:  this->light_ = state; break;
    case TURBO:  this->turbo_ = state; break;
  }
  // Echo the change onto the wire and confirm to the switch entity. We
  // do NOT call publish_state() on the climate — none of its tracked
  // fields moved, and an unprompted climate publish triggers HA churn.
  this->transmit_state_();
  this->publish_flag_(kind);
}

void TclAcClimate::publish_flag_(TclAcSwitchKind kind) {
  TclAcSwitch *sw = nullptr;
  switch (kind) {
    case ECONO:  sw = this->econo_switch_; break;
    case HEALTH: sw = this->health_switch_; break;
    case LIGHT:  sw = this->light_switch_; break;
    case TURBO:  sw = this->turbo_switch_; break;
  }
  if (sw != nullptr) {
    sw->publish_state(this->flag_value_(kind));
  }
}

void TclAcClimate::publish_all_flags_() {
  this->publish_flag_(ECONO);
  this->publish_flag_(HEALTH);
  this->publish_flag_(LIGHT);
  this->publish_flag_(TURBO);
}

void TclAcClimate::apply_state_to_(IRTcl112Ac &ac) const {
  ac.setRaw(kBaseState);

  if (this->mode == climate::CLIMATE_MODE_OFF) {
    ac.setPower(false);
  } else {
    ac.setPower(true);
    uint8_t t_mode = kTcl112AcCool;
    switch (this->mode) {
      case climate::CLIMATE_MODE_COOL:      t_mode = kTcl112AcCool; break;
      case climate::CLIMATE_MODE_DRY:       t_mode = kTcl112AcDry;  break;
      case climate::CLIMATE_MODE_FAN_ONLY:  t_mode = kTcl112AcFan;  break;
      case climate::CLIMATE_MODE_HEAT_COOL: t_mode = kTcl112AcAuto; break;
      default:                              t_mode = kTcl112AcCool; break;
    }
    ac.setMode(t_mode);
  }

  ac.setTemp(std::max(kTcl112AcTempMin,
             std::min(kTcl112AcTempMax, this->target_temperature)));

  uint8_t t_fan = kTcl112AcFanAuto;
  if (this->fan_mode.has_value()) {
    switch (*this->fan_mode) {
      case climate::CLIMATE_FAN_LOW:    t_fan = kTcl112AcFanLow;  break;
      case climate::CLIMATE_FAN_MEDIUM: t_fan = kTcl112AcFanMed;  break;
      case climate::CLIMATE_FAN_HIGH:   t_fan = kTcl112AcFanHigh; break;
      default:                          t_fan = kTcl112AcFanAuto; break;
    }
  }
  ac.setFan(t_fan);

  // The remote's swing button toggles full sweep (0b111) only; the fixed
  // positions (highest..lowest) never appear on the wire from it, so we
  // don't surface them.
  ac.setSwingVertical(this->swing_mode == climate::CLIMATE_SWING_VERTICAL
                          ? kTcl112AcSwingVOn
                          : kTcl112AcSwingVOff);

  // Independent toggle bits. The wire protocol allows any combination;
  // we just mirror our cache verbatim.
  ac.setEcono(this->econo_);
  ac.setHealth(this->health_);
  ac.setLight(this->light_);
  ac.setTurbo(this->turbo_);
}

void TclAcClimate::load_state_from_(IRTcl112Ac &ac) {
  if (!ac.getPower()) {
    this->mode = climate::CLIMATE_MODE_OFF;
  } else {
    switch (ac.getMode()) {
      case kTcl112AcCool: this->mode = climate::CLIMATE_MODE_COOL;      break;
      case kTcl112AcDry:  this->mode = climate::CLIMATE_MODE_DRY;       break;
      case kTcl112AcFan:  this->mode = climate::CLIMATE_MODE_FAN_ONLY;  break;
      case kTcl112AcAuto: this->mode = climate::CLIMATE_MODE_HEAT_COOL; break;
      // Heat isn't in our traits (cool-only unit); if the wire ever says
      // Heat, surface the nearest thing we offer rather than lie with OFF.
      default:            this->mode = climate::CLIMATE_MODE_COOL;      break;
    }
  }

  this->target_temperature = ac.getTemp();

  switch (ac.getFan()) {
    case kTcl112AcFanLow:  this->fan_mode = climate::CLIMATE_FAN_LOW;    break;
    case kTcl112AcFanMed:  this->fan_mode = climate::CLIMATE_FAN_MEDIUM; break;
    case kTcl112AcFanHigh: this->fan_mode = climate::CLIMATE_FAN_HIGH;   break;
    default:               this->fan_mode = climate::CLIMATE_FAN_AUTO;   break;
  }

  this->swing_mode = ac.getSwingVertical() != kTcl112AcSwingVOff
                         ? climate::CLIMATE_SWING_VERTICAL
                         : climate::CLIMATE_SWING_OFF;

  this->econo_ = ac.getEcono();
  this->health_ = ac.getHealth();
  this->light_ = ac.getLight();
  this->turbo_ = ac.getTurbo();
}

void TclAcClimate::transmit_state_() {
  if (this->transmitter_ == nullptr) {
    ESP_LOGW(TAG, "No transmitter bound — skipping TX");
    return;
  }

  IRTcl112Ac ac(kUnusedIRPin);
  this->apply_state_to_(ac);
  uint8_t *state = ac.getRaw();  // also computes the checksum byte

  ESP_LOGD(TAG,
           "TX TCL112 frame: "
           "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
           state[0], state[1], state[2], state[3], state[4], state[5],
           state[6], state[7], state[8], state[9], state[10], state[11],
           state[12], state[13]);

  auto call = this->transmitter_->transmit();
  auto *data = call.get_data();
  data->set_carrier_frequency(kGzCarrierHz);

  // TCL112/GZ frame: header, then 112 bits LSB-first per byte, then a
  // trailing mark. Single frame, no repeat — matching the captured remote.
  data->mark(kGzHdrMarkUs);
  data->space(kGzHdrSpaceUs);
  for (uint16_t byte_i = 0; byte_i < kTcl112AcStateLength; byte_i++) {
    for (uint8_t bit_i = 0; bit_i < 8; bit_i++) {
      data->mark(kGzBitMarkUs);
      const bool bit = ((state[byte_i] >> bit_i) & 0x01) != 0;
      data->space(bit ? kGzOneSpaceUs : kGzZeroSpaceUs);
    }
  }
  data->mark(kGzBitMarkUs);

  call.perform();
  this->last_tx_ms_ = millis();
}

bool TclAcClimate::on_receive(remote_base::RemoteReceiveData data) {
  // Suppress echoes of our own TX. We're in IR range of ourselves.
  if (millis() - this->last_tx_ms_ < kTxEchoSuppressMs) {
    return false;
  }

  // Header, then 112 bits LSB-first. Any timing mismatch ⇒ not a TCL112/GZ
  // frame; return false so other listeners on this receiver get a turn.
  if (!data.expect_item(kGzHdrMarkUs, kGzHdrSpaceUs))
    return false;

  uint8_t state[kTcl112AcStateLength] = {0};
  for (uint16_t byte_i = 0; byte_i < kTcl112AcStateLength; byte_i++) {
    for (uint8_t bit_i = 0; bit_i < 8; bit_i++) {
      if (!data.expect_mark(kGzBitMarkUs))
        return false;
      if (data.expect_space(kGzOneSpaceUs)) {
        state[byte_i] |= static_cast<uint8_t>(1u << bit_i);
      } else if (data.expect_space(kGzZeroSpaceUs)) {
        // bit is 0
      } else {
        return false;
      }
    }
  }
  // Optional trailing mark: don't fail on absence, the gap may have eaten it.
  (void) data.expect_mark(kGzBitMarkUs);

  if (!IRTcl112Ac::validChecksum(state)) {
    ESP_LOGV(TAG, "RX: frame matched timings but checksum failed; ignoring");
    return false;
  }

  ESP_LOGD(TAG,
           "RX TCL112 frame: "
           "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
           state[0], state[1], state[2], state[3], state[4], state[5],
           state[6], state[7], state[8], state[9], state[10], state[11],
           state[12], state[13]);

  IRTcl112Ac ac(kUnusedIRPin);
  ac.setRaw(state);
  this->load_state_from_(ac);
  this->publish_state();
  this->publish_all_flags_();
  return true;
}

}  // namespace tcl_ac_ir
}  // namespace esphome
