#pragma once

// Demo-local shim for the iw6 utils::command_line::get_args() the demo uses to
// support double-click .demo playback (paths passed on the process command line).
// h1-mod has no command_line util, so provide a minimal header-only one.
#include <vector>
#include <filesystem>
#include <string>
#include <Windows.h>
#include <shellapi.h>

namespace utils::command_line
{
	[[nodiscard]] inline std::vector<std::filesystem::path> get_args()
	{
		std::vector<std::filesystem::path> args;

		int argc = 0;
		LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
		if (!argv)
		{
			return args;
		}

		for (int i = 1; i < argc; ++i) // skip argv[0] (exe path)
		{
			args.emplace_back(std::filesystem::path(argv[i]));
		}

		::LocalFree(argv);
		return args;
	}
}
