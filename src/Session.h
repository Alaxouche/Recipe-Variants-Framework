// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#pragma once

#include <cstdint>
#include <string_view>

namespace RVF::Session
{
	/// Swaps the player's variants for their base item, for every group that
	/// applies at this bench, and remembers what it swapped. A null bench only
	/// matches groups without workbench keywords. Main thread only.
	void Begin(const RE::TESFurniture* a_bench, std::string_view a_reason);

	/// Gives back, as the original variants, whatever crafting did not use.
	/// Does nothing when no session is open. Main thread only.
	void End(std::string_view a_reason);

	[[nodiscard]] bool IsActive();

	/// Identifies the session Begin last opened, so a delayed check can tell
	/// whether it is still the same one.
	[[nodiscard]] std::uint32_t CurrentId();

	/// Records that the crafting menu opened during the current session.
	void MarkMenuOpened();

	/// Ends session a_id if it is still open and no crafting menu opened during
	/// it: the activation that started it never reached a menu. Main thread only.
	void EndIfNoMenu(std::uint32_t a_id);

	/// Turns every stack of this variant into its base item for good, when its
	/// group has convert_on_pickup. Main thread only.
	void ConvertForGood(const RE::TESBoundObject* a_variant);

	/// Runs ConvertForGood over every convert_on_pickup group. Main thread only.
	void ConvertAllForGood();

	/// Co-save callbacks. A session is never open when the game saves in normal
	/// play (the crafting menu blocks saving), but if one is, the save keeps
	/// what it needs to give the variants back after the next load.
	void OnSave(SKSE::SerializationInterface* a_intfc);
	void OnLoad(SKSE::SerializationInterface* a_intfc);
	void OnRevert(SKSE::SerializationInterface* a_intfc);
}
