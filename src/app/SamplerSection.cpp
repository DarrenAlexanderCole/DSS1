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
    // Which pad sits where: rows from the top are 13–16, 9–12, 5–8, 1–4 (like an MPC).
    constexpr int padAt (int row, int col) { return (3 - row) * 4 + col; }
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
        pad.setTooltip ("Key " + juce::String::charToString (static_cast<juce::juce_wchar> (padKeys[p])).toUpperCase()
                        + ". Empty: marks the playhead. Marked: plays the slice. Right-click for more.");
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
    lengthKnob.setValue (settings->getIntValue ("sampleLength", 4), juce::dontSendNotification);
    lengthKnob.setTooltip ("How long the last slice in a track plays, in beats (the others run to the next mark)");
    hwScale (lengthKnob, "1/4|1/2|1|2|4|8|16");
    lengthKnob.onValueChange = [this] {
        settings->setValue ("sampleLength", static_cast<int> (lengthKnob.getValue()));
        updateRegions();
        repaint();
    };
    addAndMakeVisible (lengthKnob);

    hwStyle (saveKey, KeyStyle::cue);
    saveKey.setTooltip ("Write this bank's slices as WAVs into the samples folder (Bank A … Bank H)");
    saveKey.onClick = [this] { saveBank(); };
    loadKey.setTooltip ("Load a sound file into the chosen pad");
    loadKey.onClick = [this] { loadFile (slotOf (selected)); };
    clearKey.setTooltip ("Clear the chosen pad, so its next press marks again");
    clearKey.onClick = [this] { clear (slotOf (selected)); };
    stopKey.onClick = [this] { sampler.stopAll(); };
    hwStyle (folderKey, KeyStyle::cream);
    folderKey.setTooltip ("Choose the folder samples are saved to");
    folderKey.onClick = [this] { chooseFolder(); };
    for (auto* k : { &saveKey, &loadKey, &clearKey, &stopKey, &folderKey })
        addAndMakeVisible (k);

    message = "PRESS A PAD (OR ITS KEY) TO MARK A SLICE";
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

namespace
{
    juce::String clock (double seconds)
    {
        const auto ms = juce::roundToInt (seconds * 1000.0);
        return juce::String (ms / 60000) + ":" + juce::String ((ms / 1000) % 60).paddedLeft ('0', 2) + "." + juce::String (ms % 1000).paddedLeft ('0', 3);
    }
}

juce::String SamplerSection::describe (int slot) const
{
    const auto& pad = kit[static_cast<std::size_t> (slot)];
    const auto key = juce::String::charToString (static_cast<juce::juce_wchar> (padKeys[slot % 16])).toUpperCase();
    const auto head = slotName (slot) + " [" + key + "]  ";
    if (pad.kind == Pad::Kind::empty)
        return head + "EMPTY: PRESS TO MARK";
    if (pad.kind == Pad::Kind::file)
        return head + pad.file.getFileNameWithoutExtension().toUpperCase();
    const auto rate = pad.audio != nullptr ? pad.audio->info.sampleRate : 44100.0;
    const auto end = sliceEnd (slot);
    return head + clock (static_cast<double> (pad.start) / rate) + " +" + juce::String (static_cast<double> (end - pad.start) / rate, 2) + " S";
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
        const bool loaded = kit[static_cast<std::size_t> (slot)].kind != Pad::Kind::empty;
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

bool SamplerSection::handleKey (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown())
        return false;
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    for (int p = 0; p < 16; ++p)
        if (c == static_cast<juce::juce_wchar> (padKeys[p]))
        {
            // Holding a key repeats it; a pad fires once per press.
            if (! held[static_cast<std::size_t> (p)])
            {
                held[static_cast<std::size_t> (p)] = true;
                padPressed (p);
            }
            return true;
        }
    return false;
}

void SamplerSection::keysChanged()
{
    for (int p = 0; p < 16; ++p)
        if (held[static_cast<std::size_t> (p)] && ! juce::KeyPress::isKeyCurrentlyDown (padKeys[p]))
            held[static_cast<std::size_t> (p)] = false;
}

void SamplerSection::padPressed (int pad)
{
    selected = pad;
    const auto slot = slotOf (pad);
    if (kit[static_cast<std::size_t> (slot)].kind == Pad::Kind::empty)
        mark (slot);
    else
        sampler.trigger (slot);
    refreshPads();
    repaint();
}

void SamplerSection::showPadMenu (int pad)
{
    selected = pad;
    refreshPads();
    repaint();
    const auto slot = slotOf (pad);
    const auto& p = kit[static_cast<std::size_t> (slot)];
    const bool loaded = p.kind != Pad::Kind::empty;
    juce::PopupMenu menu;
    menu.addSectionHeader ("PAD " + slotName (slot));
    menu.addItem (loaded ? "Mark Again at the Playhead" : "Mark at the Playhead", [this, slot] {
        clear (slot);
        mark (slot);
    });
    menu.addItem ("Load Sound File...", [this, slot] { loadFile (slot); });
    menu.addItem ("Show in Finder", p.kind == Pad::Kind::file && p.file.existsAsFile(), false, [f = p.file] { f.revealToUser(); });
    menu.addSeparator();
    menu.addItem ("Clear Pad", loaded, false, [this, slot] { clear (slot); });
    menu.addItem ("Clear Bank " + slotName (slot).substring (0, 1), [this] { clearBank(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

std::shared_ptr<const io::DecodedAudio> SamplerSection::sourceFor (const juce::File& track)
{
    for (const auto& p : kit)
        if (p.audio != nullptr && p.kind == Pad::Kind::slice && p.file == track)
            return p.audio;
    return nullptr;
}

void SamplerSection::mark (int slot)
{
    const auto loaded = std::as_const (session).audio();
    const auto* record = session.record();
    if (loaded == nullptr || record == nullptr || loaded->channels.empty())
    {
        message = "LOAD A TRACK FIRST (DOUBLE-CLICK ONE IN CATALOG)";
        repaint();
        return;
    }
    const juce::File track (juce::String::fromUTF8 (record->identity.path.c_str()));
    auto audio = sourceFor (track);  // share one copy of the track between its slices
    if (audio == nullptr)
        audio = loaded;
    const auto total = static_cast<Frame> (audio->channels.front().size());
    const auto at = juce::jlimit<Frame> (0, juce::jmax<Frame> (0, total - 1), session.snap (static_cast<Frame> (session.deck().position())));

    const auto rate = audio->info.sampleRate;
    double perBeat = 60.0 / record->info.bpm.value_or (120.0) * rate;
    if (record->grid && ! record->grid->isEmpty())
    {
        const auto beat = record->grid->beatAt (static_cast<double> (at));
        perBeat = static_cast<double> (record->grid->frameAt (beat + 1) - record->grid->frameAt (beat));
    }

    auto& pad = kit[static_cast<std::size_t> (slot)];
    pad = { Pad::Kind::slice, track, audio, at, perBeat };
    sampler.setSample (slot, audio, at, sliceEnd (slot));
    updateRegions();
    saveKit();
    message = "MARKED " + slotName (slot) + " AT " + clock (static_cast<double> (at) / rate);
    refreshPads();
    repaint();
}

Frame SamplerSection::sliceEnd (int slot) const
{
    const auto& pad = kit[static_cast<std::size_t> (slot)];
    if (pad.kind != Pad::Kind::slice || pad.audio == nullptr)
        return -1;
    const auto total = static_cast<Frame> (pad.audio->channels.front().size());
    // To the next mark in the same track in this bank …
    std::optional<Frame> next;
    const auto first = (slot / 16) * 16;
    for (int s = first; s < first + 16; ++s)
    {
        const auto& other = kit[static_cast<std::size_t> (s)];
        if (s != slot && other.kind == Pad::Kind::slice && other.file == pad.file && other.start > pad.start)
            next = juce::jmin (next.value_or (other.start), other.start);
    }
    if (next)
        return *next;
    // … or, for the last one, LAST SLICE beats.
    const auto beats = lengths[juce::jlimit (0, 6, static_cast<int> (lengthKnob.getValue()))];
    return juce::jmin (total, pad.start + static_cast<Frame> (beats * pad.framesPerBeat));
}

void SamplerSection::updateRegions()
{
    for (int slot = 0; slot < 128; ++slot)
        if (kit[static_cast<std::size_t> (slot)].kind == Pad::Kind::slice)
            sampler.setRegion (slot, kit[static_cast<std::size_t> (slot)].start, sliceEnd (slot));
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
                                              self->kit[static_cast<std::size_t> (slot)] = { Pad::Kind::file, file, audio, 0, 0 };
                                              self->sampler.setSample (slot, audio);
                                              self->updateRegions();
                                              self->saveKit();
                                              self->refreshPads();
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
    kit[static_cast<std::size_t> (slot)] = {};
    sampler.setSample (slot, nullptr);
    updateRegions();
    saveKit();
    message = "CLEARED " + slotName (slot) + ": NEXT PRESS MARKS IT";
    refreshPads();
    repaint();
}

void SamplerSection::clearBank()
{
    for (int p = 0; p < 16; ++p)
    {
        kit[static_cast<std::size_t> (slotOf (p))] = {};
        sampler.setSample (slotOf (p), nullptr);
    }
    updateRegions();
    saveKit();
    message = "CLEARED BANK " + slotName (slotOf (0)).substring (0, 1);
    refreshPads();
    repaint();
}

void SamplerSection::saveBank()
{
    const auto letter = slotName (slotOf (0)).substring (0, 1);
    const auto folder = samplesFolder().getChildFile ("Bank " + letter);
    int written = 0;
    juce::String problem;
    for (int p = 0; p < 16; ++p)
    {
        const auto slot = slotOf (p);
        const auto& pad = kit[static_cast<std::size_t> (slot)];
        if (pad.kind != Pad::Kind::slice || pad.audio == nullptr)
            continue;
        const auto& source = *pad.audio;
        const auto from = static_cast<std::size_t> (pad.start);
        const auto to = static_cast<std::size_t> (juce::jmax (pad.start + 1, sliceEnd (slot)));
        io::DecodedAudio slice;
        slice.info = source.info;
        for (const auto& ch : source.channels)
            slice.channels.emplace_back (ch.begin() + static_cast<std::ptrdiff_t> (from), ch.begin() + static_cast<std::ptrdiff_t> (to));
        slice.info.lengthFrames = static_cast<decltype (slice.info.lengthFrames)> (to - from);
        // The same 3 ms fades the pad plays with.
        const auto fade = std::min<std::size_t> (static_cast<std::size_t> (source.info.sampleRate * 0.003), (to - from) / 4);
        for (auto& ch : slice.channels)
            for (std::size_t i = 0; i < fade; ++i)
            {
                const auto g = static_cast<float> (i) / static_cast<float> (fade);
                ch[i] *= g;
                ch[ch.size() - 1 - i] *= g;
            }
        // A01 Title.wav: saving the bank again replaces what DSS1 saved for that pad before.
        const auto file = folder.getChildFile (slotName (slot) + " " + safeName (pad.file.getFileNameWithoutExtension()) + ".wav");
        folder.createDirectory();
        try
        {
            io::writeWav (file.getFullPathName().toStdString(), slice);
            ++written;
        }
        catch (const std::exception& e)
        {
            problem = e.what();
        }
    }
    if (problem.isNotEmpty())
        message = "COULDN'T SAVE: " + problem.toUpperCase();
    else if (written == 0)
        message = "NO SLICES IN BANK " + letter + " TO SAVE";
    else
        message = "SAVED " + juce::String (written) + " SLICE" + (written == 1 ? "" : "S") + " TO BANK " + letter;
    repaint();
}

void SamplerSection::saveKit()
{
    // One line per pad: empty, "file<TAB>path" or "slice<TAB>track<TAB>start frame<TAB>frames per beat".
    juce::StringArray lines;
    for (const auto& p : kit)
    {
        if (p.kind == Pad::Kind::file)
            lines.add ("file\t" + p.file.getFullPathName());
        else if (p.kind == Pad::Kind::slice)
            lines.add ("slice\t" + p.file.getFullPathName() + "\t" + juce::String (p.start) + "\t" + juce::String (p.framesPerBeat));
        else
            lines.add ({});
    }
    settings->setValue ("padKit", lines.joinIntoString ("\n"));
}

void SamplerSection::restoreKit()
{
    const auto lines = juce::StringArray::fromLines (settings->getValue ("padKit"));
    struct Entry
    {
        int slot;
        Pad pad;
    };
    std::vector<Entry> entries;
    for (int i = 0; i < juce::jmin (128, lines.size()); ++i)
    {
        auto parts = juce::StringArray::fromTokens (lines[i], "\t", {});
        if (parts.size() == 1 && parts[0].isNotEmpty())
            parts.insert (0, "file");  // kits saved by test.20: just a path
        if (parts.size() < 2 || ! juce::File::isAbsolutePath (parts[1]) || ! juce::File (parts[1]).existsAsFile())
            continue;
        Pad pad;
        pad.file = juce::File (parts[1]);
        if (parts[0] == "slice" && parts.size() >= 4)
        {
            pad.kind = Pad::Kind::slice;
            pad.start = parts[2].getLargeIntValue();
            pad.framesPerBeat = parts[3].getDoubleValue();
        }
        else if (parts[0] == "file")
            pad.kind = Pad::Kind::file;
        else
            continue;
        entries.push_back ({ i, pad });
    }
    if (entries.empty())
        return;
    // Decode each file once (slices of one track share it), off the message thread.
    std::thread ([weak = juce::WeakReference<SamplerSection> (this), entries]() mutable {
        std::map<juce::String, std::shared_ptr<const io::DecodedAudio>> decoded;
        for (auto& e : entries)
        {
            const auto path = e.pad.file.getFullPathName();
            if (decoded.count (path) == 0)
            {
                try
                {
                    decoded[path] = std::make_shared<io::DecodedAudio> (io::decode (path.toStdString()));
                }
                catch (const std::exception&)
                {
                    decoded[path] = nullptr;
                }
            }
            e.pad.audio = decoded[path];
        }
        juce::MessageManager::callAsync ([weak, entries] {
            auto* self = weak.get();
            if (self == nullptr)
                return;
            for (const auto& e : entries)
                if (e.pad.audio != nullptr && self->kit[static_cast<std::size_t> (e.slot)].kind == Pad::Kind::empty)
                {
                    self->kit[static_cast<std::size_t> (e.slot)] = e.pad;
                    self->sampler.setSample (e.slot, e.pad.audio, e.pad.start, -1);
                }
            self->updateRegions();
            self->refreshPads();
            self->repaint();
        });
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
    hw::silk (g, "LAST SLICE", "BEATS", lengthKnob.getBounds().toFloat().withHeight (16).translated (0, -18), juce::Justification::centred);
    const auto len = lengthNames[juce::jlimit (0, 6, static_cast<int> (lengthKnob.getValue()))];
    hw::glowText (g, juce::String (len), lengthKnob.getBounds().toFloat().withY (static_cast<float> (lengthKnob.getBottom()) - 4).withHeight (20),
                  hw::vfd (20.0f), hw::colour::amber, juce::Justification::centred);

    // Selected pad and messages.
    hw::recess (g, readoutArea.toFloat());
    auto r = readoutArea.toFloat().reduced (10, 4);
    hw::glowText (g, describe (slotOf (selected)), r.removeFromTop (22), hw::vfd (22.0f));
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

void SamplerSection::paintOverChildren (juce::Graphics& g)
{
    // Each pad's key, in the corner, like the legend on a drum machine.
    for (int p = 0; p < 16; ++p)
    {
        const auto b = pads[static_cast<std::size_t> (p)].getBounds().toFloat().reduced (7, 5);
        const auto loaded = kit[static_cast<std::size_t> (slotOf (p))].kind != Pad::Kind::empty;
        g.setFont (hw::brand (13.0f));
        g.setColour (loaded ? juce::Colours::white.withAlpha (0.85f) : hw::colour::silkDim.withAlpha (0.8f));
        g.drawText (juce::String::charToString (static_cast<juce::juce_wchar> (padKeys[p])).toUpperCase(), b, juce::Justification::topLeft, false);
        if (loaded)
            hw::led (g, { b.getRight() - 3.0f, b.getY() + 5.0f }, 3.0f, kit[static_cast<std::size_t> (slotOf (p))].kind == Pad::Kind::slice ? hw::colour::cueOrange : hw::colour::loopBlue, true);
    }
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
            pads[static_cast<std::size_t> (padAt (row, col))].setBounds (padArea.getX() + col * (size + 6), padArea.getY() + row * (size + 6), size, size);
    area.removeFromLeft (16);

    // Right: the chosen pad, then LENGTH and the keys, then where samples go.
    readoutArea = area.removeFromTop (48).reduced (2);
    area.removeFromTop (26);
    auto block = area.removeFromTop (88);
    lengthKnob.setBounds (block.removeFromLeft (88));
    block.removeFromLeft (10);
    const auto kw = (block.getWidth() - 6) / 2;
    auto r1 = block.removeFromTop (38);
    saveKey.setBounds (r1.removeFromLeft (kw));
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
