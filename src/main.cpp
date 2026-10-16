// SKSE plugin entry point.
//
// src/pch.h is force-included by xmake (set_pcxxheader in xmake.lua), so RE/,
// REL/, SKSE/, the `logger` alias and Plugin::* are already visible here.

namespace
{
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
#ifdef NDEBUG
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
#else
        log->set_level(spdlog::level::trace);
        log->flush_on(spdlog::level::trace);
#endif

        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
    }

    void OnMessage(SKSE::MessagingInterface::Message* a_message)
    {
        switch (a_message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            // Every form is loaded by now. Hook up game-facing work here.
            logger::info("Data loaded.");
            break;
        default:
            break;
        }
    }
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    REL::Module::reset();
    InitializeLogging();

    logger::info("{} v{} loading against runtime {}",
                 Plugin::NAME,
                 Plugin::VERSION.string(),
                 REL::Module::get().version().string());

    SKSE::Init(a_skse);
    SKSE::AllocTrampoline(1 << 10);

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener("SKSE", OnMessage)) {
        logger::critical("Failed to register the SKSE message listener.");
        return false;
    }

    return true;
}
