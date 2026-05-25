#include "TranscriptView.h"

namespace punch2pen {

TranscriptView::TranscriptView() {
  vBlankAttachment = std::make_unique<juce::VBlankAttachment>(
      this, [this] {
        const float delta = targetScrollY - currentScrollY;
        if (std::abs(delta) > 0.1f) {
          currentScrollY += delta * 0.15f;
          repaint();
        }
      });
}

TranscriptView::~TranscriptView() = default;

void TranscriptView::paint(juce::Graphics &g) {
  g.fillAll(bgPrimary);

  juce::ScopedLock lock(dataLock);

  int yOffset = getHeight() / 2 - static_cast<int>(currentScrollY);
  const int lineHeight = 40;

  g.setFont(juce::FontOptions(16.0f));

  if (!isConnected) {
    g.setColour(accentPrimary.withAlpha(0.8f));
    g.drawText("Waiting for engine connection...", 16, 12, getWidth() - 32, 24,
               juce::Justification::centredLeft);
  }

  for (const auto &line : transcriptLines) {
    int xOffset = 30;

    for (const auto &word : line.words) {
      g.setColour(textPrimary.withAlpha(word.currentAlpha));
      const juce::String spacedWord = juce::String(word.text) + " ";
      const int wordWidth = g.getCurrentFont().getStringWidth(spacedWord);
      g.drawText(spacedWord, xOffset, yOffset, wordWidth, lineHeight,
                 juce::Justification::centredLeft);
      xOffset += wordWidth;
    }

    yOffset += lineHeight;
  }
}

void TranscriptView::resized() {}

void TranscriptView::updatePlaybackPosition(double currentDAWSample) {
  juce::ScopedLock lock(dataLock);
  lastKnownPlayheadPosition = currentDAWSample;

  int activeLineIndex = -1;
  for (size_t i = 0; i < transcriptLines.size(); ++i) {
    for (auto &word : transcriptLines[i].words) {
      if (currentDAWSample >= word.startSample && currentDAWSample <= word.endSample) {
        word.currentAlpha = 1.0f;
        activeLineIndex = static_cast<int>(i);
      } else if (currentDAWSample > word.endSample) {
        word.currentAlpha = 0.4f;
      } else {
        word.currentAlpha = 0.6f;
      }
    }
  }

  if (activeLineIndex != -1) {
    targetScrollY = static_cast<float>(activeLineIndex * 40);
  }
}

void TranscriptView::appendStreamingText(const std::string &newText,
                                         double sampleTime,
                                         bool isProvisional) {
  juce::ignoreUnused(isProvisional);

  juce::ScopedLock lock(dataLock);
  if (transcriptLines.empty() || transcriptLines.back().words.size() >= 8) {
    LyricLine line;
    line.lineStartSample = sampleTime;
    transcriptLines.push_back(line);
  }

  LyricWord word;
  word.text = newText;
  word.startSample = sampleTime;
  word.endSample = sampleTime + 4800.0;
  transcriptLines.back().words.push_back(word);
}

void TranscriptView::onTranscriptionReceived(const std::string &text) {
  juce::MessageManager::callAsync([this, text] {
    streamCursorSample += 4800.0;
    appendStreamingText(text, streamCursorSample, true);
    repaint();
  });
}

void TranscriptView::onStatusChanged(bool connected) {
  juce::MessageManager::callAsync([this, connected] {
    isConnected = connected;
    repaint();
  });
}

} // namespace punch2pen
