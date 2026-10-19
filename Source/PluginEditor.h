#pragma once

#include "PluginProcessor.h"
#include <functional>

//==============================================================================
// Dark theme with a teal accent
class BassGrabLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BassGrabLookAndFeel();
};

//==============================================================================
// Piano-roll view of the detected bass line for the selected loop window.
// Click-and-drag anywhere on it to pull the notes out as a .mid file.
class PianoRollView : public juce::Component
{
public:
    std::function<void()> onDragOut;

    void setData (std::vector<bassgrab::ExportNote> n, double windowBeats, int firstBar);
    void setMessage (const juce::String& m)  { message = m; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { dragFired = false; }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override { dragFired = false; }

private:
    std::vector<bassgrab::ExportNote> notes;
    double windowBeats = 16.0;
    int firstBar = 1;
    juce::String message;
    bool dragFired = false;
};

//==============================================================================
// Big obvious "grab me" button
class DragPad : public juce::Component
{
public:
    std::function<void()> onDragOut;

    void setNoteCount (int n)
    {
        noteCount = n;
        setMouseCursor (n > 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { dragFired = false; }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override { dragFired = false; }

private:
    int noteCount = 0;
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

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshView(); }

private:
    void timerCallback() override;
    void refreshView();
    void syncControlsFromState();
    void startDragOut();
    void toggleRecording();
    void finishRecording();
    void openFileDialog();
    void saveMidiDialog();
    void loadAndReport (const juce::File&);

    BassGrabLookAndFeel lnf;                      // declared first: must outlive the child components below
    BassGrabProcessor& proc;

    juce::TextButton openBtn { "Open audio..." }, recBtn { "Record from input" }, saveBtn { "Save .mid..." };
    juce::Label fileLabel, bpmLabel, quantLabel, octLabel, rangeLabel, sensLabel, loopLabel, startLabel, infoLabel;
    juce::Slider bpmSlider, sensSlider, startSlider;
    juce::ToggleButton syncBtn { "Host BPM" };
    juce::ComboBox quantBox, rangeBox, octBox, barsBox;
    PianoRollView view;
    DragPad dragPad;
    std::unique_ptr<juce::FileChooser> chooser;

    bool recUiOn = false;
    bool wasAnalysing = false;
    double lastHostBpm = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassGrabEditor)
};
