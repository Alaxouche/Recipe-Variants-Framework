// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace RVF
{
	struct Variant
	{
		RE::TESBoundObject* form{ nullptr };
		/// How many of this variant make one base item.
		std::uint32_t perBase{ 1 };
	};

	/// A config group with every form resolved against the live load order.
	struct Group
	{
		std::string             name;
		RE::TESBoundObject*     base{ nullptr };
		std::vector<Variant>    variants;
		std::vector<RE::BGSKeyword*> workbenchKeywords;
		bool                    convertOnPickup{ false };
		std::string             source;

		/// Whether this group takes part in a session at this workbench.
		[[nodiscard]] bool AppliesTo(const RE::TESFurniture* a_bench) const;
	};

	namespace Groups
	{
		/// Resolves every config group. Call once, at kDataLoaded.
		void Build();

		/// Every resolved group, in config order. Never changes after Build().
		[[nodiscard]] const std::vector<Group>& All();

		/// The group a variant belongs to, or nullptr.
		[[nodiscard]] const Group* FindByVariant(RE::FormID a_variant);

		/// True when at least one group converts on pickup.
		[[nodiscard]] bool AnyConvertOnPickup();

		/// Whether a recipe usable at this bench asks for the variant by name.
		/// Swapping that variant away would hide the recipe, so it is left alone
		/// there. A null bench means "anywhere" and matches every such recipe.
		[[nodiscard]] bool IsNamedByRecipe(const RE::TESBoundObject* a_variant, const RE::TESFurniture* a_bench);

		/// Whether the furniture is a bench that crafts from constructible
		/// objects (cooking pot, oven, forge, tanning rack...).
		[[nodiscard]] bool IsCraftingBench(const RE::TESFurniture* a_furniture);
	}
}
