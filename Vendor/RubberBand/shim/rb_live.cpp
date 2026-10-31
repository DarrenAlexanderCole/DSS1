#include "include/rb_live.h"
#include "../rubberband/RubberBandLiveShifter.h"

using RubberBand::RubberBandLiveShifter;

struct rb_live {
    RubberBandLiveShifter shifter;
    rb_live(int rate, int channels)
        : shifter(size_t(rate), size_t(channels),
                  RubberBandLiveShifter::OptionWindowMedium |
                  RubberBandLiveShifter::OptionFormantShifted |
                  RubberBandLiveShifter::OptionChannelsTogether) {}
};

extern "C" {

rb_live *rb_live_new(int sample_rate, int channels) { return new rb_live(sample_rate, channels); }
void rb_live_free(rb_live *s) { delete s; }
int rb_live_block_size(const rb_live *s) { return int(s->shifter.getBlockSize()); }
int rb_live_start_delay(const rb_live *s) { return int(s->shifter.getStartDelay()); }
void rb_live_set_pitch_scale(rb_live *s, double scale) { s->shifter.setPitchScale(scale); }
void rb_live_shift(rb_live *s, const float *const *input, float *const *output) { s->shifter.shift(input, output); }
void rb_live_reset(rb_live *s) { s->shifter.reset(); }

}
