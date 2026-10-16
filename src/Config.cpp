// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#include "Config.h"

#include <filesystem>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace RVF
{
	namespace
	{
		using json = nlohmann::json;

		constexpr auto kFolder = "Data/SKSE/Plugins/RecipeVariantsFramework";
		constexpr int  kSupportedSchema = 1;

		/// Caught per group, so one bad group does not cost the rest of the file.
		struct EntryError : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		[[nodiscard]] std::string RequireString(const json& a_obj, const char* a_key)
		{
			const auto it = a_obj.find(a_key);
			if (it == a_obj.end() || !it->is_string() || it->get<std::string>().empty()) {
				throw EntryError(std::format("'{}' is missing or not a non-empty string", a_key));
			}
			return it->get<std::string>();
		}

		/// A single string or an array of strings, as `requires` and
		/// `workbench_keywords` accept.
		[[nodiscard]] std::vector<std::string> StringList(const json& a_obj, const char* a_key)
		{
			std::vector<std::string> out;
			const auto               it = a_obj.find(a_key);
			if (it == a_obj.end()) {
				return out;
			}
			if (it->is_string()) {
				out.push_back(it->get<std::string>());
				return out;
			}
			if (!it->is_array()) {
				throw EntryError(std::format("'{}' must be a string or an array of strings", a_key));
			}
			for (const auto& value : *it) {
				if (!value.is_string() || value.get<std::string>().empty()) {
					throw EntryError(std::format("'{}' entries must be non-empty strings", a_key));
				}
				out.push_back(value.get<std::string>());
			}
			return out;
		}

		/// "Form" or { "form": "Form", "per_base": 5 }.
		[[nodiscard]] VariantDef ParseVariant(const json& a_value)
		{
			VariantDef variant{};
			if (a_value.is_string()) {
				variant.form = a_value.get<std::string>();
			} else if (a_value.is_object()) {
				variant.form = RequireString(a_value, "form");
				if (const auto it = a_value.find("per_base"); it != a_value.end()) {
					if (!it->is_number_unsigned() || it->get<std::uint64_t>() == 0 || it->get<std::uint64_t>() > 1000) {
						throw EntryError(std::format("'{}': 'per_base' must be a whole number from 1 to 1000", variant.form));
					}
					variant.perBase = it->get<std::uint32_t>();
				}
			} else {
				throw EntryError("a variant must be a form string or an object with 'form'");
			}
			if (variant.form.empty()) {
				throw EntryError("a variant form is empty");
			}
			return variant;
		}

		[[nodiscard]] GroupDef ParseGroup(const json& a_entry, std::string_view a_source)
		{
			if (!a_entry.is_object()) {
				throw EntryError("group is not an object");
			}

			GroupDef def{};
			def.source = a_source;
			def.base = RequireString(a_entry, "base");
			// The name only labels log lines; the base is unique per group anyway.
			def.name = a_entry.contains("name") ? RequireString(a_entry, "name") : def.base;

			const auto variants = a_entry.find("variants");
			if (variants == a_entry.end() || !variants->is_array() || variants->empty()) {
				throw EntryError("'variants' is missing or empty");
			}
			for (const auto& value : *variants) {
				def.variants.push_back(ParseVariant(value));
			}

			def.workbenchKeywords = StringList(a_entry, "workbench_keywords");
			def.requiredPlugins = StringList(a_entry, "requires");

			if (const auto it = a_entry.find("convert_on_pickup"); it != a_entry.end()) {
				if (!it->is_boolean()) {
					throw EntryError("'convert_on_pickup' must be true or false");
				}
				def.convertOnPickup = it->get<bool>();
			}

			return def;
		}

		/// The shape the first prototype read: { "BaseEditorID": ["Variant", ...] }.
		/// Kept so its configs still load; every option takes its default.
		void ParseShorthand(const json& a_doc, std::string_view a_name, std::vector<GroupDef>& a_out)
		{
			std::size_t accepted = 0;
			for (const auto& [base, variants] : a_doc.items()) {
				if (base.starts_with('$')) {
					continue;
				}
				try {
					if (!variants.is_array() || variants.empty()) {
						throw EntryError("expected a non-empty array of variants");
					}
					GroupDef def{};
					def.name = base;
					def.base = base;
					def.source = a_name;
					for (const auto& value : variants) {
						def.variants.push_back(ParseVariant(value));
					}
					a_out.push_back(std::move(def));
					++accepted;
				} catch (const std::exception& e) {
					logger::error("[{}] skipping '{}': {}", a_name, base, e.what());
				}
			}
			logger::info("[{}] loaded {} group(s) (shorthand format).", a_name, accepted);
		}

		void LoadFile(const std::filesystem::path& a_path, std::vector<GroupDef>& a_out)
		{
			const auto name = a_path.filename().string();

			json doc;
			try {
				std::ifstream file(a_path);
				doc = json::parse(file, nullptr, true, true);  // allow comments
			} catch (const std::exception& e) {
				logger::error("[{}] not valid JSON, skipping the whole file: {}", name, e.what());
				return;
			}

			if (!doc.is_object()) {
				logger::error("[{}] is not a JSON object, skipping.", name);
				return;
			}

			if (const auto it = doc.find("$schema_version"); it != doc.end()) {
				if (!it->is_number_integer()) {
					logger::error("[{}] '$schema_version' must be a whole number, skipping.", name);
					return;
				}
				if (const auto version = it->get<int>(); version > kSupportedSchema) {
					logger::error("[{}] declares schema version {}, but this build only "
								  "understands up to {}. Update the plugin.",
								  name, version, kSupportedSchema);
					return;
				}
			}

			const auto groups = doc.find("groups");
			if (groups == doc.end()) {
				ParseShorthand(doc, name, a_out);
				return;
			}
			if (!groups->is_array()) {
				logger::error("[{}] 'groups' is not an array, skipping.", name);
				return;
			}

			std::size_t accepted = 0;
			for (const auto& entry : *groups) {
				try {
					a_out.push_back(ParseGroup(entry, name));
					++accepted;
				} catch (const std::exception& e) {
					logger::error("[{}] skipping a group: {}", name, e.what());
				}
			}
			logger::info("[{}] loaded {} group(s).", name, accepted);
		}
	}

	std::vector<GroupDef> Config::Load()
	{
		std::vector<GroupDef> defs;

		std::error_code ec;
		if (!std::filesystem::exists(kFolder, ec)) {
			logger::warn("'{}' does not exist -- no groups to apply.", kFolder);
			return defs;
		}

		// Sorted so the result does not depend on the filesystem's listing order.
		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::directory_iterator(kFolder, ec)) {
			if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
				files.push_back(entry.path());
			}
		}
		std::ranges::sort(files);

		for (const auto& file : files) {
			LoadFile(file, defs);
		}

		// Keep the first and report the rest, rather than silently picking one.
		std::unordered_set<std::string> seen;
		std::erase_if(defs, [&](const GroupDef& a_def) {
			if (seen.insert(a_def.base).second) {
				return false;
			}
			logger::error("[{}] a group for base '{}' is already defined, ignoring this one.",
						  a_def.source, a_def.base);
			return true;
		});

		return defs;
	}
}
