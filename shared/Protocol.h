#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace punch2pen {
namespace protocol {

enum class MessageType : uint32_t {
  AudioChunk = 1,
  TranscriptionResult = 2,
  Handshake = 3,
  HandshakeResponse = 4,
  Correction = 5
};

struct Header {
  MessageType type;
  uint32_t length;
};

// Preliminary structures, will expand as needed
struct Handshake {
  uint32_t version;
};

struct AudioChunkHeader {
  double sampleRate;
  uint32_t numSamples;
  // Followed by float32 payload
};

struct TranscriptionResultHeader {
  uint32_t textLength;
  double startTime;
  double endTime;
  // Followed by text payload
};
} // namespace protocol
} // namespace punch2pen
