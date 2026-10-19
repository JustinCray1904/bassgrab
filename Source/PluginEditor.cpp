#include "PluginEditor.h"
#include <initializer_list>

namespace
{
const char* kAudioExtensions = "wav;aif;aiff;flac;mp3;ogg;wma";

juce::String toJuce (const std::string& s) { return juce::String (s.c_str()); }

const juce::Colour kBg      { 0xff0f1116 };
const juce::Colour kPanel   { 0xff171a21 };
const juce::Colour kLine    { 0xff353c4a };
const juce::Colour kAccent  { 0xff2dd4a4 };
const juce::Colour kText    { 0xffe8edf3 };
const juce::Colour kDim     { 0xff8993a3 };
}

//==============================================================================
// Look and feel
//==============================================================================
BassGrabLookAndFeel::BassGrabLookAndFeel()
{
    setColour (juce::TextButton::buttonColourId,        juce::Colour (0xff252b38));
    setColour (juce::TextButton::buttonOnColourId,      kAccent);
    setColour (juce::TextButton::textColourOffId,       kText);
    setColour (juce::TextButton::textColourOnId,        juce::Colour (0xff06251c));

    setColour (juce::ComboBox::backgroundColourId,      juce::Colour (0xff1b2029));
    setColour (juce::ComboBox::textColourId,            kText);
    setColour (juce::ComboBox::outlineColourId,         kLine);
    setColour (juce::ComboBox::arrowColourId,           kDim);
    setColour (juce::PopupMenu::backgroundColourId,     juce::Colour (0xff1b2029));
    setColour (juce::PopupMenu::textColourId,           kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff06251c));

    setColour (juce::Slider::thumbColourId,             kAccent);
    setColour (juce::Slider::trackColourId,             kAccent);
    setColour (juce::Slider::backgroundColourId,        juce::Colour (0xff2a303c));
    setColour (juce::Slider::textBoxTextColourId,       kText);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff1b2029));
    setColour (juce::Slider::textBoxOutlineColourId,    kLine);

    setColour (juce::ToggleButton::textColourId,        kText);
    setColour (juce::ToggleButton::tickColourId,        kAccent);
    setColour (juce::ToggleButton::tickDisabledColourId, kDim);
    setColour (juce::Label::textColourId,               kDim);
}

//==============================================================================
// PianoRollView
//==============================================================================
void PianoRollView::setData (std::vector<bassgrab::ExportNote> n, double beats, int bar)
{
    notes = std::move (n);
    windowBeats = juce::jmax (4.0, beats);
    firstBar = bar;
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
    g.setColour (kPanel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (kLine);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    if (notes.empty())
    {
        g.setColour (kDim);
        g.setFont (16.0f);
        g.drawFittedText (message, getLocalBounds().reduced (40), juce::Justification::centred, 4);
        return;
    }

    const float gutter = 56.0f, rulerH = 24.0f;
    const auto grid = bounds.withTrimmedLeft (gutter).withTrimmedTop (rulerH).reduced (0.0f, 6.0f).withTrimmedRight (10.0f);

    // visible pitch range: just the notes that exist (+1 each side), at least a bit over an octave
    int lo = 127, hi = 0;
    for (const auto& n : notes) { lo = juce::jmin (lo, n.midi); hi = juce::jmax (hi, n.midi); }
    lo -= 1; hi += 1;
    while (hi - lo < 12) { --lo; ++hi; }
    lo = juce::jmax (0, lo);
    hi = juce::jmin (127, hi);
    const int   rows = hi - lo + 1;
    const float rowH = grid.getHeight() / (float) rows;
    auto rowY = [&] (int midi) { return grid.getBottom() - (float) (midi - lo + 1) * rowH; };

    bool used[128] = {};
    for (const auto& n : notes) used[juce::jlimit (0, 127, n.midi)] = true;

    const float ppb = grid.getWidth() / (float) windowBeats;

    // rows: key colour, tint on pitches that are actually used, label
    static const bool black[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
    for (int m = lo; m <= hi; ++m)
    {
        const float y = rowY (m);
        g.setColour (black[m % 12] ? juce::Colour (0xff12151b) : juce::Colour (0xff1b1f27));
        g.fillRect (grid.getX(), y, grid.getWidth(), rowH);
        if (used[m])
        {
            g.setColour (kAccent.withAlpha (0.07f));
            g.fillRect (grid.getX(), y, grid.getWidth(), rowH);
        }
        g.setColour (juce::Colour (0x12ffffff));
        g.drawHorizontalLine ((int) y, grid.getX(), grid.getRight());

        if (rowH >= 13.0f || m % 12 == 0 || used[m])
        {
            g.setColour (used[m] ? kAccent : (m % 12 == 0 ? kText : kDim));
            g.setFont (juce::jmin (13.0f, rowH * 0.8f));
            g.drawText (toJuce (bassgrab::noteName (m)), juce::Rectangle<float> (4.0f, y, gutter - 12.0f, rowH),
                        juce::Justification::centredRight);
        }
    }

    // time grid: 16th lines (if there is room), beats, bars
    const int totalBeats = (int) std::lround (windowBeats);
    if (ppb >= 36.0f)
        for (int s = 0; s < totalBeats * 4; ++s)
            if (s % 4 != 0)
            {
                g.setColour (juce::Colour (0x08ffffff));
                g.drawVerticalLine ((int) (grid.getX() + (float) s * 0.25f * ppb), grid.getY(), grid.getBottom());
            }
    for (int b = 0; b <= totalBeats; ++b)
    {
        const bool bar = b % 4 == 0;
        g.setColour (bar ? juce::Colour (0x40ffffff) : juce::Colour (0x14ffffff));
        g.drawVerticalLine ((int) (grid.getX() + (float) b * ppb), bar ? bounds.getY() + 4.0f : grid.getY(), grid.getBottom());
    }

    // ruler with bar numbers
    g.setFont (12.0f);
    const int numBars = totalBeats / 4;
    const int labelEvery = (ppb * 4.0f >= 30.0f) ? 1 : 4;
    for (int b = 0; b < numBars; b += labelEvery)
    {
        g.setColour (kDim);
        g.drawText (juce::String (firstBar + b),
                    juce::Rectangle<float> (grid.getX() + (float) b * 4.0f * ppb + 4.0f, bounds.getY() + 4.0f, 40.0f, 16.0f),
                    juce::Justification::centredLeft);
    }

    // notes
    for (const auto& n : notes)
    {
        const float x = grid.getX() + (float) n.startBeat * ppb;
        const float w = juce::jmax (4.0f, (float) n.lengthBeat * ppb - 1.5f);
        const juce::Rectangle<float> r (x, rowY (n.midi) + 1.5f, w, juce::jmax (4.0f, rowH - 3.0f));
        const float v = (float) n.velocity / 127.0f;

        g.setColour (kAccent.withBrightness (0.55f + 0.45f * v));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colour (0x66ffffff));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        if (w > 34.0f && rowH > 12.0f)
        {
            g.setColour (juce::Colour (0xdd06251c));
            g.setFont (juce::jmin (12.0f, rowH * 0.75f));
            g.drawText (toJuce (bassgrab::noteName (n.midi)), r.reduced (5.0f, 0.0f), juce::Justification::centredLeft);
        }
    }
}

//==============================================================================
// DragPad
//==============================================================================
void DragPad::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFired || noteCount <= 0 || e.getDistanceFromDragStart() < 6)
        return;
    dragFired = true;
    if (onDragOut)
        onDragOut();
}

void DragPad::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = noteCount > 0;

    juce::ColourGradient grad (juce::Colour (on ? 0xff34e0b0 : 0xff2a303c), r.getX(), r.getY(),
                               juce::Colour (on ? 0xff1aa37d : 0xff232834), r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 10.0f);

    // grip dots
    g.setColour (on ? juce::Colour (0x8006251c) : juce::Colour (0x55ffffff));
    for (int col = 0; col < 2; ++col)
        for (int row = 0; row < 3; ++row)
            g.fillEllipse (r.getX() + 14.0f + (float) col * 8.0f, r.getCentreY() - 12.0f + (float) row * 8.0f, 4.0f, 4.0f);

    g.setColour (on ? juce::Colour (0xff06251c) : kDim);
    g.setFont (15.0f);
    g.drawText (on ? "Drag MIDI into FL Studio" : "Drag MIDI (load audio first)",
                r.withTrimmedLeft (38.0f), juce::Justification::centredLeft);
}

//==============================================================================
// BassGrabEditor
//==============================================================================
BassGrabEditor::BassGrabEditor (BassGrabProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& ui = proc.ui();

    auto setupLabel = [this] (juce::Label& l, const juce::String& text, juce::Justification j)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (j);
        addAndMakeVisible (l);
    };
    setupLabel (fileLabel,  {}, juce::Justification::centredLeft);
    setupLabel (bpmLabel,   "BPM", juce::Justification::centredRight);
    setupLabel (quantLabel, "Quantize", juce::Justification::centredRight);
    setupLabel (octLabel,   "Octave", juce::Justification::centredRight);
    setupLabel (rangeLabel, "Range", juce::Justification::centredRight);
    setupLabel (sensLabel,  "Sensitivity", juce::Justification::centredRight);
    setupLabel (loopLabel,  "Loop", juce::Justification::centredRight);
    setupLabel (startLabel, "Start bar", juce::Justification::centredRight);
    setupLabel (infoLabel,  {}, juce::Justification::centredLeft);
    fileLabel.setColour (juce::Label::textColourId, kText);
    fileLabel.setFont (juce::Font (juce::FontOptions (15.0f)));

    for (auto* c : std::initializer_list<juce::Component*> { &openBtn, &recBtn, &saveBtn, &bpmSlider, &sensSlider, &startSlider,
                                                              &syncBtn, &quantBox, &rangeBox, &octBox, &barsBox, &view, &dragPad })
        addAndMakeVisible (c);

    openBtn.onClick = [this] { openFileDialog(); };
    recBtn.onClick  = [this] { toggleRecording(); };
    saveBtn.onClick = [this] { saveMidiDialog(); };

    // BPM
    bpmSlider.setSliderStyle (juce::Slider::IncDecButtons);
    bpmSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_AutoDirection);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 64, 26);
    bpmSlider.setRange (40.0, 260.0, 0.01);
    bpmSlider.setValue (ui.bpm, juce::dontSendNotification);
    bpmSlider.onValueChange = [this] { proc.ui().bpm = bpmSlider.getValue(); refreshView(); };

    syncBtn.setToggleState (ui.syncHost, juce::dontSendNotification);
    syncBtn.onClick = [this] { proc.ui().syncHost = syncBtn.getToggleState(); lastHostBpm = 0.0; refreshView(); };

    // quantise / octave / range
    const char* quants[] = { "Off", "1/4", "1/8", "1/16", "1/32" };
    for (int i = 0; i < 5; ++i) quantBox.addItem (quants[i], i + 1);
    quantBox.onChange = [this] { proc.ui().quantIndex = quantBox.getSelectedId() - 1; refreshView(); };

    const char* octs[] = { "-2 oct", "-1 oct", "As heard", "+1 oct", "+2 oct" };
    for (int i = 0; i < 5; ++i) octBox.addItem (octs[i], i + 1);
    octBox.onChange = [this] { proc.ui().octaveShift = octBox.getSelectedId() - 3; refreshView(); };

    rangeBox.addItem ("Sub / 808   (28-100 Hz)", 1);
    rangeBox.addItem ("Bass   (30-180 Hz)", 2);
    rangeBox.addItem ("Wide   (30-300 Hz)", 3);
    rangeBox.onChange = [this] { proc.ui().rangeIndex = rangeBox.getSelectedId() - 1; proc.settingsChanged(); refreshView(); };

    sensSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sensSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    sensSlider.setRange (0.0, 1.0, 0.01);
    sensSlider.onValueChange = [this]
    {
        proc.ui().sensitivity = (float) sensSlider.getValue();
        if (! sensSlider.isMouseButtonDown()) proc.settingsChanged();
    };
    sensSlider.onDragEnd = [this] { proc.settingsChanged(); };

    // loop window
    const char* bars[] = { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars", "Whole file" };
    for (int i = 0; i < 7; ++i) barsBox.addItem (bars[i], i + 1);
    barsBox.onChange = [this] { proc.ui().barsIndex = barsBox.getSelectedId() - 1; refreshView(); };

    startSlider.setSliderStyle (juce::Slider::IncDecButtons);
    startSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsNotDraggable);
    startSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 48, 26);
    startSlider.setRange (1.0, 999.0, 1.0);
    startSlider.onValueChange = [this] { proc.ui().startBar = (int) startSlider.getValue(); refreshView(); };

    // drag out (the piano roll AND the big pad both work)
    view.onDragOut    = [this] { startDragOut(); };
    dragPad.onDragOut = [this] { startDragOut(); };

    syncControlsFromState();
    proc.addChangeListener (this);
    setResizable (true, true);
    setResizeLimits (820, 520, 1800, 1000);
    setSize (1000, 660);
    refreshView();
    startTimerHz (10);
}

BassGrabEditor::~BassGrabEditor()
{
    stopTimer();
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void BassGrabEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
}

void BassGrabEditor::resized()
{
    auto r = getLocalBounds().reduced (16);

    auto top = r.removeFromTop (36);
    openBtn.setBounds (top.removeFromLeft (130));
    top.removeFromLeft (8);
    recBtn.setBounds (top.removeFromLeft (160));
    top.removeFromLeft (16);
    fileLabel.setBounds (top);

    r.removeFromTop (10);
    auto rowA = r.removeFromTop (32);
    bpmLabel.setBounds (rowA.removeFromLeft (40));
    bpmSlider.setBounds (rowA.removeFromLeft (150));
    rowA.removeFromLeft (6);
    syncBtn.setBounds (rowA.removeFromLeft (104));
    rowA.removeFromLeft (14);
    quantLabel.setBounds (rowA.removeFromLeft (70));
    quantBox.setBounds (rowA.removeFromLeft (80));
    rowA.removeFromLeft (14);
    octLabel.setBounds (rowA.removeFromLeft (56));
    octBox.setBounds (rowA.removeFromLeft (110));

    r.removeFromTop (8);
    auto rowB = r.removeFromTop (32);
    rangeLabel.setBounds (rowB.removeFromLeft (50));
    rangeBox.setBounds (rowB.removeFromLeft (190));
    rowB.removeFromLeft (14);
    sensLabel.setBounds (rowB.removeFromLeft (84));
    sensSlider.setBounds (rowB.removeFromLeft (150));
    rowB.removeFromLeft (14);
    loopLabel.setBounds (rowB.removeFromLeft (40));
    barsBox.setBounds (rowB.removeFromLeft (110));
    rowB.removeFromLeft (14);
    startLabel.setBounds (rowB.removeFromLeft (70));
    startSlider.setBounds (rowB.removeFromLeft (120));

    auto bottom = r.removeFromBottom (46);
    dragPad.setBounds (bottom.removeFromRight (260));
    bottom.removeFromRight (10);
    saveBtn.setBounds (bottom.removeFromRight (120).withSizeKeepingCentre (120, 36));
    bottom.removeFromRight (10);
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
    chooser = std::make_unique<juce::FileChooser> ("Choose a loop or audio file",
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
    syncControlsFromState();
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
        refreshView();
    }

    if (proc.isAnalysing() != wasAnalysing)
    {
        wasAnalysing = proc.isAnalysing();
        refreshView();
    }
}

// push the processor's state (which can change when a file loads) into the controls
void BassGrabEditor::syncControlsFromState()
{
    auto& ui = proc.ui();
    bpmSlider.setValue (proc.getEffectiveBpm(), juce::dontSendNotification);
    syncBtn.setToggleState (ui.syncHost, juce::dontSendNotification);
    quantBox.setSelectedId (ui.quantIndex + 1, juce::dontSendNotification);
    octBox.setSelectedId (ui.octaveShift + 3, juce::dontSendNotification);
    rangeBox.setSelectedId (ui.rangeIndex + 1, juce::dontSendNotification);
    sensSlider.setValue (ui.sensitivity, juce::dontSendNotification);
    barsBox.setSelectedId (ui.barsIndex + 1, juce::dontSendNotification);
    startSlider.setValue (ui.startBar, juce::dontSendNotification);
    startSlider.setEnabled (ui.barsIndex != 6);
    startLabel.setEnabled (ui.barsIndex != 6);
}

void BassGrabEditor::refreshView()
{
    syncControlsFromState();

    auto& ui = proc.ui();
    const double dur = proc.getDurationSec();
    const auto notes = proc.getExportNotes();
    const auto analysis = proc.getAnalysis();
    const bool whole = ui.barsIndex == 6;
    const int firstBar = whole ? 1 : ui.startBar;
    const int bars = (int) std::lround (proc.getWindowBeats() / 4.0);

    if (dur <= 0.0)
        view.setMessage ("Drop an audio loop here, press \"Open audio...\", or press \"Record from input\" and play your loop.\n\n"
                         "BassGrab picks out the BASS line and ignores melodies, chords and hats.");
    else if (analysis == nullptr)
        view.setMessage ("Analysing...");
    else
        view.setMessage ("No bass notes found in bars " + juce::String (firstBar) + "-" + juce::String (firstBar + bars - 1)
                         + ".\nTry another Start bar, raise Sensitivity, or change Range.");

    view.setData (notes, proc.getWindowBeats(), firstBar);
    dragPad.setNoteCount ((int) notes.size());
    saveBtn.setEnabled (! notes.empty());

    if (! recUiOn && dur > 0.0)
        fileLabel.setText (proc.getSourceName() + "   (" + juce::String (dur, 1) + " s)", juce::dontSendNotification);

    // summary line
    juce::String info;
    if (analysis != nullptr && ! notes.empty())
    {
        int lo = 127, hi = 0;
        for (const auto& n : notes) { lo = juce::jmin (lo, n.midi); hi = juce::jmax (hi, n.midi); }
        info << (int) notes.size() << " bass notes   |   " << toJuce (bassgrab::noteName (lo)) << " to "
             << toJuce (bassgrab::noteName (hi));
        const int root = bassgrab::dominantPitchClass (*analysis);
        if (root >= 0)
            info << "   |   Root guess: " << bassgrab::pitchClassName (root);
        info << "\n";
    }
    else if (proc.isAnalysing() && dur > 0.0)
        info = "Analysing...";

    if (dur > 0.0 && ! ui.syncHost)
        info << (proc.bpmWasReadFromFileName()
                     ? "BPM " + juce::String (proc.getEffectiveBpm(), 0) + " read from the file name"
                     : juce::String ("No BPM in the file name - set BPM to your loop's tempo"));

    infoLabel.setText (info, juce::dontSendNotification);
}
