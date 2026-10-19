#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
BassGrabProcessor::BassGrabProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();   // wav, aiff, flac, ogg, (mp3 / wma via JUCE flags)
    settingsChanged();
    worker.startThread();
}

BassGrabProcessor::~BassGrabProcessor()
{
    worker.stopThread (10000);
}

bool BassGrabProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == l.getMainInputChannelSet();
}

void BassGrabProcessor::prepareToPlay (double sampleRate, int)
{
    recSampleRate = sampleRate;
    const size_t cap = (size_t) (sampleRate * 90.0);          // up to 90 s of live capture
    if (recBuffer.size() != cap && ! recording.load())
        recBuffer.assign (cap, 0.0f);
}

//==============================================================================
// Audio thread: the plugin is transparent (audio passes through untouched). When "Record" is
// armed it also copies the incoming audio, summed to mono, into a pre-allocated buffer.
void BassGrabProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm);

    if (! recording.load())
        return;

    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();
    const int cap   = (int) recBuffer.size();
    const int w     = recPos.load();
    const int todo  = juce::jmin (n, cap - w);
    if (numCh == 0 || todo <= 0)
    {
        recording.store (false);        // buffer full - the editor notices and finishes up
        return;
    }

    for (int i = 0; i < todo; ++i)
    {
        float sum = 0.0f;
        for (int c = 0; c < numCh; ++c)
            sum += buffer.getReadPointer (c)[i];
        recBuffer[(size_t) (w + i)] = sum / (float) numCh;
    }
    recPos.store (w + todo);
    if (todo < n)
        recording.store (false);
}

//==============================================================================
void BassGrabProcessor::startRecording()
{
    recPos.store (0);
    recording.store (true);
}

void BassGrabProcessor::stopRecording()
{
    recording.store (false);
    const int n = recPos.load();
    if (n < (int) (recSampleRate * 0.3))
        return;                                              // nothing useful captured

    std::vector<float> mono (recBuffer.begin(), recBuffer.begin() + n);
    setAudio (std::move (mono), recSampleRate, "Recorded audio");
}

bool BassGrabProcessor::loadFile (const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return false;

    const auto  maxLen = (juce::int64) (reader->sampleRate * 120.0);       // cap at 2 minutes
    const int   len    = (int) juce::jmin (reader->lengthInSamples, maxLen);
    const int   numCh  = juce::jmin (2, (int) reader->numChannels);

    juce::AudioBuffer<float> tmp (numCh, len);
    if (! reader->read (&tmp, 0, len, 0, true, numCh > 1))
        return false;

    std::vector<float> mono ((size_t) len, 0.0f);
    for (int c = 0; c < numCh; ++c)
    {
        const float* src = tmp.getReadPointer (c);
        for (int i = 0; i < len; ++i)
            mono[(size_t) i] += src[i] / (float) numCh;
    }
    setAudio (std::move (mono), reader->sampleRate, f.getFileName());
    return true;
}

void BassGrabProcessor::setAudio (std::vector<float>&& mono, double sr, const juce::String& name)
{
    // waveform thumbnail (1200 bins of peak amplitude)
    constexpr int bins = 1200;
    std::vector<float> pk ((size_t) bins, 0.0f);
    const size_t n = mono.size();
    for (size_t i = 0; i < n; ++i)
    {
        const size_t b = juce::jmin ((size_t) bins - 1, i * (size_t) bins / n);
        pk[b] = juce::jmax (pk[b], std::abs (mono[i]));
    }
    const float m = *std::max_element (pk.begin(), pk.end());
    if (m > 0.0f)
        for (auto& v : pk) v /= m;

    {
        const juce::ScopedLock sl (dataLock);
        audio      = std::move (mono);
        audioSr    = sr;
        sourceName = name;
        peaks      = std::move (pk);
        analysis.reset();
    }
    requestAnalysis();
    sendChangeMessage();
}

//==============================================================================
void BassGrabProcessor::settingsChanged()
{
    static const float lo[] = { 30.0f, 32.0f, 40.0f };
    static const float hi[] = { 130.0f, 260.0f, 500.0f };
    const int r = juce::jlimit (0, 2, uiState.rangeIndex);
    {
        const juce::ScopedLock sl (dataLock);
        analysisSettings.minHz        = lo[r];
        analysisSettings.maxHz        = hi[r];
        analysisSettings.yinThreshold = 0.05f + 0.30f * uiState.sensitivity;
    }
    requestAnalysis();
}

void BassGrabProcessor::requestAnalysis()
{
    analysing.store (true);
    worker.notify();
}

void BassGrabProcessor::runAnalysis()                       // runs on the worker thread
{
    std::vector<float> copy;
    double sr = 0.0;
    bassgrab::Settings s;
    {
        const juce::ScopedLock sl (dataLock);
        copy = audio;
        sr   = audioSr;
        s    = analysisSettings;
    }

    if (copy.empty())
    {
        analysing.store (false);
        return;
    }

    auto result = std::make_shared<const bassgrab::Analysis> (bassgrab::analyse (copy.data(), copy.size(), sr, s));
    {
        const juce::ScopedLock sl (dataLock);
        analysis = result;
    }
    analysing.store (false);
    sendChangeMessage();                                    // thread-safe; the editor refreshes
}

//==============================================================================
double BassGrabProcessor::getEffectiveBpm() const
{
    const double h = hostBpm.load();
    return (uiState.syncHost && h > 20.0) ? h : uiState.bpm;
}

double BassGrabProcessor::gridBeatsForIndex (int idx)
{
    static const double g[] = { 0.0, 1.0, 0.5, 0.25, 0.125 };
    return g[juce::jlimit (0, 4, idx)];
}

juce::String BassGrabProcessor::getSourceName() const
{
    const juce::ScopedLock sl (dataLock);
    return sourceName;
}

double BassGrabProcessor::getDurationSec() const
{
    const juce::ScopedLock sl (dataLock);
    return audioSr > 0.0 ? (double) audio.size() / audioSr : 0.0;
}

std::vector<float> BassGrabProcessor::getPeaks() const
{
    const juce::ScopedLock sl (dataLock);
    return peaks;
}

std::shared_ptr<const bassgrab::Analysis> BassGrabProcessor::getAnalysis() const
{
    const juce::ScopedLock sl (dataLock);
    return analysis;
}

std::vector<bassgrab::ExportNote> BassGrabProcessor::getExportNotes() const
{
    auto a = getAnalysis();
    if (a == nullptr)
        return {};
    return bassgrab::toExportNotes (*a, getEffectiveBpm(), gridBeatsForIndex (uiState.quantIndex),
                                    uiState.octaveShift * 12);
}

std::vector<uint8_t> BassGrabProcessor::buildMidiBytes() const
{
    auto notes = getExportNotes();
    if (notes.empty())
        return {};
    return bassgrab::makeMidiFile (notes, getEffectiveBpm(), "BassGrab");
}

juce::File BassGrabProcessor::writeMidiToTemp()
{
    const auto bytes = buildMidiBytes();
    if (bytes.empty())
        return {};

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("BassGrab");
    dir.createDirectory();

    const auto base = getSourceName().upToLastOccurrenceOf (".", false, false);
    auto file = dir.getChildFile (juce::File::createLegalFileName (base + " - bass") + ".mid");
    if (! file.replaceWithData (bytes.data(), bytes.size()))
        return {};
    return file;
}

bool BassGrabProcessor::saveMidiTo (const juce::File& f)
{
    const auto bytes = buildMidiBytes();
    return ! bytes.empty() && f.replaceWithData (bytes.data(), bytes.size());
}

//==============================================================================
void BassGrabProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("BassGrab");
    xml.setAttribute ("bpm",   uiState.bpm);
    xml.setAttribute ("sync",  uiState.syncHost ? 1 : 0);
    xml.setAttribute ("quant", uiState.quantIndex);
    xml.setAttribute ("range", uiState.rangeIndex);
    xml.setAttribute ("sens",  (double) uiState.sensitivity);
    xml.setAttribute ("oct",   uiState.octaveShift);
    copyXmlToBinary (xml, dest);
}

void BassGrabProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName ("BassGrab"))
        {
            uiState.bpm         = xml->getDoubleAttribute ("bpm", 120.0);
            uiState.syncHost    = xml->getBoolAttribute ("sync", true);
            uiState.quantIndex  = juce::jlimit (0, 4, xml->getIntAttribute ("quant", 3));
            uiState.rangeIndex  = juce::jlimit (0, 2, xml->getIntAttribute ("range", 1));
            uiState.sensitivity = (float) xml->getDoubleAttribute ("sens", 0.33);
            uiState.octaveShift = juce::jlimit (-3, 3, xml->getIntAttribute ("oct", 0));
            settingsChanged();
        }
}

juce::AudioProcessorEditor* BassGrabProcessor::createEditor()
{
    return new BassGrabEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BassGrabProcessor();
}
