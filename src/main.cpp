// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

// SKSE plugin entry point.
//
// src/pch.h is force-included by xmake (set_pcxxheader in xmake.lua), so RE/,
// REL/, SKSE/, the `logger` alias and Plugin::* are already visible here.

#include "Events.h"
#include "Groups.h"
#include "Session.h"
#include "Settings.h"

namespace
{
	/// Co-save section tag. Must not collide with another plugin's.
	constexpr std::uint32_t kSerializationID = 'ISFW';

	void InitializeLogging()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			util::report_and_fail("Failed to locate the SKSE log directory."sv);
		}
		*path /= std::format("{}.log", Plugin::NAME);

#ifdef NDEBUG
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#else
		auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#endif

		auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
		// Permissive until the settings are read; ApplyLogLevel narrows it.
		log->set_level(spdlog::level::trace);
		log->flush_on(spdlog::level::trace);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
	}

	void ApplyLogLevel()
	{
		const auto level = RVF::Settings::LogLevel();
		spdlog::set_level(level);
		spdlog::flush_on(level);
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		switch (a_message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			// Every form exists: groups can be resolved and sinks registered.
			try {
				RVF::Groups::Build();
				RVF::Events::Register();
			} catch (const std::exception& e) {
				// A throw would cross the SKSE dispatch and take the game down.
				logger::critical("Startup aborted: {}", e.what());
			}
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			// The co-save has been read and the player's inventory is live. Settle a
			// session the save was made in, then catch up on pickup conversions for
			// items carried from before a group existed.
			SKSE::GetTaskInterface()->AddTask([] {
				RVF::Session::End("restored from the save");
				RVF::Session::ConvertAllForGood();
			});
			break;
		default:
			break;
		}
	}
}

// SKSEPlugin_Version and SKSEPlugin_Query come from the commonlibsse-ng.plugin
// rule (xmake.lua), which generates them from the project name and version.
SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	REL::Module::reset();
	InitializeLogging();

	// SKSE loads plugins on the game's main thread; event handlers compare
	// against this to know whether they may touch the inventory right away.
	RVF::Events::RecordMainThread();

	// REL::Version::string() separates with '-' by default, which reads as
	// "v1-0-0-0" in a bug report. Ask for dots.
	logger::info("{} v{} loading against runtime {}",
				 Plugin::NAME,
				 Plugin::VERSION.string("."sv),
				 REL::Module::get().version().string("."sv));

	RVF::Settings::Load();
	ApplyLogLevel();

	// log = false: this plugin owns its logger. CommonLib's default would open
	// the same file again and truncate everything written above.
	SKSE::Init(a_skse, { .log = false });

	if (!RVF::Settings::Get().enabled) {
		logger::info("Disabled in the settings file -- nothing will be swapped.");
		return true;
	}

	auto* serialization = SKSE::GetSerializationInterface();
	if (!serialization) {
		logger::critical("No serialization interface; refusing to load rather than risk unsettled swaps.");
		return false;
	}
	serialization->SetUniqueID(kSerializationID);
	serialization->SetSaveCallback(RVF::Session::OnSave);
	serialization->SetLoadCallback(RVF::Session::OnLoad);
	serialization->SetRevertCallback(RVF::Session::OnRevert);

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener("SKSE", OnMessage)) {
		logger::critical("Failed to register the SKSE message listener.");
		return false;
	}

	return true;
}
