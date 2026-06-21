#include "samsung_ac_ir.h"
#include "samsung_ac_ir_switch.h"
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

  // Aux toggle bits. The fan_special triplet (Powerful / Breeze / Econo) is
  // internally mutex'd by the codec — we only ever set Powerful, so the
  // other two stay cleared by stateReset() above and remain off on the wire.
  ac.setPowerful(this->fast_);
  ac.setQuiet(this->quiet_);
  ac.setBeep(this->beep_);
  ac.setClean(this->clean_);
  ac.setDisplay(this->display_);
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

  this->fast_    = ac.getPowerful();
  this->quiet_   = ac.getQuiet();
  this->display_ = ac.getDisplay();
  // Deliberately NOT reading BeepToggle or CleanToggle10/11 back from
  // decoded remote frames. Wire-level captures (see commit message)
  // confirm:
  //   - The remote sends BeepToggle=1 only on the Beep button press
  //     itself (same frame for ON-direction and OFF-direction press),
  //     and BeepToggle=0 on every other command (temp, mode, etc.).
  //   - Same pulse pattern for CleanToggle10/11.
  //   - The AC's stored beep/clean mode lives entirely in the AC and
  //     is never echoed on the wire.
  // If we trusted getBeep()/getClean() on RX, every non-aux remote press
  // would flicker HA's switch to OFF — and the matching ON pulse from
  // an Beep-press would mean we re-enter the "beep is on" state for
  // ONE frame even when the user pressed Beep with intent to turn it
  // OFF. There's no way to recover the AC's true mode over IR, so the
  // honest model is: the switch tracks HA's intent for OUR TXs only.
}

void SamsungAcClimate::transmit_state_() {
  IRSamsungAc ac(kUnusedIRPin);
  this->apply_state_to_(ac);
  uint8_t *state = ac.getRaw();  // also computes the checksum nibbles

  ESP_LOGD(TAG,
           "TX SAMSUNG_AC frame: "
           "%02X %02X %02X %02X %02X %02X %02X "
           "%02X %02X %02X %02X %02X %02X %02X",
           state[0], state[1], state[2], state[3], state[4], state[5], state[6],
           state[7], state[8], state[9], state[10], state[11], state[12], state[13]);

  this->emit_raw_frame_(state);
}

void SamsungAcClimate::tap_button(SamsungAcButtonKind kind) {
  IRSamsungAc ac(kUnusedIRPin);
  this->apply_state_to_(ac);

  // Override the cached aux bit so this one frame carries the toggle
  // request regardless of switch state. apply_state_to_() already wrote
  // the cached value; we just stomp on it.
  const char *kind_str = "?";
  switch (kind) {
    case BeepTap:  ac.setBeep(true);  kind_str = "beep";  break;
    case CleanTap: ac.setClean(true); kind_str = "clean"; break;
  }

  uint8_t *state = ac.getRaw();  // first checksum pass (byte 2 hi nibble = 0)

  // Set the "remote-issued" watermark bit at byte 2 high nibble, bit 4
  // (mask 0x10). Empirically verified — without this bit, the frame only
  // drives per-command audible; with it, the AC interprets the matching
  // Toggle bit as a stored-mode toggle, exactly mirroring a physical
  // remote button press. The struct in ir_Samsung.h marks this nibble as
  // anonymous padding so we can't address it through bit-fields; patching
  // the raw byte is the only path.
  state[2] |= 0x10;

  // checksum() only writes byte 2's LOW nibble (Sum1Upper), so the bit we
  // just set survives the recompute. Re-run getRaw() to refresh the
  // section-1 checksum to match the modified byte 2.
  ac.getRaw();

  ESP_LOGD(TAG,
           "TX SAMSUNG_AC %s-toggle: "
           "%02X %02X %02X %02X %02X %02X %02X "
           "%02X %02X %02X %02X %02X %02X %02X",
           kind_str,
           state[0], state[1], state[2], state[3], state[4], state[5], state[6],
           state[7], state[8], state[9], state[10], state[11], state[12], state[13]);

  this->emit_raw_frame_(state);
}

void SamsungAcClimate::emit_raw_frame_(const uint8_t *state) {
  if (this->transmitter_ == nullptr) {
    ESP_LOGW(TAG, "No transmitter bound — skipping TX");
    return;
  }

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
  this->publish_all_aux_flags_();
  return true;
}

void SamsungAcClimate::register_switch(SamsungAcSwitch *sw,
                                       SamsungAcSwitchKind kind) {
  switch (kind) {
    case Fast:    this->fast_switch_    = sw; break;
    case Quiet:   this->quiet_switch_   = sw; break;
    case Beep:    this->beep_switch_    = sw; break;
    case Clean:   this->clean_switch_   = sw; break;
    case Display: this->display_switch_ = sw; break;
  }
}

bool SamsungAcClimate::flag_value_(SamsungAcSwitchKind kind) const {
  switch (kind) {
    case Fast:    return this->fast_;
    case Quiet:   return this->quiet_;
    case Beep:    return this->beep_;
    case Clean:   return this->clean_;
    case Display: return this->display_;
  }
  return false;
}

void SamsungAcClimate::set_flag(SamsungAcSwitchKind kind, bool state) {
  switch (kind) {
    case Fast:    this->fast_    = state; break;
    case Quiet:   this->quiet_   = state; break;
    case Beep:    this->beep_    = state; break;
    case Clean:   this->clean_   = state; break;
    case Display: this->display_ = state; break;
  }
  // Echo onto the wire, then publish ALL aux states back. We do NOT call
  // publish_state() on the climate — none of its tracked fields moved, and
  // an unprompted climate publish causes HA churn. We publish all five aux
  // entities (not just the touched one) so any future addition of Breeze /
  // Econo to the user-facing surface stays correct under the codec's
  // fan_special mutex.
  this->transmit_state_();
  this->publish_all_aux_flags_();
}

void SamsungAcClimate::publish_flag_(SamsungAcSwitchKind kind) {
  SamsungAcSwitch *sw = nullptr;
  switch (kind) {
    case Fast:    sw = this->fast_switch_;    break;
    case Quiet:   sw = this->quiet_switch_;   break;
    case Beep:    sw = this->beep_switch_;    break;
    case Clean:   sw = this->clean_switch_;   break;
    case Display: sw = this->display_switch_; break;
  }
  if (sw != nullptr) {
    sw->publish_state(this->flag_value_(kind));
  }
}

void SamsungAcClimate::publish_all_aux_flags_() {
  this->publish_flag_(Fast);
  this->publish_flag_(Quiet);
  this->publish_flag_(Beep);
  this->publish_flag_(Clean);
  this->publish_flag_(Display);
}

}  // namespace samsung_ac_ir
}  // namespace esphome
