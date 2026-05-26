#pragma once

#include "IPCClient.h"
#include "PluginProcessor.h"
#include <JuceHeader.h>
#include <atomic>

namespace punch2pen {

/**
    WebView-based plugin editor.

    Replaces the native Punch2PenAudioProcessorEditor. Loads
    Source/ui/public/index.html (embedded as binary data via
    BinaryData::getNamedResource) and bridges to the page using
    juce::WebBrowserComponent's native-function API.

    C++ → JS (called from this class via evaluateJavascript / NativeFunction
    invokeFunction):
      appendWord(text, startSample, endSample, barNumber)
      updatePlayhead(currentDAWSample)
      setConnectionStatus(connected)
      updatePosition(bar, beat)
      setState(stateName)

    JS → C++ (registered as native functions on window.punch2pen):
      onWordClicked(word, x, y)
      submitCorrection(original, corrected)
      onCorrectionCancelled()
      onReady()
 */
class WebViewEditor : public juce::Component,
                      public IPCClient::Listener,
                      private juce::Timer {
public:
  explicit WebViewEditor(Punch2PenAudioProcessor &);
  ~WebViewEditor() override;

  void paint(juce::Graphics &g) override;
  void resized() override;

  // IPCClient::Listener
  void onTranscriptionReceived(const std::string &text) override;
  void onStatusChanged(bool connected) override;

private:
  void timerCallback() override;

  // Push a fragment of JS into the page. If the page hasn't signalled
  // onReady() yet, the script is buffered when queueIfPending is true and
  // dropped otherwise — used for transient 30Hz updates (playhead etc.)
  // that would otherwise grow the queue unboundedly if the WebView never
  // initialises (e.g. missing WebView2 runtime).
  void runJs(const juce::String &script, bool queueIfPending = true);

  // Helpers that wrap the C++ → JS contract.
  void jsAppendWord(const juce::String &text,
                    double startSample, double endSample, int bar);
  void jsUpdatePlayhead(double sample);
  void jsUpdatePosition(int bar, int beat);
  void jsSetConnectionStatus(bool connected);
  void jsSetState(const juce::String &state);

  // Native callbacks invoked by the page.
  juce::var nativeOnWordClicked(const juce::Array<juce::var> &args);
  juce::var nativeSubmitCorrection(const juce::Array<juce::var> &args);
  juce::var nativeOnCorrectionCancelled(const juce::Array<juce::var> &args);
  juce::var nativeOnReady(const juce::Array<juce::var> &args);

  Punch2PenAudioProcessor &audioProcessor;
  std::unique_ptr<juce::WebBrowserComponent> webView;

  // Tracks what we last told the page, so we don't spam the bridge.
  bool   lastConnected     = false;
  double streamCursorSample = 0.0;
  std::atomic<uint32_t> takeGeneration { 0 };
  int    lastBar           = -1;
  int    lastBeat          = -1;
  juce::String lastState   = {};
  bool   pageReady         = false;

  // Buffer for any setState / append calls that fire before onReady.
  juce::StringArray queuedJs;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebViewEditor)
};

} // namespace punch2pen
