#pragma once
#include "demo_timeline.hpp"

namespace demo_playback
{
	[[nodiscard]] bool playing();
    bool converted();
    std::optional<int> team_score(unsigned team);
	[[nodiscard]] bool is_replay_armed();
	[[nodiscard]] bool startplay(std::string_view file_name, bool repeat = true);
	[[nodiscard]] bool startplay(std::filesystem::path& file_path, bool repeat = true);
	[[nodiscard]] void stopplay();

	[[nodiscard]] bool paused();
	void toggle_pause();
	[[nodiscard]] std::optional<std::int32_t> current_time();
	[[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> time_bounds();
	[[nodiscard]] bool seeking();
	[[nodiscard]] unsigned session_id();
	[[nodiscard]] bool seek_to(std::int32_t server_time);
	[[nodiscard]] const std::optional<demo_timeline::index>& timeline_index();
	[[nodiscard]] const std::filesystem::path& current_path();

	// returns true if the key event was consumed by demo playback and should not be
	// forwarded to the game (used by input.cpp to wire Space -> pause/unpause)
	[[nodiscard]] bool handle_key_event(int key, int down);
}
