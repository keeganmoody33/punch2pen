#include "PluginProcessor.h"
#include "PluginEditor.h"

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

void Punch2PenAudioProcessor::setCurrentProgram(int index) {}

const juce::String Punch2PenAudioProcessor::getProgramName(int index) {
  return {};
}

void Punch2PenAudioProcessor::changeProgramName(int index,
                                                const juce::String &newName) {}

void Punch2PenAudioProcessor::prepareToPlay(double sampleRate,
                                            int samplesPerBlock) {
  // Use this method as the place to do any pre-playback
  // initialisation that you need..
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
  juce::ScopedNoDenormals noDenormals;
  auto totalNumInputChannels = getTotalNumInputChannels();
  auto totalNumOutputChannels = getTotalNumOutputChannels();

  for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
    buffer.clear(i, 0, buffer.getNumSamples());

  // 1. Get Transport Info
  if (auto *ph = getPlayHead()) {
    if (auto pos = ph->getPosition()) {
      if (auto bpm = pos->getBpm())
        currentBpm = *bpm;

      lastTransportPosition.bpm = *currentBpm;

      if (auto ppq = pos->getPpqPosition())
        lastTransportPosition.ppq = *ppq;

      if (auto ts = pos->getTimeSignature()) {
        lastTransportPosition.timeSigNum = ts->numerator;
        lastTransportPosition.timeSigDenom = ts->denominator;
      }

      lastTransportPosition.isPlaying = pos->getIsPlaying();
      lastTransportPosition.isRecording = pos->getIsRecording();
    }
  }

  // 2. Capture Audio if Recording
  if (lastTransportPosition.isRecording) {
    // We only take the first channel for voice recognition usually
    auto *channelData = buffer.getReadPointer(0);

    // If ring buffer is safe, write
    audioRingBuffer->write(channelData, buffer.getNumSamples());
  }
}

bool Punch2PenAudioProcessor::hasEditor() const {
  return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor *Punch2PenAudioProcessor::createEditor() {
  return new Punch2PenAudioProcessorEditor(*this);
}

void Punch2PenAudioProcessor::getStateInformation(juce::MemoryBlock &destData) {
  // You should use this method to store your parameters in the memory block.
  // You could do that either as raw data, or use the XML or ValueTree classes
  // as intermediaries
}

void Punch2PenAudioProcessor::setStateInformation(const void *data,
                                                  int sizeInBytes) {
  // You should use this method to restore your parameters from this memory
  // block, whose contents will have been created by the getStateInformation()
  // call.
}

// This creates new instances of the plugin..
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new Punch2PenAudioProcessor();
}

Punch2PenAudioProcessor::TransportPosition
Punch2PenAudioProcessor::getTransportPosition() const {
  return lastTransportPosition;
}
