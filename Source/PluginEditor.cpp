#include "PluginEditor.h"
#include <initializer_list>

namespace
{
const char* kAudioExtensions = "wav;aif;aiff;flac;mp3;ogg;wma";

juce::String toJuce (const std::string& s) { return juce::String (s.c_str()); }
}

//==============================================================================
// PianoRollView
//==============================================================================
void PianoRollView::setData (std::vector<bassgrab::ExportNote> n, std::vector<float> p, double durBeats)
{
    notes = std::move (n);
    peaks = std::move (p);
    durationBeats = durBeats;
    setMouseCursor (notes.empty() ? juce::MouseCursor::NormalCursor : juce::MouseCursor::DraggingHandCursor);
    repaint();
}

void PianoRollView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFired || notes.empty() || e.getDistanceFromDragStart() < 8)
        return;

    dragFired = true;            // on Windows the OS drag loop blocks until the drop finishes
    if (onDragOut)
        onDragOut();
}

void PianoRollView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff14161b));
    g.fillRoundedRectangle (bounds, 8.0f);

    if (notes.empty())
    {
        g.setColour (juce::Colour (0xff7d8794));
        g.setFont (16.0f);
        g.drawFittedText (message, getLocalBounds().reduced (30), juce::Justification::centred, 3);
        return;
    }

    const float gutter = 50.0f;
    const auto area = bounds.withTrimmedLeft (gutter).reduced (0.0f, 4.0f).withTrimmedRight (6.0f);

    // visible pitch range
    int lo = 127, hi = 0;
    for (const auto& n : notes) { lo = juce::jmin (lo, n.midi); hi = juce::jmax (hi, n.midi); }
    lo -= 2; hi += 2;
    while (hi - lo < 11) { --lo; ++hi; }
    lo = juce::jmax (0, lo);
    hi = juce::jmin (127, hi);
    const int   rows = hi - lo + 1;
    const float rowH = area.getHeight() / (float) rows;
    auto rowY = [&] (int midi) { return area.getBottom() - (float) (midi - lo + 1) * rowH; };

    // time range
    double endBeat = durationBeats;
    for (const auto& n : notes) endBeat = juce::jmax (endBeat, n.startBeat + n.lengthBeat);
    const double totalBeats = juce::jmax (4.0, std::ceil (endBeat / 4.0) * 4.0);
    const float  ppb = area.getWidth() / (float) totalBeats;

    // rows + key labels
    static const bool black[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
    for (int m = lo; m <= hi; ++m)
    {
        const float y = rowY (m);
        g.setColour (black[m % 12] ? juce::Colour (0xff101216) : juce::Colour (0xff1a1d24));
        g.fillRect (area.getX(), y, area.getWidth(), rowH);
        g.setColour (juce::Colour (0x14ffffff));
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());

        if (rowH >= 10.5f || m % 12 == 0)
        {
            g.setColour (m % 12 == 0 ? juce::Colour (0xffe6edf5) : juce::Colour (0xff7d8794));
            g.setFont (juce::jmin (12.0f, rowH * 0.85f));
            g.drawText (toJuce (bassgrab::noteName (m)), juce::Rectangle<float> (4.0f, y, gutter - 10.0f, rowH),
                        juce::Justification::centredRight);
        }
    }

    // beat / bar grid
    for (int b = 0; b <= (int) totalBeats; ++b)
    {
        g.setColour (b % 4 == 0 ? juce::Colour (0x40ffffff) : juce::Colour (0x14ffffff));
        g.drawVerticalLine ((int) (area.getX() + (float) b * ppb), area.getY(), area.getBottom());
    }

    // faint waveform of the source audio along the bottom
    if (! peaks.empty() && durationBeats > 0.0)
    {
        const float wpx = (float) durationBeats * ppb;
        g.setColour (juce::Colour (0x2438bdf8));
        for (float px = 0.0f; px < wpx; px += 2.0f)
        {
            const size_t idx = juce::jmin (peaks.size() - 1, (size_t) (px / wpx * (float) peaks.size()));
            const float  h   = peaks[idx] * area.getHeight() * 0.22f;
            g.fillRect (area.getX() + px, area.getBottom() - h, 1.5f, h);
        }
    }

    // notes
    for (const auto& n : notes)
    {
        const float x = area.getX() + (float) n.startBeat * ppb;
        const float w = juce::jmax (3.0f, (float) n.lengthBeat * ppb - 1.5f);
        const juce::Rectangle<float> r (x, rowY (n.midi) + 1.0f, w, juce::jmax (3.0f, rowH - 2.0f));
        const float v = (float) n.velocity / 127.0f;

        g.setColour (juce::Colour (0xff2dd4a4).withBrightness (0.45f + 0.5f * v));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colour (0x59ffffff));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        if (w > 30.0f && rowH > 11.0f)
        {
            g.setColour (juce::Colour (0xcc000000));
            g.setFont (juce::jmin (11.5f, rowH * 0.8f));
            g.drawText (toJuce (bassgrab::noteName (n.midi)), r.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
        }
    }

    // hint chip
    g.setColour (juce::Colour (0xaa000000));
    const juce::Rectangle<float> chip (bounds.getRight() - 262.0f, bounds.getY() + 8.0f, 252.0f, 22.0f);
    g.fillRoundedRectangle (chip, 11.0f);
    g.setColour (juce::Colour (0xffcfe9e0));
    g.setFont (12.5f);
    g.drawText ("Drag the notes into FL Studio", chip, juce::Justification::centred);
}

//==============================================================================
// BassGrabEditor
//==============================================================================
BassGrabEditor::BassGrabEditor (BassGrabProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    auto& ui = proc.ui();

    auto setupLabel = [this] (juce::Label& l, const juce::String& text, juce::Justification j)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (j);
        l.setColour (juce::Label::textColourId, juce::Colour (0xff9aa4b2));
        addAndMakeVisible (l);
    };
    setupLabel (fileLabel,  {}, juce::Justification::centredLeft);
    setupLabel (bpmLabel,   "BPM", juce::Justification::centredRight);
    setupLabel (quantLabel, "Quantize", juce::Justification::centredRight);
    setupLabel (octLabel,   "Octave", juce::Justification::centredRight);
    setupLabel (rangeLabel, "Range", juce::Justification::centredRight);
    setupLabel (sensLabel,  "Sensitivity", juce::Justification::centredRight);
    setupLabel (infoLabel,  {}, juce::Justification::centredLeft);
    fileLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe6edf5));

    for (auto* c : std::initializer_list<juce::Component*> { &openBtn, &recBtn, &saveBtn, &bpmSlider, &sensSlider,
                                                              &syncBtn, &quantBox, &rangeBox, &octBox, &view })
        addAndMakeVisible (c);

    // --- buttons -------------------------------------------------------------------------------
    openBtn.onClick = [this] { openFileDialog(); };
    recBtn.onClick  = [this] { toggleRecording(); };
    saveBtn.onClick = [this] { saveMidiDialog(); };

    // --- BPM -----------------------------------------------------------------------------------
    bpmSlider.setSliderStyle (juce::Slider::IncDecButtons);
    bpmSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_AutoDirection);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 64, 24);
    bpmSlider.setRange (40.0, 260.0, 0.01);
    bpmSlider.setValue (ui.bpm, juce::dontSendNotification);
    bpmSlider.onValueChange = [this] { proc.ui().bpm = bpmSlider.getValue(); refreshView(); };

    syncBtn.setToggleState (ui.syncHost, juce::dontSendNotification);
    syncBtn.onClick = [this] { proc.ui().syncHost = syncBtn.getToggleState(); lastHostBpm = 0.0; refreshView(); };

    // --- quantise / octave / range ------------------------------------------------------------
    const char* quants[] = { "Off", "1/4", "1/8", "1/16", "1/32" };
    for (int i = 0; i < 5; ++i) quantBox.addItem (quants[i], i + 1);
    quantBox.setSelectedId (ui.quantIndex + 1, juce::dontSendNotification);
    quantBox.onChange = [this] { proc.ui().quantIndex = quantBox.getSelectedId() - 1; refreshView(); };

    const char* octs[] = { "-2 oct", "-1 oct", "As heard", "+1 oct", "+2 oct" };
    for (int i = 0; i < 5; ++i) octBox.addItem (octs[i], i + 1);
    octBox.setSelectedId (ui.octaveShift + 3, juce::dontSendNotification);
    octBox.onChange = [this] { proc.ui().octaveShift = octBox.getSelectedId() - 3; refreshView(); };

    rangeBox.addItem ("Sub / 808  (30-130 Hz)", 1);
    rangeBox.addItem ("Bass  (32-260 Hz)", 2);
    rangeBox.addItem ("Low-mid  (40-500 Hz)", 3);
    rangeBox.setSelectedId (ui.rangeIndex + 1, juce::dontSendNotification);
    rangeBox.onChange = [this] { proc.ui().rangeIndex = rangeBox.getSelectedId() - 1; proc.settingsChanged(); refreshView(); };

    sensSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sensSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    sensSlider.setRange (0.0, 1.0, 0.01);
    sensSlider.setValue (ui.sensitivity, juce::dontSendNotification);
    sensSlider.onValueChange = [this]
    {
        proc.ui().sensitivity = (float) sensSlider.getValue();
        if (! sensSlider.isMouseButtonDown()) proc.settingsChanged();
    };
    sensSlider.onDragEnd = [this] { proc.settingsChanged(); };

    // --- drag out ------------------------------------------------------------------------------
    view.onDragOut = [this] { startDragOut(); };

    proc.addChangeListener (this);
    setResizable (true, true);
    setResizeLimits (760, 480, 1800, 1000);
    setSize (940, 600);
    refreshView();
    startTimerHz (10);
}

BassGrabEditor::~BassGrabEditor()
{
    stopTimer();
    proc.removeChangeListener (this);
}

//==============================================================================
void BassGrabEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0f13));
}

void BassGrabEditor::resized()
{
    auto r = getLocalBounds().reduced (14);

    auto top = r.removeFromTop (32);
    openBtn.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (8);
    recBtn.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (14);
    fileLabel.setBounds (top);

    r.removeFromTop (10);
    auto rowA = r.removeFromTop (28);
    bpmLabel.setBounds (rowA.removeFromLeft (36));
    bpmSlider.setBounds (rowA.removeFromLeft (140));
    syncBtn.setBounds (rowA.removeFromLeft (100));
    rowA.removeFromLeft (10);
    quantLabel.setBounds (rowA.removeFromLeft (66));
    quantBox.setBounds (rowA.removeFromLeft (80));
    rowA.removeFromLeft (10);
    octLabel.setBounds (rowA.removeFromLeft (56));
    octBox.setBounds (rowA.removeFromLeft (110));

    r.removeFromTop (8);
    auto rowB = r.removeFromTop (28);
    rangeLabel.setBounds (rowB.removeFromLeft (46));
    rangeBox.setBounds (rowB.removeFromLeft (190));
    rowB.removeFromLeft (14);
    sensLabel.setBounds (rowB.removeFromLeft (84));
    sensSlider.setBounds (rowB.removeFromLeft (juce::jmin (300, rowB.getWidth())));

    auto bottom = r.removeFromBottom (32);
    saveBtn.setBounds (bottom.removeFromRight (130));
    infoLabel.setBounds (bottom);

    view.setBounds (r.reduced (0, 10));
}

//==============================================================================
bool BassGrabEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension (kAudioExtensions))
            return true;
    return false;
}

void BassGrabEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() > 0)
        loadAndReport (juce::File (files[0]));
}

void BassGrabEditor::loadAndReport (const juce::File& f)
{
    if (recUiOn) finishRecording();
    if (! proc.loadFile (f))
    {
        fileLabel.setText ("Couldn't read that file. Try a WAV, AIFF, FLAC, MP3 or OGG.", juce::dontSendNotification);
        return;
    }
    refreshView();
}

void BassGrabEditor::openFileDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a bass loop or audio file",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.wma");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                                  loadAndReport (f);
                          });
}

void BassGrabEditor::saveMidiDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Save bass line as MIDI",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                       .getChildFile ("bass.mid"),
                                                   "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              proc.saveMidiTo (f.withFileExtension ("mid"));
                          });
}

//==============================================================================
void BassGrabEditor::toggleRecording()
{
    if (recUiOn)
    {
        finishRecording();
        return;
    }
    proc.startRecording();
    recUiOn = true;
    recBtn.setButtonText ("Stop and analyse");
    recBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffb3261e));
}

void BassGrabEditor::finishRecording()
{
    proc.stopRecording();
    recUiOn = false;
    recBtn.setButtonText ("Record from input");
    recBtn.removeColour (juce::TextButton::buttonColourId);

    if (proc.getDurationSec() <= 0.0)
        fileLabel.setText ("Nothing captured - press Record, then play the loop through this mixer track.",
                           juce::dontSendNotification);
    refreshView();
}

void BassGrabEditor::startDragOut()
{
    const auto file = proc.writeMidiToTemp();
    if (! file.existsAsFile())
        return;

    juce::StringArray files;
    files.add (file.getFullPathName());
    juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, &view);
}

//==============================================================================
void BassGrabEditor::timerCallback()
{
    if (recUiOn)
    {
        if (! proc.isRecording())
            finishRecording();                       // buffer filled up
        else
            fileLabel.setText ("Recording...  " + juce::String (proc.getRecordedSeconds(), 1) + " s",
                               juce::dontSendNotification);
    }

    const double h = proc.getHostBpm();
    const bool hostOk = proc.ui().syncHost && h > 20.0;
    bpmSlider.setEnabled (! hostOk);
    if (hostOk && std::abs (h - lastHostBpm) > 0.01)
    {
        lastHostBpm = h;
        bpmSlider.setValue (h, juce::dontSendNotification);
        refreshView();
    }

    if (proc.isAnalysing() != wasAnalysing)
    {
        wasAnalysing = proc.isAnalysing();
        refreshView();
    }
}

void BassGrabEditor::refreshView()
{
    const double dur = proc.getDurationSec();
    const double bpm = proc.getEffectiveBpm();
    const auto notes = proc.getExportNotes();
    const auto analysis = proc.getAnalysis();

    if (dur <= 0.0)
        view.setMessage ("Drop an audio loop here, press \"Open audio...\", or press \"Record from input\" and play your loop.\n"
                         "Works best on a bass-only sound.");
    else if (analysis == nullptr)
        view.setMessage ("Analysing...");
    else
        view.setMessage ("No bass notes found.\nTry raising Sensitivity, or pick a different Range.");

    view.setData (notes, proc.getPeaks(), dur * bpm / 60.0);

    if (! recUiOn && dur > 0.0)
        fileLabel.setText (proc.getSourceName() + "   (" + juce::String (dur, 1) + " s)", juce::dontSendNotification);

    // summary line
    juce::String info;
    if (analysis != nullptr && ! notes.empty())
    {
        int lo = 127, hi = 0;
        for (const auto& n : notes) { lo = juce::jmin (lo, n.midi); hi = juce::jmax (hi, n.midi); }
        info << (int) notes.size() << " notes   |   " << toJuce (bassgrab::noteName (lo)) << " to "
             << toJuce (bassgrab::noteName (hi));
        const int root = bassgrab::dominantPitchClass (*analysis);
        if (root >= 0)
            info << "   |   Root guess: " << bassgrab::pitchClassName (root);
        if (std::abs (analysis->tuningCents) >= 5.0)
            info << "   |   audio is " << juce::String (analysis->tuningCents, 0) << " cents off A440 (compensated)";
    }
    else if (proc.isAnalysing() && dur > 0.0)
        info = "Analysing...";
    infoLabel.setText (info, juce::dontSendNotification);
    saveBtn.setEnabled (! notes.empty());
}
