// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#include "Groups.h"

#include <charconv>
#include <unordered_map>
#include <unordered_set>

#include "Config.h"
#include "Recipes.h"
#include "Settings.h"

namespace RVF
{
	namespace
	{
		std::vector<Group>                          g_groups;
		std::unordered_map<RE::FormID, std::size_t> g_byVariant;
		bool                                        g_anyPickup{ false };

		/// Variant -> bench keyword of every recipe that names it (null: any bench).
		std::unordered_map<RE::FormID, std::vector<const RE::BGSKeyword*>> g_namedBy;

		[[nodiscard]] std::string_view Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t");
			return a_text.substr(first, last - first + 1);
		}

		[[nodiscard]] bool IsPluginName(std::string_view a_text)
		{
			if (a_text.size() < 5) {
				return false;
			}
			std::string ext(a_text.substr(a_text.size() - 4));
			std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return ext == ".esp" || ext == ".esm" || ext == ".esl";
		}

		[[nodiscard]] bool IsLoaded(std::string_view a_plugin)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh && (dh->LookupLoadedModByName(a_plugin) || dh->LookupLoadedLightModByName(a_plugin));
		}

		/// "Plugin.esp|0x123", "0x123~Plugin.esp" (SPID/KID/BOS style) or an editor ID.
		[[nodiscard]] RE::TESForm* Resolve(std::string_view a_text, std::string& a_why)
		{
			const auto sep = a_text.find_first_of("|~");
			if (sep == std::string_view::npos) {
				auto* form = RE::TESForm::LookupByEditorID(Trim(a_text));
				if (!form) {
					// Most item records drop their editor ID once loaded; only keywords
					// and a few other types keep it without help.
					a_why = "no form has this editor ID at runtime. Items only keep their editor "
							"ID with powerofthree's Tweaks installed; \"Plugin.esp|0xID\" always works";
				}
				return form;
			}

			const auto left = Trim(a_text.substr(0, sep));
			const auto right = Trim(a_text.substr(sep + 1));
			std::string_view plugin, id;
			if (IsPluginName(left)) {
				plugin = left;
				id = right;
			} else if (IsPluginName(right)) {
				plugin = right;
				id = left;
			} else {
				a_why = "neither side of the separator is a plugin file name";
				return nullptr;
			}

			if (id.starts_with("0x") || id.starts_with("0X")) {
				id.remove_prefix(2);
			}
			RE::FormID localID{};
			const auto [end, ec] = std::from_chars(id.data(), id.data() + id.size(), localID, 16);
			if (ec != std::errc{} || end != id.data() + id.size()) {
				a_why = std::format("'{}' is not a hexadecimal form ID", id);
				return nullptr;
			}
			// A load-order index here would silently resolve the wrong record.
			if (localID > 0xFFFFFF) {
				a_why = std::format("form ID 0x{:X} must be local to its plugin (at most 6 hex digits)", localID);
				return nullptr;
			}

			if (!IsLoaded(plugin)) {
				a_why = std::format("'{}' is not in the load order", plugin);
				return nullptr;
			}

			auto* form = RE::TESDataHandler::GetSingleton()->LookupForm(localID, plugin);
			if (!form) {
				a_why = std::format("'{}' has no record 0x{:06X}", plugin, localID);
			}
			return form;
		}

		[[nodiscard]] RE::TESBoundObject* ResolveItem(std::string_view a_text, std::string& a_why)
		{
			auto* form = Resolve(a_text, a_why);
			if (!form) {
				return nullptr;
			}
			auto* item = form->IsInventoryObject() ? form->As<RE::TESBoundObject>() : nullptr;
			if (!item) {
				a_why = std::format("it is a {} record, not an inventory item", RE::FormTypeToString(form->GetFormType()));
			}
			return item;
		}

		[[nodiscard]] std::string Describe(const RE::TESBoundObject* a_item)
		{
			const auto* name = a_item->GetName();
			return std::format("'{}' [0x{:08X}]", name && *name ? name : "?", a_item->GetFormID());
		}

		[[nodiscard]] std::optional<Group> BuildGroup(const GroupDef& a_def)
		{
			for (const auto& plugin : a_def.requiredPlugins) {
				if (!IsLoaded(plugin)) {
					logger::info("[{}] group '{}' skipped: requires '{}'.", a_def.source, a_def.name, plugin);
					return std::nullopt;
				}
			}

			Group group{};
			group.name = a_def.name;
			group.source = a_def.source;
			group.convertOnPickup = a_def.convertOnPickup;

			std::string why;
			group.base = ResolveItem(a_def.base, why);
			if (!group.base) {
				logger::error("[{}] group '{}' skipped: base '{}': {}.", a_def.source, a_def.name, a_def.base, why);
				return std::nullopt;
			}

			for (const auto& def : a_def.variants) {
				auto* form = ResolveItem(def.form, why);
				if (!form) {
					// Usually an optional plugin that is not installed: drop the one variant.
					logger::warn("[{}] group '{}': variant '{}' ignored: {}.", a_def.source, a_def.name, def.form, why);
					continue;
				}
				if (form == group.base) {
					logger::warn("[{}] group '{}': variant '{}' is the base itself, ignored.", a_def.source, a_def.name, def.form);
					continue;
				}
				if (std::ranges::any_of(group.variants, [&](const Variant& v) { return v.form == form; })) {
					logger::warn("[{}] group '{}': variant '{}' is listed twice, ignored.", a_def.source, a_def.name, def.form);
					continue;
				}
				group.variants.push_back({ form, def.perBase });
			}

			for (const auto& editorID : a_def.workbenchKeywords) {
				if (auto* keyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(editorID)) {
					group.workbenchKeywords.push_back(keyword);
				} else {
					logger::warn("[{}] group '{}': keyword '{}' not found, ignored.", a_def.source, a_def.name, editorID);
				}
			}
			// Asked for specific benches but none exist: applying everywhere instead
			// would be the opposite of what the author wrote.
			if (!a_def.workbenchKeywords.empty() && group.workbenchKeywords.empty()) {
				logger::error("[{}] group '{}' skipped: none of its workbench keywords exist.", a_def.source, a_def.name);
				return std::nullopt;
			}

			return group;
		}

		/// Indexes every recipe that names a variant instead of its base. Those
		/// recipes keep working because their variant is never swapped at their
		/// bench; the log lists them so a mod author can point them at the base.
		void IndexNamedVariants()
		{
			g_namedBy.clear();
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) {
				return;
			}

			std::unordered_map<RE::FormID, std::size_t> recipes;
			const auto* hidden = Recipes::HiddenKeyword();
			for (const auto* cobj : dh->GetFormArray<RE::BGSConstructibleObject>()) {
				// A merged copy sits at no bench: it protects nothing.
				if (!cobj || (hidden && cobj->benchKeyword == hidden)) {
					continue;
				}
				cobj->requiredItems.ForEachContainerObject([&](RE::ContainerObject& a_entry) {
					if (a_entry.obj && g_byVariant.contains(a_entry.obj->GetFormID())) {
						const auto id = a_entry.obj->GetFormID();
						g_namedBy[id].push_back(cobj->benchKeyword);
						++recipes[id];
						logger::debug("    recipe [0x{:08X}] making '{}' asks for variant '{}' by name.",
									  cobj->GetFormID(),
									  cobj->createdItem ? cobj->createdItem->GetName() : "?",
									  a_entry.obj->GetName());
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}

			for (const auto& [id, count] : recipes) {
				const auto* form = RE::TESForm::LookupByID<RE::TESBoundObject>(id);
				logger::info("Variant {} is named by {} recipe(s): it is not swapped at their benches. "
							 "Point those recipes at the base item to let every variant count there.",
							 form ? Describe(form) : std::format("[0x{:08X}]", id), count);
			}
		}
	}

	bool Group::AppliesTo(const RE::TESFurniture* a_bench) const
	{
		if (workbenchKeywords.empty()) {
			return true;
		}
		return a_bench && std::ranges::any_of(workbenchKeywords, [&](const RE::BGSKeyword* a_keyword) {
			return a_bench->HasKeyword(a_keyword);
		});
	}

	void Groups::Build()
	{
		g_groups.clear();
		g_byVariant.clear();

		for (const auto& def : Config::Load()) {
			if (auto group = BuildGroup(def)) {
				g_groups.push_back(std::move(*group));
			}
		}

		// A form is either what recipes ask for or a stand-in for it, never both:
		// a chain (A stands in for B, B for C) would make the swap order matter.
		std::unordered_set<RE::FormID> bases;
		for (const auto& group : g_groups) {
			bases.insert(group.base->GetFormID());
		}

		for (std::size_t i = 0; i < g_groups.size(); ++i) {
			auto& group = g_groups[i];
			std::erase_if(group.variants, [&](const Variant& a_variant) {
				const auto id = a_variant.form->GetFormID();
				if (bases.contains(id)) {
					logger::error("[{}] group '{}': {} is the base of another group, so it cannot "
								  "also be a variant. Ignored.",
								  group.source, group.name, Describe(a_variant.form));
					return true;
				}
				if (const auto [it, inserted] = g_byVariant.try_emplace(id, i); !inserted) {
					logger::error("[{}] group '{}': {} already belongs to group '{}'. Ignored.",
								  group.source, group.name, Describe(a_variant.form), g_groups[it->second].name);
					return true;
				}
				return false;
			});
		}

		// Erasing would shift the indices g_byVariant holds, so only report.
		std::size_t usable = 0;
		for (const auto& group : g_groups) {
			if (group.variants.empty()) {
				logger::warn("[{}] group '{}' has no usable variant left and does nothing.", group.source, group.name);
				continue;
			}
			++usable;
			g_anyPickup = g_anyPickup || group.convertOnPickup;

			logger::info("Group '{}': base {}, {} variant(s){}{}.",
						 group.name, Describe(group.base), group.variants.size(),
						 group.workbenchKeywords.empty() ? "" : std::format(", {} workbench keyword(s)", group.workbenchKeywords.size()),
						 group.convertOnPickup ? ", converts on pickup" : "");
			for (const auto& variant : group.variants) {
				logger::debug("    variant {}{}", Describe(variant.form),
							  variant.perBase > 1 ? std::format(" ({} make one base)", variant.perBase) : "");
			}
		}
		logger::info("{} usable group(s), {} variant form(s).", usable, g_byVariant.size());

		// Before the index: a merged copy sits at no bench any more, so its
		// variant no longer needs protecting.
		if (Settings::Get().mergeDuplicates) {
			Recipes::MergeDuplicates();
		}
		IndexNamedVariants();
	}

	const std::vector<Group>& Groups::All()
	{
		return g_groups;
	}

	const Group* Groups::FindByVariant(RE::FormID a_variant)
	{
		const auto it = g_byVariant.find(a_variant);
		return it != g_byVariant.end() ? std::addressof(g_groups[it->second]) : nullptr;
	}

	bool Groups::AnyConvertOnPickup()
	{
		return g_anyPickup;
	}

	bool Groups::IsNamedByRecipe(const RE::TESBoundObject* a_variant, const RE::TESFurniture* a_bench)
	{
		const auto it = a_variant ? g_namedBy.find(a_variant->GetFormID()) : g_namedBy.end();
		if (it == g_namedBy.end()) {
			return false;
		}
		return std::ranges::any_of(it->second, [&](const RE::BGSKeyword* a_keyword) {
			// A recipe without a bench keyword, or a bench we cannot see, could be
			// the one in use: assume it is.
			return !a_keyword || !a_bench || a_bench->HasKeyword(a_keyword);
		});
	}

	bool Groups::IsCraftingBench(const RE::TESFurniture* a_furniture)
	{
		if (!a_furniture) {
			return false;
		}
		// The three bench types whose menu lists constructible objects. Alchemy and
		// enchanting build their lists from ingredients and enchantments instead.
		using BenchType = RE::TESFurniture::WorkBenchData::BenchType;
		switch (a_furniture->workBenchData.benchType.get()) {
		case BenchType::kCreateObject:
		case BenchType::kSmithingWeapon:
		case BenchType::kSmithingArmor:
			return true;
		default:
			return false;
		}
	}
}
