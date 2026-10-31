// A small C bridge to Rubber Band's real-time pitch shifter, for use from Swift.
#ifndef RB_LIVE_H
#define RB_LIVE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rb_live rb_live;

rb_live *rb_live_new(int sample_rate, int channels);
void rb_live_free(rb_live *shifter);
/// Frames per call to rb_live_shift; fixed for the shifter's lifetime.
int rb_live_block_size(const rb_live *shifter);
/// Output delay in frames, at the current pitch scale.
int rb_live_start_delay(const rb_live *shifter);
void rb_live_set_pitch_scale(rb_live *shifter, double scale);
/// Shifts exactly one block. `input` and `output` are arrays of per-channel pointers.
void rb_live_shift(rb_live *shifter, const float *const *input, float *const *output);
void rb_live_reset(rb_live *shifter);

#ifdef __cplusplus
}
#endif

#endif
