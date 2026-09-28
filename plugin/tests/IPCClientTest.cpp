#include "../Source/IPCClient.h"
#include "../Source/EngineLaunchPaths.h"
#include "../Source/RingBuffer.h"
#include "../../shared/Protocol.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <vector>
#include <chrono>

namespace Punch2Pen = punch2pen;

static const int TEST_PORT = 17483;

// Reads exactly `len` bytes from the socket, returns true on success.
static bool readExact(juce::StreamingSocket &sock, void *dest, int len) {
  return sock.read(dest, len, true) == len;
}

static bool replyToHandshake(juce::StreamingSocket &client) {
  Punch2Pen::protocol::Header header;
  if (!readExact(client, &header, sizeof(header)))
    return false;
  if (header.type != Punch2Pen::protocol::MessageType::Handshake ||
      header.length != (uint32_t)sizeof(Punch2Pen::protocol::Handshake))
    return false;

  Punch2Pen::protocol::Handshake handshake;
  if (!readExact(client, &handshake, sizeof(handshake)))
    return false;

  Punch2Pen::protocol::Header reply;
  reply.type = Punch2Pen::protocol::MessageType::HandshakeResponse;
  Punch2Pen::protocol::HandshakeResponse response;
  response.version = Punch2Pen::protocol::kProtocolVersion;
  response.accepted =
      handshake.version == Punch2Pen::protocol::kProtocolVersion ? 1u : 0u;
  reply.length = (uint32_t)sizeof(response);

  return client.write(&reply, sizeof(reply)) == sizeof(reply) &&
         client.write(&response, sizeof(response)) == sizeof(response) &&
         response.accepted != 0;
}

static juce::StreamingSocket *acceptAndHandshake(juce::StreamingSocket &server) {
  juce::StreamingSocket *client = server.waitForNextConnection();
  if (client == nullptr)
    return nullptr;
  if (!replyToHandshake(*client)) {
    delete client;
    return nullptr;
  }
  return client;
}

void testConnectionAndAudioChunk() {
  // Spin up a server on a background thread
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT, "127.0.0.1");
  assert(bound);

  std::atomic<bool> gotAudioChunk{false};
  std::atomic<bool> headerOk{false};
  std::atomic<bool> payloadOk{false};

  const int numSamples = 16;
  const double sampleRate = 44100.0;

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = acceptAndHandshake(server);
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
    assert(chunkHeader.dawSampleTime == 0.0);

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

  // Create IPCClient with test port (auto-connects on construction).
  // Disable auto-launch so a failed connect doesn't shell out to the engine binary.
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(TEST_PORT, /*autoLaunchEngine=*/false);

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

  ipcClient->sendAudioChunk(samples.data(), numSamples, sampleRate, 0.0);

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

void testOutgoingChunksUseHostSampleRate() {
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT + 4, "127.0.0.1");
  assert(bound);

  std::atomic<bool> gotAudioChunk{false};
  std::atomic<bool> rateOk{false};
  const double hostRate = 44100.0;
  const int chunkSize = 4096;

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = acceptAndHandshake(server);
    if (client == nullptr)
      return;

    Punch2Pen::protocol::Header header;
    if (!readExact(*client, &header, sizeof(header))) {
      delete client;
      return;
    }

    Punch2Pen::protocol::AudioChunkHeader chunkHeader;
    if (!readExact(*client, &chunkHeader, sizeof(chunkHeader))) {
      delete client;
      return;
    }

    rateOk = (chunkHeader.sampleRate == hostRate &&
              chunkHeader.numSamples == (uint32_t)chunkSize &&
              chunkHeader.dawSampleTime == 96000.0);

    std::vector<float> payload(chunkSize);
    readExact(*client, payload.data(), chunkSize * (int)sizeof(float));
    gotAudioChunk = true;
    delete client;
  });

  juce::Thread::sleep(100);
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(
      TEST_PORT + 4, /*autoLaunchEngine=*/false);
  ipcClient->setHostSampleRate(hostRate);

  Punch2Pen::AudioRingBuffer ring(chunkSize * 2);
  std::vector<float> samples(chunkSize, 0.25f);
  ring.write(samples.data(), chunkSize, 96000.0);
  ipcClient->setAudioSource(&ring);

  for (int i = 0; i < 50; ++i) {
    if (ipcClient->isConnected())
      break;
    juce::Thread::sleep(100);
  }
  assert(ipcClient->isConnected());

  for (int i = 0; i < 50; ++i) {
    if (gotAudioChunk)
      break;
    juce::Thread::sleep(100);
  }

  ipcClient.reset();
  serverThread.join();
  server.close();

  assert(gotAudioChunk);
  assert(rateOk);

  std::cout << "[PASS] testOutgoingChunksUseHostSampleRate" << std::endl;
}

void testTransportStop() {
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT + 1, "127.0.0.1");
  assert(bound);

  std::atomic<bool> gotStop{false};
  std::atomic<bool> headerOk{false};

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = acceptAndHandshake(server);
    if (client == nullptr)
      return;

    Punch2Pen::protocol::Header header;
    if (!readExact(*client, &header, sizeof(header))) {
      delete client;
      return;
    }

    headerOk = (header.type == Punch2Pen::protocol::MessageType::TransportStop &&
                header.length ==
                    (uint32_t)sizeof(Punch2Pen::protocol::TransportStopHeader));
    Punch2Pen::protocol::TransportStopHeader stopHeader;
    if (headerOk &&
        readExact(*client, &stopHeader, sizeof(stopHeader)))
      headerOk = (stopHeader.captureEpoch == 0);
    gotStop = true;
    delete client;
  });

  juce::Thread::sleep(100);
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(TEST_PORT + 1, /*autoLaunchEngine=*/false);

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
    juce::StreamingSocket *client = acceptAndHandshake(server);
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
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(TEST_PORT + 2, /*autoLaunchEngine=*/false);

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
    juce::StreamingSocket *client = acceptAndHandshake(server);
    if (client == nullptr)
      return;
    // Accept connection then immediately close
    juce::Thread::sleep(200);
    client->close();
    delete client;
  });

  juce::Thread::sleep(100);
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(TEST_PORT + 3, /*autoLaunchEngine=*/false);

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

void testOutgoingChunksStampDawSampleTime() {
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT + 5, "127.0.0.1");
  assert(bound);

  std::atomic<bool> gotAudioChunk{false};
  std::atomic<bool> timeOk{false};
  const double origin = 48000.0;
  const int chunkSize = 4096;

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = acceptAndHandshake(server);
    if (client == nullptr)
      return;

    Punch2Pen::protocol::Header header;
    if (!readExact(*client, &header, sizeof(header))) {
      delete client;
      return;
    }

    Punch2Pen::protocol::AudioChunkHeader chunkHeader;
    if (!readExact(*client, &chunkHeader, sizeof(chunkHeader))) {
      delete client;
      return;
    }

    timeOk = (chunkHeader.dawSampleTime == origin &&
              chunkHeader.numSamples == (uint32_t)chunkSize);

    std::vector<float> payload(chunkSize);
    readExact(*client, payload.data(), chunkSize * (int)sizeof(float));
    gotAudioChunk = true;
    delete client;
  });

  juce::Thread::sleep(100);
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(
      TEST_PORT + 5, /*autoLaunchEngine=*/false);
  ipcClient->setHostSampleRate(48000.0);

  Punch2Pen::AudioRingBuffer ring(chunkSize * 2);
  std::vector<float> samples(chunkSize, 0.5f);
  ring.write(samples.data(), chunkSize, origin);
  ipcClient->setAudioSource(&ring);

  for (int i = 0; i < 50; ++i) {
    if (ipcClient->isConnected())
      break;
    juce::Thread::sleep(100);
  }
  assert(ipcClient->isConnected());

  for (int i = 0; i < 50; ++i) {
    if (gotAudioChunk)
      break;
    juce::Thread::sleep(100);
  }

  ipcClient.reset();
  serverThread.join();
  server.close();

  assert(gotAudioChunk);
  assert(timeOk);

  std::cout << "[PASS] testOutgoingChunksStampDawSampleTime" << std::endl;
}

void testTranscriptionResultForwardsTimes() {
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT + 6, "127.0.0.1");
  assert(bound);

  struct TestListener : Punch2Pen::IPCClient::Listener {
    std::atomic<bool> got{false};
    std::atomic<bool> ok{false};
    void onTranscriptionReceived(const std::string &text, double startTime,
                                 double endTime, uint32_t captureEpoch) override {
      ok = (text == "hello" && startTime == 48000.0 && endTime == 52800.0 &&
            captureEpoch == 9);
      got = true;
    }
    void onStatusChanged(bool) override {}
  };

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = acceptAndHandshake(server);
    if (client == nullptr)
      return;

    juce::Thread::sleep(150);

    const std::string text = "hello";
    Punch2Pen::protocol::Header header;
    header.type = Punch2Pen::protocol::MessageType::TranscriptionResult;

    Punch2Pen::protocol::TranscriptionResultHeader resultHeader;
    resultHeader.textLength = (uint32_t)text.size();
    resultHeader.startTime = 48000.0;
    resultHeader.endTime = 52800.0;
    resultHeader.captureEpoch = 9;
    header.length =
        (uint32_t)(sizeof(resultHeader) + resultHeader.textLength);

    client->write(&header, sizeof(header));
    client->write(&resultHeader, sizeof(resultHeader));
    client->write(text.data(), (int)text.size());

    juce::Thread::sleep(200);
    client->close();
    delete client;
  });

  juce::Thread::sleep(100);
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(
      TEST_PORT + 6, /*autoLaunchEngine=*/false);
  TestListener listener;
  ipcClient->addListener(&listener);

  for (int i = 0; i < 50; ++i) {
    if (ipcClient->isConnected())
      break;
    juce::Thread::sleep(100);
  }
  assert(ipcClient->isConnected());

  for (int i = 0; i < 50; ++i) {
    if (listener.got.load())
      break;
    juce::Thread::sleep(100);
  }

  ipcClient->removeListener(&listener);
  ipcClient.reset();
  serverThread.join();
  server.close();

  assert(listener.got.load());
  assert(listener.ok.load());

  std::cout << "[PASS] testTranscriptionResultForwardsTimes" << std::endl;
}

void testRequestCaptureResetDrainsRingOnIpcThread() {
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(
      TEST_PORT + 7, /*autoLaunchEngine=*/false);

  Punch2Pen::AudioRingBuffer ring(4096);
  std::vector<float> samples(128, 0.5f);
  assert(ring.write(samples.data(), 128, 48000.0));
  assert(ring.getNumReady() == 128);

  ipcClient->setAudioSource(&ring);
  ipcClient->requestCaptureReset();

  for (int i = 0; i < 50; ++i) {
    if (ring.getNumReady() == 0 && !ipcClient->captureResetPending())
      break;
    juce::Thread::sleep(50);
  }

  assert(ring.getNumReady() == 0);
  assert(!ipcClient->captureResetPending());

  ipcClient.reset();
  std::cout << "[PASS] testRequestCaptureResetDrainsRingOnIpcThread"
            << std::endl;
}

void testTransportStopFlushesPartialChunk() {
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT + 8, "127.0.0.1");
  assert(bound);

  std::atomic<bool> gotChunk{false};
  std::atomic<bool> gotStop{false};
  std::atomic<bool> chunkOk{false};
  const int leftover = 128;
  const double origin = 24000.0;

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = acceptAndHandshake(server);
    if (client == nullptr)
      return;

    Punch2Pen::protocol::Header header;
    if (!readExact(*client, &header, sizeof(header))) {
      delete client;
      return;
    }

    if (header.type == Punch2Pen::protocol::MessageType::AudioChunk) {
      Punch2Pen::protocol::AudioChunkHeader chunkHeader;
      if (!readExact(*client, &chunkHeader, sizeof(chunkHeader))) {
        delete client;
        return;
      }
      chunkOk = (chunkHeader.numSamples == (uint32_t)leftover &&
                 chunkHeader.dawSampleTime == origin);
      std::vector<float> payload(leftover);
      readExact(*client, payload.data(), leftover * (int)sizeof(float));
      gotChunk = true;

      if (!readExact(*client, &header, sizeof(header))) {
        delete client;
        return;
      }
    }

    gotStop = (header.type == Punch2Pen::protocol::MessageType::TransportStop);
    delete client;
  });

  juce::Thread::sleep(100);
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(
      TEST_PORT + 8, /*autoLaunchEngine=*/false);
  ipcClient->setHostSampleRate(48000.0);

  Punch2Pen::AudioRingBuffer ring(4096);
  std::vector<float> samples(leftover, 0.25f);
  assert(ring.write(samples.data(), leftover, origin));
  ipcClient->setAudioSource(&ring);

  for (int i = 0; i < 50; ++i) {
    if (ipcClient->isConnected())
      break;
    juce::Thread::sleep(100);
  }
  assert(ipcClient->isConnected());

  ipcClient->flagTransportStop();

  for (int i = 0; i < 50; ++i) {
    if (gotStop.load())
      break;
    juce::Thread::sleep(100);
  }

  ipcClient.reset();
  serverThread.join();
  server.close();

  assert(gotChunk.load());
  assert(chunkOk.load());
  assert(gotStop.load());
  assert(ring.getNumReady() == 0);

  std::cout << "[PASS] testTransportStopFlushesPartialChunk" << std::endl;
}

void testStopFlushLeavesNewerEpochInRing() {
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT + 9, "127.0.0.1");
  assert(bound);

  std::atomic<int> audioChunks{0};
  std::atomic<bool> gotStop{false};
  std::atomic<bool> chunkOk{false};
  std::atomic<bool> stopBeforeNewerTake{true};
  const int leftoverA = 128;
  const int takeB = 256;
  const double originA = 24000.0;

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = acceptAndHandshake(server);
    if (client == nullptr)
      return;

    while (true) {
      Punch2Pen::protocol::Header header;
      if (!readExact(*client, &header, sizeof(header)))
        break;

      if (header.type == Punch2Pen::protocol::MessageType::AudioChunk) {
        Punch2Pen::protocol::AudioChunkHeader chunkHeader;
        if (!readExact(*client, &chunkHeader, sizeof(chunkHeader)))
          break;
        std::vector<float> payload(chunkHeader.numSamples);
        if (!readExact(*client, payload.data(),
                       (int)chunkHeader.numSamples * (int)sizeof(float)))
          break;
        if (gotStop.load())
          stopBeforeNewerTake = false;
        if (chunkHeader.numSamples == (uint32_t)leftoverA &&
            chunkHeader.dawSampleTime == originA)
          chunkOk = true;
        audioChunks.fetch_add(1);
      } else if (header.type == Punch2Pen::protocol::MessageType::TransportStop) {
        gotStop = true;
        break;
      } else if (header.length > 0) {
        std::vector<char> skip((size_t)header.length);
        if (!readExact(*client, skip.data(), (int)header.length))
          break;
      }
    }
    delete client;
  });

  juce::Thread::sleep(100);
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(
      TEST_PORT + 9, /*autoLaunchEngine=*/false);
  ipcClient->setHostSampleRate(48000.0);

  for (int i = 0; i < 50; ++i) {
    if (ipcClient->isConnected())
      break;
    juce::Thread::sleep(100);
  }
  assert(ipcClient->isConnected());

  Punch2Pen::AudioRingBuffer ring(8192);
  std::vector<float> samplesA(leftoverA, 0.25f);
  std::vector<float> samplesB(takeB, 0.75f);
  assert(ring.write(samplesA.data(), leftoverA, originA, 0));
  assert(ring.write(samplesB.data(), takeB, 96000.0, 1));
  ipcClient->setAudioSource(&ring);
  ipcClient->flagTransportStop(0);

  for (int i = 0; i < 50; ++i) {
    if (gotStop.load())
      break;
    juce::Thread::sleep(100);
  }

  ipcClient.reset();
  serverThread.join();
  server.close();

  assert(gotStop.load());
  assert(chunkOk.load());
  assert(audioChunks.load() == 1);
  assert(stopBeforeNewerTake.load());
  assert(ring.getNumReady() == takeB);
  assert(ring.peekEpoch() == 1);

  std::cout << "[PASS] testStopFlushLeavesNewerEpochInRing" << std::endl;
}

void testStopDoesNotEmitNewerTake() {
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT + 10, "127.0.0.1");
  assert(bound);

  std::atomic<int> audioChunks{0};
  std::atomic<bool> gotStop{false};

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = acceptAndHandshake(server);
    if (client == nullptr)
      return;

    while (true) {
      Punch2Pen::protocol::Header header;
      if (!readExact(*client, &header, sizeof(header)))
        break;
      if (header.type == Punch2Pen::protocol::MessageType::AudioChunk) {
        Punch2Pen::protocol::AudioChunkHeader chunkHeader;
        if (!readExact(*client, &chunkHeader, sizeof(chunkHeader)))
          break;
        std::vector<float> payload(chunkHeader.numSamples);
        if (!readExact(*client, payload.data(),
                       (int)chunkHeader.numSamples * (int)sizeof(float)))
          break;
        audioChunks.fetch_add(1);
      } else if (header.type ==
                 Punch2Pen::protocol::MessageType::TransportStop) {
        Punch2Pen::protocol::TransportStopHeader stopHeader;
        if (!readExact(*client, &stopHeader, sizeof(stopHeader)))
          break;
        gotStop = true;
        break;
      }
    }
    delete client;
  });

  juce::Thread::sleep(100);
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(
      TEST_PORT + 10, /*autoLaunchEngine=*/false);
  ipcClient->setHostSampleRate(48000.0);

  for (int i = 0; i < 50; ++i) {
    if (ipcClient->isConnected())
      break;
    juce::Thread::sleep(100);
  }
  assert(ipcClient->isConnected());

  const int takeB = 4096;
  Punch2Pen::AudioRingBuffer ring(8192);
  std::vector<float> samplesB(takeB, 0.5f);
  assert(ring.write(samplesB.data(), takeB, 96000.0, 1));
  ipcClient->setAudioSource(&ring);
  ipcClient->flagTransportStop(0);

  for (int i = 0; i < 50; ++i) {
    if (gotStop.load())
      break;
    juce::Thread::sleep(100);
  }

  ipcClient.reset();
  serverThread.join();
  server.close();

  assert(gotStop.load());
  assert(audioChunks.load() == 0);

  std::cout << "[PASS] testStopDoesNotEmitNewerTake" << std::endl;
}

void testQueuedStopsDrainEachEpoch() {
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT + 11, "127.0.0.1");
  assert(bound);

  std::atomic<int> audioChunks{0};
  std::atomic<int> stops{0};
  std::atomic<bool> orderOk{true};
  const int leftover = 128;
  const double originA = 24000.0;
  const double originB = 96000.0;

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = acceptAndHandshake(server);
    if (client == nullptr)
      return;

    uint32_t lastStopEpoch = 0;
    while (stops.load() < 2) {
      Punch2Pen::protocol::Header header;
      if (!readExact(*client, &header, sizeof(header)))
        break;
      if (header.type == Punch2Pen::protocol::MessageType::AudioChunk) {
        Punch2Pen::protocol::AudioChunkHeader chunkHeader;
        if (!readExact(*client, &chunkHeader, sizeof(chunkHeader)))
          break;
        std::vector<float> payload(chunkHeader.numSamples);
        if (!readExact(*client, payload.data(),
                       (int)chunkHeader.numSamples * (int)sizeof(float)))
          break;
        if (stops.load() == 0) {
          if (chunkHeader.captureEpoch != 0 ||
              chunkHeader.dawSampleTime != originA ||
              chunkHeader.numSamples != (uint32_t)leftover)
            orderOk = false;
        } else {
          if (chunkHeader.captureEpoch != 1 ||
              chunkHeader.dawSampleTime != originB ||
              chunkHeader.numSamples != (uint32_t)leftover)
            orderOk = false;
        }
        audioChunks.fetch_add(1);
      } else if (header.type == Punch2Pen::protocol::MessageType::TransportStop) {
        Punch2Pen::protocol::TransportStopHeader stopHeader;
        if (!readExact(*client, &stopHeader, sizeof(stopHeader)))
          break;
        if (stops.load() == 0 && stopHeader.captureEpoch != 0)
          orderOk = false;
        if (stops.load() == 1 && stopHeader.captureEpoch != 1)
          orderOk = false;
        if (audioChunks.load() != (int)(stops.load() + 1))
          orderOk = false;
        lastStopEpoch = stopHeader.captureEpoch;
        (void)lastStopEpoch;
        stops.fetch_add(1);
      } else if (header.length > 0) {
        std::vector<char> skip((size_t)header.length);
        if (!readExact(*client, skip.data(), (int)header.length))
          break;
      }
    }
    delete client;
  });

  juce::Thread::sleep(100);
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(
      TEST_PORT + 11, /*autoLaunchEngine=*/false);
  ipcClient->setHostSampleRate(48000.0);

  for (int i = 0; i < 50; ++i) {
    if (ipcClient->isConnected())
      break;
    juce::Thread::sleep(100);
  }
  assert(ipcClient->isConnected());

  Punch2Pen::AudioRingBuffer ring(8192);
  std::vector<float> samplesA(leftover, 0.25f);
  std::vector<float> samplesB(leftover, 0.75f);
  assert(ring.write(samplesA.data(), leftover, originA, 0));
  assert(ring.write(samplesB.data(), leftover, originB, 1));
  ipcClient->setAudioSource(&ring);
  ipcClient->flagTransportStop(0);
  ipcClient->flagTransportStop(1);

  for (int i = 0; i < 50; ++i) {
    if (stops.load() >= 2)
      break;
    juce::Thread::sleep(100);
  }

  ipcClient.reset();
  serverThread.join();
  server.close();

  assert(audioChunks.load() == 2);
  assert(stops.load() == 2);
  assert(orderOk.load());
  assert(ring.getNumReady() == 0);

  std::cout << "[PASS] testQueuedStopsDrainEachEpoch" << std::endl;
}

void testHandshakeIsFirstMessage() {
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT + 12, "127.0.0.1");
  assert(bound);

  std::atomic<bool> gotHandshake{false};
  std::atomic<bool> handshakeOk{false};

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = server.waitForNextConnection();
    if (client == nullptr)
      return;

    Punch2Pen::protocol::Header header;
    if (!readExact(*client, &header, sizeof(header))) {
      delete client;
      return;
    }

    gotHandshake = (header.type == Punch2Pen::protocol::MessageType::Handshake &&
                    header.length ==
                        (uint32_t)sizeof(Punch2Pen::protocol::Handshake));
    Punch2Pen::protocol::Handshake handshake;
    if (gotHandshake && readExact(*client, &handshake, sizeof(handshake)))
      handshakeOk = (handshake.version == Punch2Pen::protocol::kProtocolVersion);

    Punch2Pen::protocol::Header reply;
    reply.type = Punch2Pen::protocol::MessageType::HandshakeResponse;
    Punch2Pen::protocol::HandshakeResponse response;
    response.version = Punch2Pen::protocol::kProtocolVersion;
    response.accepted = 1;
    reply.length = (uint32_t)sizeof(response);
    client->write(&reply, sizeof(reply));
    client->write(&response, sizeof(response));
    delete client;
  });

  juce::Thread::sleep(100);
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(
      TEST_PORT + 12, /*autoLaunchEngine=*/false);

  for (int i = 0; i < 50; ++i) {
    if (ipcClient->isConnected())
      break;
    juce::Thread::sleep(100);
  }

  ipcClient.reset();
  serverThread.join();
  server.close();

  assert(gotHandshake.load());
  assert(handshakeOk.load());

  std::cout << "[PASS] testHandshakeIsFirstMessage" << std::endl;
}

void testRejectedHandshakeDoesNotConnect() {
  juce::StreamingSocket server;
  bool bound = server.createListener(TEST_PORT + 13, "127.0.0.1");
  assert(bound);

  std::thread serverThread([&]() {
    juce::StreamingSocket *client = server.waitForNextConnection();
    if (client == nullptr)
      return;

    Punch2Pen::protocol::Header header;
    if (!readExact(*client, &header, sizeof(header))) {
      delete client;
      return;
    }
    Punch2Pen::protocol::Handshake handshake;
    readExact(*client, &handshake, sizeof(handshake));

    Punch2Pen::protocol::Header reply;
    reply.type = Punch2Pen::protocol::MessageType::HandshakeResponse;
    Punch2Pen::protocol::HandshakeResponse response;
    response.version = Punch2Pen::protocol::kProtocolVersion;
    response.accepted = 0;
    reply.length = (uint32_t)sizeof(response);
    client->write(&reply, sizeof(reply));
    client->write(&response, sizeof(response));
    juce::Thread::sleep(50);
    client->close();
    delete client;
  });

  juce::Thread::sleep(100);
  auto ipcClient = std::make_unique<Punch2Pen::IPCClient>(
      TEST_PORT + 13, /*autoLaunchEngine=*/false);

  for (int i = 0; i < 20; ++i) {
    if (ipcClient->isConnected())
      break;
    juce::Thread::sleep(100);
  }

  assert(!ipcClient->isConnected());

  ipcClient.reset();
  serverThread.join();
  server.close();

  std::cout << "[PASS] testRejectedHandshakeDoesNotConnect" << std::endl;
}

void testPluginBundleContentsDirFindsNestedHelper() {
  const juce::File root =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("p2p-launch-paths-test");
  root.deleteRecursively();

  const juce::File component = root.getChildFile("punch2pen.component");
  const juce::File auBinary =
      component.getChildFile("Contents/MacOS/punch2pen");
  const juce::File auHelper = component.getChildFile(
      "Contents/Helpers/punch2penEngine.app/Contents/MacOS/punch2penEngine");
  auBinary.getParentDirectory().createDirectory();
  auHelper.getParentDirectory().createDirectory();
  auBinary.replaceWithText("fake-au");
  auHelper.replaceWithText("fake-engine");

  const juce::File fromBinary =
      Punch2Pen::pluginBundleContentsDir(auBinary);
  assert(fromBinary == component.getChildFile("Contents"));
  assert(Punch2Pen::nestedEngineApp(fromBinary).getFileName() ==
         "punch2penEngine.app");
  assert(Punch2Pen::engineAppInnerBinary(
             Punch2Pen::nestedEngineApp(fromBinary))
             .existsAsFile());

  const juce::File fromBundle =
      Punch2Pen::pluginBundleContentsDir(component);
  assert(fromBundle == component.getChildFile("Contents"));

  const juce::File vst3 = root.getChildFile("punch2pen.vst3");
  const juce::File vst3Binary = vst3.getChildFile("Contents/MacOS/punch2pen");
  vst3Binary.getParentDirectory().createDirectory();
  vst3Binary.replaceWithText("fake-vst3");
  const juce::File fromVst3 = Punch2Pen::pluginBundleContentsDir(vst3Binary);
  assert(fromVst3 == vst3.getChildFile("Contents"));

  const juce::File leftover =
      Punch2Pen::leftoverDevEngine("/tmp/p2p-home");
  assert(leftover ==
         juce::File("/tmp/p2p-home/punch2pen/bin/punch2penEngine"));
  assert(Punch2Pen::systemEngineApp() ==
         juce::File("/Applications/Punch2Pen/punch2penEngine.app"));
  assert(Punch2Pen::systemEngineCli() ==
         juce::File("/Applications/Punch2Pen/punch2penEngine"));

  root.deleteRecursively();
  std::cout << "[PASS] testPluginBundleContentsDirFindsNestedHelper"
            << std::endl;
}

int main() {
  // RAII initializer for JUCE Thread internals. JUCE only ships the _GUI
  // variant; per its own header docs, it's the recommended initializer for
  // console-app main()s, and ensures MessageManager isn't leaked under
  // JUCE_CHECK_MEMORY_LEAKS. Matches PluginProcessorStateTest.cpp.
  juce::ScopedJuceInitialiser_GUI juceInit;
  std::signal(SIGPIPE, SIG_IGN);

  testConnectionAndAudioChunk();
  testOutgoingChunksUseHostSampleRate();
  testOutgoingChunksStampDawSampleTime();
  testTranscriptionResultForwardsTimes();
  testRequestCaptureResetDrainsRingOnIpcThread();
  testTransportStopFlushesPartialChunk();
  testStopFlushLeavesNewerEpochInRing();
  testStopDoesNotEmitNewerTake();
  testQueuedStopsDrainEachEpoch();
  testHandshakeIsFirstMessage();
  testRejectedHandshakeDoesNotConnect();
  testPluginBundleContentsDirFindsNestedHelper();
  testTransportStop();
  testCorrection();
  testDisconnectDetection();

  std::cout << "All IPCClient tests passed!" << std::endl;
  return 0;
}
