// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#pragma once

namespace RVF
{
	/// Hears the player activate and sit at a crafting bench, the crafting menu
	/// open and close, and (only when a group asks for it) items enter the
	/// player's inventory. Inventory changes run on the main thread: right away
	/// when the event arrives there, otherwise as an SKSE task.
	class Events final :
		public REX::Singleton<Events>,
		public RE::BSTEventSink<RE::TESActivateEvent>,
		public RE::BSTEventSink<RE::TESFurnitureEvent>,
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
		public RE::BSTEventSink<RE::TESContainerChangedEvent>
	{
	public:
		/// Call from SKSEPlugin_Load, which runs on the game's main thread.
		static void RecordMainThread();

		/// Call at kDataLoaded, after Groups::Build().
		static void Register();

		RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event,
											  RE::BSTEventSource<RE::TESActivateEvent>*) override;
		RE::BSEventNotifyControl ProcessEvent(const RE::TESFurnitureEvent* a_event,
											  RE::BSTEventSource<RE::TESFurnitureEvent>*) override;
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
											  RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;
		RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event,
											  RE::BSTEventSource<RE::TESContainerChangedEvent>*) override;
	};
}
