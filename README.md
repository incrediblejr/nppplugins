
# Notepad++ Plugins

The now almost 10 years old Notepad++ plugins exported from original SVN repository with updated project generation, setup process and codefixes to enable both 32 and 64 bit builds.

## Plugins

__Open File In Solution (OFIS)__

Open File In Solution (OFIS) lets you index specific folders and possible specific types of resources (xml-, cpp-, py-files) for a fast indexing of files (like Visual Assist's Open File In Solution for Visual Studio).

It also has the option to monitor named directories so if you delete/add files in the folders it will automatically remove them from indexed files (for example if you update your trunk through git/svn/perforce/hg/etc).

__Tortoise SVN__

Use various SVN commands from Notepad++ with the added concept of a root directory (so one can work on a repository and not just files).

__Solution Tools__

Various helpers/nice to have features like configurable switch header/implementation files, goto file etc.

__SolutionHub__

All above plugins have some 'connection to'/'concept of' a solution/project and SolutionHub handles all file indexing and settings for this. Plugins register to SolutionHub and (file-) searches is passed as queries messages which in turn delivered back as response messages.

__SolutionHub UI__

SolutionHub is just an file/solution/connections service and SolutionHub UI lets one edit/create/delete solutions and its connections to the registered plugins (OFIS and Tortoise SVN).

##	 Building

__Dependencies__

[premake5](https://premake.github.io/download.html#v5)

__Getting Source__

[nppplugins](https://github.com/incrediblejr/nppplugins)

__Quick Start__

* Clone the [nppplugins](https://github.com/incrediblejr/nppplugins) git repository
* Download [premake5](https://premake.github.io/download.html#v5) and place it in the root of the cloned repository
* Open a commandprompt in the root of the cloned repository
* Setup the local development environment by running:

  `premake5 setup`

  This will setup a Notepad++ development environment, which includes cloning a Notepad++ repository, building Scintilla and copying/creating files to enable a _local_ environment.

  Please check the output as this requires that `git` is accessible from the command-line, this can set by `--gitpath=PATH` command-line option if not.

* Generate Visual Studio 2017 project files by running:

  `premake5 vs2017`

  now the solution files can be found in the `.build` folder and can be opened by

  `start .build/nppplugins.sln`

* There is a couple more command-line switches to make building and deploying a bit easier, like:

  `premake5 build` builds the local Notepad++ and the plugins
  
  `premake5 package-plugins` zips all built plugins for easier redistribution
  
  `premake5 make-release` shortcut for running 'vs2017', 'build' and 'package-plugins' in succession
  
  `premake5 local-install` deploys built and packaged plugins for use by the installed Notepad++

  For further documentation of command-line actions/options run

  `premake5 help`

  or just open `premake5.lua` and take a peek (it reads like novel).
