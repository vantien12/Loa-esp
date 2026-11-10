#include "eq_speaker.h"

#ifdef USE_ESP32

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cmath>
#include <cstring>

namespace esphome::eq_speaker {

static const char *const TAG = "eq_speaker";
static constexpr size_t BUFFER_BYTES = 8192;
static constexpr uint32_t STATE_TRANSITION_TIMEOUT_MS = 5000;
static constexpr float PI_F = 3.14159265358979f;

enum FilterKind : uint8_t { LOW_SHELF = 0, PEAKING = 1, HIGH_SHELF = 2, HIGH_PASS = 3 };

// Công thức giống hệt EQ của VBot (Audio EQ Cookbook, Q = 0.707)
static Biquad make_filter(FilterKind kind, float frequency, float db, uint32_t rate) {
  Biquad f;
  if (rate == 0 || frequency <= 0.0f || frequency >= rate * 0.49f) {
    return f;
  }
  if (kind != HIGH_PASS && std::fabs(db) < 0.01f) {
    return f;
  }
  const double A = std::pow(10.0, db / 40.0);
  const double w = 2.0 * M_PI * frequency / rate;
  const double c = std::cos(w), s = std::sin(w);
  double b0, b1, b2, a0, a1, a2;
  if (kind == HIGH_PASS) {
    const double alpha = s / (2.0 * 0.7071067811865476);
    b0 = (1 + c) / 2;
    b1 = -(1 + c);
    b2 = (1 + c) / 2;
    a0 = 1 + alpha;
    a1 = -2 * c;
    a2 = 1 - alpha;
  } else if (kind == PEAKING) {
    const double alpha = s / (2.0 * 0.7071067811865476);
    b0 = 1 + alpha * A;
    b1 = -2 * c;
    b2 = 1 - alpha * A;
    a0 = 1 + alpha / A;
    a1 = -2 * c;
    a2 = 1 - alpha / A;
  } else {
    const double alpha = s / std::sqrt(2.0), t = 2 * std::sqrt(A) * alpha;
    if (kind == LOW_SHELF) {
      b0 = A * ((A + 1) - (A - 1) * c + t);
      b1 = 2 * A * ((A - 1) - (A + 1) * c);
      b2 = A * ((A + 1) - (A - 1) * c - t);
      a0 = (A + 1) + (A - 1) * c + t;
      a1 = -2 * ((A - 1) + (A + 1) * c);
      a2 = (A + 1) + (A - 1) * c - t;
    } else {
      b0 = A * ((A + 1) + (A - 1) * c + t);
      b1 = -2 * A * ((A - 1) + (A + 1) * c);
      b2 = A * ((A + 1) + (A - 1) * c - t);
      a0 = (A + 1) - (A - 1) * c + t;
      a1 = 2 * ((A - 1) - (A + 1) * c);
      a2 = (A + 1) - (A - 1) * c - t;
    }
  }
  f.b0 = b0 / a0;
  f.b1 = b1 / a0;
  f.b2 = b2 / a0;
  f.a1 = a1 / a0;
  f.a2 = a2 / a0;
  f.active = true;
  return f;
}

// Biên độ (dB) của một biquad tại tần số hz
static float biquad_db(const Biquad &f, float hz, uint32_t rate) {
  if (!f.active) {
    return 0.0f;
  }
  const double w = 2.0 * M_PI * hz / rate;
  const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
  const double nr = f.b0 + f.b1 * c1 + f.b2 * c2, ni = -(f.b1 * s1 + f.b2 * s2);
  const double dr = 1.0 + f.a1 * c1 + f.a2 * c2, di = -(f.a1 * s1 + f.a2 * s2);
  const double mag2 = (nr * nr + ni * ni) / (dr * dr + di * di);
  return 10.0 * std::log10(mag2 > 1e-20 ? mag2 : 1e-20);
}

float EqSpeaker::response_db_(float hz, uint32_t rate) const {
  float total = 0.0f;
  for (const auto &f : this->filters_) {
    total += biquad_db(f, hz, rate);
  }
  return total;
}

void EqSpeaker::setup() {
  RAMAllocator<uint8_t> allocator;
  this->buffer_ = allocator.allocate(BUFFER_BYTES);
  if (this->buffer_ == nullptr) {
    ESP_LOGE(TAG, "Không cấp phát được bộ đệm");
    this->mark_failed();
    return;
  }
  this->buffer_size_ = BUFFER_BYTES;

  this->output_speaker_->add_audio_output_callback([this](uint32_t new_frames, int64_t write_timestamp) {
    this->audio_output_callback_(new_frames, write_timestamp);
  });
}

void EqSpeaker::dump_config() {
  ESP_LOGCONFIG(TAG, "EQ Speaker:");
  ESP_LOGCONFIG(TAG, "  Bật: %s", YESNO(this->enabled_));
  for (uint8_t i = 0; i < NUM_BANDS; i++) {
    ESP_LOGCONFIG(TAG, "  %5.0f Hz: %+.1f dB", this->freqs_hz_[i], this->gains_db_[i]);
  }
  ESP_LOGCONFIG(TAG, "  Preamp: %+.1f dB | Lọc thông cao: %.0f Hz", this->preamp_db_, this->highpass_hz_);
}

void EqSpeaker::set_band_frequency(uint8_t band, float hz) {
  if (band < NUM_BANDS) {
    this->freqs_hz_[band] = hz;
    this->dirty_.store(true);
  }
}

void EqSpeaker::set_band_gain(uint8_t band, float db) {
  if (band < NUM_BANDS) {
    this->gains_db_[band] = clamp(db, -12.0f, 12.0f);
    this->dirty_.store(true);
    ESP_LOGD(TAG, "Dải %.0f Hz = %+.1f dB", this->freqs_hz_[band], this->gains_db_[band]);
  }
}

void EqSpeaker::set_preamp(float db) {
  this->preamp_db_ = clamp(db, -12.0f, 12.0f);
  this->dirty_.store(true);
}

void EqSpeaker::set_highpass_frequency(float hz) {
  this->highpass_hz_ = clamp(hz, 0.0f, 300.0f);
  this->dirty_.store(true);
}

void EqSpeaker::set_enabled(bool enabled) {
  this->enabled_ = enabled;
  this->dirty_.store(true);
  ESP_LOGD(TAG, "EQ %s", enabled ? "bật" : "tắt");
}

void EqSpeaker::recompute_() {
  const uint32_t rate = this->audio_stream_info_.get_sample_rate();
  this->coeff_rate_ = rate;
  this->filters_[0] = (this->highpass_hz_ >= 10.0f) ? make_filter(HIGH_PASS, this->highpass_hz_, 0, rate) : Biquad{};
  for (uint8_t i = 0; i < NUM_BANDS; i++) {
    FilterKind kind = (i == 0) ? LOW_SHELF : ((i == NUM_BANDS - 1) ? HIGH_SHELF : PEAKING);
    this->filters_[i + 1] = make_filter(kind, this->freqs_hz_[i], this->gains_db_[i], rate);
  }

  // Chừa khoảng trống cho phần được tăng để không bị vỡ tiếng: đo đỉnh đáp tuyến thật của cả chuỗi lọc
  float peak_db = 0.0f;
  for (int i = 0; i <= 96; i++) {
    const float hz = 20.0f * std::pow(1000.0f, i / 96.0f);  // 20 Hz .. 20 kHz
    if (hz >= rate * 0.49f) {
      break;
    }
    peak_db = std::max(peak_db, this->response_db_(hz, rate));
  }
  this->input_gain_ = std::pow(10.0f, (this->preamp_db_ - peak_db) / 20.0f);

  bool any = false;
  for (const auto &f : this->filters_) {
    any |= f.active;
  }
  this->processing_ = this->enabled_ && (any || std::fabs(this->preamp_db_) >= 0.01f);
}

void EqSpeaker::reset_states_() {
  for (auto &band : this->states_) {
    for (auto &st : band) {
      st = BiquadState{};
    }
  }
}

void EqSpeaker::process_(const uint8_t *in, uint8_t *out, size_t frames, uint8_t bytes_per_sample,
                         uint8_t channels) {
  const float in_scale = (bytes_per_sample == 2) ? (1.0f / 32768.0f) : (1.0f / 2147483648.0f);
  const float out_scale = (bytes_per_sample == 2) ? 32767.0f : 2147483520.0f;
  const float gain = this->input_gain_;

  for (size_t n = 0; n < frames; n++) {
    for (uint8_t ch = 0; ch < channels; ch++) {
      const size_t idx = n * channels + ch;
      float x;
      if (bytes_per_sample == 2) {
        int16_t v;
        std::memcpy(&v, in + idx * 2, 2);
        x = v * in_scale;
      } else {
        int32_t v;
        std::memcpy(&v, in + idx * 4, 4);
        x = v * in_scale;
      }
      x *= gain;
      for (uint8_t k = 0; k <= NUM_BANDS; k++) {
        const Biquad &f = this->filters_[k];
        if (!f.active) {
          continue;
        }
        BiquadState &s = this->states_[k][ch];
        const float y = f.b0 * x + s.z1;
        s.z1 = f.b1 * x - f.a1 * y + s.z2;
        s.z2 = f.b2 * x - f.a2 * y;
        x = y;
      }
      x = clamp(x, -1.0f, 1.0f);
      if (bytes_per_sample == 2) {
        const int16_t v = static_cast<int16_t>(lrintf(x * out_scale));
        std::memcpy(out + idx * 2, &v, 2);
      } else {
        const int32_t v = static_cast<int32_t>(lrintf(x * out_scale));
        std::memcpy(out + idx * 4, &v, 4);
      }
    }
  }
}

size_t EqSpeaker::play(const uint8_t *data, size_t length, TickType_t ticks_to_wait) {
  if (this->is_failed()) {
    return 0;
  }
  if (this->is_stopped()) {
    this->start();
  }

  if (this->reset_requested_.exchange(false)) {
    this->pending_len_ = 0;
    this->pending_offset_ = 0;
    this->reset_states_();
  }

  // 1) Gửi nốt phần đã xử lý mà loa I2S chưa nhận
  if (this->pending_len_ > 0) {
    const size_t sent =
        this->output_speaker_->play(this->buffer_ + this->pending_offset_, this->pending_len_, ticks_to_wait);
    this->pending_offset_ += sent;
    this->pending_len_ -= sent;
    if (this->pending_len_ > 0) {
      return 0;
    }
    ticks_to_wait = 0;
  }

  const uint8_t bps = this->audio_stream_info_.get_bits_per_sample() / 8;
  const uint8_t channels = this->audio_stream_info_.get_channels();
  const bool supported = (bps == 2 || bps == 4) && channels >= 1 && channels <= MAX_CHANNELS;

  if (this->dirty_.exchange(false) || this->coeff_rate_ != this->audio_stream_info_.get_sample_rate()) {
    this->recompute_();
  }

  // 2) EQ tắt hoặc định dạng lạ: chuyển thẳng
  if (!supported || !this->processing_) {
    return this->output_speaker_->play(data, length, ticks_to_wait);
  }

  // 3) Xử lý một khối (căn theo khung) rồi đẩy sang loa I2S
  const size_t frame_bytes = static_cast<size_t>(bps) * channels;
  size_t take = std::min(length, this->buffer_size_);
  take -= take % frame_bytes;
  if (take == 0) {
    return 0;
  }
  this->process_(data, this->buffer_, take / frame_bytes, bps, channels);
  const size_t sent = this->output_speaker_->play(this->buffer_, take, ticks_to_wait);
  if (sent < take) {
    this->pending_offset_ = sent;
    this->pending_len_ = take - sent;
  }
  return take;
}

void EqSpeaker::start() {
  if (this->is_failed()) {
    return;
  }
  if (this->state_ == speaker::STATE_STOPPED || this->state_ == speaker::STATE_STOPPING) {
    this->reset_requested_.store(true);
    this->dirty_.store(true);
    this->output_speaker_->set_audio_stream_info(this->audio_stream_info_);
    this->output_speaker_->start();
    this->state_ = speaker::STATE_STARTING;
    this->state_start_ms_ = millis();
    this->enable_loop_soon_any_context();
  }
}

void EqSpeaker::stop() {
  if (this->state_ != speaker::STATE_STOPPED) {
    this->state_ = speaker::STATE_STOPPING;
    this->state_start_ms_ = millis();
  }
  this->reset_requested_.store(true);
  this->output_speaker_->stop();
  this->enable_loop_soon_any_context();
}

void EqSpeaker::finish() {
  this->output_speaker_->finish();
  this->enable_loop_soon_any_context();
}

bool EqSpeaker::has_buffered_data() const {
  return this->pending_len_ > 0 || this->output_speaker_->has_buffered_data();
}

void EqSpeaker::set_mute_state(bool mute_state) {
  this->mute_state_ = mute_state;
  this->output_speaker_->set_mute_state(mute_state);
}

void EqSpeaker::set_volume(float volume) {
  this->volume_ = volume;
  this->output_speaker_->set_volume(volume);
}

void EqSpeaker::loop() {
  const uint32_t now = millis();
  switch (this->state_) {
    case speaker::STATE_STARTING:
      if (this->output_speaker_->is_running()) {
        this->state_ = speaker::STATE_RUNNING;
      } else if (now - this->state_start_ms_ > STATE_TRANSITION_TIMEOUT_MS) {
        ESP_LOGW(TAG, "Loa phía sau không khởi động, dừng lại");
        this->stop();
      }
      break;
    case speaker::STATE_RUNNING:
      if (this->output_speaker_->is_stopped()) {
        this->state_ = speaker::STATE_STOPPED;
      }
      break;
    case speaker::STATE_STOPPING:
      if (this->output_speaker_->is_stopped()) {
        this->state_ = speaker::STATE_STOPPED;
      } else if (now - this->state_start_ms_ > STATE_TRANSITION_TIMEOUT_MS) {
        this->output_speaker_->stop();
      }
      break;
    case speaker::STATE_STOPPED:
      this->disable_loop();
      break;
  }
}

}  // namespace esphome::eq_speaker

#endif
