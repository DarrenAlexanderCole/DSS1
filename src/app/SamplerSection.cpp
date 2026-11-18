#include "SamplerSection.h"
#include "Hardware.h"
#include "Settings.h"
#include "Theme.h"

#include "core/SeratoTags.h"

#include <thread>

namespace dss1::ui
{

namespace
{
    constexpr double lengths[] { 0.25, 0.5, 1, 2, 4, 8, 16 };
    const char* const lengthNames[] { "1/4", "1/2", "1", "2", "4", "8", "16" };

    juce::Colour bankColour (int b)
    {
        const auto c = serato::defaultCueColour (b);
        return juce::Colour (c.red, c.green, c.blue);
    }

    juce::String safeName (juce::String s)
    {
        return juce::File::createLegalFileName (s.trim()).substring (0, 60);
    }
}

SamplerSection::SamplerSection (Session& s) : session (s), sampler (s.output().sampler())
{
    settings = &appSettings();

    for (int b = 0; b < 8; ++b)
    {
        auto& k = bankKeys[static_cast<std::size_t> (b)];
        k.setButtonText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + b)));
        k.setTooltip ("Bank " + k.getButtonText());
        k.onClick = [this, b] { selectBank (b); };
        addAndMakeVisible (k);
    }
    for (int p = 0; p < 16; ++p)
    {
        auto& pad = pads[static_cast<std::size_t> (p)];
        pad.setButtonText (juce::String (p + 1));
        pad.setTooltip ("Click: play (or choose an empty pad).  Right-click: capture, load, save, clear.");
        pad.onClick = [this, p] {
            if (juce::ModifierKeys::currentModifiers.isPopupMenu())
                return;
            padPressed (p);
        };
        pad.setTriggeredOnMouseDown (true);
        pad.addMouseListener (this, false);
        addAndMakeVisible (pad);
    }

    lengthKnob.setRange (0, 6, 1);
    lengthKnob.setValue (settings->getIntValue ("sampleLength", 3), juce::dontSendNotification);
    lengthKnob.setTooltip ("How many beats CAPTURE takes from the playhead (when no loop is playing)");
    hwScale (lengthKnob, "1/4|1/2|1|2|4|8|16");
    lengthKnob.onValueChange = [this] {
        settings->setValue ("sampleLength", static_cast<int> (lengthKnob.getValue()));
        repaint();
    };
    addAndMakeVisible (lengthKnob);

    hwStyle (captureKey, KeyStyle::cue);
    captureKey.setTooltip ("Capture into the chosen pad: the loop that's playing, or LENGTH beats from the playhead");
    captureKey.onClick = [this] { capture (slotOf (selected)); };
    loadKey.setTooltip ("Load a sound file into the chosen pad");
    loadKey.onClick = [this] { loadFile (slotOf (selected)); };
    clearKey.onClick = [this] { clear (slotOf (selected)); };
    stopKey.onClick = [this] { sampler.stopAll(); };
    hwStyle (folderKey, KeyStyle::cream);
    folderKey.setTooltip ("Choose the folder samples are saved to");
    folderKey.onClick = [this] { chooseFolder(); };
    for (auto* k : { &captureKey, &loadKey, &clearKey, &stopKey, &folderKey })
        addAndMakeVisible (k);

    selectBank (0);
    restoreKit();
    startTimerHz (30);
}

SamplerSection::~SamplerSection() = default;

juce::File SamplerSection::samplesFolder() const
{
    const auto chosen = settings->getValue ("samplesFolder");
    if (chosen.isNotEmpty() && juce::File::isAbsolutePath (chosen))
        return juce::File (chosen);
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DSS1 Samples");
}

juce::String SamplerSection::slotName (int slot) const
{
    return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + slot / 16)) + juce::String (slot % 16 + 1).paddedLeft ('0', 2);
}

void SamplerSection::selectBank (int b)
{
    bank = juce::jlimit (0, 7, b);
    for (int i = 0; i < 8; ++i)
        bankKeys[static_cast<std::size_t> (i)].setToggleState (i == bank, juce::dontSendNotification);
    refreshPads();
    repaint();
}

void SamplerSection::refreshPads()
{
    for (int p = 0; p < 16; ++p)
    {
        const auto slot = slotOf (p);
        const bool loaded = sampler.sample (slot) != nullptr;
        const bool on = sampler.isPlaying (slot);
        auto colour = bankColour (bank);
        if (p == selected && ! loaded)
            colour = colour.withMultipliedBrightness (0.35f);
        hwPad (pads[static_cast<std::size_t> (p)], on ? colour.brighter (0.6f) : colour, loaded || p == selected);
        wasPlaying[static_cast<std::size_t> (p)] = on;
    }
}

void SamplerSection::timerCallback()
{
    // Light pads while they play.
    bool changed = false;
    for (int p = 0; p < 16; ++p)
        changed |= sampler.isPlaying (slotOf (p)) != wasPlaying[static_cast<std::size_t> (p)];
    if (changed)
        refreshPads();
    sampler.collectGarbage();
}

void SamplerSection::padPressed (int pad)
{
    selected = pad;
    const auto slot = slotOf (pad);
    if (sampler.sample (slot) != nullptr)
        sampler.trigger (slot);
    refreshPads();
    repaint();
}

void SamplerSection::showPadMenu (int pad)
{
    selected = pad;
    refreshPads();
    const auto slot = slotOf (pad);
    juce::PopupMenu menu;
    menu.addSectionHeader ("PAD " + slotName (slot));
    menu.addItem ("Capture From the Deck", [this, slot] { capture (slot); });
    menu.addItem ("Load Sound File...", [this, slot] { loadFile (slot); });
    const bool loaded = sampler.sample (slot) != nullptr;
    menu.addItem ("Show in Finder", loaded && files[static_cast<std::size_t> (slot)].existsAsFile(), false,
                  [f = files[static_cast<std::size_t> (slot)]] { f.revealToUser(); });
    menu.addSeparator();
    menu.addItem ("Clear Pad", loaded, false, [this, slot] { clear (slot); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void SamplerSection::capture (int slot)
{
    const auto source = std::as_const (session).audio();
    const auto* record = session.record();
    if (source == nullptr || record == nullptr || source->channels.empty())
    {
        message = "LOAD A TRACK FIRST (DOUBLE-CLICK ONE IN CATALOG)";
        repaint();
        return;
    }
    // The loop that's playing, or LENGTH beats from the playhead (on the grid, else at the track's BPM).
    auto& deck = session.deck();
    const auto rate = source->info.sampleRate;
    double start, end;
    juce::String what;
    if (deck.isLooping())
    {
        start = deck.loopStartFrame();
        end = deck.loopEndFrame();
        what = "LOOP";
    }
    else
    {
        const auto beats = lengths[juce::jlimit (0, 6, static_cast<int> (lengthKnob.getValue()))];
        start = deck.position();
        if (record->grid && ! record->grid->isEmpty())
            end = record->grid->frameAt (record->grid->beatAt (start) + beats);
        else
            end = start + beats * 60.0 / record->info.bpm.value_or (120.0) * rate;
        what = juce::String (lengthNames[juce::jlimit (0, 6, static_cast<int> (lengthKnob.getValue()))]) + (beats > 1 ? " BEATS" : " BEAT");
    }
    const auto total = static_cast<double> (source->channels.front().size());
    start = juce::jlimit (0.0, total, start);
    end = juce::jlimit (start, total, end);
    if (end - start < rate * 0.01)
    {
        message = "NOTHING TO CAPTURE HERE";
        repaint();
        return;
    }

    auto slice = std::make_shared<io::DecodedAudio>();
    slice->info = source->info;
    const auto from = static_cast<std::size_t> (start), to = static_cast<std::size_t> (end);
    for (const auto& ch : source->channels)
        slice->channels.emplace_back (ch.begin() + static_cast<std::ptrdiff_t> (from), ch.begin() + static_cast<std::ptrdiff_t> (to));
    slice->info.lengthFrames = static_cast<decltype (slice->info.lengthFrames)> (to - from);
    // A few milliseconds' fade at each end, so the pad doesn't click.
    const auto fade = std::min<std::size_t> (static_cast<std::size_t> (rate * 0.003), (to - from) / 4);
    for (auto& ch : slice->channels)
        for (std::size_t i = 0; i < fade; ++i)
        {
            const auto g = static_cast<float> (i) / static_cast<float> (fade);
            ch[i] *= g;
            ch[ch.size() - 1 - i] *= g;
        }

    // Deliver it: a WAV in the samples folder, Bank X / X07 Title (2 beats).wav.
    const auto title = juce::String::fromUTF8 (record->info.title.c_str());
    const auto folder = samplesFolder().getChildFile ("Bank " + slotName (slot).substring (0, 1));
    const auto file = folder.getNonexistentChildFile (slotName (slot) + " " + safeName (title + " (" + what.toLowerCase() + ")"), ".wav", false);
    folder.createDirectory();
    try
    {
        io::writeWav (file.getFullPathName().toStdString(), *slice);
    }
    catch (const std::exception& e)
    {
        message = "COULDN'T SAVE: " + juce::String (e.what()).toUpperCase();
        repaint();
        return;
    }
    setSlot (slot, slice, file);
    message = "CAPTURED " + what + " TO " + slotName (slot);
    sampler.trigger (slot);
    repaint();
}

void SamplerSection::loadFile (int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sound into pad " + slotName (slot), samplesFolder(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              message = "LOADING " + file.getFileName().toUpperCase() + "...";
                              repaint();
                              std::thread ([weak = juce::WeakReference<SamplerSection> (this), slot, file] {
                                  std::shared_ptr<const io::DecodedAudio> audio;
                                  juce::String problem;
                                  try
                                  {
                                      audio = std::make_shared<io::DecodedAudio> (io::decode (file.getFullPathName().toStdString()));
                                  }
                                  catch (const std::exception& e)
                                  {
                                      problem = e.what();
                                  }
                                  juce::MessageManager::callAsync ([weak, slot, file, audio, problem] {
                                      if (auto* self = weak.get())
                                      {
                                          if (audio)
                                          {
                                              self->setSlot (slot, audio, file);
                                              self->message = "LOADED " + file.getFileName().toUpperCase() + " TO " + self->slotName (slot);
                                          }
                                          else
                                          {
                                              self->message = "COULDN'T OPEN IT: " + problem.toUpperCase();
                                          }
                                          self->repaint();
                                      }
                                  });
                              }).detach();
                          });
}

void SamplerSection::clear (int slot)
{
    setSlot (slot, nullptr, {});
    message = "CLEARED " + slotName (slot) + " (THE FILE IS STILL IN THE SAMPLES FOLDER)";
    repaint();
}

void SamplerSection::setSlot (int slot, std::shared_ptr<const io::DecodedAudio> audio, const juce::File& file)
{
    sampler.setSample (slot, std::move (audio));
    files[static_cast<std::size_t> (slot)] = file;
    saveKit();
    refreshPads();
}

void SamplerSection::saveKit()
{
    juce::StringArray paths;
    for (const auto& f : files)
        paths.add (f.getFullPathName());
    settings->setValue ("padKit", paths.joinIntoString ("\n"));
}

void SamplerSection::restoreKit()
{
    const auto paths = juce::StringArray::fromLines (settings->getValue ("padKit"));
    std::vector<std::pair<int, juce::File>> toLoad;
    for (int i = 0; i < juce::jmin (128, paths.size()); ++i)
        if (paths[i].isNotEmpty() && juce::File::isAbsolutePath (paths[i]) && juce::File (paths[i]).existsAsFile())
        {
            files[static_cast<std::size_t> (i)] = juce::File (paths[i]);
            toLoad.emplace_back (i, juce::File (paths[i]));
        }
    if (toLoad.empty())
        return;
    std::thread ([weak = juce::WeakReference<SamplerSection> (this), toLoad] {
        for (const auto& [slot, file] : toLoad)
        {
            std::shared_ptr<const io::DecodedAudio> audio;
            try
            {
                audio = std::make_shared<io::DecodedAudio> (io::decode (file.getFullPathName().toStdString()));
            }
            catch (const std::exception&)
            {
                continue;
            }
            juce::MessageManager::callAsync ([weak, slot = slot, audio] {
                if (auto* self = weak.get())
                {
                    self->sampler.setSample (slot, audio);
                    self->refreshPads();
                }
            });
        }
    }).detach();
}

void SamplerSection::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the folder your samples are saved to", samplesFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc) {
                              const auto dir = fc.getResult();
                              if (! dir.isDirectory())
                                  return;
                              settings->setValue ("samplesFolder", dir.getFullPathName());
                              message = "NEW SAMPLES GO TO " + dir.getFileName().toUpperCase();
                              repaint();
                          });
}

void SamplerSection::paint (juce::Graphics& g)
{
    hw::silk (g, "SAMPLER", "8 BANKS x 16 PADS", bankKeys[0].getBounds().toFloat().withWidth (300).withHeight (16).translated (0, -22));
    hw::silk (g, "LENGTH", "BEATS", lengthKnob.getBounds().toFloat().withHeight (16).translated (0, -18), juce::Justification::centred);
    const auto len = lengthNames[juce::jlimit (0, 6, static_cast<int> (lengthKnob.getValue()))];
    hw::glowText (g, juce::String (len), lengthKnob.getBounds().toFloat().withY (static_cast<float> (lengthKnob.getBottom()) - 4).withHeight (20),
                  hw::vfd (20.0f), hw::colour::amber, juce::Justification::centred);

    // Selected pad and messages.
    hw::recess (g, readoutArea.toFloat());
    auto r = readoutArea.toFloat().reduced (10, 4);
    const auto slot = slotOf (selected);
    const auto& f = files[static_cast<std::size_t> (slot)];
    hw::glowText (g, slotName (slot) + "  " + (sampler.sample (slot) != nullptr ? f.getFileNameWithoutExtension().toUpperCase().fromFirstOccurrenceOf (" ", false, false) : juce::String ("EMPTY")),
                  r.removeFromTop (22), hw::vfd (22.0f));
    hw::glowText (g, message, r.removeFromTop (20), hw::vfd (19.0f), hw::colour::amberMid);
    hw::glass (g, readoutArea.toFloat());

    // Where samples go.
    hw::silk (g, "SAMPLES TO", {}, folderArea.toFloat().withHeight (16).translated (0, -20));
    hw::recess (g, folderArea.toFloat());
    auto path = samplesFolder().getFullPathName();
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    if (path.startsWith (home))
        path = "~" + path.substring (home.length());
    hw::glowText (g, path, folderArea.toFloat().reduced (10, 0), hw::vfd (20.0f));
    hw::glass (g, folderArea.toFloat());
}

void SamplerSection::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (22);
    auto banks = area.removeFromTop (34);
    const auto bw = (banks.getWidth() - 7 * 4) / 8;
    for (auto& k : bankKeys)
    {
        k.setBounds (banks.removeFromLeft (bw));
        banks.removeFromLeft (4);
    }
    area.removeFromTop (10);

    // Pads: a 4 x 4 square on the left, as big as fits.
    const auto size = juce::jmin ((area.getHeight() - 3 * 6) / 4, (area.getWidth() / 2 - 3 * 6) / 4);
    padArea = area.removeFromLeft (size * 4 + 18).withHeight (size * 4 + 18);
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            pads[static_cast<std::size_t> (row * 4 + col)].setBounds (padArea.getX() + col * (size + 6), padArea.getY() + row * (size + 6), size, size);
    area.removeFromLeft (16);

    // Right: the chosen pad, then LENGTH and the keys, then where samples go.
    readoutArea = area.removeFromTop (48).reduced (2);
    area.removeFromTop (26);
    auto block = area.removeFromTop (88);
    lengthKnob.setBounds (block.removeFromLeft (88));
    block.removeFromLeft (10);
    const auto kw = (block.getWidth() - 6) / 2;
    auto r1 = block.removeFromTop (38);
    captureKey.setBounds (r1.removeFromLeft (kw));
    r1.removeFromLeft (6);
    loadKey.setBounds (r1);
    block.removeFromTop (8);
    auto r2 = block.removeFromTop (38);
    clearKey.setBounds (r2.removeFromLeft (kw));
    r2.removeFromLeft (6);
    stopKey.setBounds (r2);

    auto folderRow = area.removeFromBottom (36);
    folderKey.setBounds (folderRow.removeFromRight (96));
    folderRow.removeFromRight (6);
    folderArea = folderRow.reduced (2, 2);
}

void SamplerSection::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;
    for (int p = 0; p < 16; ++p)
        if (e.eventComponent == &pads[static_cast<std::size_t> (p)])
            showPadMenu (p);
}

} // namespace dss1::ui
