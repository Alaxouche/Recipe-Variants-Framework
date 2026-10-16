set_xmakever('3.0.1')
includes('lib/commonlibsse-ng')

set_project('RecipeVariantsFramework')
-- Three components only: xmake parses this as semver, and the plugin rule appends
-- the fourth itself (SKSE version {major, minor, patch, 0}, FileVersion "x.y.z.0").
set_version('1.0.0')
set_license('GPL-3.0')

set_languages('c++23')
set_warnings('allextra')
set_policy('package.requires_lock', true)
set_toolset('msvc', 'ninja')

add_rules('mode.debug', 'mode.releasedbg', 'mode.release')

-- Group configs are JSON. Required directly rather than through commonlibsse-ng's
-- optional `rex_json` feature so this target owns the dependency regardless of
-- how the library is configured. Same version as the author's other frameworks.
add_requires('nlohmann_json v3.12.0')

option('skyrim_se')
    set_default(false)
    set_showmenu(true)
    set_description('Build for Skyrim Special Edition')
option_end()

option('skyrim_ae')
    set_default(false)
    set_showmenu(true)
    set_description('Build for Skyrim Anniversary Edition')
option_end()

option('skyrim_vr')
    set_default(false)
    set_showmenu(true)
    set_description('Build for Skyrim VR only')
option_end()

if has_config('skyrim_vr') and (has_config('skyrim_se') or has_config('skyrim_ae')) then
    raise('Cannot combine Skyrim VR with SE/AE builds. Enable only one configuration.')
end

target('RecipeVariantsFramework')
    add_deps('commonlibsse-ng')

    local runtime = 'se_ae'
    if has_config('skyrim_vr') then
        runtime = 'vr'
    elseif has_config('skyrim_ae') and not has_config('skyrim_se') then
        runtime = 'ae'
    elseif has_config('skyrim_se') and not has_config('skyrim_ae') then
        runtime = 'se'
    end

    add_rules('commonlibsse-ng.plugin', {
        name        = 'RecipeVariantsFramework',
        author      = 'Alaxouche',
        description = 'Lets crafting recipes accept every variant of an item, not just the one they name.',
        runtime     = runtime
    })

    add_packages('nlohmann_json')

    add_files('src/**.cpp')
    add_headerfiles('src/**.h')

    -- ClibUtil's headers live under ClibUtil/include, so this is what makes
    -- #include <CLIBUtil/string.hpp> resolve. Its SimpleIni is bundled at
    -- CLIBUtil/detail/SimpleIni.h, so no separate simpleini checkout is needed.
    add_includedirs(
        'src',
        '$(projectdir)',
        '$(projectdir)/ClibUtil/include',
        '$(projectdir)/xbyak'
    )

    set_pcxxheader('src/pch.h')

    -- xbyak (and anything else reaching for windows.h) drags in the min/max
    -- macros, which break std::min/std::max in ClibUtil and elsewhere.
    add_defines('NOMINMAX', 'WIN32_LEAN_AND_MEAN')

    -- Always emit a .pdb next to the .dll. mode.release otherwise builds with
    -- hidden symbols and produces no .pdb at all, which leaves CrashLoggerSSE
    -- unable to resolve this plugin's frames in a crash log. The
    -- commonlibsse-ng.plugin rule installs target:symbolfile() alongside the
    -- DLL, so the .pdb lands in <mods>/<name>/SKSE/Plugins/ on its own.
    set_symbols('debug')
    set_strip('none')

    if has_config('skyrim_vr') then
        add_defines('ENABLE_SKYRIM_VR')
    elseif has_config('skyrim_se') and not has_config('skyrim_ae') then
        add_defines('ENABLE_SKYRIM_SE')
    elseif has_config('skyrim_ae') and not has_config('skyrim_se') then
        add_defines('ENABLE_SKYRIM_AE')
    else
        add_defines('ENABLE_SKYRIM_SE')
        add_defines('ENABLE_SKYRIM_AE')
    end

    -- Ship data/ alongside the DLL: the settings file. Group configs belong to
    -- the mods that use the framework (Data/SKSE/Plugins/RecipeVariantsFramework/*.json),
    -- not to the framework itself.
    add_installfiles('data/(**)')
