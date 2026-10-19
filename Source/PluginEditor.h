#pragma once

#include "PluginProcessor.h"
#include <functional>

//==============================================================================
// Piano-roll style display of the detected bass line. Click-and-drag anywhere on it to pull the
// notes out of the plugin as a .mid file - drop onto an FL Studio piano roll, channel or playlist.
class PianoRollView : public juce::Component
{
public:
    std::function<void()> onDragOut;

    void setData (std::vector<bassgrab::ExportNote> n, std::vector<float> p, double durationBeats);
    void setMessage (const juce::String& m)  { message = m; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { dragFired = false; }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override { dragFired = false; }

private:
    std::vector<bassgrab::ExportNote> notes;
    std::vector<float> peaks;
    double durationBeats = 0.0;
    juce::String message;
    bool dragFired = false;
};

//==============================================================================
class BassGrabEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       public juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit BassGrabEditor (BassGrabProcessor&);
    ~BassGrabEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // drop an audio file anywhere on the plugin window
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshView(); }

private:
    void timerCallback() override;
    void refreshView();
    void startDragOut();
    void toggleRecording();
    void finishRecording();
    void openFileDialog();
    void saveMidiDialog();
    void loadAndReport (const juce::File&);

    BassGrabProcessor& proc;

    juce::TextButton openBtn { "Open audio..." }, recBtn { "Record from input" }, saveBtn { "Save .mid..." };
    juce::Label fileLabel, bpmLabel, quantLabel, octLabel, rangeLabel, sensLabel, infoLabel;
    juce::Slider bpmSlider, sensSlider;
    juce::ToggleButton syncBtn { "Host BPM" };
    juce::ComboBox quantBox, rangeBox, octBox;
    PianoRollView view;
    std::unique_ptr<juce::FileChooser> chooser;

    bool recUiOn = false;
    bool wasAnalysing = false;
    double lastHostBpm = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassGrabEditor)
};
