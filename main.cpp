#include "AlsaAudio.h"
#include <alsa/pcm.h>
#include <iostream>
#include <string>

void printUsage(const char *programName) {
  std::cout << "Usage: " << programName << " [options]\n"
            << "Options:\n"
            << "  -d device   ALSA device (default: plughw:0,0)\n"
            << "  -r rate     Sample rate in Hz (default: 48000)\n"
            << "  -c channels Number of channels (default: 2)\n"
            << "  -l latency  Target latency in microseconds (default: 10000)\n"
            << "  -p periods  Number of periods (default: 4)\n"
            << "  -h         Show this help message\n";
}

int main(int argc, char **argv) {
  std::string dev = "plughw:0,0";
  unsigned int rate = 48000;
  unsigned int channels = 2;
  unsigned int latency = 10000; // microseconds
  unsigned int periods = 4;

  // Parse command line arguments
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-h") {
      printUsage(argv[0]);
      return 0;
    } else if (i + 1 < argc) {
      if (arg == "-d") {
        dev = argv[++i];
      } else if (arg == "-r") {
        rate = std::stoi(argv[++i]);
      } else if (arg == "-c") {
        channels = std::stoi(argv[++i]);
      } else if (arg == "-l") {
        latency = std::stoi(argv[++i]);
      } else if (arg == "-p") {
        periods = std::stoi(argv[++i]);
      }
    }
  }

  try {
    std::cout << "Using format: SND_PCM_FORMAT_S16_LE" << std::endl;
    AlsaAudio audio(
        dev, channels, rate, latency, periods, SND_PCM_FORMAT_S16_LE,
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
