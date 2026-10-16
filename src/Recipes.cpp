// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#include "Recipes.h"

#include <map>

#include "Groups.h"
#include "Settings.h"

namespace RVF::Recipes
{
	namespace
	{
		RE::BGSKeyword* g_hidden{ nullptr };

		/// What a required item counts as once variants are read as their base.
		struct Resolved
		{
			RE::TESBoundObject* item{ nullptr };
			std::int32_t        count{ 0 };
		};

		/// The variant entry, or null when the item is not a variant.
		[[nodiscard]] const Variant* AsVariant(const RE::TESBoundObject* a_item, const Group*& a_group)
		{
			a_group = a_item ? Groups::FindByVariant(a_item->GetFormID()) : nullptr;
			if (!a_group) {
				return nullptr;
			}
			const auto it = std::ranges::find_if(a_group->variants, [&](const Variant& a_v) { return a_v.form == a_item; });
			return it != a_group->variants.end() ? std::addressof(*it) : nullptr;
		}

		/// The recipe's requirements with every variant read as its base, or
		/// nullopt when a count does not make whole base items. Sorted, merged.
		[[nodiscard]] std::optional<std::vector<std::pair<RE::FormID, std::int32_t>>> Canonical(
			const RE::BGSConstructibleObject* a_cobj, bool& a_namesVariant, bool& a_inGroup)
		{
			std::map<RE::FormID, std::int32_t> sums;
			bool                               ok = true;
			a_namesVariant = false;
			a_inGroup = false;

			a_cobj->requiredItems.ForEachContainerObject([&](RE::ContainerObject& a_entry) {
				if (!a_entry.obj) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const Group* group = nullptr;
				if (const auto* variant = AsVariant(a_entry.obj, group)) {
					a_namesVariant = true;
					a_inGroup = true;
					if (a_entry.count % static_cast<std::int32_t>(variant->perBase) != 0) {
						ok = false;
						return RE::BSContainer::ForEachResult::kStop;
					}
					sums[group->base->GetFormID()] += a_entry.count / static_cast<std::int32_t>(variant->perBase);
				} else {
					sums[a_entry.obj->GetFormID()] += a_entry.count;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});

			if (!ok) {
				return std::nullopt;
			}
			// A base item alone also makes the recipe part of a group's set.
			for (const auto& [id, count] : sums) {
				for (const auto& group : Groups::All()) {
					a_inGroup = a_inGroup || group.base->GetFormID() == id;
				}
			}
			return std::vector<std::pair<RE::FormID, std::int32_t>>(sums.begin(), sums.end());
		}

		/// Points every variant the recipe names at its base. Only done when each
		/// variant has its own entry and no base is named twice afterwards, so the
		/// entry count and layout stay exactly as the engine built them.
		[[nodiscard]] bool Retarget(RE::BGSConstructibleObject* a_cobj)
		{
			std::vector<std::pair<RE::ContainerObject*, Resolved>> changes;
			std::vector<RE::FormID>                                 seen;
			bool                                                    ok = true;

			a_cobj->requiredItems.ForEachContainerObject([&](RE::ContainerObject& a_entry) {
				if (!a_entry.obj) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const Group* group = nullptr;
				Resolved     target{ a_entry.obj, a_entry.count };
				if (const auto* variant = AsVariant(a_entry.obj, group)) {
					target = { group->base, a_entry.count / static_cast<std::int32_t>(variant->perBase) };
					changes.emplace_back(std::addressof(a_entry), target);
				}
				if (std::ranges::find(seen, target.item->GetFormID()) != seen.end()) {
					ok = false;
					return RE::BSContainer::ForEachResult::kStop;
				}
				seen.push_back(target.item->GetFormID());
				return RE::BSContainer::ForEachResult::kContinue;
			});

			if (!ok) {
				return false;
			}
			for (auto& [entry, target] : changes) {
				entry->obj = target.item;
				entry->count = target.count;
			}
			return true;
		}

		[[nodiscard]] std::string Describe(const RE::BGSConstructibleObject* a_cobj)
		{
			const auto* made = a_cobj->createdItem;
			const auto* name = made ? made->GetName() : nullptr;
			return std::format("[0x{:08X}] '{}' x{}", a_cobj->GetFormID(), name && *name ? name : "?",
							   a_cobj->data.numConstructed);
		}
	}

	void MergeDuplicates()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh || Groups::All().empty()) {
			return;
		}

		// Recipes with conditions (a perk, a quest stage) are left out: two of them
		// can only be the same recipe if their conditions match too, and comparing
		// condition chains is not worth the risk of hiding the wrong one.
		struct Candidate
		{
			RE::BGSConstructibleObject* cobj{ nullptr };
			bool                        namesVariant{ false };
			bool                        makesVariant{ false };
		};
		std::map<std::string, std::vector<Candidate>> sets;

		// Cutting recipes make one half model per copy (Gourd04 into two Gourd02,
		// or into two Gourd03): with this on, a result that is a variant counts as
		// its base too, so those copies fold into one that makes the base.
		const bool mergeResults = Settings::Get().mergeVariantResults;

		for (auto* cobj : dh->GetFormArray<RE::BGSConstructibleObject>()) {
			if (!cobj || !cobj->createdItem || !cobj->benchKeyword || cobj->conditions.head) {
				continue;
			}
			bool namesVariant = false, inGroup = false;
			const auto items = Canonical(cobj, namesVariant, inGroup);
			if (!items) {
				continue;
			}

			auto  result = cobj->createdItem->GetFormID();
			bool  makesVariant = false;
			if (mergeResults) {
				const Group* group = nullptr;
				if (AsVariant(cobj->createdItem->As<RE::TESBoundObject>(), group)) {
					result = group->base->GetFormID();
					makesVariant = true;
					inGroup = true;
				} else {
					inGroup = inGroup || std::ranges::any_of(Groups::All(), [&](const Group& a_group) {
						return a_group.base->GetFormID() == result;
					});
				}
			}
			if (!inGroup) {
				continue;
			}

			auto key = std::format("{:08X}|{}|{:08X}", result, cobj->data.numConstructed,
								   cobj->benchKeyword->GetFormID());
			for (const auto& [id, count] : *items) {
				key += std::format("|{:08X}x{}", id, count);
			}
			sets[key].push_back({ cobj, namesVariant, makesVariant });
		}

		auto&       hidden = g_hidden;
		std::size_t kept = 0, folded = 0, retargeted = 0;

		for (auto& [key, recipes] : sets) {
			if (recipes.size() < 2) {
				continue;
			}

			// Keep one that already names only base items (usually the vanilla
			// recipe), making the base item when one does; failing that, point one
			// at the bases.
			auto keeper = std::ranges::find_if(recipes, [](const Candidate& a_c) { return !a_c.namesVariant && !a_c.makesVariant; });
			if (keeper == recipes.end()) {
				keeper = std::ranges::find_if(recipes, [](const Candidate& a_c) { return !a_c.namesVariant; });
			}
			if (keeper == recipes.end()) {
				keeper = std::ranges::find_if(recipes, [](const Candidate& a_c) { return !a_c.makesVariant; });
				if (keeper == recipes.end()) {
					keeper = recipes.begin();
				}
				if (!Retarget(keeper->cobj)) {
					logger::debug("Recipes for {} left as they are: they cannot be pointed at the base items.",
								  Describe(keeper->cobj));
					continue;
				}
				++retargeted;
			}

			if (!hidden) {
				// Its own keyword, with an editor ID: other plugins read keyword
				// editor IDs, and an empty one is known to crash some of them.
				hidden = RE::BGSKeyword::CreateKeyword("RVF_MergedRecipe");
				if (!hidden) {
					logger::error("Could not create the keyword that hides merged recipes; nothing merged.");
					return;
				}
			}

			++kept;
			logger::debug("Kept {}{}; hiding {} copy(ies):", Describe(keeper->cobj),
						  keeper->namesVariant ? " (now asks for the base items)" : "", recipes.size() - 1);
			for (auto& candidate : recipes) {
				if (candidate.cobj == keeper->cobj) {
					continue;
				}
				candidate.cobj->benchKeyword = hidden;
				++folded;
				logger::debug("    {}", Describe(candidate.cobj));
			}
		}

		if (folded > 0) {
			logger::info("Merged {} duplicate recipe(s) into {} ({} pointed at their base items).",
						 folded, kept, retargeted);
		} else {
			logger::info("No duplicate recipes to merge.");
		}
	}

	const RE::BGSKeyword* HiddenKeyword()
	{
		return g_hidden;
	}
}
