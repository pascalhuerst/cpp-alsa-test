#pragma once
#include <alsa/asoundlib.h>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

struct AudioFrame {
  float left;
  float right;
};

class AlsaAudio {
public:
  using AudioCallback = std::function<void(
      const AudioFrame *input, AudioFrame *output, size_t num_frames)>;

  AlsaAudio(const std::string &device, unsigned int channels,
            unsigned int sample_rate,
            unsigned int latency, // in microseconds
            unsigned int periods, snd_pcm_format_t format,
            AudioCallback callback);
  ~AlsaAudio();

  void start();
  void stop();

private:
  void process_audio();

  snd_pcm_t *capture_handle;
  snd_pcm_t *playback_handle;
  AudioCallback callback;
  std::atomic<bool> running;
  unsigned int channels;
  unsigned int period_size;
  std::vector<AudioFrame> input_buffer;
  std::vector<AudioFrame> output_buffer;
};