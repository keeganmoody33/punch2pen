#include "../Source/RingBuffer.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace Punch2Pen = punch2pen;

void testWriteAndReadBack() {
  Punch2Pen::AudioRingBuffer rb(1024);

  const int N = 256;
  std::vector<float> input(N);
  for (int i = 0; i < N; ++i)
    input[i] = (float)i * 0.01f;

  bool ok = rb.write(input.data(), N);
  assert(ok);

  std::vector<float> output(N, 0.0f);
  int readCount = rb.read(output.data(), N);
  assert(readCount == N);

  for (int i = 0; i < N; ++i)
    assert(output[i] == input[i]);

  std::cout << "[PASS] testWriteAndReadBack" << std::endl;
}

void testOverflowDetection() {
  const int capacity = 64;
  Punch2Pen::AudioRingBuffer rb(capacity);

  std::vector<float> data(capacity + 1, 1.0f);
  bool ok = rb.write(data.data(), capacity + 1);
  assert(!ok);

  std::cout << "[PASS] testOverflowDetection" << std::endl;
}

void testWrapAround() {
  const int capacity = 128;
  Punch2Pen::AudioRingBuffer rb(capacity);

  // Fill most of the buffer
  const int firstWrite = 100;
  std::vector<float> first(firstWrite);
  for (int i = 0; i < firstWrite; ++i)
    first[i] = (float)i;

  bool ok = rb.write(first.data(), firstWrite);
  assert(ok);

  // Read those out to free space
  std::vector<float> tmp(firstWrite);
  int readCount = rb.read(tmp.data(), firstWrite);
  assert(readCount == firstWrite);

  // Write again crossing the boundary
  const int secondWrite = 80;
  std::vector<float> second(secondWrite);
  for (int i = 0; i < secondWrite; ++i)
    second[i] = (float)(i + 1000);

  ok = rb.write(second.data(), secondWrite);
  assert(ok);

  std::vector<float> result(secondWrite);
  readCount = rb.read(result.data(), secondWrite);
  assert(readCount == secondWrite);

  for (int i = 0; i < secondWrite; ++i)
    assert(result[i] == second[i]);

  std::cout << "[PASS] testWrapAround" << std::endl;
}

void testGetNumReady() {
  Punch2Pen::AudioRingBuffer rb(256);

  assert(rb.getNumReady() == 0);

  std::vector<float> data(50, 1.0f);
  rb.write(data.data(), 50);
  assert(rb.getNumReady() == 50);

  std::vector<float> out(20);
  rb.read(out.data(), 20);
  assert(rb.getNumReady() == 30);

  std::cout << "[PASS] testGetNumReady" << std::endl;
}

void testReset() {
  Punch2Pen::AudioRingBuffer rb(256);

  std::vector<float> data(100, 1.0f);
  rb.write(data.data(), 100);
  assert(rb.getNumReady() == 100);

  rb.reset();
  assert(rb.getNumReady() == 0);

  std::cout << "[PASS] testReset" << std::endl;
}

int main() {
  testWriteAndReadBack();
  testOverflowDetection();
  testWrapAround();
  testGetNumReady();
  testReset();

  std::cout << "All RingBuffer tests passed!" << std::endl;
  return 0;
}
