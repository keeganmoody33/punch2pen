#pragma once

#include <JuceHeader.h>
#include <vector>

namespace punch2pen {

/**
 * A thread-safe Single-Producer Single-Consumer (SPSC) ring buffer
 * for transferring float audio samples from the audio thread to a background
 * thread. Each sample carries the DAW timeline position it was captured at,
 * so overflowed writes cannot shift later timestamps.
 *
 * reset() is safe only on the consumer thread (the same thread that calls
 * read()), never concurrently with prepareToRead / finishedRead.
 */
class AudioRingBuffer {
public:
  AudioRingBuffer(size_t capacity)
      : buffer(capacity), dawTimes(capacity, 0.0),
        abstractFifo((int)capacity) {}

  /**
   * Write samples into the buffer.
   * Safe to call from the audio thread (Producer).
   * `dawStart` is the host sample position of source[0].
   * Returns true if all samples were written, false if buffer was full
   * (overflow).
   */
  bool write(const float *source, int numSamples, double dawStart = 0.0) {
    int start1, size1, start2, size2;
    abstractFifo.prepareToWrite(numSamples, start1, size1, start2, size2);

    if (size1 + size2 < numSamples) {
      return false; // Buffer full — caller keeps its later dawStart
    }

    if (size1 > 0) {
      juce::FloatVectorOperations::copy(buffer.data() + start1, source, size1);
      for (int i = 0; i < size1; ++i)
        dawTimes[static_cast<size_t>(start1 + i)] = dawStart + static_cast<double>(i);
    }
    if (size2 > 0) {
      juce::FloatVectorOperations::copy(buffer.data() + start2, source + size1,
                                        size2);
      for (int i = 0; i < size2; ++i)
        dawTimes[static_cast<size_t>(start2 + i)] =
            dawStart + static_cast<double>(size1 + i);
    }

    abstractFifo.finishedWrite(size1 + size2);
    return true;
  }

  /**
   * Read samples from the buffer.
   * Safe to call from the background thread (Consumer).
   * When outDawStart is non-null and any samples are read, it receives the
   * DAW sample time of destination[0].
   */
  int read(float *destination, int numSamples, double *outDawStart = nullptr) {
    int start1, size1, start2, size2;
    abstractFifo.prepareToRead(numSamples, start1, size1, start2, size2);

    int totalRead = size1 + size2;
    if (totalRead == 0)
      return 0;

    if (outDawStart != nullptr)
      *outDawStart = dawTimes[static_cast<size_t>(start1)];

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

  // Consumer thread only.
  void reset() { abstractFifo.reset(); }

private:
  std::vector<float> buffer;
  std::vector<double> dawTimes;
  juce::AbstractFifo abstractFifo;
};

} // namespace punch2pen
