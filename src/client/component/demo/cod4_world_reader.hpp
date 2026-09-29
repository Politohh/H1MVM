#pragma once
#include "cod4_playerstate_reader.hpp"
#include "cod4_world_fields.hpp"
#include <sstream>

namespace cod4
{
    template<std::size_t N> struct wire_state
    {
        std::array<std::uint32_t, N / 4> words{};
        unsigned get(unsigned offset) const
        {
            if (offset % 4) throw std::runtime_error("Unaligned COD4 wire field");
            return words.at(offset / 4);
        }
        void set(unsigned offset, unsigned value)
        {
            if (offset % 4) throw std::runtime_error("Unaligned COD4 wire field");
            words.at(offset / 4) = value;
        }
    };
    using entity_state = wire_state<244>;
    using client_state = wire_state<100>;
    struct world_frame
    {
        int time{}, sequence{};
        std::map<unsigned, entity_state> entities;
        std::map<unsigned, client_state> clients;
    };
    struct hud_frame
    {
        int time{};
        std::array<std::vector<wire_state<160>>, 2> groups;
    };
    struct server_command
    {
        int time{}, sequence{}, index{};
        std::string text;
    };
    struct world_track
    {
        int client_num{};
        std::size_t discarded_deltas{};
        std::map<unsigned, entity_state> baselines;
        std::vector<world_frame> frames;
        std::vector<hud_frame> hud_frames;
        std::vector<server_command> commands;
        const world_frame& sample(int time) const
        {
            if (frames.empty()) throw std::runtime_error("Empty COD4 world track");
            const auto next = std::upper_bound(frames.begin(), frames.end(), time,
                [](int t, const world_frame& f) { return t < f.time; });
            return next == frames.begin() ? frames.front() : *(next - 1);
        }
        const hud_frame& sample_hud(int time) const
        {
            if (hud_frames.empty()) throw std::runtime_error("Empty COD4 HUD track");
            const auto next = std::upper_bound(hud_frames.begin(), hud_frames.end(), time,
                [](int t, const hud_frame& f) { return t < f.time; });
            return next == hud_frames.begin() ? hud_frames.front() : *(next - 1);
        }
    };
    namespace detail
    {
        inline unsigned entity_index(reader& msg, int& prior, unsigned width)
        {
            if (msg.bits(1)) ++prior;
            else if (width != 10 || msg.bits(1)) prior = msg.bits(width);
            else prior += msg.bits(4);
            if (prior < 0 || static_cast<unsigned>(prior) >= (1u << width))
                throw std::runtime_error("Invalid COD4 entity index");
            return prior;
        }
        template<std::size_t N> void state_delta(reader& msg, int time, wire_state<N>& state,
            state_field field, const std::array<float, 3>& center)
        {
            state.set(field.offset, player_delta(msg, time, {field.name, field.bits, field.hint}, state.get(field.offset), center));
        }
        inline bool entity_delta(reader& msg, int time, entity_state& state, unsigned number,
            const std::array<float, 3>& center)
        {
            if (msg.bits(1)) return false;
            if (msg.bits(1))
            {
                const auto count = msg.bits(6);
                state_delta(msg, time, state, fields_for_entity(0)[0], center);
                const auto fields = fields_for_entity(state.get(4));
                if (!count || count > fields.size()) throw std::runtime_error("Invalid COD4 entity field count");
                for (unsigned i = 1; i < count; ++i) state_delta(msg, time, state, fields[i], center);
            }
            state.set(0, number);
            return true;
        }
        struct player_tail
        {
            std::array<wire_state<28>, 16> objectives{};
            std::array<std::array<wire_state<160>, 31>, 2> hud{};
        };
        inline void read_player_tail(reader& msg, int time, player_tail& tail, const std::array<float, 3>& center)
        {
            if (msg.bits(1)) for (auto& objective : tail.objectives)
            {
                objective.set(0, msg.bits(3));
                if (msg.bits(1)) for (const auto f : objective_fields) state_delta(msg, time, objective, f, center);
            }
            if (msg.bits(1)) for (auto& group : tail.hud)
            {
                const auto used = msg.bits(5);
                for (unsigned i = 0; i < used; ++i)
                {
                    const auto last = msg.bits(6);
                    if (last >= hud_fields.size()) throw std::runtime_error("Invalid COD4 HUD field count");
                    for (unsigned j = 0; j <= last; ++j) state_delta(msg, time, group[i], hud_fields[j], center);
                }
                for (unsigned i = used; i < group.size(); ++i) group[i] = {};
            }
            if (msg.bits(1)) { msg.require(128); msg.pos += 128; }
        }
        // Consume the same fixed prefix as decode_players; state is retained for
        // the source XOR codecs. HUD records are decoded only to reach the world.
        inline void read_player_prefix(reader& msg, player_frame& frame)
        {
            const bool absolute = msg.bits(1) != 0;
            const auto count = msg.bits(8);
            if (count > player_fields.size()) throw std::runtime_error("Invalid COD4 player field count");
            for (unsigned i = 0; i < count; ++i)
                frame.values[i] = player_delta(msg, frame.time, player_fields[i], absolute && player_fields[i].hint == 3 ? 0 : frame.values[i]);
            if (msg.bits(1))
            {
                const auto mask = msg.bits(5);
                for (unsigned i = 0; i < 3; ++i)
                    if (mask & (1u << i)) frame.stats[i] = msg.get<std::int16_t>();
                if (mask & 8) frame.stats[3] = static_cast<std::int32_t>(msg.bits(6));
                if (mask & 16) frame.stats[4] = msg.get<std::uint8_t>();
            }
            if (msg.bits(1)) for (unsigned i = 0; i < 4; ++i) if (msg.bits(1))
            {
                const auto mask = msg.get<std::uint16_t>();
                for (unsigned j = 0; j < 16; ++j) if (mask & (1u << j)) frame.ammo[i * 16 + j] = msg.get<std::int16_t>();
            }
            for (unsigned i = 0; i < 8; ++i) if (msg.bits(1))
            {
                const auto mask = msg.get<std::uint16_t>();
                for (unsigned j = 0; j < 16; ++j) if (mask & (1u << j)) frame.clips[i * 16 + j] = msg.get<std::int16_t>();
            }
        }
    }
    inline world_track decode_world(std::span<const std::uint8_t> bytes)
    {
        const auto camera = decode(bytes);
        std::array<float, 3> center{};
        if (const auto found = camera.configstrings.find(12); found != camera.configstrings.end())
        {
            std::istringstream input(found->second); input >> center[0] >> center[1] >> center[2];
            if (!input) throw std::runtime_error("Invalid COD4 map center");
        }
        detail::reader file{bytes};
        world_track track;
        struct baseline { player_frame player; detail::player_tail tail; world_frame world; };
        std::map<int, baseline> history;
        bool eof = false;
        int last_time = 0;
        while (file.pos < bytes.size())
        {
            const auto type = file.get<std::uint8_t>();
            if (type == 1) { file.require(52); file.pos += 52; continue; }
            if (type != 0) throw std::runtime_error("Unsupported COD4 world block");
            const auto sequence = file.get<int>(), size = file.get<int>();
            if (size == -1) { eof = true; break; }
            if (size < 4 || size > 0x20000) throw std::runtime_error("Invalid COD4 world packet size");
            file.require(size);
            const auto decoded = detail::decompress(bytes.subspan(file.pos + 4, size - 4)); file.pos += size;
            detail::reader msg{decoded};
            while (msg.pos < decoded.size())
            {
                const auto op = msg.get<std::uint8_t>();
                if (op == 7) break;
                if (!op) continue;
                if (op == 4)
                {
                    const auto index = msg.get<int>();
                    track.commands.push_back({last_time, sequence, index, msg.string()});
                    continue;
                }
                if (op == 11) { msg.get<int>(); msg.get<std::uint8_t>(); msg.string(); msg.string(); continue; }
                if (op == 1)
                {
                    msg.get<int>(); int prior = -1;
                    while (true)
                    {
                        const auto section = msg.get<std::uint8_t>();
                        if (section == 7) break;
                        if (section == 2)
                        {
                            const auto n = msg.get<std::uint16_t>(); int index = -1;
                            for (unsigned i = 0; i < n; ++i) { index = msg.bits(1) ? index + 1 : msg.bits(12); msg.string(); }
                        }
                        else if (section == 3)
                        {
                            const auto number = detail::entity_index(msg, prior, 10);
                            entity_state state;
                            if (!detail::entity_delta(msg, 0, state, number, center)) throw std::runtime_error("Removed COD4 gamestate baseline");
                            track.baselines[number] = state;
                        }
                        else throw std::runtime_error("Unknown COD4 gamestate section");
                    }
                    track.client_num = msg.get<int>(); msg.get<int>();
                    continue;
                }
                if (op != 6) throw std::runtime_error("Unsupported COD4 world service " + std::to_string(op));
                const int time = msg.get<int>(); const auto delta = msg.get<std::uint8_t>(); msg.get<std::uint8_t>();
                last_time = time;
                baseline current;
                if (delta)
                {
                    const auto found = history.find(sequence - delta);
                    // Match COD4 CL_ParseSnapshot's MSG_Discard on an absent
                    // delta frame, preserving only snapshots we can reconstruct.
                    if (found == history.end()) { ++track.discarded_deltas; break; }
                    current = found->second;
                }
                current.player.time = time; current.player.sequence = sequence;
                current.world.time = time; current.world.sequence = sequence;
                detail::read_player_prefix(msg, current.player);
                detail::read_player_tail(msg, time, current.tail, center);
                int prior = -1;
                while (true)
                {
                    const auto number = detail::entity_index(msg, prior, 10);
                    if (number == 1023) break;
                    auto state = current.world.entities.contains(number) ? current.world.entities.at(number)
                        : track.baselines.contains(number) ? track.baselines.at(number) : entity_state{};
                    if (detail::entity_delta(msg, time, state, number, center)) current.world.entities[number] = state;
                    else current.world.entities.erase(number);
                }
                prior = -1;
                while (msg.bits(1))
                {
                    const auto number = detail::entity_index(msg, prior, 6);
                    if (msg.bits(1)) { current.world.clients.erase(number); continue; }
                    auto& client = current.world.clients[number]; client.set(0, number);
                    if (msg.bits(1))
                    {
                        const auto count = msg.bits(5);
                        if (count > client_fields.size()) throw std::runtime_error("Invalid COD4 client field count");
                        for (unsigned i = 0; i < count; ++i) detail::state_delta(msg, time, client, client_fields[i], center);
                    }
                }
                history[sequence] = current;
                while (!history.empty() && history.begin()->first < sequence - 255) history.erase(history.begin());
                while (!track.frames.empty() && track.frames.back().time >= time) track.frames.pop_back();
                while (!track.hud_frames.empty() && track.hud_frames.back().time >= time) track.hud_frames.pop_back();
                hud_frame hud;
                hud.time = time;
                for (unsigned group = 0; group < 2; ++group)
                    for (const auto& element : current.tail.hud[group])
                    {
                        if (!element.get(0)) break;
                        hud.groups[group].push_back(element);
                    }
                track.hud_frames.push_back(std::move(hud));
                track.frames.push_back(std::move(current.world));
            }
        }
        if (!eof || file.pos != bytes.size() || track.frames.empty()) throw std::runtime_error("Incomplete COD4 world track");
        return track;
    }
}
