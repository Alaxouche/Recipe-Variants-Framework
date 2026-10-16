// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#include "Session.h"

#include <mutex>

#include "Groups.h"
#include "Inventory.h"

namespace RVF::Session
{
	namespace
	{
		/// One stack moved from a variant into the base item.
		struct Swapped
		{
			RE::TESBoundObject* variant{ nullptr };
			/// Owner of the stack, so the base items it became stay stolen.
			RE::TESForm*        owner{ nullptr };
			/// Base items it became.
			std::int32_t        units{ 0 };
			std::uint32_t       perBase{ 1 };
		};

		struct GroupState
		{
			RE::TESBoundObject*  base{ nullptr };
			/// Base items the player held before the swap: crafting uses these first.
			std::int32_t         baseBefore{ 0 };
			std::vector<Swapped> swapped;
		};

		/// Every touch of this state happens on the main thread (tasks and co-save
		/// callbacks); the lock only guards against a future caller forgetting that.
		std::mutex              g_lock;
		std::vector<GroupState> g_open;
		/// From Begin to End, even when nothing was swapped: the late start on the
		/// menu opening must not run a second time for the same sitting.
		bool                    g_inSession{ false };
		std::uint32_t           g_sessionId{ 0 };
		bool                    g_menuSeen{ false };

		/// Bounds every stack loop: each pass removes a whole stack, so a pass that
		/// does not shrink the count means the engine refused the change.
		constexpr int kMaxPasses = 256;

		[[nodiscard]] std::string Describe(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return "<none>";
			}
			const auto* name = a_form->GetName();
			return std::format("'{}' [0x{:08X}]", name && *name ? name : "?", a_form->GetFormID());
		}

		/// Moves every whole multiple of perBase of a variant into its base item.
		/// a_out, when given, records what moved so it can be given back.
		std::int32_t SwapIn(RE::PlayerCharacter* a_player, const Group& a_group, const Variant& a_variant,
							std::vector<Swapped>* a_out)
		{
			std::int32_t total = 0;
			for (int pass = 0; pass < kMaxPasses; ++pass) {
				const auto stacks = Inventory::MovableStacks(a_player, a_variant.form);
				const auto it = std::ranges::find_if(stacks, [&](const Inventory::Stack& a_stack) {
					return a_stack.count >= static_cast<std::int32_t>(a_variant.perBase);
				});
				if (it == stacks.end()) {
					break;
				}

				const auto units = it->count / static_cast<std::int32_t>(a_variant.perBase);
				const auto before = Inventory::Count(a_player, a_variant.form);
				Inventory::Replace(a_player, a_variant.form, *it, units * static_cast<std::int32_t>(a_variant.perBase),
								   a_group.base, units);
				if (Inventory::Count(a_player, a_variant.form) >= before) {
					logger::error("Could not remove {} from the player's inventory; leaving it alone.",
								  Describe(a_variant.form));
					break;
				}

				total += units;
				if (a_out) {
					a_out->push_back({ a_variant.form, it->owner, units, a_variant.perBase });
				}
			}
			return total;
		}

		/// Turns a_units of the base back into a variant, taking the base items from
		/// stacks with the variant's former owner first. The variant always gets the
		/// owner of the base items it replaces, so the number of stolen items holds.
		std::int32_t GiveBack(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_base, const Swapped& a_entry,
							  std::int32_t a_units)
		{
			std::int32_t done = 0;
			for (int pass = 0; pass < kMaxPasses && done < a_units; ++pass) {
				const auto stacks = Inventory::MovableStacks(a_player, a_base);
				const auto sameOwner = [&](const Inventory::Stack& a_stack) { return a_stack.owner == a_entry.owner; };
				auto       it = std::ranges::find_if(stacks, sameOwner);
				if (it == stacks.end()) {
					it = stacks.begin();
				}
				if (it == stacks.end()) {
					break;
				}

				const auto units = (std::min)(a_units - done, it->count);
				const auto before = Inventory::Count(a_player, a_base);
				Inventory::Replace(a_player, a_base, *it, units,
								   a_entry.variant, units * static_cast<std::int32_t>(a_entry.perBase));
				if (Inventory::Count(a_player, a_base) >= before) {
					logger::error("Could not remove {} from the player's inventory; stopping.", Describe(a_base));
					break;
				}
				done += units;
			}
			return done;
		}

		void EndLocked(std::string_view a_reason)
		{
			g_inSession = false;
			if (g_open.empty()) {
				return;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				logger::error("Session end ({}): no player, nothing given back.", a_reason);
				g_open.clear();
				return;
			}

			for (auto& state : g_open) {
				std::int32_t swapped = 0;
				for (const auto& entry : state.swapped) {
					swapped += entry.units;
				}

				// Crafting may have used base items and made some (an oven bakes the
				// bread a group swaps). Only the net change is visible, and the items
				// the player owned as the base itself count as used first. Whatever
				// the split, the total handed back never exceeds what is there, so
				// the item count is always right; at worst a variant comes back as
				// a different variant of the same group.
				const auto baseAfter = Inventory::Count(player, state.base);
				const auto used = state.baseBefore + swapped - baseAfter;
				const auto usedFromSwapped = std::clamp(used - state.baseBefore, 0, swapped);
				const auto toReturn = (std::min)(swapped - usedFromSwapped, baseAfter);

				// The last stacks swapped in are the first counted as used.
				std::vector<std::int32_t> back;
				back.reserve(state.swapped.size());
				for (const auto& entry : state.swapped) {
					back.push_back(entry.units);
				}
				auto consume = swapped - toReturn;
				for (auto i = back.size(); i-- > 0 && consume > 0;) {
					const auto take = (std::min)(consume, back[i]);
					back[i] -= take;
					consume -= take;
				}

				std::int32_t returned = 0;
				for (std::size_t i = 0; i < state.swapped.size(); ++i) {
					if (back[i] > 0) {
						returned += GiveBack(player, state.base, state.swapped[i], back[i]);
					}
				}

				logger::info("Session end ({}): {} -- {} swapped in, {} used by crafting, {} given back.",
							 a_reason, Describe(state.base), swapped, swapped - toReturn, returned);
				if (returned < toReturn) {
					logger::warn("    {} could not be given back: the base items left are locked "
								 "(quest item, favourite...).",
								 toReturn - returned);
				}
			}

			g_open.clear();
		}
	}

	void Begin(const RE::TESFurniture* a_bench, std::string_view a_reason)
	{
		std::scoped_lock lock(g_lock);

		// Only reachable if a session's end was missed; settle it before a new one.
		if (!g_open.empty()) {
			logger::warn("A session was still open when a new one started; closing it first.");
			EndLocked("superseded");
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		std::int32_t total = 0;
		for (const auto& group : Groups::All()) {
			if (group.variants.empty() || !group.AppliesTo(a_bench)) {
				continue;
			}

			// These never come back, so they need no record. Anything still around
			// predates the group (or the plugin) and is converted now.
			if (group.convertOnPickup) {
				for (const auto& variant : group.variants) {
					if (!Groups::IsNamedByRecipe(variant.form, nullptr)) {
						SwapIn(player, group, variant, nullptr);
					}
				}
				continue;
			}

			GroupState state{ group.base, Inventory::Count(player, group.base), {} };
			for (const auto& variant : group.variants) {
				// Never take away a recipe: one here that asks for this very variant
				// would find none left.
				if (Groups::IsNamedByRecipe(variant.form, a_bench)) {
					logger::debug("    {} kept: a recipe at this bench asks for it by name.", Describe(variant.form));
					continue;
				}
				total += SwapIn(player, group, variant, std::addressof(state.swapped));
			}
			if (!state.swapped.empty()) {
				logger::debug("    {}: {} stack(s) swapped in.", Describe(group.base), state.swapped.size());
				g_open.push_back(std::move(state));
			}
		}

		g_inSession = true;
		++g_sessionId;
		// A session started while the menu is already up came too late for the
		// recipe list, which the menu builds when it opens.
		auto* ui = RE::UI::GetSingleton();
		g_menuSeen = ui && ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME);
		if (g_menuSeen && total > 0) {
			logger::warn("Session started after the crafting menu opened ({}): its recipe list was built "
						 "before the swap and may not show the swapped items until it is reopened.",
						 a_reason);
		}
		if (total > 0) {
			logger::info("Session start ({}) at {}: {} base item(s) from {} group(s).",
						 a_reason, Describe(a_bench), total, g_open.size());
		} else {
			logger::debug("Session start ({}) at {}: nothing to swap.", a_reason, Describe(a_bench));
		}
	}

	void End(std::string_view a_reason)
	{
		std::scoped_lock lock(g_lock);
		EndLocked(a_reason);
	}

	bool IsActive()
	{
		std::scoped_lock lock(g_lock);
		return g_inSession;
	}

	std::uint32_t CurrentId()
	{
		std::scoped_lock lock(g_lock);
		return g_sessionId;
	}

	void MarkMenuOpened()
	{
		std::scoped_lock lock(g_lock);
		g_menuSeen = true;
	}

	void EndIfNoMenu(std::uint32_t a_id)
	{
		std::scoped_lock lock(g_lock);
		if (g_inSession && g_sessionId == a_id && !g_menuSeen) {
			EndLocked("no crafting menu opened");
		}
	}

	void ConvertForGood(const RE::TESBoundObject* a_variant)
	{
		if (!a_variant) {
			return;
		}
		const auto* group = Groups::FindByVariant(a_variant->GetFormID());
		// A conversion for good would make a recipe naming this variant impossible.
		if (!group || !group->convertOnPickup || Groups::IsNamedByRecipe(a_variant, nullptr)) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		std::scoped_lock lock(g_lock);
		for (const auto& variant : group->variants) {
			if (variant.form == a_variant) {
				if (const auto units = SwapIn(player, *group, variant, nullptr); units > 0) {
					logger::debug("Converted {} into {} x{}.", Describe(variant.form), Describe(group->base), units);
				}
			}
		}
	}

	void ConvertAllForGood()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !Groups::AnyConvertOnPickup()) {
			return;
		}

		std::scoped_lock lock(g_lock);
		std::int32_t     total = 0;
		for (const auto& group : Groups::All()) {
			if (group.convertOnPickup) {
				for (const auto& variant : group.variants) {
					if (!Groups::IsNamedByRecipe(variant.form, nullptr)) {
						total += SwapIn(player, group, variant, nullptr);
					}
				}
			}
		}
		if (total > 0) {
			logger::info("Converted {} carried variant item(s) into their base item.", total);
		}
	}

	// ---------------------------------------------------------------- co-save

	namespace
	{
		constexpr std::uint32_t kRecordSession = 'SESN';
		constexpr std::uint32_t kVersion = 1;

		// A corrupt save hands back garbage counts; refuse them before allocating.
		constexpr std::uint32_t kMaxGroups = 4096;
		constexpr std::uint32_t kMaxEntries = 65536;

		template <class T>
		[[nodiscard]] bool Read(SKSE::SerializationInterface* a_intfc, T& a_value)
		{
			return a_intfc->ReadRecordData(a_value) == sizeof(T);
		}

		/// Reads a FormID and maps its load-order index onto the current order. The
		/// form is null for 0 (no owner) and for a plugin that left the load order;
		/// false means the record itself ran short.
		[[nodiscard]] bool ReadForm(SKSE::SerializationInterface* a_intfc, RE::TESForm*& a_form)
		{
			RE::FormID id{};
			if (!Read(a_intfc, id)) {
				return false;
			}
			a_form = nullptr;
			if (RE::FormID current{}; id != 0 && a_intfc->ResolveFormID(id, current)) {
				a_form = RE::TESForm::LookupByID(current);
			}
			return true;
		}
	}

	void OnSave(SKSE::SerializationInterface* a_intfc)
	{
		std::scoped_lock lock(g_lock);
		if (g_open.empty()) {
			return;
		}

		logger::warn("Saving while a crafting session is open; it will be settled after the next load.");
		if (!a_intfc->OpenRecord(kRecordSession, kVersion)) {
			logger::error("Could not open the co-save record; the open session is not saved.");
			return;
		}

		const auto groups = static_cast<std::uint32_t>(g_open.size());
		a_intfc->WriteRecordData(groups);
		for (const auto& state : g_open) {
			a_intfc->WriteRecordData(state.base->GetFormID());
			a_intfc->WriteRecordData(state.baseBefore);
			a_intfc->WriteRecordData(static_cast<std::uint32_t>(state.swapped.size()));
			for (const auto& entry : state.swapped) {
				a_intfc->WriteRecordData(entry.variant->GetFormID());
				a_intfc->WriteRecordData(entry.owner ? entry.owner->GetFormID() : RE::FormID{ 0 });
				a_intfc->WriteRecordData(entry.units);
				a_intfc->WriteRecordData(entry.perBase);
			}
		}
	}

	void OnLoad(SKSE::SerializationInterface* a_intfc)
	{
		std::scoped_lock lock(g_lock);
		g_open.clear();

		std::uint32_t type{}, version{}, length{};
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type != kRecordSession) {
				continue;
			}
			if (version != kVersion) {
				logger::error("Co-save session record has version {}, expected {}; ignored.", version, kVersion);
				continue;
			}

			std::uint32_t groups{};
			if (!Read(a_intfc, groups) || groups > kMaxGroups) {
				logger::error("Co-save session record is damaged; ignored.");
				continue;
			}

			bool damaged = false;
			for (std::uint32_t g = 0; g < groups && !damaged; ++g) {
				GroupState    state{};
				RE::TESForm*  base{};
				std::uint32_t count{};
				if (!ReadForm(a_intfc, base) || !Read(a_intfc, state.baseBefore) ||
					!Read(a_intfc, count) || count > kMaxEntries) {
					damaged = true;
					break;
				}
				state.base = base ? base->As<RE::TESBoundObject>() : nullptr;

				for (std::uint32_t e = 0; e < count; ++e) {
					Swapped      entry{};
					RE::TESForm* variant{};
					if (!ReadForm(a_intfc, variant) || !ReadForm(a_intfc, entry.owner) ||
						!Read(a_intfc, entry.units) || !Read(a_intfc, entry.perBase)) {
						damaged = true;
						break;
					}
					entry.variant = variant ? variant->As<RE::TESBoundObject>() : nullptr;
					if (entry.variant && entry.units > 0 && entry.perBase > 0) {
						state.swapped.push_back(entry);
					}
				}

				// A plugin that left the load order took its items with it.
				if (!damaged && state.base && !state.swapped.empty()) {
					g_open.push_back(std::move(state));
				}
			}

			if (damaged) {
				logger::error("Co-save session record is damaged; the open session is dropped.");
				g_open.clear();
			}
		}

		g_inSession = !g_open.empty();
		if (g_inSession) {
			logger::info("This save holds an open crafting session ({} group(s)); settling it once the game is loaded.",
						 g_open.size());
		}
	}

	void OnRevert(SKSE::SerializationInterface*)
	{
		// Runs before every load and on a new game: the previous game's swaps mean
		// nothing in the next one.
		std::scoped_lock lock(g_lock);
		g_open.clear();
		g_inSession = false;
	}
}
