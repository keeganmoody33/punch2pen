#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace punch2pen {

struct TimedWord {
  std::string text;
  double startSample = 0.0;
  double endSample = 0.0;
};

// Whisper segment/token times are centiseconds from the start of the
// (resampled) buffer. Map them onto the DAW sample timeline of that buffer.
inline double whisperCentisecondsToDawSamples(double bufferStartDawSample,
                                              long long tCentiseconds,
                                              double hostSampleRate) {
  if (hostSampleRate <= 0.0)
    return bufferStartDawSample;
  const double seconds = static_cast<double>(tCentiseconds) / 100.0;
  return bufferStartDawSample + seconds * hostSampleRate;
}

// Split `text` on whitespace and distribute [startSample, endSample]
// across the words in proportion to character length.
inline std::vector<TimedWord> splitWordsAcrossRange(const std::string &text,
                                                    double startSample,
                                                    double endSample) {
  std::vector<std::string> parts;
  std::string current;
  for (const char c : text) {
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      if (!current.empty()) {
        parts.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty())
    parts.push_back(current);

  std::vector<TimedWord> words;
  if (parts.empty())
    return words;

  int totalChars = 0;
  for (const auto &part : parts)
    totalChars += static_cast<int>(part.size());
  if (totalChars <= 0)
    totalChars = 1;

  const double span = std::max(0.0, endSample - startSample);
  double cursor = startSample;
  int consumed = 0;
  for (size_t i = 0; i < parts.size(); ++i) {
    consumed += static_cast<int>(parts[i].size());
    const double wordEnd =
        (i + 1 == parts.size())
            ? endSample
            : startSample + span * (static_cast<double>(consumed) /
                                    static_cast<double>(totalChars));
    TimedWord word;
    word.text = parts[i];
    word.startSample = cursor;
    word.endSample = std::max(cursor, wordEnd);
    words.push_back(std::move(word));
    cursor = wordEnd;
  }
  return words;
}

// Host blocks are contiguous when the next origin matches the previous
// block's end. Loops, seeks, and dropped writes jump by many samples.
inline bool isDawTimelineDiscontinuous(double expectedOrigin,
                                       double actualOrigin) {
  return std::abs(actualOrigin - expectedOrigin) > 1.0;
}

// Host rate is stored once per capture window. A change means token offsets
// and resampler phase belong to a different timeline — close the window.
inline bool isHostSampleRateChange(double currentRate, double nextRate) {
  if (nextRate <= 0.0)
    return false;
  return std::abs(nextRate - currentRate) >= 0.5;
}

inline bool isBlankTranscript(const std::string &text) {
  for (const char c : text) {
    if (std::isspace(static_cast<unsigned char>(c)) == 0)
      return false;
  }
  return true;
}

// Live vs committed DAW windows for cloud STT. `sentEnd` only moves after
// resampled PCM has actually been sent, not when host audio is queued.
struct CloudStreamWindow {
  double origin = 0.0;
  double sentEnd = 0.0;
  double queuedEnd = 0.0;
  size_t pcmSamplesSent = 0;
  uint32_t epoch = 0;
  bool active = false;

  void start(double dawOrigin, uint32_t captureEpoch = 0) {
    origin = dawOrigin;
    sentEnd = dawOrigin;
    queuedEnd = dawOrigin;
    pcmSamplesSent = 0;
    epoch = captureEpoch;
    active = true;
  }

  void notePcmSent(size_t pcmCount, double hostSampleRate, int targetRate) {
    if (pcmCount == 0 || !active || targetRate <= 0)
      return;
    pcmSamplesSent += pcmCount;
    if (hostSampleRate <= 0.0)
      return;
    sentEnd = origin + static_cast<double>(pcmSamplesSent) * hostSampleRate /
                           static_cast<double>(targetRate);
  }
};

// Buffer cloud transcript deltas until a completion event, then split once
// across the committed (or still-live) DAW window. Empty deltas do not
// consume that window; finalize snapshots live → committed without zeroing.
struct CloudDeltaAssembler {
  CloudStreamWindow live;
  CloudStreamWindow committed;
  std::string pending;
  bool awaitingCompletion = false;

  void captureLiveOrigin(double dawSampleTime, uint32_t captureEpoch = 0) {
    if (!live.active)
      live.start(dawSampleTime, captureEpoch);
  }

  bool needsFinalizeForOrigin(double dawSampleTime) const {
    if (!live.active)
      return false;
    return isDawTimelineDiscontinuous(live.queuedEnd, dawSampleTime);
  }

  void noteHostSamples(int sampleCount) {
    if (!live.active || sampleCount <= 0)
      return;
    live.queuedEnd += static_cast<double>(sampleCount);
  }

  void noteLivePcmSent(size_t pcmCount, double hostSampleRate, int targetRate) {
    live.notePcmSent(pcmCount, hostSampleRate, targetRate);
  }

  void finalize() {
    committed = live;
    awaitingCompletion = true;
    live = CloudStreamWindow{};
  }

  // Returns false when the delta is ignored (empty / leading whitespace).
  bool addDelta(const std::string &delta) {
    if (delta.empty())
      return false;
    if (pending.empty() && isBlankTranscript(delta))
      return false;
    pending += delta;
    awaitingCompletion = true;
    return true;
  }

  std::vector<TimedWord> complete(const std::string &doneTranscript = {}) {
    const std::string text =
        !isBlankTranscript(doneTranscript) ? doneTranscript : pending;
    pending.clear();
    awaitingCompletion = false;

    const CloudStreamWindow &window = committed.active ? committed : live;
    const double start = window.origin;
    double end = window.sentEnd;
    if (end < start)
      end = start;
    committed = CloudStreamWindow{};
    return splitWordsAcrossRange(text, start, end);
  }
};

} // namespace punch2pen
