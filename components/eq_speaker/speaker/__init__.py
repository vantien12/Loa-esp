"""EQ 6 dải cho loa ESPHome (giống EQ của VBot): đặt giữa mixer và loa I2S.

speaker:
  - platform: eq_speaker
    id: eq_out
    output_speaker: i2s_speaker
    bass: 4          # dB ở 100 Hz (low-shelf)
"""

import esphome.codegen as cg
from esphome.components import audio, speaker
import esphome.config_validation as cv
from esphome.const import (
    CONF_BITS_PER_SAMPLE,
    CONF_ID,
    CONF_NUM_CHANNELS,
    CONF_OUTPUT_SPEAKER,
    CONF_SAMPLE_RATE,
    PLATFORM_ESP32,
)
from esphome.types import ConfigType

from .. import eq_speaker_ns

AUTO_LOAD = ["audio"]

EqSpeaker = eq_speaker_ns.class_("EqSpeaker", cg.Component, speaker.Speaker)

# Tên dải, tần số (Hz) — giống VBot: 100 low-shelf, 4 dải peaking, 10 kHz high-shelf
BANDS = [
    ("bass", 100),
    ("low_mid", 250),
    ("mid", 630),
    ("high_mid", 1600),
    ("treble", 4000),
    ("air", 10000),
]
CONF_PREAMP = "preamp"
CONF_HIGHPASS = "highpass_frequency"
CONF_ENABLED = "enabled"

_gain = cv.All(cv.float_, cv.Range(min=-12.0, max=12.0))


def _set_stream_limits(config: ConfigType) -> ConfigType:
    audio.set_stream_limits(
        min_bits_per_sample=16,
        max_bits_per_sample=32,
        min_channels=1,
        max_channels=2,
    )(config)
    return config


def _validate_output(config: ConfigType) -> ConfigType:
    audio.final_validate_audio_schema(
        "eq_speaker",
        audio_device=CONF_OUTPUT_SPEAKER,
        bits_per_sample=config.get(CONF_BITS_PER_SAMPLE),
        channels=config.get(CONF_NUM_CHANNELS),
        sample_rate=config.get(CONF_SAMPLE_RATE),
    )(config)
    return config


CONFIG_SCHEMA = cv.All(
    speaker.SPEAKER_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(EqSpeaker),
            cv.Required(CONF_OUTPUT_SPEAKER): cv.use_id(speaker.Speaker),
            # Định dạng phải khớp loa I2S phía sau (mặc định: 32-bit, mono, 48 kHz)
            cv.Optional(CONF_BITS_PER_SAMPLE, default=32): cv.one_of(16, 32, int=True),
            cv.Optional(CONF_NUM_CHANNELS, default=1): cv.int_range(min=1, max=2),
            cv.Optional(CONF_SAMPLE_RATE, default=48000): cv.int_range(min=16000, max=48000),
            cv.Optional(CONF_ENABLED, default=True): cv.boolean,
            cv.Optional(CONF_PREAMP, default=0.0): _gain,
            # Lọc bỏ âm quá trầm loa nhỏ không phát được (đỡ rè, nghe to và sạch hơn). 0 = tắt
            cv.Optional(CONF_HIGHPASS, default=0): cv.int_range(min=0, max=300),
            **{cv.Optional(name, default=0.0): _gain for name, _ in BANDS},
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on([PLATFORM_ESP32]),
    _set_stream_limits,
)

FINAL_VALIDATE_SCHEMA = _validate_output


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await speaker.register_speaker(var, config)
    out = await cg.get_variable(config[CONF_OUTPUT_SPEAKER])
    cg.add(var.set_output_speaker(out))
    for i, (name, freq) in enumerate(BANDS):
        cg.add(var.set_band_frequency(i, freq))
        cg.add(var.set_band_gain(i, config[name]))
    cg.add(var.set_preamp(config[CONF_PREAMP]))
    cg.add(var.set_highpass_frequency(config[CONF_HIGHPASS]))
    cg.add(var.set_enabled(config[CONF_ENABLED]))
