local function sprintf(s, ...) if ... == nil then return s else return s:format(...) end end
local function printf(s, ...) print(sprintf(s, ...)) end

local function Q(s) return '"'..s..'"' end
local function Q2(s) return Q(Q(s)) end

local function ESLASH(s) return s:gsub('/', '\\'):gsub('\\', [[\\]]) end
local function SLASH(s) return s:gsub('/', '\\') end
local function FSLASH(s) return s:gsub('\\', '/') end
--http://lua-users.org/wiki/StringInterpolation
local function interp(s, tab) return (s:gsub('($%b{})', function(w) return tab[w:sub(3, -2)] or w end)) end

local function split(str, sep)
	local sep, fields = sep or ":", {}
	local pattern = string.format("([^%s]+)", sep)
	str:gsub(pattern, function(c) fields[#fields+1] = c end)
	return fields
end

local function mkdir_recursive(path)
	local pathparts = split(path, "/")
	local p = ""
	for _, part in ipairs(pathparts) do
		p = p..part
		os.execute("{MKDIR} "..Q(p))
		p = p.."/"
	end
end

local function starts_with(str, start) return str:sub(1, #start) == start end

local function os_capture(cmd, raw)
	local f = assert(io.popen(cmd, 'r'))
	local s = assert(f:read('*a'))
	f:close()
	if raw then return s end
	s = string.gsub(s, '^%s+', '')
	s = string.gsub(s, '%s+$', '')
	s = string.gsub(s, '[\n\r]+', ' ')
	return s
end

local function table_copy(t)
	local r = {}
	for k, v in pairs(t) do
		r[k] = v
	end
	return r
end

local function file_exists(filepath)
	local res
	local f = io.open(filepath, "r");
	if f then
		res = true
		f:close()
	end

	return res
end

local option_interp = { ["spaces"] = (" "):rep(40) }
local self_options = {}
local onewoptions = newoption
function newoption(t)
	if t.default then
		local optional_nl = t.allowed and "" or "\n"
		t.description = t.description..sprintf("\n${spaces}default:%s%s", t.default, optional_nl)
	else
		t.description = t.description .."\n"
	end
	t.description = interp(t.description, option_interp)
	t.category = "nppplugins"
	self_options[t.trigger] = true
	onewoptions(t)
end

local action_interp = { ["spaces"] = (" "):rep(17+2) }
local self_actions = {}
local onewaction = newaction
function newaction(t)
	t.category = "nppplugins"
	self_actions[t.trigger] = true
	t.description = interp(t.description, action_interp)
	onewaction(t)
end

ROOT_DIR = path.getabsolute(".") .. "/"

local temprepopath = "temp"
local PLUGIN_VERSION_NON_RELEASE = "DEVELOPMENT"

local action_option = {}

local function add_action_option(name, desc)
	action_option[name] = true

	newaction {
		trigger = name,
		description = desc
	}

	newoption {
		trigger = name,
		description = desc
	}
end

newoption {
	trigger		= "gitpath",
	value		= "FILEPATH",
	description	= "Path to git (if not in PATH)",
	default		= "git.exe"
}

newoption {
	trigger		= "sevenzippath",
	value		= "FILEPATH",
	description	= "Path to 7-zip (if not in PATH)",
	default		= [[C:\Program Files\7-Zip\7z.exe]]
}

add_action_option("clean", [[Remove solution files]])
add_action_option("clean-plugins", [[Remove built plugins]])
add_action_option("deploy", [[Copy setup files for Notepad++ and all individual plugins configuration and documentation files to build-folders.

${spaces}This can also be used after a 'Clean' or 'Rebuild' is used within Visual Studio to ensure that the local development environment is setup.
]])

newaction {
	trigger		= "setup",
	description	= [[Setup the initial environment. Which includes:

${spaces}- Cloning a Notepad++ repository
${spaces}- Building Scintilla for all configurations (debug+release for both x86 and x64)
${spaces}- Creating 'doLocalConf.xml' to enable a 'local' debugging environment
${spaces}- Creating langs.xml and stylers.xml in build-folders

${spaces}Use '--setup-npprepo' to specify which repo to clone
${spaces}Use '--setup-nppbranch' to optionally specify which branch to use
]]
}

newoption {
	trigger		= "setup-npprepo",
	value		= "URL",
	description	= "Notepad++ repository to use for initial setup",
	default		= [[https://github.com/notepad-plus-plus/notepad-plus-plus.git]]
}

newoption {
	trigger		= "setup-nppbranch",
	value		= "NAME",
	description	= "Optional branch of '--setup-npprepo' to use for initial setup"
}

newaction {
	trigger 	= "build",
	description = [[Build plugins and optionally Notepad++.

${spaces}Use '--build-configuration' to specify which configuration(s) should be built
${spaces}Use '--build-npp' to specify if Notepad++ should be built
]]
}

newoption {
	trigger		= "build-configuration",
	value		= "CONFIGURATION(s)",
	description	= [[Which build configuration(s) should be used when running 'build']],
	default		= "debug+release",
	allowed = {
		{"debug", "debug"},
		{"debug+release", "debug and release"},
		{"release", "release"}
	}
}

newoption {
	trigger 	= "build-npp",
	value		= "yes/no",
	description = "Build Notepad++ (i.e. not just the plugins)",
	default		= "yes",
	allowed = {
		{"yes", "Do build Notepad++"},
		{"no", "Do _not_ build Notepad++"}
	}
}

newoption {
	trigger		= "plugin-version",
	value		= "VERSION",
	default		= PLUGIN_VERSION_NON_RELEASE,
	description	= [[This has multiple uses, namely:

${spaces}* optionally tag the plugins with a version during project generation
${spaces}* direct which version should be used by 'package-plugins' and 'local-install'

${spaces}NB: if a version is wanted then '--plugin-version' has to be specified to actions that
${spaces}does project generation and/or packaging indirectly (ex 'make-release')
]]
}

add_action_option("package-plugins", [[Package 'release'-configuration of plugins to folder '.releases/$arch$/$plugin-version$' (where $arch$ is x86 and x64)

${spaces}Use '--plugin-version' to specify version (if any)
]])

newaction {
	trigger		= "local-install",
	description	= [[Extract built plugins to local Notepad++ installation

${spaces}Use '--install-arch' to specify if 32 or 64 bit version should be installed
${spaces}Use '--install-basepath' to specify where the plugins should be installed
${spaces}Use '--plugin-version' to specify version (if any)
]]
}

newoption {
	trigger		= "install-arch",
	value		= "ARCH",
	description	= [[Install 32 or 64 bit plugins that was packaged by 'package-plugins' to install location specified by '--install-basepath']],
	default		= "x64",
	allowed = {
		{"x86", "32 bit version"},
		{"x64", "64 bit version"}
	}
}

newoption {
	trigger		= "install-basepath",
	value		= "BASEPATH",
	description	= [[Where to install the plugins, will install to $BASEPATH$/Notepad++/Plugins]],
	default		= "%programdata%"
}

if _OPTIONS["install-basepath"]:lower() == "%programfiles%" then
	if _OPTIONS["install-arch"] == "x86" then
		_OPTIONS["install-basepath"] = "%programfiles(x86)%"
	end
end

local make_release_action_name = "make-release"
newaction {
	trigger		= make_release_action_name,
	description	= [[Shortcut for running actions 'vs2017', 'build' and 'package-plugins' in succession

${spaces}NB: if built plugins should be tagged with a version then also specify '--plugin-version'
	]]
}

newaction {
	trigger		= "help",
	description	= [[Show help for 'nppplugins' only]]
}

for action_name, _ in pairs(action_option) do
	if _ACTION == action_name then
		_OPTIONS[action_name] = true
	end
end

if premake and premake.option and type(premake.option.validate) == "function" then
	if not premake.option.validate(_OPTIONS) then

		print "run 'premake5 help' for available commandline switches"
		return
	end
end

local function git_available() return os_capture(Q(_OPTIONS["gitpath"]).." --version") ~= "" end
local function sevenzip_available() return os_capture(Q(_OPTIONS["sevenzippath"]).." --help") ~= "" end

-- '-bso0' -> disable standard output stream messages
-- '-bsp0' -> disable progress information stream messages
local sevenzip_disable_stream_switches = "-bso0 -bsp0"
local function sevenzip_has_output_stream_switch()
	return false
end

local function sevenzip_get_available_disable_stream_switches()
	if sevenzip_has_output_stream_switch() then return sevenzip_disable_stream_switches else return "" end
end

if _ACTION == "help" then
	local keep_actions = { ["vs2017"]=true }

	local alloptions = premake and premake.option and premake.option.list or {}
	for k, v in pairs(alloptions) do
		if self_options[k] == nil then
			alloptions[k] = nil
		end
	end
	local allactions = premake and premake.action and premake.action._list or {}
	for k, v in pairs(allactions) do
		if self_actions[k] == nil and keep_actions[k] == nil then
			allactions[k] = nil
		end
	end

	if premake and type(premake.showhelp) == "function" then
		premake.showhelp()
	end

	return
end

PLUGIN_VERSION = _OPTIONS["plugin-version"]

local settings = {
	{
		arch = "x86",
		archswitch = "x86",
		buildfolderprefix = "",

		devenv_arch = "Win32",

		debug = {
			buildfolder = "PowerEditor/visual.net/Unicode Debug"
		},

		release = {
			buildfolder = "PowerEditor/bin"
		}
	},

	{
		arch = "x64",
		archswitch = "x86_amd64",
		buildfolderprefix = "x64",

		devenv_arch = "x64",

		debug = {
			buildfolder = "PowerEditor/visual.net/x64/Unicode Debug"
		},

		release = {
			buildfolder = "PowerEditor/bin64"
		}
	}
}

local function buildfolder_by_arch_config(arch, debug_or_release)
	debug_or_release = debug_or_release:lower()
	assert(debug_or_release == "debug" or debug_or_release == "release")
	for _, config in ipairs(settings) do
		if config.arch == arch then
			return config[debug_or_release].buildfolder
		end
	end
	assert(false, "could not find buildfolder for arch: "..arch)
end

local plugins = {}

local function run_git_command(git_command)
	local command = Q(_OPTIONS["gitpath"]).." "..git_command
	return os_capture(command)
end

local function get_revision_hash()
	-- https://stackoverflow.com/questions/949314/how-to-retrieve-the-hash-for-the-current-commit-in-git
	local git_command = [[rev-parse HEAD]]
	local hash = run_git_command(git_command)
	if hash == "" then
		print "ERROR: failed to get revision hash"
	end

	return Q(hash)
end

local function build_contact_email()
	local name = "fredrik"
	local domain = "incrediblejunior"
	return ([["%s@%s.com"]]):format(name, domain)
end

REVISION_HASH = get_revision_hash()
CONTACT_EMAIL = build_contact_email()
REPO_PATH = [["https://github.com/incrediblejr/nppplugins"]]

solution "nppplugins"
	location ".build"
	configurations { "Debug", "Release" }
	platforms { "x64", "x86" }

	configuration { "windows" }
		defines { "WIN32", "_WIN32" }
		flags { "NoManifest" }
		links { }

	configuration { "x86", "vs*" }

	configuration { "x64", "vs*" }
		defines { "_WIN64" }

	configuration { "vs*" }
		defines {
			"WIN32_LEAN_AND_MEAN",
			"VC_EXTRALEAN",

			"_SCL_SECURE_NO_WARNINS",
			"_CRT_SECURE_NO_WARNINGS",
			"_CRT_SECURE_NO_DEPRECATE",
			"REVISION_HASH="..REVISION_HASH,
			"CONTACT_EMAIL="..CONTACT_EMAIL,
			"REPO_PATH="..REPO_PATH,
			"PLUGIN_VERSION="..Q(PLUGIN_VERSION)
		}

	configuration "Debug"
		objdir ".build/obj_debug"
		targetdir ".build/bin_debug"

		defines { "DEBUG" }

		symbols "On"
		editandcontinue "Off"
		exceptionhandling "Off"
		rtti "Off"
		warnings "Extra"
		optimize "Off"

	configuration "Release"
		objdir ".build/obj_release"
		targetdir ".build/bin_release"
		defines { "NDEBUG" }

		symbols "On"
		editandcontinue "Off"
		exceptionhandling "Off"
		rtti "Off"
		warnings "Extra"
		optimize "Full"

	configuration {}

	external "notepadPlus"
		location "PowerEditor/visual.net"
		uuid "FCF60E65-1B78-4D1D-AB59-4FC00AC8C248"
		kind "WindowedApp"
		language "C++"

		configmap {
			["Debug"] = "Unicode Debug",
			["Release"] = "Unicode Release",
		}

function make_plugin(name, project_settings)
	project_settings = project_settings or {}
	project_settings.name = name
	plugins[#plugins+1] = project_settings

	project (name)
		uuid (os.uuid(name))
		location ".build"
		kind "SharedLib"

		configuration { "Release"}
			buildoptions { "/MT" }

		configuration { "Debug" }
			buildoptions { "/MTd" }

		for _, arch in ipairs { "x86", "x64" } do
			for _, c in ipairs { "debug", "release" } do
				configuration { arch, c }
					targetdir(ROOT_DIR..buildfolder_by_arch_config(arch, c).."/plugins/"..name)
			end
		end

		configuration {}

		language "C++"

		files {
			"nppplugin_shared/**",
			name.."/src/**",
		}

		includedirs {
			"nppplugin_shared/",
			name.."/src",
			"Scintilla/include",
			"PowerEditor/src/MISC/PluginsManager",
		}

		if project_settings.dependson then
			dependson { table.unpack(project_settings.dependson) }
		end

		if project_settings.postbuildcommands then
			postbuildcommands { table.unpack(project_settings.postbuildcommands) }
		end
end

local function wrootdir(s) return ROOT_DIR..s end

local solutionhub_postbuild_commands = {
	sprintf("{MKDIR} %s", wrootdir "nppplugin_shared/nppplugin_solutionhub_interface"),
	sprintf("{COPY} %s %s", wrootdir "nppplugin_solutionhub/src/nppplugin_solutionhub_com_interface.h", wrootdir "nppplugin_shared/nppplugin_solutionhub_interface"),
	sprintf("{COPY} %s %s", wrootdir "nppplugin_solutionhub/src/filerecords.h", wrootdir "nppplugin_shared/nppplugin_solutionhub_interface"),
}

make_plugin("nppplugin_ofis2", {config=true, doc=true, dependson={"nppplugin_solutionhub"}})
make_plugin("nppplugin_solutionhub", {config=true, postbuildcommands=solutionhub_postbuild_commands})
make_plugin("nppplugin_solutionhub_ui",{dependson={"nppplugin_solutionhub"}})
make_plugin("nppplugin_solutiontools", {config=true, doc=true, dependson={"nppplugin_solutionhub"}})
make_plugin("nppplugin_svn", {config=true, doc=true, dependson={"nppplugin_solutionhub"}})

local function deploy_npp_setup_files()
	printf("Copying setup files (langs/stylers/misc xml files)")
	for _, config in ipairs { "debug", "release" } do
		for _, archsettings in ipairs(settings) do
			local dstfolder = archsettings[config].buildfolder
			mkdir_recursive(dstfolder)

			local dolocal = path.join(dstfolder, "doLocalConf.xml")
			local dolocalfile = io.open(dolocal, "w")
			if dolocalfile then
				dolocalfile:close()
			else
				printf("failed to create %s", Q(dolocal))
			end

			for _, modelfilebase in ipairs {"langs", "stylers"} do
				local srcfilename = modelfilebase..".model.xml"
				local dstfilename = modelfilebase..".xml"

				local srcpath = path.join("PowerEditor/src", srcfilename)
				local dstpath = path.join(dstfolder, dstfilename)
				local df = io.open(dstpath, "w")
				if df then
					df:close()
				else
					printf("failed to create %s", Q(dstpath))
				end

				local command = sprintf("{COPY} %s %s", SLASH(srcpath), SLASH(Q(dstpath)))
				os.execute(command)
			end
		end
	end
end

if _ACTION == "setup" then
	print "Setup initial N++ workspace, start"

	local has_npp = os.isdir("PowerEditor") or os.isdir("scintilla")
	if has_npp then
		print "It seems like Notepad++ is already setup, doing nothing..."
		return
	end

	if not git_available() then
		print "\nERROR: git does not seem to be available, stopping initial setup (please check '--gitpath' option).\n\nSetup initial Notepad++ workspace, failed!\n"
		return
	end

	do
		local branch = _OPTIONS["setup-nppbranch"]
		local repo = _OPTIONS["setup-npprepo"]
		printf("Cloning initial N++ repo. %s, branch:[%s]", Q(repo), branch and branch or "")

		local fullcommandline
		if branch and branch ~= "" then
			local git_clone_commandline = [[%s clone --depth 1 %s -b %s %s]]
			fullcommandline = git_clone_commandline:format(Q(_OPTIONS["gitpath"]), repo, branch, temprepopath)
		else
			local git_clone_commandline = [[%s clone --depth 1 %s %s]]
			fullcommandline = git_clone_commandline:format(Q(_OPTIONS["gitpath"]), repo, temprepopath)
		end

		os.execute(fullcommandline)
	end

	do
		printf("Moving cloned initial N++ repo.")
		os.execute(("{MOVE} %s/PowerEditor PowerEditor"):format(temprepopath))
		os.execute(("{MOVE} %s/scintilla scintilla"):format(temprepopath))
		os.execute(("{RMDIR} %s"):format(temprepopath))

		for _, config in ipairs { "debug", "release" } do
			for _, archsettings in ipairs(settings) do
				local dstfolder = archsettings[config].buildfolder
				local dstfolderplugins = path.join(dstfolder, "plugins")
				mkdir_recursive(dstfolderplugins)
			end
		end
	end

	do
		local scilexer_buildbatfile_template = [[
@ECHO OFF
setlocal
set VCVARS_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build

call "%VCVARS_PATH%\vcvarsall.bat" ${archswitch}
${missingrcfix}
pushd scintilla\win32
nmake NOBOOST=1 clean -f scintilla.mak
nmake NOBOOST=1 ${debugswitch} -f scintilla.mak

popd]]
		local tempbatfilename = "buildscilexer.bat"
		local missingrcfix_format = [[set PATH=%WindowsSdkDir%bin\${arch};%WindowsSdkDir%bin\%WindowsSDKVersion%${arch};%PATH%]]

		for _, config in ipairs { "debug", "release" } do
			for _, archsettings in ipairs(settings) do
				local arch = archsettings.arch
				archsettings.missingrcfix = interp(missingrcfix_format, archsettings)

				archsettings.debugswitch = config == "debug" and "DEBUG=1" or ""
				local formatted = interp(scilexer_buildbatfile_template, archsettings)
				local batfile = io.open(tempbatfilename, "w")
				batfile:write(formatted)
				batfile:close()
				os.execute("call "..tempbatfilename)

				local dstfolder = archsettings[config].buildfolder

				local command = sprintf("{COPY} scintilla/bin/SciLexer.dll %s", Q(dstfolder))
				os.execute(command)
			end
		end
		-- finally delete the tempfile
		os.execute("{DELETE} "..tempbatfilename)
	end


	_OPTIONS["deploy"] = true

	print "Setup initial N++ workspace, done!"
end

if _OPTIONS["clean"] then
	os.execute("{RMDIR} .build")
end

if _OPTIONS["clean-plugins"] then
	for _, arch in ipairs  { "x86", "x64" } do
		for _, c in ipairs { "debug", "release" } do
			local build_folder = Q(ROOT_DIR..buildfolder_by_arch_config(arch, c).."/plugins")
			os.execute("{RMDIR} "..build_folder)
		end
	end
	_OPTIONS["deploy"] = true
end


if _ACTION == "build" then
	if not file_exists(".build/nppplugins.sln") then
		print "ERROR: 'build' was requested but solution-files has not been generated yet, please run 'premake5 vs2017' first."
		return
	end

	printf("'%s' building (%s) configuration(s) with version:%s", _ACTION, _OPTIONS["build-configuration"], PLUGIN_VERSION)

local buildtemplate=[[
@ECHO OFF
setlocal
set VCVARS_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build

call "%VCVARS_PATH%\vcvarsall.bat" ${archswitch}
${missingrcfix}

${build}

:End
]]
	local build_base = ""
	if _OPTIONS["build-npp"] == "yes" then
		build_base = build_base.."devenv /rebuild ${config} ${solutionfile}\n"
	else
		for _, project_settings in ipairs(plugins) do
			local plugin_name = project_settings.name
			build_base = build_base.."devenv /rebuild ${config} ${solutionfile} /project "..plugin_name.."\n"
		end
	end

	local batfilenamebase = "build_plugins_"
	local build_configs = split(_OPTIONS["build-configuration"], "+")
	for _, config in ipairs (build_configs) do
		for _, archsettings in ipairs(settings) do repeat
			local arch = archsettings.arch

			local build_settings = {
				solutionfile = ".build/nppplugins.sln",
				config = Q(config.."|"..archsettings.devenv_arch)
			}

			local build = interp(build_base, build_settings)
			local missingrcfix_format = [[set PATH=%WindowsSdkDir%bin\${arch};%WindowsSdkDir%bin\%WindowsSDKVersion%${arch};%PATH%]]

			local missingrcfix = interp(missingrcfix_format, archsettings)
			local batfile_settings = {
				arch = archsettings.arch,
				archswitch = archsettings.archswitch,
				missingrcfix = missingrcfix,
				build = build
			}

			local formatted = interp(buildtemplate, batfile_settings)

			local filename = batfilenamebase..arch..".bat"
			local batfile = io.open(filename, "w")
			if batfile then
				batfile:write(formatted)
				batfile:close()

				os.execute("call "..filename)
				os.execute("{DELETE} "..filename)
			else
				printf("ERROR: failed to create file %s", Q(filename))
			end
		until true end
	end

	_OPTIONS["deploy"] = true -- 'build' does a _rebuild_ hence cleaning the Notepad++ setupfiles (langs/stylers/doLocalConf.xml)
end

local function deploy_npp_and_plugin_files()
	print "Deploying individual plugins configuration and settings files.."
	local function copy_plugin_folder(plugin_name, folder_name, arch, c)
		local src_folder = ROOT_DIR..plugin_name.."/resources/"..folder_name
		local dst_folder = ROOT_DIR..buildfolder_by_arch_config(arch, c).."/plugins/"..plugin_name.."/"..folder_name
		mkdir_recursive(dst_folder)

		os.execute("{COPY} "..Q(src_folder).." "..Q(dst_folder))
	end

	for _, project_settings in ipairs(plugins) do
		local plugin_name = project_settings.name

		if project_settings.config then
			for _, arch in ipairs { "x86", "x64" } do
				for _, c in ipairs { "debug", "release" } do
					copy_plugin_folder(plugin_name, "config", arch, c)
				end
			end
			printf("   %s %s files deployed", plugin_name, "config")
		end

		if project_settings.doc then
			for _, arch in ipairs { "x86", "x64" } do
				for _, c in ipairs { "debug", "release" } do
					copy_plugin_folder(plugin_name, "doc", arch, c)
				end
			end
			printf("   %s %s files deployed", plugin_name, "doc")
		end
	end

	deploy_npp_setup_files()
end

if _OPTIONS["deploy"] then
	deploy_npp_and_plugin_files()
end

local function package_plugins(version, sevenzippath)
	if not sevenzip_available() then
		print "Package of built plugins requested but 7-zip if not available, please check '--sevenzippath' option"
	else
		local sevenzip = Q(FSLASH(sevenzippath))
		local stream_switches = sevenzip_get_available_disable_stream_switches()

		for _, arch in ipairs { "x86", "x64" } do
			local release_folder = sprintf(".releases/%s/%s", arch, version)
			os.execute("{RMDIR} "..release_folder)
			mkdir_recursive(release_folder)
			for _, c in ipairs { "release" } do
				for _, project_settings in ipairs(plugins) do repeat
					local plugin_name = project_settings.name
					local build_folder = ROOT_DIR..buildfolder_by_arch_config(arch, c).."/plugins/"..plugin_name
					-- NTS: append [[/*.*]] to get rid of the plugin_name-folder

					local exclude = [[-xr!*.pdb -xr!*.lib -xr!*.ilk -xr!*.exp]]
					local command = [[${sevenzip} a ${name}.zip ${stream_switches} -r ${folder} ${exclude}]]
					local t = {
						name = path.join(release_folder, plugin_name),
						sevenzip = sevenzip,
						folder = Q(ESLASH(build_folder)),
						exclude = exclude,
						stream_switches=stream_switches
					}

					local fcommand = interp(command, t)

					-- http://lua-users.org/lists/lua-l/2013-11/msg00367.html
					os.execute("type NUL && "..fcommand)
				until true end
			end
		end
	end
end

if _OPTIONS["package-plugins"] then
	package_plugins(PLUGIN_VERSION, _OPTIONS["sevenzippath"])
end

if _ACTION == "local-install" then
	printf("'%s' with version:%s", _ACTION, PLUGIN_VERSION)
	if not sevenzip_available() then
		print "Local install of built plugins requested but 7-zip if not available, please check '--sevenzippath' option"
	else
		local sevenzip = Q(FSLASH(_OPTIONS["sevenzippath"]))
		local arch = _OPTIONS["install-arch"]
		local basepath = _OPTIONS["install-basepath"]
		local version = PLUGIN_VERSION
		local release_folder = sprintf(".releases/%s/%s", arch, version)

		if not os.isdir(release_folder) then
			printf("'%s' FAILED, '%s' is not a folder, please verify that target is built and the correct '--plugin-version' is used.", _ACTION, release_folder)
			return
		end

		local stream_switches = sevenzip_get_available_disable_stream_switches()
		local pluginspath = basepath.."/Notepad++/plugins"

		for _, project_settings in ipairs(plugins) do
			local plugin_name = project_settings.name

			-- '-y' -> Assume Yes on all queries
			local command = [[${sevenzip} x ${name}.zip -y ${stream_switches} -o${output_folder}]]
			local t = {
				name = path.join(release_folder, plugin_name),
				sevenzip = sevenzip,
				output_folder = Q(ESLASH(pluginspath)),
				stream_switches = stream_switches
			}

			local fcommand = interp(command, t)

			-- http://lua-users.org/lists/lua-l/2013-11/msg00367.html
			os.execute("type NUL && "..fcommand)
		end
	end
end

if _ACTION == make_release_action_name then
	printf("'%s' with version:%s", _ACTION, PLUGIN_VERSION)
	local org_commandlineargs = {}

	for _, v in ipairs(_ARGV) do repeat
		if v == make_release_action_name then
			table.insert(org_commandlineargs, 1, "") -- remove the action and place it first
		else
			local nvalue
			local t = split(v, "=")
			if #t == 2 then
				nvalue = sprintf("%s=%s", t[1], Q(t[2]))
			elseif #t == 1 then
				nvalue = sprintf("%s", t[1])
			else
				assert(false, "split in "..#t.." parts?")
			end
			table.insert(org_commandlineargs, nvalue)
		end
	until true end

	do
		local current_action = "vs2017"
		printf("'%s' running action '%s'", make_release_action_name, current_action)
		-- first clean and generate project files
		-- as the both REVISION_HASH and PLUGIN_VERSION is set in the project files
		local newcommandlineargs = table_copy(org_commandlineargs)
		newcommandlineargs[1] = current_action
		table.insert(newcommandlineargs, "--clean")
		table.insert(newcommandlineargs, "--clean-plugins")

		local fullcommand = "premake5.exe "..table.concat(newcommandlineargs, " ")

		local fullcommand_res = os_capture(fullcommand)
	end

	do
		local current_action = "build"
		printf("'%s' running action '%s'", make_release_action_name, current_action)

		local newcommandlineargs = table_copy(org_commandlineargs)
		newcommandlineargs[1] = current_action
		table.insert(newcommandlineargs, [[--build-npp="no"]])
		table.insert(newcommandlineargs, [[--build-configuration="release"]])

		local fullcommand = "premake5.exe "..table.concat(newcommandlineargs, " ")
		local fullcommand_res = os_capture(fullcommand)
	end

	do
		local current_action = "package-plugins"
		printf("'%s' running action '%s'", make_release_action_name, current_action)

		local newcommandlineargs = table_copy(org_commandlineargs)
		newcommandlineargs[1] = current_action
		local fullcommand = "premake5.exe "..table.concat(newcommandlineargs, " ")
		local fullcommand_res = os_capture(fullcommand)
	end
end
