#pragma once

#include <JuceHeader.h>

namespace punch2pen {

// Walk from a loaded plugin image (Mach-O, bundle, or Contents/MacOS
// binary) up to the AU/VST3 Contents directory. Logic's AU host executable
// is not a valid start path — callers should pass dladdr of this plugin.
inline juce::File pluginBundleContentsDir(const juce::File &imageFile) {
  juce::File cursor = imageFile;
  for (int i = 0; i < 12 && cursor != cursor.getParentDirectory(); ++i) {
    if (cursor.hasFileExtension("component") ||
        cursor.hasFileExtension("vst3"))
      return cursor.getChildFile("Contents");
    if (cursor.getFileName() == "Contents" &&
        cursor.getChildFile("MacOS").isDirectory())
      return cursor;
    cursor = cursor.getParentDirectory();
  }
  if (imageFile.getParentDirectory().getFileName() == "MacOS")
    return imageFile.getParentDirectory().getParentDirectory();
  return imageFile.getParentDirectory();
}

inline juce::File nestedEngineApp(const juce::File &contentsDir) {
  return contentsDir.getChildFile("Helpers/punch2penEngine.app");
}

inline juce::File engineAppInnerBinary(const juce::File &app) {
  return app.getChildFile("Contents/MacOS/punch2penEngine");
}

inline juce::File systemEngineApp() {
  return juce::File("/Applications/Punch2Pen/punch2penEngine.app");
}

inline juce::File systemEngineCli() {
  return juce::File("/Applications/Punch2Pen/punch2penEngine");
}

inline juce::File leftoverDevEngine(const juce::String &homeDir) {
  return juce::File(homeDir).getChildFile("punch2pen/bin/punch2penEngine");
}

} // namespace punch2pen
