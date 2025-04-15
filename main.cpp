#include "AlsaAudio.h"
#include <alsa/pcm.h>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
  std::string dev = "plughw:0,0";

  try {
    AlsaAudio audio(
        dev, 2, 48000, 256, SND_PCM_FORMAT_FLOAT,
        [](const AudioFrame *input, AudioFrame *output, size_t numFrames) {
          // Simple passthrough
          for (size_t i = 0; i < numFrames; ++i) {
            output[i] = input[i];
          }
        });

    audio.start();
    std::cout << "Press Enter to stop..." << std::endl;
    std::cin.get();
    audio.stop();
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
