#include "io/AudioDecoder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <stdexcept>

// The decoders are single-header libraries; their code is compiled here, once.
#if defined(__clang__) || defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wunused-function"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wdouble-promotion"
 #pragma GCC diagnostic ignored "-Wcast-align"
 #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
 #pragma GCC diagnostic ignored "-Wold-style-cast"
 #pragma GCC diagnostic ignored "-Wuseless-cast"
 #pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO_W
#include <dr_wav.h>

#define DR_FLAC_IMPLEMENTATION
#include <dr_flac.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#include <minimp3_ex.h>

#if defined(__clang__) || defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif

namespace dss1::io
{

namespace
{
    std::string extensionOf (const std::string& path)
    {
        return std::filesystem::path (path).extension().string();
    }

    AudioFormat formatOf (const std::string& path)
    {
        const auto format = formatForExtension (extensionOf (path));
        if (! format)
            throw std::runtime_error ("DSS1 can't read " + std::filesystem::path (path).filename().string()
                                      + " yet: it reads WAV, AIFF, FLAC and MP3");
        return *format;
    }

    [[noreturn]] void unreadable (const std::string& path, const char* why)
    {
        throw std::runtime_error ("Couldn't read " + std::filesystem::path (path).filename().string() + ": " + why);
    }

    void checkInfo (const std::string& path, const AudioInfo& info)
    {
        if (info.sampleRate <= 0 || info.channels <= 0)
            unreadable (path, "the file doesn't say its sample rate or channel count");
    }

    /// Splits interleaved samples into one vector per channel.
    std::vector<std::vector<float>> deinterleave (const std::vector<float>& interleaved, int channels, std::size_t frames)
    {
        std::vector<std::vector<float>> out (static_cast<std::size_t> (channels), std::vector<float> (frames));
        const auto ch = static_cast<std::size_t> (channels);
        for (std::size_t f = 0; f < frames; ++f)
            for (std::size_t c = 0; c < ch; ++c)
                out[c][f] = interleaved[f * ch + c];
        return out;
    }

    // --- WAV and AIFF ------------------------------------------------------------------------------------

    struct Wav
    {
        drwav wav {};
        bool open = false;

        explicit Wav (const std::string& path) { open = drwav_init_file (&wav, path.c_str(), nullptr) != DRWAV_FALSE; }
        ~Wav()
        {
            if (open)
                drwav_uninit (&wav);
        }
        Wav (const Wav&) = delete;
        Wav& operator= (const Wav&) = delete;
    };

    AudioInfo probeWav (const std::string& path, AudioFormat format)
    {
        Wav w (path);
        if (! w.open)
            unreadable (path, "it isn't a WAV or AIFF file DSS1 understands");
        AudioInfo info { format, static_cast<double> (w.wav.sampleRate), static_cast<int> (w.wav.channels),
                         static_cast<Frame> (w.wav.totalPCMFrameCount), 0 };
        checkInfo (path, info);
        return info;
    }

    DecodedAudio decodeWav (const std::string& path, AudioFormat format)
    {
        Wav w (path);
        if (! w.open)
            unreadable (path, "it isn't a WAV or AIFF file DSS1 understands");
        DecodedAudio out;
        out.info = { format, static_cast<double> (w.wav.sampleRate), static_cast<int> (w.wav.channels),
                     static_cast<Frame> (w.wav.totalPCMFrameCount), 0 };
        checkInfo (path, out.info);

        const auto frames = static_cast<std::size_t> (w.wav.totalPCMFrameCount);
        std::vector<float> interleaved (frames * w.wav.channels);
        const auto read = drwav_read_pcm_frames_f32 (&w.wav, frames, interleaved.data());
        out.info.lengthFrames = static_cast<Frame> (read);
        out.channels = deinterleave (interleaved, out.info.channels, static_cast<std::size_t> (read));
        return out;
    }

    // --- FLAC --------------------------------------------------------------------------------------------

    struct Flac
    {
        drflac* flac = nullptr;
        explicit Flac (const std::string& path) : flac (drflac_open_file (path.c_str(), nullptr)) {}
        ~Flac()
        {
            if (flac != nullptr)
                drflac_close (flac);
        }
        Flac (const Flac&) = delete;
        Flac& operator= (const Flac&) = delete;
    };

    AudioInfo probeFlac (const std::string& path)
    {
        Flac f (path);
        if (f.flac == nullptr)
            unreadable (path, "it isn't a FLAC file DSS1 understands");
        AudioInfo info { AudioFormat::flac, static_cast<double> (f.flac->sampleRate), static_cast<int> (f.flac->channels),
                         static_cast<Frame> (f.flac->totalPCMFrameCount), 0 };
        checkInfo (path, info);
        return info;
    }

    DecodedAudio decodeFlac (const std::string& path)
    {
        Flac f (path);
        if (f.flac == nullptr)
            unreadable (path, "it isn't a FLAC file DSS1 understands");
        DecodedAudio out;
        out.info = probeFlac (path);

        const auto frames = static_cast<std::size_t> (f.flac->totalPCMFrameCount);
        std::vector<float> interleaved (frames * f.flac->channels);
        const auto read = drflac_read_pcm_frames_f32 (f.flac, frames, interleaved.data());
        out.info.lengthFrames = static_cast<Frame> (read);
        out.channels = deinterleave (interleaved, out.info.channels, static_cast<std::size_t> (read));
        return out;
    }

    // --- MP3 ---------------------------------------------------------------------------------------------

    struct Mp3
    {
        mp3dec_ex_t dec {};
        bool open = false;

        explicit Mp3 (const std::string& path) { open = mp3dec_ex_open (&dec, path.c_str(), MP3D_SEEK_TO_SAMPLE) == 0; }
        ~Mp3()
        {
            if (open)
                mp3dec_ex_close (&dec);
        }
        Mp3 (const Mp3&) = delete;
        Mp3& operator= (const Mp3&) = delete;

        AudioInfo info() const
        {
            const auto channels = dec.info.channels > 0 ? dec.info.channels : 1;
            return { AudioFormat::mp3, static_cast<double> (dec.info.hz), dec.info.channels,
                     static_cast<Frame> (dec.samples / static_cast<std::uint64_t> (channels)),
                     static_cast<Frame> (dec.start_delay / channels) };
        }
    };

    AudioInfo probeMp3 (const std::string& path)
    {
        Mp3 m (path);
        if (! m.open)
            unreadable (path, "it isn't an MP3 file DSS1 understands");
        const auto info = m.info();
        checkInfo (path, info);
        return info;
    }

    DecodedAudio decodeMp3 (const std::string& path)
    {
        Mp3 m (path);
        if (! m.open)
            unreadable (path, "it isn't an MP3 file DSS1 understands");
        DecodedAudio out;
        out.info = m.info();
        checkInfo (path, out.info);

        std::vector<float> interleaved (static_cast<std::size_t> (m.dec.samples));
        const auto read = mp3dec_ex_read (&m.dec, interleaved.data(), interleaved.size());
        if (read != interleaved.size() && m.dec.last_error != 0)
            unreadable (path, "the MP3 data is damaged");
        const auto frames = read / static_cast<std::size_t> (out.info.channels);
        out.info.lengthFrames = static_cast<Frame> (frames);
        out.channels = deinterleave (interleaved, out.info.channels, frames);
        return out;
    }
} // namespace

std::optional<AudioFormat> formatForExtension (std::string extension)
{
    if (! extension.empty() && extension.front() == '.')
        extension.erase (0, 1);
    std::transform (extension.begin(), extension.end(), extension.begin(),
                    [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    if (extension == "wav" || extension == "wave")
        return AudioFormat::wav;
    if (extension == "aif" || extension == "aiff" || extension == "aifc")
        return AudioFormat::aiff;
    if (extension == "flac")
        return AudioFormat::flac;
    if (extension == "mp3")
        return AudioFormat::mp3;
    return std::nullopt;
}

AudioInfo probe (const std::string& path)
{
    switch (const auto format = formatOf (path))
    {
        case AudioFormat::wav:
        case AudioFormat::aiff: return probeWav (path, format);
        case AudioFormat::flac: return probeFlac (path);
        case AudioFormat::mp3:  return probeMp3 (path);
    }
    unreadable (path, "unknown format");
}

DecodedAudio decode (const std::string& path)
{
    switch (const auto format = formatOf (path))
    {
        case AudioFormat::wav:
        case AudioFormat::aiff: return decodeWav (path, format);
        case AudioFormat::flac: return decodeFlac (path);
        case AudioFormat::mp3:  return decodeMp3 (path);
    }
    unreadable (path, "unknown format");
}

void writeWav (const std::string& path, const DecodedAudio& audio)
{
    const auto channels = audio.channels.size();
    if (channels == 0 || audio.info.sampleRate <= 0)
        throw std::runtime_error ("Nothing to write to " + std::filesystem::path (path).filename().string());

    drwav_data_format format {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = static_cast<drwav_uint32> (channels);
    format.sampleRate = static_cast<drwav_uint32> (std::lround (audio.info.sampleRate));
    format.bitsPerSample = 24;

    drwav wav {};
    if (drwav_init_file_write (&wav, path.c_str(), &format, nullptr) == DRWAV_FALSE)
        throw std::runtime_error ("Couldn't create " + path);

    const auto frames = audio.channels.front().size();
    std::vector<std::uint8_t> block;
    constexpr std::size_t framesPerBlock = 4096;
    bool ok = true;
    for (std::size_t start = 0; start < frames && ok; start += framesPerBlock)
    {
        const auto n = std::min (framesPerBlock, frames - start);
        block.resize (n * channels * 3);
        auto* out = block.data();
        for (std::size_t f = start; f < start + n; ++f)
            for (std::size_t c = 0; c < channels; ++c)
            {
                const auto x = std::clamp (audio.channels[c][f], -1.0f, 1.0f);
                const auto v = static_cast<std::int32_t> (std::lround (static_cast<double> (x) * 8388607.0));
                *out++ = static_cast<std::uint8_t> (v & 0xFF);
                *out++ = static_cast<std::uint8_t> ((v >> 8) & 0xFF);
                *out++ = static_cast<std::uint8_t> ((v >> 16) & 0xFF);
            }
        ok = drwav_write_pcm_frames (&wav, n, block.data()) == n;
    }
    drwav_uninit (&wav);
    if (! ok)
        throw std::runtime_error ("Couldn't write all of " + path);
}

} // namespace dss1::io
