#include "AlsaAudio.h"
#include <alsa/asoundlib.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unistd.h> // for sleep()

AlsaAudio::AlsaAudio(const std::string &device, unsigned int channels,
                     unsigned int sample_rate, unsigned int latency,
                     unsigned int periods, snd_pcm_format_t format,
                     AudioCallback callback)
    : callback(callback), running(false), channels(channels) {

  // Open PCM device for recording
  int rc =
      snd_pcm_open(&capture_handle, device.c_str(), SND_PCM_STREAM_CAPTURE, 0);
  if (rc < 0) {
    throw std::runtime_error("Cannot open capture device: " +
                             std::string(snd_strerror(rc)));
  }

  // Open PCM device for playback
  rc = snd_pcm_open(&playback_handle, device.c_str(), SND_PCM_STREAM_PLAYBACK,
                    0);
  if (rc < 0) {
    snd_pcm_close(capture_handle);
    throw std::runtime_error("Cannot open playback device: " +
                             std::string(snd_strerror(rc)));
  }

  // Configure PCM devices
  snd_pcm_hw_params_t *hw_params;
  snd_pcm_hw_params_alloca(&hw_params);

  // Configure capture
  snd_pcm_hw_params_any(capture_handle, hw_params);
  snd_pcm_hw_params_set_access(capture_handle, hw_params,
                               SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(capture_handle, hw_params, format);
  snd_pcm_hw_params_set_channels(capture_handle, hw_params, channels);
  snd_pcm_hw_params_set_rate(capture_handle, hw_params, sample_rate, 0);

  // Set low latency parameters
  snd_pcm_uframes_t buffer_size = (sample_rate * latency) / 1000000;
  snd_pcm_hw_params_set_buffer_size_near(capture_handle, hw_params,
                                         &buffer_size);
  snd_pcm_uframes_t period_size = buffer_size / periods;
  snd_pcm_hw_params_set_period_size_near(capture_handle, hw_params,
                                         &period_size, 0);

  rc = snd_pcm_hw_params(capture_handle, hw_params);
  if (rc < 0) {
    throw std::runtime_error("Cannot configure capture device: " +
                             std::string(snd_strerror(rc)));
  }

  // Get capture parameters right after configuration
  int dir;
  snd_pcm_uframes_t capture_buffer_size;
  snd_pcm_uframes_t capture_period_size;

  snd_pcm_hw_params_get_buffer_size(hw_params, &capture_buffer_size);
  snd_pcm_hw_params_get_period_size(hw_params, &capture_period_size, &dir);
  double capture_latency = (double)capture_buffer_size * 1000.0 / sample_rate;

  // Configure playback with same parameters
  snd_pcm_hw_params_any(playback_handle, hw_params);
  snd_pcm_hw_params_set_access(playback_handle, hw_params,
                               SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(playback_handle, hw_params, format);
  snd_pcm_hw_params_set_channels(playback_handle, hw_params, channels);
  snd_pcm_hw_params_set_rate(playback_handle, hw_params, sample_rate, 0);

  // Use same low latency parameters for playback
  snd_pcm_hw_params_set_buffer_size_near(playback_handle, hw_params,
                                         &buffer_size);
  snd_pcm_hw_params_set_period_size_near(playback_handle, hw_params,
                                         &period_size, 0);

  rc = snd_pcm_hw_params(playback_handle, hw_params);
  if (rc < 0) {
    throw std::runtime_error("Cannot configure playback device: " +
                             std::string(snd_strerror(rc)));
  }

  // Get playback parameters
  snd_pcm_uframes_t playback_buffer_size;
  snd_pcm_uframes_t playback_period_size;

  snd_pcm_hw_params_get_buffer_size(hw_params, &playback_buffer_size);
  snd_pcm_hw_params_get_period_size(hw_params, &playback_period_size, &dir);
  double playback_latency = (double)playback_buffer_size * 1000.0 / sample_rate;
  double total_latency = capture_latency + playback_latency;

  // Store the period size we'll use for audio processing
  period_size = playback_period_size;

  std::cout << "\nCapture Configuration:" << std::endl;
  std::cout << "Buffer Size: " << capture_buffer_size << " frames" << std::endl;
  std::cout << "Period Size: " << capture_period_size << " frames" << std::endl;
  std::cout << "Periods: " << periods << std::endl;
  std::cout << "Sample Rate: " << sample_rate << " Hz" << std::endl;
  std::cout << "Channels: " << channels << std::endl;
  std::cout << "Latency: " << capture_latency << " ms" << std::endl;

  std::cout << "\nPlayback Configuration:" << std::endl;
  std::cout << "Buffer Size: " << playback_buffer_size << " frames"
            << std::endl;
  std::cout << "Period Size: " << playback_period_size << " frames"
            << std::endl;
  std::cout << "Periods: " << periods << std::endl;
  std::cout << "Sample Rate: " << sample_rate << " Hz" << std::endl;
  std::cout << "Channels: " << channels << std::endl;
  std::cout << "Latency: " << playback_latency << " ms" << std::endl;
  std::cout << "\nTotal Round-trip Latency: " << total_latency << " ms"
            << std::endl;
  std::cout << std::endl;

  input_buffer.resize(period_size);
  output_buffer.resize(period_size);
}

AlsaAudio::~AlsaAudio() {
  stop();
  snd_pcm_close(capture_handle);
  snd_pcm_close(playback_handle);
}

void AlsaAudio::start() {
  if (!running.load()) {
    running = true;
    std::thread processingThread(&AlsaAudio::process_audio, this);
    processingThread.detach();
  }
}

void AlsaAudio::stop() { running = false; }

void AlsaAudio::process_audio() {
  std::vector<int16_t> capture_buffer(period_size * channels);
  std::vector<int16_t> playback_buffer(period_size * channels);
  const float int16_max =
      static_cast<float>(std::numeric_limits<int16_t>::max());

  while (running.load()) {
    int rc = snd_pcm_readi(capture_handle, capture_buffer.data(), period_size);
    if (rc == -EPIPE) {
      // Overrun occurred
      std::cerr << "Capture overrun occurred" << std::endl;
      snd_pcm_prepare(capture_handle);
      continue;
    } else if (rc == -ESTRPIPE) {
      // Stream was suspended
      while ((rc = snd_pcm_resume(capture_handle)) == -EAGAIN) {
        sleep(1);
      }
      if (rc < 0) {
        rc = snd_pcm_prepare(capture_handle);
        if (rc < 0) {
          std::cerr << "Failed to recover capture from suspend: "
                    << snd_strerror(rc) << std::endl;
          return;
        }
      }
      continue;
    } else if (rc < 0) {
      std::cerr << "Error reading from capture device: " << snd_strerror(rc)
                << std::endl;
      return;
    } else if (rc != (int)period_size) {
      std::cerr << "Short read from capture device, read " << rc << " frames"
                << std::endl;
    }

    // Convert from int16 to float [-1.0, 1.0]
    for (size_t i = 0; i < period_size; i++) {
      input_buffer[i].left = capture_buffer[i * 2] / int16_max;
      input_buffer[i].right = capture_buffer[i * 2 + 1] / int16_max;
    }

    callback(input_buffer.data(), output_buffer.data(), period_size);

    // Convert from float [-1.0, 1.0] to int16
    for (size_t i = 0; i < period_size; i++) {
      playback_buffer[i * 2] =
          static_cast<int16_t>(output_buffer[i].left * int16_max);
      playback_buffer[i * 2 + 1] =
          static_cast<int16_t>(output_buffer[i].right * int16_max);
    }

    rc = snd_pcm_writei(playback_handle, playback_buffer.data(), period_size);
    if (rc == -EPIPE) {
      // Underrun occurred
      std::cerr << "Playback underrun occurred" << std::endl;
      rc = snd_pcm_prepare(playback_handle);
      if (rc < 0) {
        std::cerr << "Failed to recover playback from underrun: "
                  << snd_strerror(rc) << std::endl;
        return;
      }
    } else if (rc == -ESTRPIPE) {
      // Stream was suspended
      while ((rc = snd_pcm_resume(playback_handle)) == -EAGAIN) {
        sleep(1);
      }
      if (rc < 0) {
        rc = snd_pcm_prepare(playback_handle);
        if (rc < 0) {
          std::cerr << "Failed to recover playback from suspend: "
                    << snd_strerror(rc) << std::endl;
          return;
        }
      }
    } else if (rc < 0) {
      std::cerr << "Error writing to playback device: " << snd_strerror(rc)
                << std::endl;
      return;
    } else if (rc != (int)period_size) {
      std::cerr << "Short write to playback device, wrote " << rc << " frames"
                << std::endl;
    }
  }
}
