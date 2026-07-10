#include "midea_ac_ir.h"
#include "midea_ac_ir_switch.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace midea_ac_ir {

static const char *const TAG = "midea_ac_ir";

// ---------------------------------------------------------------------------
// Protocol constants.
//
// Timings are the upstream reference values
// (IRremoteESP8266 ir_Coolix.cpp, protocol marked stable there); our
// captures off an RG51Y5/E remote measured within ~6% of all of them, well
// inside the receiver's 25% tolerance.
static constexpr uint16_t kHdrMarkUs   = 4692;
static constexpr uint16_t kHdrSpaceUs  = 4416;
static constexpr uint16_t kBitMarkUs   = 552;
static constexpr uint16_t kOneSpaceUs  = 1656;
static constexpr uint16_t kZeroSpaceUs = 552;
static constexpr uint16_t kBlockGapUs  = 5244;
static constexpr uint32_t kCarrierHz   = 38000;

// Every constant below was captured from the RG51Y5/E remote and, where
// noted, TX-verified against the living-room unit in the mapping session
// (2026-07-05). The unit proved byte-exact reference-Coolix throughout.
//
// 24-bit words. On the wire each data byte is followed by its bitwise
// complement (6 bytes per block, MSB-first) and a normal command sends the
// whole block twice. The unit BEEPS at single-block sends but does not
// commit them — always send two blocks, except vane-step (see below).
static constexpr uint32_t kWordOff      = 0xB27BE0;  // TX-verified
static constexpr uint32_t kWordSleep    = 0xB2E003;  // captured ×2
static constexpr uint32_t kWordTurbo    = 0xB5F5A2;  // captured ×3
static constexpr uint32_t kWordLed      = 0xB5F5A5;  // captured ×3
static constexpr uint32_t kWordClean    = 0xB5F5AA;  // captured; panel "SC"
// Vane step. The remote emits this as a SINGLE block (captured: ~100 raw
// edges vs ~200 for everything else) — a double-block send would step the
// vane twice. 7 positions, direction reverses at the ends.
static constexpr uint32_t kWordVaneStep = 0xB20FE0;

// Eco uses the 48-bit Coolix48 extension: same block framing, but the third
// byte pair is NOT byte+complement — it carries the on/off payload. Distinct
// words per direction (remote-verified: user tracked which press was OFF),
// so eco is a real settable state, not a blind toggle.
static constexpr uint8_t kEcoOnWire[6]  = {0xB5, 0x4A, 0xF5, 0x0A, 0x82, 0x40};
static constexpr uint8_t kEcoOffWire[6] = {0xB5, 0x4A, 0xF5, 0x0A, 0x83, 0x40};

// State word layout (all field values capture-verified):
//   byte0 = 0xB2
//   byte1 = [fan:3][sensor:5]   sensor 0b11111 = "no sensor temp"
//   byte2 = [temp:4][mode:2][0:2]
static constexpr uint8_t kSensorIgnore = 0b11111;
static constexpr uint8_t kModeCool = 0b00;
static constexpr uint8_t kModeDry  = 0b01;
static constexpr uint8_t kModeAuto = 0b10;
// Fan-only mode has no code of its own: it's Dry's mode bits plus this
// sentinel in the temp nibble. (This is the exact encoding the stale
// IRremoteESP8266 bundled in the replaced Tasmota build got wrong.)
static constexpr uint8_t kFanModeTempSentinel = 0b1110;

static constexpr uint8_t kFanAuto  = 0b101;  // cool & fan-only modes
static constexpr uint8_t kFanAuto0 = 0b000;  // dry & auto modes force this
static constexpr uint8_t kFanMin   = 0b100;
static constexpr uint8_t kFanMed   = 0b010;
static constexpr uint8_t kFanMax   = 0b001;

static constexpr uint8_t kTempMin = 17;
static constexpr uint8_t kTempMax = 30;
// Gray-coded temp nibbles, index = °C - 17. TX-verified at 17, 24, 25, 30.
static constexpr uint8_t kTempCodes[14] = {
    0b0000, 0b0001, 0b0011, 0b0010, 0b0110, 0b0111, 0b0101,
    0b0100, 0b1100, 0b1101, 0b1001, 0b1000, 0b1010, 0b1011,
};

// Window after our own TX during which we ignore captured frames. A full
// double-block send is ~260 ms; the receiver hangs right next to our LED.
static constexpr uint32_t kTxEchoSuppressMs = 800;

// The unit ignores wake-up state frames for a while after the discrete off
// word (observed: ignored at 8 s, accepted at 120 s — likely compressor
// lockout). We can't fix that over IR; we log so the operator understands
// why a rapid off→on from HA may need a second attempt.
static constexpr uint32_t kPostOffLockoutHintMs = 15000;

static int8_t temp_code_to_celsius(uint8_t code) {
  for (uint8_t i = 0; i < 14; i++) {
    if (kTempCodes[i] == code)
      return static_cast<int8_t>(kTempMin + i);
  }
  return -1;
}

void MideaAcClimate::setup() {
  if (this->receiver_ != nullptr) {
    this->receiver_->register_listener(this);
  }
  // Initial publish so HA shows *something* before any TX/RX activity.
  this->mode = climate::CLIMATE_MODE_OFF;
  this->target_temperature = 24.0f;
  this->fan_mode = climate::CLIMATE_FAN_AUTO;
  this->publish_state();
}

void MideaAcClimate::dump_config() {
  LOG_CLIMATE("", "Midea AC IR (Coolix)", this);
  ESP_LOGCONFIG(TAG, "  Transmitter bound: %s",
                this->transmitter_ != nullptr ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Receiver bound:    %s",
                this->receiver_ != nullptr ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  TX echo suppress:  %u ms",
                static_cast<unsigned>(kTxEchoSuppressMs));
}

climate::ClimateTraits MideaAcClimate::traits() {
  auto traits = climate::ClimateTraits();
  // Heat exists in the Coolix mode field (0b11) and on the remote's mode
  // cycle, but this cool-only unit doesn't even ACK heat frames (no beep,
  // verified live). Exposing it would let HA command something the
  // hardware silently drops — same reasoning as voltas_ac.
  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_DRY,
      climate::CLIMATE_MODE_FAN_ONLY,
      climate::CLIMATE_MODE_AUTO,
  });
  traits.set_visual_min_temperature(static_cast<float>(kTempMin));
  traits.set_visual_max_temperature(static_cast<float>(kTempMax));
  traits.set_visual_temperature_step(1.0f);
  traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_AUTO,
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH,
  });
  // No swing modes: the vane has no absolute positions on the wire, only a
  // single-step toggle word. That's exposed as the `direct` button.
  return traits;
}

void MideaAcClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value())
    this->mode = *call.get_mode();
  if (call.get_target_temperature().has_value())
    this->target_temperature = *call.get_target_temperature();
  if (call.get_fan_mode().has_value())
    this->fan_mode = *call.get_fan_mode();

  this->transmit_state_();
  this->publish_state();
}

void MideaAcClimate::register_switch(MideaAcSwitch *sw, MideaAcSwitchKind kind) {
  if (kind == Eco) {
    this->eco_switch_ = sw;
  }
}

void MideaAcClimate::set_eco(bool state) {
  this->eco_ = state;
  this->transmit_wire_(state ? kEcoOnWire : kEcoOffWire, 2);
  if (this->eco_switch_ != nullptr) {
    this->eco_switch_->publish_state(state);
  }
}

void MideaAcClimate::tap_button(MideaAcButtonKind kind) {
  switch (kind) {
    case Direct:
      // Single block — the remote does the same, and a double-block send
      // would step the vane twice.
      this->transmit_word_(kWordVaneStep, 1);
      break;
    case Sleep:
      // The remote pairs sleep with a state frame (captured behavior:
      // state word interleaved between sleep words). Mirror it so the
      // unit gets the same train it expects from the OEM remote.
      this->transmit_state_();
      this->transmit_word_(kWordSleep, 2);
      break;
    case Turbo:
      this->transmit_word_(kWordTurbo, 2);
      break;
    case Led:
      this->transmit_word_(kWordLed, 2);
      break;
    case Clean:
      this->transmit_word_(kWordClean, 2);
      break;
  }
}

bool MideaAcClimate::encode_state_(uint8_t *b1, uint8_t *b2) const {
  if (this->mode == climate::CLIMATE_MODE_OFF)
    return false;

  uint8_t mode_bits = kModeCool;
  bool fan_only = false;
  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL: mode_bits = kModeCool; break;
    case climate::CLIMATE_MODE_DRY:  mode_bits = kModeDry;  break;
    case climate::CLIMATE_MODE_AUTO: mode_bits = kModeAuto; break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      mode_bits = kModeDry;  // fan-only = dry bits + temp sentinel
      fan_only = true;
      break;
    default: mode_bits = kModeCool; break;
  }

  uint8_t fan_bits = kFanAuto;
  if (this->fan_mode.has_value()) {
    switch (*this->fan_mode) {
      case climate::CLIMATE_FAN_LOW:    fan_bits = kFanMin; break;
      case climate::CLIMATE_FAN_MEDIUM: fan_bits = kFanMed; break;
      case climate::CLIMATE_FAN_HIGH:   fan_bits = kFanMax; break;
      default:                          fan_bits = kFanAuto; break;
    }
  }
  // Dry & Auto use the alternate "auto" fan code; explicit speeds aren't
  // honored there by the unit (the remote doesn't offer them either).
  if ((this->mode == climate::CLIMATE_MODE_DRY ||
       this->mode == climate::CLIMATE_MODE_AUTO) &&
      fan_bits == kFanAuto) {
    fan_bits = kFanAuto0;
  }

  uint8_t temp_code;
  if (fan_only) {
    temp_code = kFanModeTempSentinel;
  } else {
    float clamped = std::max<float>(kTempMin,
                    std::min<float>(kTempMax, this->target_temperature));
    temp_code = kTempCodes[static_cast<uint8_t>(clamped) - kTempMin];
  }

  *b1 = static_cast<uint8_t>((fan_bits << 5) | kSensorIgnore);
  *b2 = static_cast<uint8_t>((temp_code << 4) | (mode_bits << 2));
  return true;
}

void MideaAcClimate::transmit_state_() {
  uint8_t b1 = 0, b2 = 0;
  if (!this->encode_state_(&b1, &b2)) {
    this->transmit_word_(kWordOff, 2);
    ESP_LOGD(TAG, "TX off word: %06X", static_cast<unsigned>(kWordOff));
    ESP_LOGI(TAG,
             "Unit may ignore wake-up frames for a while after off "
             "(compressor lockout; observed ignored at 8s, accepted at 120s)");
    return;
  }
  const uint32_t word = (0xB2u << 16) | (static_cast<uint32_t>(b1) << 8) | b2;
  ESP_LOGD(TAG, "TX state word: %06X", static_cast<unsigned>(word));
  this->transmit_word_(word, 2);
}

void MideaAcClimate::transmit_word_(uint32_t word, uint8_t blocks) {
  const uint8_t d0 = (word >> 16) & 0xFF;
  const uint8_t d1 = (word >> 8) & 0xFF;
  const uint8_t d2 = word & 0xFF;
  const uint8_t wire[6] = {
      d0, static_cast<uint8_t>(~d0),
      d1, static_cast<uint8_t>(~d1),
      d2, static_cast<uint8_t>(~d2),
  };
  this->transmit_wire_(wire, blocks);
}

void MideaAcClimate::transmit_wire_(const uint8_t wire[6], uint8_t blocks) {
  if (this->transmitter_ == nullptr) {
    ESP_LOGW(TAG, "No transmitter bound — skipping TX");
    return;
  }

  auto call = this->transmitter_->transmit();
  auto *data = call.get_data();
  data->set_carrier_frequency(kCarrierHz);

  for (uint8_t blk = 0; blk < blocks; blk++) {
    data->mark(kHdrMarkUs);
    data->space(kHdrSpaceUs);
    for (uint8_t byte_i = 0; byte_i < 6; byte_i++) {
      for (int bit_i = 7; bit_i >= 0; bit_i--) {  // MSB-first
        data->mark(kBitMarkUs);
        const bool bit = ((wire[byte_i] >> bit_i) & 0x01) != 0;
        data->space(bit ? kOneSpaceUs : kZeroSpaceUs);
      }
    }
    data->mark(kBitMarkUs);
    if (blk + 1 < blocks) {
      data->space(kBlockGapUs);
    }
  }

  call.perform();
  this->last_tx_ms_ = millis();
}

bool MideaAcClimate::on_receive(remote_base::RemoteReceiveData data) {
  // Suppress echoes of our own TX. The receiver is centimetres away.
  if (millis() - this->last_tx_ms_ < kTxEchoSuppressMs) {
    return false;
  }

  if (!data.expect_mark(kHdrMarkUs))  return false;
  if (!data.expect_space(kHdrSpaceUs)) return false;

  uint8_t wire[6] = {0};
  for (uint8_t byte_i = 0; byte_i < 6; byte_i++) {
    for (int bit_i = 7; bit_i >= 0; bit_i--) {
      if (!data.expect_mark(kBitMarkUs))
        return false;
      if (data.expect_space(kOneSpaceUs)) {
        wire[byte_i] |= static_cast<uint8_t>(1u << bit_i);
      } else if (data.expect_space(kZeroSpaceUs)) {
        // bit is 0
      } else {
        return false;
      }
    }
  }
  // A second identical block follows (except vane-step); we decode block 1
  // and consume the frame — no need to walk the repeat.

  // Coolix48 (eco) first: its third byte pair violates byte+complement, so
  // it must be matched before the framing check below.
  if (std::equal(wire, wire + 6, kEcoOnWire) ||
      std::equal(wire, wire + 6, kEcoOffWire)) {
    this->eco_ = std::equal(wire, wire + 6, kEcoOnWire);
    ESP_LOGD(TAG, "RX eco %s (Coolix48)", ONOFF(this->eco_));
    if (this->eco_switch_ != nullptr) {
      this->eco_switch_->publish_state(this->eco_);
    }
    return true;
  }

  // Standard Coolix: every data byte is followed by its complement.
  if (wire[1] != static_cast<uint8_t>(~wire[0]) ||
      wire[3] != static_cast<uint8_t>(~wire[2]) ||
      wire[5] != static_cast<uint8_t>(~wire[4])) {
    return false;
  }
  const uint32_t word = (static_cast<uint32_t>(wire[0]) << 16) |
                        (static_cast<uint32_t>(wire[2]) << 8) | wire[4];

  switch (word) {
    case kWordOff:
      ESP_LOGD(TAG, "RX off word");
      this->mode = climate::CLIMATE_MODE_OFF;
      this->publish_state();
      return true;
    // Blind toggles: the AC tracks the resulting mode internally and never
    // echoes it, so there is no state to update here. Consuming them keeps
    // them from being mistaken for state words by anything downstream.
    case kWordSleep:
    case kWordTurbo:
    case kWordLed:
    case kWordClean:
    case kWordVaneStep:
      ESP_LOGD(TAG, "RX toggle word %06X (no tracked state)",
               static_cast<unsigned>(word));
      return true;
    default:
      break;
  }

  if (wire[0] != 0xB2) {
    return false;  // not a state word we understand
  }

  const uint8_t fan_bits = wire[2] >> 5;
  const uint8_t temp_code = wire[4] >> 4;
  const uint8_t mode_bits = (wire[4] >> 2) & 0x3;

  if (mode_bits == kModeDry && temp_code == kFanModeTempSentinel) {
    this->mode = climate::CLIMATE_MODE_FAN_ONLY;
  } else {
    switch (mode_bits) {
      case kModeCool: this->mode = climate::CLIMATE_MODE_COOL; break;
      case kModeDry:  this->mode = climate::CLIMATE_MODE_DRY;  break;
      case kModeAuto: this->mode = climate::CLIMATE_MODE_AUTO; break;
      default:
        // 0b11 is heat in the reference layout; this unit never sends it.
        ESP_LOGD(TAG, "RX unknown mode bits %u; ignoring frame", mode_bits);
        return false;
    }
    const int8_t celsius = temp_code_to_celsius(temp_code);
    if (celsius > 0) {
      this->target_temperature = static_cast<float>(celsius);
    }
  }

  switch (fan_bits) {
    case kFanMin: this->fan_mode = climate::CLIMATE_FAN_LOW;    break;
    case kFanMed: this->fan_mode = climate::CLIMATE_FAN_MEDIUM; break;
    case kFanMax: this->fan_mode = climate::CLIMATE_FAN_HIGH;   break;
    // kFanAuto, kFanAuto0, and the zone-follow/fixed codes all render as
    // AUTO — the unit is driving the fan itself in those states.
    default:      this->fan_mode = climate::CLIMATE_FAN_AUTO;   break;
  }

  ESP_LOGD(TAG, "RX state word %06X", static_cast<unsigned>(word));
  this->publish_state();
  return true;
}

}  // namespace midea_ac_ir
}  // namespace esphome
