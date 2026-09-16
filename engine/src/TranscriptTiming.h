#pragma once

#include <algorithm>
#include <cctype>
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

} // namespace punch2pen
