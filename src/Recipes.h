// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#pragma once

namespace RVF::Recipes
{
	/// Folds together the recipes that only differ by which variant of a group
	/// they ask for -- the copies a mod made one per variant before this
	/// framework existed. One recipe of each set is kept and pointed at the base
	/// items; the others are moved to a bench keyword no workbench carries, so
	/// they no longer show. Nothing on disk changes: without this plugin the mod's
	/// own copies keep working as before.
	///
	/// Call once at kDataLoaded, after the groups are built and before the
	/// recipes naming variants are indexed.
	void MergeDuplicates();

	/// The bench keyword merged copies were moved to, or null when nothing was
	/// merged.
	[[nodiscard]] const RE::BGSKeyword* HiddenKeyword();
}
