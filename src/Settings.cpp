// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#include "Settings.h"

#include <CLIBUtil/simpleINI.hpp>

namespace RVF::Settings
{
	namespace
	{
		constexpr auto kPath = "Data/SKSE/Plugins/RecipeVariantsFramework.ini";

		Values g_values;
	}

	void Load()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.SetMultiKey(false);

		// Not an error: the write below creates the file with every default.
		const auto status = ini.LoadFile(kPath);

		using clib_util::ini::get_value;

		get_value(ini, g_values.enabled, "General", "Enabled",
				  ";Master switch. Set to false and no item is ever swapped.");

		get_value(ini, g_values.mergeDuplicates, "Recipes", "MergeDuplicates",
				  ";Hide the copies of a recipe that only differ by which variant they ask for"
				  "\n;(one copy per apple, per tomato...), keeping one that asks for the base item."
				  "\n;Done in memory at startup; the plugins on disk are not changed.");

		get_value(ini, g_values.mergeVariantResults, "Recipes", "MergeVariantResults",
				  ";Also fold the copies that only differ by which variant they make (a"
				  "\n;cutting recipe per half model), keeping the one that makes the base item."
				  "\n;The other models then no longer come out of that recipe.");

		get_value(ini, g_values.logLevel, "Log", "Level",
				  ";trace, debug, info, warn, error, critical or off."
				  "\n;debug lists every variant of every group at startup.");

		if (status < 0) {
			logger::info("No settings file yet -- writing defaults to '{}'.", kPath);
		}
		ini.SaveFile(kPath);
	}

	const Values& Get()
	{
		return g_values;
	}

	spdlog::level::level_enum LogLevel()
	{
		const auto level = spdlog::level::from_str(g_values.logLevel);

		// from_str returns `off` for anything unrecognised, which would silently
		// empty the log. Only honour it when it was asked for.
		if (level == spdlog::level::off && g_values.logLevel != "off") {
			return spdlog::level::info;
		}
		return level;
	}
}
