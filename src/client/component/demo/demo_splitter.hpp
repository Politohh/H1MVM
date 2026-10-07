#pragma once
#include "demo_file_footer.hpp"
#include "demo_timeline.hpp"
#include <zlib.h>
#include <array>
#include <functional>
#include <limits>
#include <ostream>
#include <stdexcept>

// File-only operations: no engine state, game addresses or source mutation.
namespace demo_splitter
{
    struct result { int first{}, last{}; std::size_t frames{}; };
    using progress = std::function<bool(int)>;

    template<typename Frame> std::vector<Frame> clip_frames(const std::vector<Frame>& frames, int start, int end)
    {
        if (frames.size() < 2 || start < 0 || end <= start || static_cast<std::int64_t>(end) >
            static_cast<std::int64_t>(frames.back().time) - frames.front().time)
            throw std::runtime_error("Choose a start and end within the recording");
        const auto from = static_cast<std::int64_t>(frames.front().time) + start;
        const auto to = static_cast<std::int64_t>(frames.front().time) + end;
        const auto first = std::lower_bound(frames.begin(), frames.end(), from,
            [](const auto& frame, auto time) { return frame.time < time; });
        const auto last = std::upper_bound(first, frames.end(), to,
            [](auto time, const auto& frame) { return time < frame.time; });
        if (last - first < 2 || (last - 1)->time <= first->time)
            throw std::runtime_error("The range must contain at least two frames");
        return {first, last};
    }

    inline void write_chunk(std::ostream& out, unsigned id, std::span<const std::uint8_t> bytes)
    {
        if (bytes.size() > 131072) throw std::runtime_error("Oversized split demo chunk");
        const auto tag = static_cast<std::uint8_t>(id | (bytes.size() < 256 ? 128 : 0));
        const auto size = static_cast<std::uint32_t>(bytes.size());
        out.write(reinterpret_cast<const char*>(&tag), 1);
        out.write(reinterpret_cast<const char*>(&size), size < 256 ? 1 : 4);
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!out) throw std::runtime_error("Cannot write split recording");
    }

    inline std::vector<std::uint8_t> unpack(std::span<const std::uint8_t> bytes)
    {
        if (bytes.size() <= 8 || !(bytes[7] & 128)) throw std::runtime_error("Unsupported recording packet compression");
        std::vector<std::uint8_t> packet(131072);
        uLongf size = static_cast<uLongf>(packet.size());
        if (uncompress(packet.data(), &size, bytes.data() + 8, static_cast<uLong>(bytes.size() - 8)) != Z_OK || !size)
            throw std::runtime_error("Invalid compressed recording packet");
        packet.resize(size);
        return packet;
    }

    // Independent converted snapshots can retain their original server clock.
    // Renumber only the outer packets/archive slots, retaining native payloads
    // and complete model, animation, corpse, facial and effect metadata.
    inline result split_native(std::istream& input, std::ostream& output, int start, int end,
        const progress& update = {})
    {
        const auto tail = demo_file::read_footer(input);
        if (!tail || start < 0 || end <= start || static_cast<std::int64_t>(end) >
            static_cast<std::int64_t>(tail->last) - tail->first)
            throw std::runtime_error("Choose a start and end within the recording");
        const auto from = static_cast<std::int64_t>(tail->first) + start;
        const auto to = static_cast<std::int64_t>(tail->first) + end;
        input.clear(); input.seekg(0, std::ios::end); const auto length = input.tellg(); input.seekg(0);
        result saved;
        bool converted{}, gamestate{}, state{}, snapshot{}, eof{};
        int previous_time = -1, previous_sequence{};
        std::vector<std::uint8_t> prediction;
        std::uint32_t sequence = 1;
        while (input && !eof)
        {
            std::uint8_t tag{}; std::uint32_t size{};
            input.read(reinterpret_cast<char*>(&tag), 1);
            input.read(reinterpret_cast<char*>(&size), tag & 128 ? 1 : 4);
            if (!input || (tag & 96) || size > 131072 || length - input.tellg() < size)
                throw std::runtime_error("Truncated or invalid recording chunk");
            const auto id = tag & 31;
            std::vector<std::uint8_t> bytes(size);
            input.read(reinterpret_cast<char*>(bytes.data()), size);
            if (!input) throw std::runtime_error("Cannot read recording chunk");
            if (id == 31) { if (size) throw std::runtime_error("Invalid recording end marker"); eof = true; break; }
            if (id == 6) continue; // Rebuild duration and range-filtered kill markers.
            if (id == 5)
            {
                const auto first = std::find(bytes.begin(), bytes.end(), 0);
                if (first != bytes.end())
                {
                    const auto begin = first + 1, last = std::find(begin, bytes.end(), 0);
                    const std::string description(begin, last);
                    converted = description == "H1 COD4 conversion v1" || description == "H1 COD4 conversion v2"
                        || description == "H1 COD4 conversion v3" || description == "H1 COD4 conversion v4"
                        || description == "H1 COD4 conversion v5";
                }
                if (!converted) throw std::runtime_error("Split currently requires a converted COD4 replay");
            }
            if (id == 3)
            {
                if (size != 50 || bytes[0] != 0 || !prediction.empty())
                    throw std::runtime_error("Unsupported recording camera data");
                prediction = std::move(bytes); continue;
            }
            if (id == 4)
            {
                if (snapshot || state) throw std::runtime_error("Recording changes gamestate; split this session separately");
                state = true;
            }
            if (id == 2)
            {
                if (!converted) throw std::runtime_error("Split currently requires a converted COD4 replay");
                const auto packet = unpack(bytes);
                int source_sequence{}; std::memcpy(&source_sequence, bytes.data(), 4);
                if (source_sequence <= previous_sequence) throw std::runtime_error("Invalid recording packet sequence");
                previous_sequence = source_sequence;
                if ((packet[0] & 7) == 0 && !gamestate && !snapshot)
                {
                    gamestate = true;
                    std::memcpy(bytes.data(), &sequence, 4);
                }
                else
                {
                    if (!gamestate || !state || packet.size() < 7 || (packet[0] & 7) != 6
                        || packet[5] != 0 || packet[6] != 8 || prediction.empty())
                        throw std::runtime_error("Recording lacks independent snapshots for splitting");
                    int time{}, camera_time{};
                    std::memcpy(&time, packet.data() + 1, 4);
                    std::memcpy(&camera_time, prediction.data() + 2, 4);
                    if (time < tail->first || time > tail->last || time < previous_time || time != camera_time)
                        throw std::runtime_error("Invalid recording snapshot clock");
                    snapshot = true; previous_time = time;
                    if (time >= from && time <= to)
                    {
                        if (!saved.frames) saved.first = time;
                        saved.last = time;
                        prediction[1] = static_cast<std::uint8_t>(saved.frames);
                        write_chunk(output, 3, prediction);
                        ++saved.frames; ++sequence;
                        std::memcpy(bytes.data(), &sequence, 4);
                        write_chunk(output, 2, bytes);
                    }
                    prediction.clear();
                    if (update && (source_sequence % 128) == 0 && !update(static_cast<int>(
                        static_cast<std::int64_t>(input.tellg()) * 100 / static_cast<std::int64_t>(length))))
                        throw std::runtime_error("Split cancelled");
                    continue;
                }
            }
            else if (snapshot || (id != 0 && id != 1 && id != 4 && id != 5 && id != 7 && id != 8))
                throw std::runtime_error("Unsupported recording chunk layout");
            write_chunk(output, id, bytes);
        }
        if (!eof || input.tellg() != length || !prediction.empty() || saved.frames < 2 || saved.last <= saved.first)
            throw std::runtime_error("The range must contain at least two frames in a complete recording");
        std::string footer(reinterpret_cast<const char*>(&saved.first), 4);
        footer.append(reinterpret_cast<const char*>(&saved.last), 4);
        const auto seconds = (saved.last - saved.first) / 1000;
        footer += "[" + std::to_string(seconds / 3600) + ":" + std::to_string(seconds / 60 % 60)
            + ":" + std::to_string(seconds % 60) + "]";
        footer.push_back(0);
        if (const auto timeline = demo_timeline::decode(tail->bytes, tail->first, tail->last))
            footer += demo_timeline::encode(timeline->pov, timeline->events, saved.first, saved.last);
        write_chunk(output, 6, {reinterpret_cast<const std::uint8_t*>(footer.data()), footer.size()});
        write_chunk(output, 31, {});
        if (update && !update(100)) throw std::runtime_error("Split cancelled");
        return saved;
    }
}
