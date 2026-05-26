#include "../Source/IPCClient.h"
#include "../../shared/Protocol.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>

namespace Punch2Pen = punch2pen;

static const int TEST_PORT = 17483;

// Reads exactly `len` bytes from the socket, returns true on success.
static bool readExact(juce::StreamingSocket &sock, void *dest, int len) {
  int totalRead = 0;
  auto *buf = static_cast<char *>(dest);
  while (totalRead < len) {
    int n = sock.read(buf + totalRead, len - totalRead, false);
    if (n <= 0)
      return false;
    totalRead += n;
  }
  return true;
}

void testConnectionAndAudioChunk() {
  // Spin up a server on a background thread
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT, "127.0.0.1");
  assert(bound);

  std::atomic<bool> serverReady{true};
  std::atomic<bool> gotAudioChunk{false};
  std::atomic<bool> headerOk{false};
  std::atomic<bool> payloadOk{false};

  const int numSamples = 16;
  const double sampleRate = 44100.0;

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = server.waitForNextConnection();
    if (client == nullptr)
      return;

    // Read header
    Punch2Pen::protocol::Header header;
    if (!readExact(*client, &header, sizeof(header))) {
      delete client;
      return;
    }

    headerOk = (header.type == Punch2Pen::protocol::MessageType::AudioChunk);

    // Read audio chunk sub-header
    Punch2Pen::protocol::AudioChunkHeader chunkHeader;
    if (!readExact(*client, &chunkHeader, sizeof(chunkHeader))) {
      delete client;
      return;
    }

    assert(chunkHeader.sampleRate == sampleRate);
    assert(chunkHeader.numSamples == (uint32_t)numSamples);

    // Read float payload
    std::vector<float> payload(numSamples);
    if (!readExact(*client, payload.data(), numSamples * (int)sizeof(float))) {
      delete client;
      return;
    }

    payloadOk = true;
    for (int i = 0; i < numSamples; ++i) {
      if (payload[i] != (float)i * 0.1f) {
        payloadOk = false;
        break;
      }
    }

    gotAudioChunk = true;
    delete client;
  });

  // Give server a moment to start listening
  juce::Thread::sleep(100);

  // Create IPCClient with test port (auto-connects on construction)
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(TEST_PORT);

  // Wait for connection
  for (int i = 0; i < 50; ++i) {
    if (ipcClient->isConnected())
      break;
    juce::Thread::sleep(100);
  }
  assert(ipcClient->isConnected());

  // Send audio chunk
  std::vector<float> samples(numSamples);
  for (int i = 0; i < numSamples; ++i)
    samples[i] = (float)i * 0.1f;

  ipcClient->sendAudioChunk(samples.data(), numSamples, sampleRate);

  // Wait for server to receive
  for (int i = 0; i < 50; ++i) {
    if (gotAudioChunk)
      break;
    juce::Thread::sleep(100);
  }

  // Tear down client before joining server
  ipcClient.reset();
  serverThread.join();
  server.close();

  assert(gotAudioChunk);
  assert(headerOk);
  assert(payloadOk);

  std::cout << "[PASS] testConnectionAndAudioChunk" << std::endl;
}

void testTransportStop() {
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT + 1, "127.0.0.1");
  assert(bound);

  std::atomic<bool> gotStop{false};
  std::atomic<bool> headerOk{false};

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = server.waitForNextConnection();
    if (client == nullptr)
      return;

    Punch2Pen::protocol::Header header;
    if (!readExact(*client, &header, sizeof(header))) {
      delete client;
      return;
    }

    headerOk = (header.type == Punch2Pen::protocol::MessageType::TransportStop &&
                header.length == 0);
    gotStop = true;
    delete client;
  });

  juce::Thread::sleep(100);
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(TEST_PORT + 1);

  for (int i = 0; i < 50; ++i) {
    if (ipcClient->isConnected())
      break;
    juce::Thread::sleep(100);
  }
  assert(ipcClient->isConnected());

  ipcClient->sendTransportStop();

  for (int i = 0; i < 50; ++i) {
    if (gotStop)
      break;
    juce::Thread::sleep(100);
  }

  ipcClient.reset();
  serverThread.join();
  server.close();

  assert(gotStop);
  assert(headerOk);

  std::cout << "[PASS] testTransportStop" << std::endl;
}

void testCorrection() {
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT + 2, "127.0.0.1");
  assert(bound);

  const std::string original = "hello";
  const std::string corrected = "world";

  std::atomic<bool> gotCorrection{false};
  std::atomic<bool> allOk{false};

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = server.waitForNextConnection();
    if (client == nullptr)
      return;

    // Read Header
    Punch2Pen::protocol::Header header;
    if (!readExact(*client, &header, sizeof(header))) {
      delete client;
      return;
    }

    if (header.type != Punch2Pen::protocol::MessageType::Correction) {
      delete client;
      return;
    }

    // Read CorrectionHeader
    Punch2Pen::protocol::CorrectionHeader corrHeader;
    if (!readExact(*client, &corrHeader, sizeof(corrHeader))) {
      delete client;
      return;
    }

    // Read original string
    std::vector<char> origBuf(corrHeader.originalLength);
    if (!readExact(*client, origBuf.data(), (int)corrHeader.originalLength)) {
      delete client;
      return;
    }

    // Read corrected string
    std::vector<char> corrBuf(corrHeader.correctedLength);
    if (!readExact(*client, corrBuf.data(), (int)corrHeader.correctedLength)) {
      delete client;
      return;
    }

    std::string origStr(origBuf.begin(), origBuf.end());
    std::string corrStr(corrBuf.begin(), corrBuf.end());

    allOk = (corrHeader.originalLength == (uint32_t)original.size() &&
             corrHeader.correctedLength == (uint32_t)corrected.size() &&
             origStr == original && corrStr == corrected);

    gotCorrection = true;
    delete client;
  });

  juce::Thread::sleep(100);
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(TEST_PORT + 2);

  for (int i = 0; i < 50; ++i) {
    if (ipcClient->isConnected())
      break;
    juce::Thread::sleep(100);
  }
  assert(ipcClient->isConnected());

  ipcClient->sendCorrection(original, corrected);

  for (int i = 0; i < 50; ++i) {
    if (gotCorrection)
      break;
    juce::Thread::sleep(100);
  }

  ipcClient.reset();
  serverThread.join();
  server.close();

  assert(gotCorrection);
  assert(allOk);

  std::cout << "[PASS] testCorrection" << std::endl;
}

void testDisconnectDetection() {
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT + 3, "127.0.0.1");
  assert(bound);

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = server.waitForNextConnection();
    if (client == nullptr)
      return;
    // Accept connection then immediately close
    juce::Thread::sleep(200);
    client->close();
    delete client;
  });

  juce::Thread::sleep(100);
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(TEST_PORT + 3);

  for (int i = 0; i < 50; ++i) {
    if (ipcClient->isConnected())
      break;
    juce::Thread::sleep(100);
  }
  assert(ipcClient->isConnected());

  // Wait for server to close its end
  serverThread.join();
  server.close();

  // The client's run() loop should eventually detect disconnect.
  // Attempt a send to trigger detection.
  for (int i = 0; i < 50; ++i) {
    if (!ipcClient->isConnected())
      break;
    // Trigger a write to detect the broken pipe
    ipcClient->sendTransportStop();
    juce::Thread::sleep(200);
  }
  assert(!ipcClient->isConnected());

  ipcClient.reset();

  std::cout << "[PASS] testDisconnectDetection" << std::endl;
}

int main() {
  // JUCE message manager is needed for event-based classes
  juce::ScopedJuceInitialiser_GUI init;

  testConnectionAndAudioChunk();
  testTransportStop();
  testCorrection();
  testDisconnectDetection();

  std::cout << "All IPCClient tests passed!" << std::endl;
  return 0;
}
