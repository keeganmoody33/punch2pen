#include "Protocol.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

void testHeaderSerialization() {
  punch2pen::protocol::Header header;
  header.type = punch2pen::protocol::MessageType::AudioChunk;
  header.length = 1024;

  std::vector<char> buf(sizeof(header));
  std::memcpy(buf.data(), &header, sizeof(header));

  punch2pen::protocol::Header deserialized;
  std::memcpy(&deserialized, buf.data(), sizeof(deserialized));

  assert(deserialized.type == punch2pen::protocol::MessageType::AudioChunk);
  assert(deserialized.length == 1024);

  std::cout << "[PASS] testHeaderSerialization" << std::endl;
}

void testAudioChunkHeaderSerialization() {
  punch2pen::protocol::AudioChunkHeader chunkHeader;
  chunkHeader.sampleRate = 48000.0;
  chunkHeader.numSamples = 256;
  chunkHeader.dawSampleTime = 96000.0;

  std::vector<char> buf(sizeof(chunkHeader));
  std::memcpy(buf.data(), &chunkHeader, sizeof(chunkHeader));

  punch2pen::protocol::AudioChunkHeader deserialized;
  std::memcpy(&deserialized, buf.data(), sizeof(deserialized));

  assert(deserialized.sampleRate == 48000.0);
  assert(deserialized.numSamples == 256);
  assert(deserialized.dawSampleTime == 96000.0);

  std::cout << "[PASS] testAudioChunkHeaderSerialization" << std::endl;
}

void testCorrectionHeaderSerialization() {
  punch2pen::protocol::CorrectionHeader corrHeader;
  corrHeader.originalLength = 5;
  corrHeader.correctedLength = 10;

  std::vector<char> buf(sizeof(corrHeader));
  std::memcpy(buf.data(), &corrHeader, sizeof(corrHeader));

  punch2pen::protocol::CorrectionHeader deserialized;
  std::memcpy(&deserialized, buf.data(), sizeof(deserialized));

  assert(deserialized.originalLength == 5);
  assert(deserialized.correctedLength == 10);

  std::cout << "[PASS] testCorrectionHeaderSerialization" << std::endl;
}

void testTranscriptionResultHeaderSerialization() {
  punch2pen::protocol::TranscriptionResultHeader resultHeader;
  resultHeader.textLength = 42;
  resultHeader.startTime = 1.0;
  resultHeader.endTime = 2.5;

  std::vector<char> buf(sizeof(resultHeader));
  std::memcpy(buf.data(), &resultHeader, sizeof(resultHeader));

  punch2pen::protocol::TranscriptionResultHeader deserialized;
  std::memcpy(&deserialized, buf.data(), sizeof(deserialized));

  assert(deserialized.textLength == 42);
  assert(deserialized.startTime == 1.0);
  assert(deserialized.endTime == 2.5);

  std::cout << "[PASS] testTranscriptionResultHeaderSerialization" << std::endl;
}

void testMessageTypeCoverage() {
  assert(static_cast<uint32_t>(punch2pen::protocol::MessageType::AudioChunk) == 1);
  assert(static_cast<uint32_t>(punch2pen::protocol::MessageType::TranscriptionResult) == 2);
  assert(static_cast<uint32_t>(punch2pen::protocol::MessageType::Handshake) == 3);
  assert(static_cast<uint32_t>(punch2pen::protocol::MessageType::HandshakeResponse) == 4);
  assert(static_cast<uint32_t>(punch2pen::protocol::MessageType::Correction) == 5);
  assert(static_cast<uint32_t>(punch2pen::protocol::MessageType::TransportStop) == 6);

  std::cout << "[PASS] testMessageTypeCoverage" << std::endl;
}

void testFullMessageRoundTrip() {
  // Simulate constructing and parsing a full Correction message
  std::string original = "helo";
  std::string corrected = "hello";

  punch2pen::protocol::Header header;
  header.type = punch2pen::protocol::MessageType::Correction;

  punch2pen::protocol::CorrectionHeader corrHeader;
  corrHeader.originalLength = static_cast<uint32_t>(original.size());
  corrHeader.correctedLength = static_cast<uint32_t>(corrected.size());

  header.length = static_cast<uint32_t>(sizeof(corrHeader) + original.size() + corrected.size());

  // Serialize into a buffer
  std::vector<char> buf;
  buf.resize(sizeof(header) + sizeof(corrHeader) + original.size() + corrected.size());
  size_t offset = 0;
  std::memcpy(buf.data() + offset, &header, sizeof(header));
  offset += sizeof(header);
  std::memcpy(buf.data() + offset, &corrHeader, sizeof(corrHeader));
  offset += sizeof(corrHeader);
  std::memcpy(buf.data() + offset, original.data(), original.size());
  offset += original.size();
  std::memcpy(buf.data() + offset, corrected.data(), corrected.size());

  // Deserialize
  offset = 0;
  punch2pen::protocol::Header parsedHeader;
  std::memcpy(&parsedHeader, buf.data() + offset, sizeof(parsedHeader));
  offset += sizeof(parsedHeader);

  assert(parsedHeader.type == punch2pen::protocol::MessageType::Correction);

  punch2pen::protocol::CorrectionHeader parsedCorrHeader;
  std::memcpy(&parsedCorrHeader, buf.data() + offset, sizeof(parsedCorrHeader));
  offset += sizeof(parsedCorrHeader);

  std::string parsedOriginal(buf.data() + offset, parsedCorrHeader.originalLength);
  offset += parsedCorrHeader.originalLength;
  std::string parsedCorrected(buf.data() + offset, parsedCorrHeader.correctedLength);

  assert(parsedOriginal == "helo");
  assert(parsedCorrected == "hello");

  std::cout << "[PASS] testFullMessageRoundTrip" << std::endl;
}

int main() {
  testHeaderSerialization();
  testAudioChunkHeaderSerialization();
  testCorrectionHeaderSerialization();
  testTranscriptionResultHeaderSerialization();
  testMessageTypeCoverage();
  testFullMessageRoundTrip();
  std::cout << "All Protocol serialization tests passed!" << std::endl;
  return 0;
}
