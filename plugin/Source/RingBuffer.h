#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

namespace punch2pen {

/**
 * A thread-safe Single-Producer Single-Consumer (SPSC) ring buffer
 * for transferring float audio samples from the audio thread to a background
 * thread.
 */
class AudioRingBuffer {
public:
  AudioRingBuffer(size_t capacity)
      : buffer(capacity), abstractFifo((int)capacity) {}

  /**
   * Write samples into the buffer.
   * Safe to call from the audio thread (Producer).
   * Returns true if all samples were written, false if buffer was full
   * (overflow).
   */
  bool write(const float *source, int numSamples) {
    int start1, size1, start2, size2;
    abstractFifo.prepareToWrite(numSamples, start1, size1, start2, size2);

    if (size1 + size2 < numSamples) {
      return false; // Buffer full
    }

    if (size1 > 0) {
      juce::FloatVectorOperations::copy(buffer.data() + start1, source, size1);
    }
    if (size2 > 0) {
      juce::FloatVectorOperations::copy(buffer.data() + start2, source + size1,
                                        size2);
    }

    abstractFifo.finishedWrite(size1 + size2);
    return true;
  }

  /**
   * Read samples from the buffer.
   * Safe to call from the background thread (Consumer).
   * Returns number of samples actually read.
   */
  int read(float *destination, int numSamples) {
    int start1, size1, start2, size2;
    abstractFifo.prepareToRead(numSamples, start1, size1, start2, size2);

    int totalRead = size1 + size2;
    if (totalRead == 0)
      return 0;

    if (size1 > 0) {
      juce::FloatVectorOperations::copy(destination, buffer.data() + start1,
                                        size1);
    }
    if (size2 > 0) {
      juce::FloatVectorOperations::copy(destination + size1,
                                        buffer.data() + start2, size2);
    }

    abstractFifo.finishedRead(totalRead);
    return totalRead;
  }

  int getNumReady() const { return abstractFifo.getNumReady(); }

  void reset() { abstractFifo.reset(); }

private:
  std::vector<float> buffer;
  juce::AbstractFifo abstractFifo;
};

} // namespace punch2pen
