// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace RVF
{
	/// One alternative to a group's base item, as written in a config.
	struct VariantDef
	{
		/// Editor ID, "Plugin.esp|0x123" or "0x123~Plugin.esp".
		std::string   form;
		/// How many of this variant make one base item: 1 for a whole variant,
		/// 2 for a half, 5 for a garlic clove.
		std::uint32_t perBase{ 1 };
	};

	/// One config entry, validated when the folder is read. Forms stay text here:
	/// nothing can be looked up before kDataLoaded.
	struct GroupDef
	{
		/// Names the group in the log and de-duplicates across files.
		std::string             name;
		/// The item the recipes ask for.
		std::string             base;
		std::vector<VariantDef> variants;

		/// Keyword editor IDs. The group only applies at a workbench carrying one of
		/// them (CraftingCookpot, BYOHCraftingOven...). Empty means any crafting bench.
		std::vector<std::string> workbenchKeywords;

		/// Turn variants into the base item for good as soon as the player picks them
		/// up, instead of only for the time of a crafting session.
		bool convertOnPickup{ false };

		/// All must be in the load order for the group to apply. Empty means always.
		/// Not named `requires`: that is a keyword.
		std::vector<std::string> requiredPlugins;

		/// Source file, for log messages.
		std::string source;
	};

	namespace Config
	{
		/// Reads every *.json in Data/SKSE/Plugins/RecipeVariantsFramework, in filename
		/// order. Malformed files are logged and skipped; a malformed group inside an
		/// otherwise good file is skipped on its own.
		[[nodiscard]] std::vector<GroupDef> Load();
	}
}
