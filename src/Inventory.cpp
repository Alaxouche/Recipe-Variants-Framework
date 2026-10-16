// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#include "Inventory.h"

namespace RVF::Inventory
{
	namespace
	{
		[[nodiscard]] bool IsMovable(RE::ExtraDataList& a_xList)
		{
			for (auto& data : a_xList) {
				switch (data.GetType()) {
				case RE::ExtraDataType::kCount:
				case RE::ExtraDataType::kOwnership:
					break;
				default:
					return false;
				}
			}
			// SetOwner refuses dynamic forms, so such a stack could not keep its owner.
			const auto* owner = a_xList.GetOwner();
			return !owner || !owner->IsDynamicForm();
		}

		/// The item's total count and its entry, which lists the extra-data stacks.
		struct Entry
		{
			std::int32_t                             total{ 0 };
			std::unique_ptr<RE::InventoryEntryData> data;
		};

		[[nodiscard]] Entry Find(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_item)
		{
			auto inventory = a_ref->GetInventory([&](RE::TESBoundObject& a_object) { return &a_object == a_item; });
			const auto it = inventory.find(a_item);
			if (it == inventory.end()) {
				return {};
			}
			return { it->second.first, std::move(it->second.second) };
		}
	}

	std::int32_t Count(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_item)
	{
		return a_ref && a_item ? (std::max)(Find(a_ref, a_item).total, 0) : 0;
	}

	std::vector<Stack> MovableStacks(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_item)
	{
		std::vector<Stack> stacks;
		if (!a_ref || !a_item) {
			return stacks;
		}

		const auto entry = Find(a_ref, a_item);
		if (entry.total <= 0) {
			return stacks;
		}

		// Items not covered by any extra-data list are plain.
		std::int32_t listed = 0;
		if (entry.data && entry.data->extraLists) {
			for (auto* xList : *entry.data->extraLists) {
				if (!xList) {
					continue;
				}
				const auto count = xList->GetCount();
				if (count <= 0) {
					continue;
				}
				listed += count;
				if (IsMovable(*xList)) {
					stacks.push_back({ xList, xList->GetOwner(), count });
				}
			}
		}

		if (const auto plain = entry.total - listed; plain > 0) {
			stacks.push_back({ nullptr, nullptr, plain });
		}
		return stacks;
	}

	void Replace(RE::TESObjectREFR* a_ref,
				 RE::TESBoundObject* a_from, const Stack& a_stack, std::int32_t a_removeCount,
				 RE::TESBoundObject* a_to, std::int32_t a_addCount)
	{
		if (!a_ref || !a_from || !a_to || a_removeCount <= 0 || a_addCount <= 0) {
			return;
		}

		// Add before removing, as YASTM does when it swaps soul gems: RemoveItem can
		// free the source list, and the owner is read from it.
		if (a_stack.owner) {
			// One list per item, the shape YASTM ships; the engine stacks lists that
			// share an owner, as it does when items are stolen one at a time.
			for (std::int32_t i = 0; i < a_addCount; ++i) {
				auto* xList = new RE::ExtraDataList();
				xList->SetOwner(a_stack.owner);
				a_ref->AddObjectToContainer(a_to, xList, 1, nullptr);
			}
		} else {
			a_ref->AddObjectToContainer(a_to, nullptr, a_addCount, nullptr);
		}

		a_ref->RemoveItem(a_from, a_removeCount, RE::ITEM_REMOVE_REASON::kRemove, a_stack.xList, nullptr);
	}
}
