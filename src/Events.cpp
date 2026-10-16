// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#include "Events.h"

#include <chrono>
#include <thread>

#include "Groups.h"
#include "Session.h"

namespace RVF
{
	namespace
	{
		using Result = RE::BSEventNotifyControl;

		std::thread::id g_mainThread;

		/// How long an activation may take to reach the crafting menu before its
		/// session is given back. The menu opens within the sit animation.
		constexpr auto kMenuTimeout = std::chrono::seconds(10);

		[[nodiscard]] bool OnMainThread()
		{
			return std::this_thread::get_id() == g_mainThread;
		}

		[[nodiscard]] std::string ThreadNote()
		{
			return OnMainThread() ? "main thread" : "other thread";
		}

		[[nodiscard]] const RE::TESFurniture* FurnitureOf(RE::TESObjectREFR* a_ref)
		{
			const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			return base ? base->As<RE::TESFurniture>() : nullptr;
		}

		/// The bench behind the open crafting menu, for a session that starts late.
		[[nodiscard]] const RE::TESFurniture* CurrentMenuBench()
		{
			auto* ui = RE::UI::GetSingleton();
			const auto menu = ui ? ui->GetMenu<RE::CraftingMenu>() : nullptr;
			auto* subMenu = menu ? menu->GetCraftingSubMenu() : nullptr;
			return subMenu ? subMenu->furniture : nullptr;
		}

		[[nodiscard]] bool CraftingMenuOpen()
		{
			auto* ui = RE::UI::GetSingleton();
			return ui && ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME);
		}

		void Queue(std::function<void()> a_task)
		{
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask(std::move(a_task));
			}
		}

		/// Runs now when the event came on the main thread (so the swap lands
		/// before whatever the engine does next, such as opening the menu),
		/// otherwise on the next SKSE task pass.
		void RunOnMainThread(std::function<void()> a_task)
		{
			if (OnMainThread()) {
				a_task();
			} else {
				Queue(std::move(a_task));
			}
		}

		/// Gives the session back if no crafting menu opened in time. Paced from
		/// its own thread, then handed to the main thread: an SKSE task that
		/// re-queued itself would spin inside a single frame.
		void WatchForMenu(std::uint32_t a_session)
		{
			std::thread([a_session] {
				std::this_thread::sleep_for(kMenuTimeout);
				Queue([a_session] { Session::EndIfNoMenu(a_session); });
			}).detach();
		}
	}

	void Events::RecordMainThread()
	{
		g_mainThread = std::this_thread::get_id();
	}

	void Events::Register()
	{
		auto* sink = GetSingleton();

		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESActivateEvent>(sink);
			holder->AddEventSink<RE::TESFurnitureEvent>(sink);
			// Only worth hearing every item move when a group converts on pickup.
			if (Groups::AnyConvertOnPickup()) {
				holder->AddEventSink<RE::TESContainerChangedEvent>(sink);
			}
		} else {
			logger::critical("No script event source; workbench sessions will only start when the menu opens.");
		}

		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(sink);
		} else {
			logger::critical("No UI singleton; sessions will only end when the player leaves the bench.");
		}

		logger::info("Event sinks registered{}.", Groups::AnyConvertOnPickup() ? " (with pickup conversion)" : "");
	}

	Result Events::ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*)
	{
		if (!a_event || !a_event->actionRef || !a_event->actionRef->IsPlayerRef()) {
			return Result::kContinue;
		}

		const auto* bench = FurnitureOf(a_event->objectActivated.get());
		if (!Groups::IsCraftingBench(bench)) {
			return Result::kContinue;
		}

		// The earliest sign of a crafting session: the crafting menu builds its
		// recipe list as it opens, and by the time the player sits it already has.
		logger::debug("Bench activated ({}, menu {}).", ThreadNote(), CraftingMenuOpen() ? "open" : "closed");
		RunOnMainThread([bench] {
			if (Session::IsActive()) {
				return;
			}
			Session::Begin(bench, "workbench activated");
			WatchForMenu(Session::CurrentId());
		});
		return Result::kContinue;
	}

	Result Events::ProcessEvent(const RE::TESFurnitureEvent* a_event, RE::BSTEventSource<RE::TESFurnitureEvent>*)
	{
		if (!a_event || !a_event->actor || !a_event->actor->IsPlayerRef()) {
			return Result::kContinue;
		}

		const auto* bench = FurnitureOf(a_event->targetFurniture.get());
		if (!Groups::IsCraftingBench(bench)) {
			return Result::kContinue;
		}

		const bool entering = a_event->type == RE::TESFurnitureEvent::FurnitureEventType::kEnter;
		logger::debug("Bench {} ({}, menu {}).", entering ? "entered" : "left", ThreadNote(),
					  CraftingMenuOpen() ? "open" : "closed");
		if (entering) {
			// Normally activation already opened the session.
			RunOnMainThread([bench] {
				if (!Session::IsActive()) {
					Session::Begin(bench, "workbench entered");
				}
			});
		} else {
			RunOnMainThread([] { Session::End("workbench left"); });
		}
		return Result::kContinue;
	}

	Result Events::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		if (!a_event || a_event->menuName != RE::CraftingMenu::MENU_NAME) {
			return Result::kContinue;
		}

		logger::debug("Crafting menu {} ({}).", a_event->opening ? "opening" : "closing", ThreadNote());
		if (a_event->opening) {
			// Activation is the normal start. This catches benches reached without
			// one (a script opening the menu). Queued, so it runs after any pending
			// Begin and finds that session open.
			Queue([] {
				if (Session::IsActive()) {
					Session::MarkMenuOpened();
					return;
				}
				const auto* bench = CurrentMenuBench();
				// The same menu serves alchemy and enchanting, which use no recipes.
				if (bench && !Groups::IsCraftingBench(bench)) {
					return;
				}
				Session::Begin(bench, "crafting menu opened");
			});
		} else {
			Queue([] { Session::End("crafting menu closed"); });
		}
		return Result::kContinue;
	}

	Result Events::ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*)
	{
		// 0x14 is the player reference.
		if (!a_event || a_event->newContainer != 0x14 || a_event->itemCount <= 0) {
			return Result::kContinue;
		}

		const auto* group = Groups::FindByVariant(a_event->baseObj);
		if (!group || !group->convertOnPickup) {
			return Result::kContinue;
		}

		const auto* variant = RE::TESForm::LookupByID<RE::TESBoundObject>(a_event->baseObj);
		Queue([variant] { Session::ConvertForGood(variant); });
		return Result::kContinue;
	}
}
