#pragma once

#ifdef USE_ESP32

#include "esphome/components/speaker/speaker.h"
#include "esphome/core/component.h"

#include <atomic>
#include <cstdint>

namespace esphome::eq_speaker {

static constexpr uint8_t NUM_BANDS = 6;
static constexpr uint8_t MAX_CHANNELS = 2;

/// Bộ lọc biquad dạng Direct Form II transposed, tính bằng float.
struct Biquad {
  float b0{1.0f}, b1{0.0f}, b2{0.0f}, a1{0.0f}, a2{0.0f};
  bool active{false};
};

struct BiquadState {
  float z1{0.0f}, z2{0.0f};
};

/// Loa trung gian: nhận âm thanh từ mixer, chạy EQ 6 dải (giống VBot) + lọc thông cao, rồi đẩy sang loa I2S.
class EqSpeaker final : public Component, public speaker::Speaker {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  size_t play(const uint8_t *data, size_t length, TickType_t ticks_to_wait) override;
  size_t play(const uint8_t *data, size_t length) override { return this->play(data, length, 0); }

  void start() override;
  void stop() override;
  void finish() override;

  void set_pause_state(bool pause_state) override { this->output_speaker_->set_pause_state(pause_state); }
  bool get_pause_state() const override { return this->output_speaker_->get_pause_state(); }
  bool has_buffered_data() const override;

  void set_mute_state(bool mute_state) override;
  bool get_mute_state() override { return this->output_speaker_->get_mute_state(); }
  void set_volume(float volume) override;
  float get_volume() override { return this->output_speaker_->get_volume(); }

  void set_output_speaker(speaker::Speaker *speaker) { this->output_speaker_ = speaker; }

  // Điều chỉnh khi đang chạy (gọi từ number/switch trong YAML). Áp dụng ngay ở khối âm thanh kế tiếp.
  void set_band_frequency(uint8_t band, float hz);
  void set_band_gain(uint8_t band, float db);
  float get_band_gain(uint8_t band) const { return band < NUM_BANDS ? this->gains_db_[band] : 0.0f; }
  void set_preamp(float db);
  void set_highpass_frequency(float hz);
  void set_enabled(bool enabled);
  bool is_enabled() const { return this->enabled_; }

 protected:
  void recompute_();
  void reset_states_();
  void process_(const uint8_t *in, uint8_t *out, size_t frames, uint8_t bytes_per_sample, uint8_t channels);
  float response_db_(float hz, uint32_t rate) const;

  speaker::Speaker *output_speaker_{nullptr};

  // Thông số người dùng (ghi từ vòng lặp chính)
  float freqs_hz_[NUM_BANDS]{100, 250, 630, 1600, 4000, 10000};
  float gains_db_[NUM_BANDS]{0, 0, 0, 0, 0, 0};
  float preamp_db_{0.0f};
  float highpass_hz_{0.0f};
  bool enabled_{true};
  std::atomic<bool> dirty_{true};
  std::atomic<bool> reset_requested_{false};

  // Trạng thái xử lý (chỉ dùng trong luồng mixer gọi play())
  Biquad filters_[NUM_BANDS + 1];  // [0] = lọc thông cao, [1..6] = các dải EQ
  BiquadState states_[NUM_BANDS + 1][MAX_CHANNELS];
  float input_gain_{1.0f};
  bool processing_{false};
  uint32_t coeff_rate_{0};

  // Dữ liệu đã xử lý nhưng loa I2S chưa nhận hết
  uint8_t *buffer_{nullptr};
  size_t buffer_size_{0};
  size_t pending_offset_{0};
  size_t pending_len_{0};

  uint32_t state_start_ms_{0};
};

}  // namespace esphome::eq_speaker

#endif
