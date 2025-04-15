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
      const AudioFrame *input, AudioFrame *output, size_t numFrames)>;

  AlsaAudio(const std::string &device, unsigned int channels,
            unsigned int sampleRate,
            unsigned int latency, // in microseconds
            unsigned int periods, snd_pcm_format_t format,
            AudioCallback callback);
  ~AlsaAudio();

  void start();
  void stop();

private:
  void processAudio();

  snd_pcm_t *captureHandle;
  snd_pcm_t *playbackHandle;
  AudioCallback callback;
  std::atomic<bool> running;
  unsigned int channels;
  unsigned int periodSize;
  std::vector<AudioFrame> inputBuffer;
  std::vector<AudioFrame> outputBuffer;
};