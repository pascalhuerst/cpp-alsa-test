#include "AlsaAudio.h"
#include <alsa/asoundlib.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h> // for sleep()

AlsaAudio::AlsaAudio(const std::string &device, unsigned int channels,
                     unsigned int sampleRate, unsigned int latency,
                     unsigned int periods, snd_pcm_format_t format,
                     AudioCallback callback)
    : callback(callback), running(false), channels(channels) {

  // Open PCM device for recording
  int rc =
      snd_pcm_open(&captureHandle, device.c_str(), SND_PCM_STREAM_CAPTURE, 0);
  if (rc < 0) {
    throw std::runtime_error("Cannot open capture device: " +
                             std::string(snd_strerror(rc)));
  }

  // Open PCM device for playback
  rc =
      snd_pcm_open(&playbackHandle, device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
  if (rc < 0) {
    snd_pcm_close(captureHandle);
    throw std::runtime_error("Cannot open playback device: " +
                             std::string(snd_strerror(rc)));
  }

  // Configure PCM devices
  snd_pcm_hw_params_t *hw_params;
  snd_pcm_hw_params_alloca(&hw_params);

  // Configure capture
  snd_pcm_hw_params_any(captureHandle, hw_params);
  snd_pcm_hw_params_set_access(captureHandle, hw_params,
                               SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(captureHandle, hw_params, format);
  snd_pcm_hw_params_set_channels(captureHandle, hw_params, channels);
  snd_pcm_hw_params_set_rate(captureHandle, hw_params, sampleRate, 0);

  // Set low latency parameters
  snd_pcm_uframes_t buffer_size = (sampleRate * latency) / 1000000;
  snd_pcm_hw_params_set_buffer_size_near(captureHandle, hw_params,
                                         &buffer_size);
  snd_pcm_uframes_t period_size = buffer_size / periods;
  snd_pcm_hw_params_set_period_size_near(captureHandle, hw_params, &period_size,
                                         0);

  rc = snd_pcm_hw_params(captureHandle, hw_params);
  if (rc < 0) {
    throw std::runtime_error("Cannot configure capture device: " +
                             std::string(snd_strerror(rc)));
  }

  // Configure playback with same parameters
  snd_pcm_hw_params_any(playbackHandle, hw_params);
  snd_pcm_hw_params_set_access(playbackHandle, hw_params,
                               SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(playbackHandle, hw_params, format);
  snd_pcm_hw_params_set_channels(playbackHandle, hw_params, channels);
  snd_pcm_hw_params_set_rate(playbackHandle, hw_params, sampleRate, 0);

  // Use same low latency parameters for playback
  snd_pcm_hw_params_set_buffer_size_near(playbackHandle, hw_params,
                                         &buffer_size);
  snd_pcm_hw_params_set_period_size_near(playbackHandle, hw_params,
                                         &period_size, 0);

  rc = snd_pcm_hw_params(playbackHandle, hw_params);
  if (rc < 0) {
    throw std::runtime_error("Cannot configure playback device: " +
                             std::string(snd_strerror(rc)));
  }

  // Update periodSize member with actual value used
  periodSize = period_size;

  int dir;

  // Get capture parameters
  snd_pcm_hw_params_get_buffer_size(hw_params, &buffer_size);
  snd_pcm_hw_params_get_period_size(hw_params, &period_size, &dir);

  double capture_latency = (double)buffer_size * 1000.0 / sampleRate;

  std::cout << "\nCapture Configuration:" << std::endl;
  std::cout << "Buffer Size: " << buffer_size << " frames" << std::endl;
  std::cout << "Period Size: " << period_size << " frames" << std::endl;
  std::cout << "Periods: " << periods << std::endl;
  std::cout << "Sample Rate: " << sampleRate << " Hz" << std::endl;
  std::cout << "Channels: " << channels << std::endl;
  std::cout << "Latency: " << capture_latency << " ms" << std::endl;

  // Configure playback with same parameters
  snd_pcm_hw_params_get_buffer_size(hw_params, &buffer_size);
  snd_pcm_hw_params_get_period_size(hw_params, &period_size, &dir);

  double playback_latency = (double)buffer_size * 1000.0 / sampleRate;
  double total_latency = capture_latency + playback_latency;

  std::cout << "\nPlayback Configuration:" << std::endl;
  std::cout << "Buffer Size: " << buffer_size << " frames" << std::endl;
  std::cout << "Period Size: " << period_size << " frames" << std::endl;
  std::cout << "Periods: " << periods << std::endl;
  std::cout << "Sample Rate: " << sampleRate << " Hz" << std::endl;
  std::cout << "Channels: " << channels << std::endl;
  std::cout << "Latency: " << playback_latency << " ms" << std::endl;
  std::cout << "\nTotal Round-trip Latency: " << total_latency << " ms"
            << std::endl;
  std::cout << std::endl;

  inputBuffer.resize(periodSize);
  outputBuffer.resize(periodSize);
}

AlsaAudio::~AlsaAudio() {
  stop();
  snd_pcm_close(captureHandle);
  snd_pcm_close(playbackHandle);
}

void AlsaAudio::start() {
  if (!running.load()) {
    running = true;
    std::thread processingThread(&AlsaAudio::processAudio, this);
    processingThread.detach();
  }
}

void AlsaAudio::stop() { running = false; }

void AlsaAudio::processAudio() {
  while (running.load()) {
    // Read from capture device
    int rc = snd_pcm_readi(captureHandle, inputBuffer.data(), periodSize);
    if (rc == -EPIPE) {
      // Overrun occurred
      std::cerr << "Capture overrun occurred" << std::endl;
      snd_pcm_prepare(captureHandle);
      continue;
    } else if (rc == -ESTRPIPE) {
      // Stream was suspended
      while ((rc = snd_pcm_resume(captureHandle)) == -EAGAIN) {
        sleep(1);
      }
      if (rc < 0) {
        rc = snd_pcm_prepare(captureHandle);
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
    } else if (rc != (int)periodSize) {
      std::cerr << "Short read from capture device, read " << rc << " frames"
                << std::endl;
    }

    // Process audio
    callback(inputBuffer.data(), outputBuffer.data(), periodSize);

    // Write to playback device
    rc = snd_pcm_writei(playbackHandle, outputBuffer.data(), periodSize);
    if (rc == -EPIPE) {
      // Underrun occurred
      std::cerr << "Playback underrun occurred" << std::endl;
      rc = snd_pcm_prepare(playbackHandle);
      if (rc < 0) {
        std::cerr << "Failed to recover playback from underrun: "
                  << snd_strerror(rc) << std::endl;
        return;
      }
    } else if (rc == -ESTRPIPE) {
      // Stream was suspended
      while ((rc = snd_pcm_resume(playbackHandle)) == -EAGAIN) {
        sleep(1);
      }
      if (rc < 0) {
        rc = snd_pcm_prepare(playbackHandle);
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
    } else if (rc != (int)periodSize) {
      std::cerr << "Short write to playback device, wrote " << rc << " frames"
                << std::endl;
    }
  }
}
