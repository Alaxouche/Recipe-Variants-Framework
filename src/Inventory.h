// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#pragma once

#include <cstdint>
#include <vector>

namespace RVF::Inventory
{
	/// Part of one item's count that can be swapped as a unit.
	struct Stack
	{
		/// Null for plain items, which carry no extra data at all.
		RE::ExtraDataList* xList{ nullptr };
		/// Who the items belong to: set on stolen items, null otherwise.
		RE::TESForm*       owner{ nullptr };
		std::int32_t       count{ 0 };
	};

	/// How many of the item the reference holds.
	[[nodiscard]] std::int32_t Count(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_item);

	/// The stacks of the item that are safe to swap: plain items, and lists holding
	/// nothing but a count and an owner. Anything else (a quest alias, a favourite
	/// hotkey, a script's unique ID...) would be lost by a swap, so it is left alone.
	[[nodiscard]] std::vector<Stack> MovableStacks(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_item);

	/// Removes a_removeCount of a_from out of a_stack and adds a_addCount of a_to
	/// with the stack's owner, so stolen items stay stolen. Main thread only.
	void Replace(RE::TESObjectREFR* a_ref,
				 RE::TESBoundObject* a_from, const Stack& a_stack, std::int32_t a_removeCount,
				 RE::TESBoundObject* a_to, std::int32_t a_addCount);
}
