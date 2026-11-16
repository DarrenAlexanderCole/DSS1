# Test audio

Two seconds of silence with a short click at exactly 0.5 s (frame 22050) and 1.25 s (frame 55125), 44.1 kHz.
The same audio in every format, so tests can check that each decoder puts the clicks on the same frames.

| File | Made with |
|---|---|
| `clicks.wav` | 16-bit stereo PCM (the source) |
| `clicks.aiff` | `ffmpeg -i clicks.wav -ac 1 -c:a pcm_s16be clicks.aiff` (mono) |
| `clicks.flac` | `ffmpeg -i clicks.wav -c:a flac clicks.flac` |
| `clicks.mp3` | `ffmpeg -i clicks.wav -c:a libmp3lame -b:a 192k clicks.mp3` (has a LAME header with encoder delay and padding) |
