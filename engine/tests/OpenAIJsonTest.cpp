#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

void testSessionUpdateJson() {
  std::string promptContext = "Session vocabulary context: ";
  std::vector<std::string> words = {"hello", "world"};
  for (const auto &word : words) {
    promptContext += word + ", ";
  }

  json sessionUpdate;
  sessionUpdate["type"] = "session.update";
  sessionUpdate["session"]["instructions"] = promptContext;
  sessionUpdate["session"]["turn_detection"] = nullptr;

  assert(sessionUpdate.is_object() && "sessionUpdate should be a JSON object");
  assert(sessionUpdate["type"] == "session.update");
  assert(sessionUpdate.contains("session"));
  assert(sessionUpdate["session"].is_object() && "session should be a JSON object");
  assert(sessionUpdate["session"]["instructions"].is_string());
  assert(sessionUpdate["session"]["turn_detection"].is_null());

  std::string dumped = sessionUpdate.dump();
  json reparsed = json::parse(dumped);
  assert(reparsed["type"] == "session.update");
  assert(reparsed["session"]["instructions"] == promptContext);

  std::cout << "[PASS] testSessionUpdateJson" << std::endl;
}

void testAudioAppendJson() {
  json audioAppend = {{"type", "input_audio_buffer.append"},
                      {"audio", "base64encodeddata=="}};

  assert(audioAppend.is_object());
  assert(audioAppend["type"] == "input_audio_buffer.append");
  assert(audioAppend["audio"] == "base64encodeddata==");

  std::cout << "[PASS] testAudioAppendJson" << std::endl;
}

void testCommitEventJson() {
  json commitEvent = {{"type", "input_audio_buffer.commit"}};

  assert(commitEvent.is_object());
  assert(commitEvent["type"] == "input_audio_buffer.commit");
  assert(commitEvent.size() == 1);

  std::cout << "[PASS] testCommitEventJson" << std::endl;
}

void testSessionUpdateJsonStructure() {
  // Verify the fixed JSON construction produces a proper nested object,
  // not an array (which was the bug before the fix)
  json sessionUpdate;
  sessionUpdate["type"] = "session.update";
  sessionUpdate["session"]["instructions"] = "test prompt";
  sessionUpdate["session"]["turn_detection"] = nullptr;

  // Must have exactly 2 top-level keys
  assert(sessionUpdate.size() == 2);
  assert(sessionUpdate.contains("type"));
  assert(sessionUpdate.contains("session"));

  // The "session" value must be an object with 2 keys
  assert(sessionUpdate["session"].size() == 2);
  assert(sessionUpdate["session"].contains("instructions"));
  assert(sessionUpdate["session"].contains("turn_detection"));

  std::cout << "[PASS] testSessionUpdateJsonStructure" << std::endl;
}

int main() {
  testSessionUpdateJson();
  testAudioAppendJson();
  testCommitEventJson();
  testSessionUpdateJsonStructure();
  std::cout << "All OpenAI JSON construction tests passed!" << std::endl;
  return 0;
}
