#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "RingBuffer.h"
#include <cmath>
#include <cstdlib>

Punch2PenAudioProcessor::Punch2PenAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
  // Initialize Ring Buffer (10 seconds @ 192kHz approx)
  audioRingBuffer = std::make_unique<punch2pen::AudioRingBuffer>(192000 * 10);

  // Initialize IPC
  ipcClient = std::make_unique<punch2pen::IPCClient>();
}

Punch2PenAudioProcessor::~Punch2PenAudioProcessor() {}

const juce::String Punch2PenAudioProcessor::getName() const {
  return JucePlugin_Name;
}

bool Punch2PenAudioProcessor::acceptsMidi() const {
#if JucePlugin_WantsMidiInput
  return true;
#else
  return false;
#endif
}

bool Punch2PenAudioProcessor::producesMidi() const {
#if JucePlugin_ProducesMidiOutput
  return true;
#else
  return false;
#endif
}

bool Punch2PenAudioProcessor::isMidiEffect() const {
#if JucePlugin_IsMidiEffect
  return true;
#else
  return false;
#endif
}

double Punch2PenAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int Punch2PenAudioProcessor::getNumPrograms() {
  return 1; // NB: some hosts don't cope very well if you tell them there are 0
            // programs, so this should be at least 1, even if you're not really
            // implementing programs.
}

int Punch2PenAudioProcessor::getCurrentProgram() { return 0; }

// ... (previous code)

void Punch2PenAudioProcessor::setCurrentProgram(int /*programIndex*/) {}

const juce::String
Punch2PenAudioProcessor::getProgramName(int /*programIndex*/) {
  return {};
}

void Punch2PenAudioProcessor::changeProgramName(
    int /*programIndex*/, const juce::String & /*newName*/) {}

void Punch2PenAudioProcessor::prepareToPlay(double /*sampleRate*/,
                                            int /*samplesPerBlock*/) {
  if (const char *modeEnv = std::getenv("PUNCH2PEN_TRANSCRIBE_MODE")) {
    const std::string mode(modeEnv);
    if (mode == "online") {
      ipcClient->setTranscriptionMode(
          punch2pen::IPCClient::TranscriptionMode::Online);
    } else {
      ipcClient->setTranscriptionMode(
          punch2pen::IPCClient::TranscriptionMode::Offline);
    }
  }
}

void Punch2PenAudioProcessor::releaseResources() {
  // When playback stops, you can use this as an opportunity to free up any
  // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool Punch2PenAudioProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
#if JucePlugin_IsMidiEffect
  juce::ignoreUnused(layouts);
  return true;
#else
  // This is the place where you check if the layout is supported.
  // In this template code we only support mono or stereo.
  // Some plugin hosts, such as certain GarageBand versions, will only
  // load plugins that support stereo bus layouts.
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
      layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;

  // This checks if the input layout matches the output layout
#if !JucePlugin_IsSynth
  if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
    return false;
#endif

  return true;
#endif
}
#endif

void Punch2PenAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                           juce::MidiBuffer &midiMessages) {
  juce::ignoreUnused(midiMessages);
  juce::ScopedNoDenormals noDenormals;
  auto totalNumInputChannels = getTotalNumInputChannels();
  auto totalNumOutputChannels = getTotalNumOutputChannels();

  for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
    buffer.clear(i, 0, buffer.getNumSamples());

  bool isRecording = transportIsRecording.load();

  // 1. Get Transport Info
  if (auto *ph = getPlayHead()) {
    if (auto pos = ph->getPosition()) {
      if (auto bpm = pos->getBpm())
        currentBpm.store(*bpm);

      if (auto ppq = pos->getPpqPosition())
        transportPpq.store(*ppq);

      if (auto ts = pos->getTimeSignature()) {
        transportTimeSigNum.store(ts->numerator);
        transportTimeSigDenom.store(ts->denominator);
      }

      transportIsPlaying.store(pos->getIsPlaying());
      isRecording = pos->getIsRecording();
      transportIsRecording.store(isRecording);
    }
  }

  // 2. Capture Audio if Recording
  if (isRecording) {
    // We only take the first channel for voice recognition usually
    auto *channelData = buffer.getReadPointer(0);

    // If ring buffer is safe, write
    audioRingBuffer->write(channelData, buffer.getNumSamples());
  }

  if (wasRecordingLastBlock && !isRecording) {
    if (ipcClient)
      ipcClient->flagTransportStop();
  }

  wasRecordingLastBlock = isRecording;
}

bool Punch2PenAudioProcessor::hasEditor() const {
  return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor *Punch2PenAudioProcessor::createEditor() {
  return new Punch2PenAudioProcessorEditor(*this);
}

void Punch2PenAudioProcessor::getStateInformation(juce::MemoryBlock &destData) {
  juce::ValueTree state("Punch2PenState");

  if (ipcClient) {
    state.setProperty(
        "transcriptionMode",
        ipcClient->getTranscriptionMode() ==
                punch2pen::IPCClient::TranscriptionMode::Online
            ? "online"
            : "offline",
        nullptr);
  }

  state.setProperty("bpm", currentBpm.load(), nullptr);

  std::unique_ptr<juce::XmlElement> xml(state.createXml());
  if (xml != nullptr)
    copyXmlToBinary(*xml, destData);
}

void Punch2PenAudioProcessor::setStateInformation(const void *data,
                                                  int sizeInBytes) {
  std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
  if (xml == nullptr)
    return;

  juce::ValueTree state = juce::ValueTree::fromXml(*xml);
  if (!state.isValid())
    return;

  if (state.hasProperty("transcriptionMode") && ipcClient) {
    juce::String mode = state.getProperty("transcriptionMode").toString();
    ipcClient->setTranscriptionMode(
        mode == "online" ? punch2pen::IPCClient::TranscriptionMode::Online
                         : punch2pen::IPCClient::TranscriptionMode::Offline);
  }

  if (state.hasProperty("bpm")) {
    currentBpm.store((double)state.getProperty("bpm"));
  }
}

// This creates new instances of the plugin..
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new Punch2PenAudioProcessor();
}

Punch2PenAudioProcessor::TransportPosition
Punch2PenAudioProcessor::getTransportPosition() const {
  TransportPosition position;
  position.ppq = transportPpq.load();
  position.bpm = currentBpm.load();
  position.timeSigNum = transportTimeSigNum.load();
  position.timeSigDenom = transportTimeSigDenom.load();
  position.isPlaying = transportIsPlaying.load();
  position.isRecording = transportIsRecording.load();

  const auto ppqPerBeat = 4.0 / juce::jmax(1.0, (double)position.timeSigDenom);
  const auto ppqPerBar = juce::jmax(1, position.timeSigNum) * ppqPerBeat;
  const auto safePpq = juce::jmax(0.0, position.ppq);
  position.bar = (int)(safePpq / ppqPerBar) + 1;
  position.beat = (int)(std::fmod(safePpq, ppqPerBar) / ppqPerBeat) + 1;
  return position;
}
