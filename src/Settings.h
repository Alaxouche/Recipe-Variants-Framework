// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#pragma once

#include <string>

namespace RVF::Settings
{
	struct Values
	{
		bool        enabled{ true };
		/// spdlog level name: trace, debug, info, warn, error, critical, off.
		std::string logLevel{ "info" };

		/// Hide the recipe copies that only differ by which variant they ask for,
		/// keeping one per set that asks for the base items.
		bool mergeDuplicates{ true };

		/// Also fold copies that only differ by which variant they make (one
		/// cutting recipe per half model), keeping the one that makes the base.
		bool mergeVariantResults{ true };
	};

	/// Reads the INI, writing back any missing key with its default and comment.
	/// Safe to call when the file does not exist.
	void Load();

	[[nodiscard]] const Values& Get();

	/// The configured level, or info when the name is not recognised.
	[[nodiscard]] spdlog::level::level_enum LogLevel();
}
