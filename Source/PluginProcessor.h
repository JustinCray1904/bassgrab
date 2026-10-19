#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>
#include <vector>
#include "BassAnalyzer.h"

class BassGrabProcessor : public juce::AudioProcessor,
                          public juce::ChangeBroadcaster
{
public:
    // Everything the user can tweak. Only touched on the message thread.
    struct UiState
    {
        double bpm         = 120.0;
        bool   syncHost    = true;
        int    quantIndex  = 3;      // 0 off, 1 = 1/4, 2 = 1/8, 3 = 1/16, 4 = 1/32
        int    rangeIndex  = 1;      // 0 sub/808, 1 bass, 2 low-mid
        float  sensitivity = 0.33f;  // 0..1  (higher = detects more, noisier)
        int    octaveShift = 0;      // in octaves, exported MIDI only
    };

    BassGrabProcessor();
    ~BassGrabProcessor() override;

    // --- AudioProcessor ---------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BassGrab"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- BassGrab API (message thread) --------------------------------------------------------
    bool loadFile (const juce::File&);
    void startRecording();
    void stopRecording();
    bool isRecording() const        { return recording.load(); }
    double getRecordedSeconds() const { return recSampleRate > 0 ? recPos.load() / recSampleRate : 0.0; }

    UiState& ui() { return uiState; }
    void settingsChanged();                         // call after changing range / sensitivity

    double getEffectiveBpm() const;
    double getHostBpm() const       { return hostBpm.load(); }
    bool   isAnalysing() const      { return analysing.load(); }

    juce::String getSourceName() const;
    double getDurationSec() const;
    std::vector<float> getPeaks() const;
    std::shared_ptr<const bassgrab::Analysis> getAnalysis() const;
    std::vector<bassgrab::ExportNote> getExportNotes() const;

    juce::File writeMidiToTemp();
    bool saveMidiTo (const juce::File&);

    static double gridBeatsForIndex (int idx);

private:
    class Worker : public juce::Thread
    {
    public:
        explicit Worker (BassGrabProcessor& p) : juce::Thread ("BassGrab analysis"), owner (p) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                wait (-1);
                if (threadShouldExit()) break;
                owner.runAnalysis();
            }
        }
    private:
        BassGrabProcessor& owner;
    };

    void requestAnalysis();
    void runAnalysis();
    void setAudio (std::vector<float>&& mono, double sr, const juce::String& name);
    std::vector<uint8_t> buildMidiBytes() const;

    UiState uiState;

    mutable juce::CriticalSection dataLock;
    std::vector<float> audio;                  // mono, original sample rate
    double audioSr = 0.0;
    juce::String sourceName;
    std::vector<float> peaks;                  // waveform thumbnail
    bassgrab::Settings analysisSettings;
    std::shared_ptr<const bassgrab::Analysis> analysis;

    juce::AudioFormatManager formats;

    // live capture
    std::vector<float> recBuffer;
    double recSampleRate = 44100.0;
    std::atomic<int>  recPos { 0 };
    std::atomic<bool> recording { false };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<bool> analysing { false };

    Worker worker { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassGrabProcessor)
};
