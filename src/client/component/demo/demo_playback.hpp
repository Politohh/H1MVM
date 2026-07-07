#pragma once

namespace demo_playback
{
	[[nodiscard]] bool playing();
	[[nodiscard]] bool is_replay_armed();
	[[nodiscard]] bool startplay(std::string_view file_name, bool repeat = true);
	[[nodiscard]] bool startplay(std::filesystem::path& file_path, bool repeat = true);
	[[nodiscard]] void stopplay();
}
