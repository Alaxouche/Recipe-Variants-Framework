// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Alaxouche

#pragma once

#pragma warning(push)
#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>
#include <REX/REX/Singleton.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <optional>
#include <ranges>
#include <utility>

#ifdef NDEBUG
#	include <spdlog/sinks/basic_file_sink.h>
#else
#	include <spdlog/sinks/msvc_sink.h>
#endif
#pragma warning(pop)

using namespace std::literals;

namespace logger = SKSE::log;

namespace util
{
	using SKSE::stl::report_and_fail;
}

#include "plugin.h"
