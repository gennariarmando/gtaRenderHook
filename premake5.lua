workspace "gtaRenderHook"
	configurations { "Release", "Debug" }
	location "project_files"
   
project "gtaRenderHook"
	files {
		"source/***.*",
		"vendor/tinyxml2/tinyxml2.cpp", "vendor/tinyxml2/tinyxml2.h",
		"vendor/AntTweakBar/include/AntTweakBar.h",
		"vendor/FastNoiseSIMD/FastNoiseSIMD/*.*"
	}
	
	includedirs { 
		"source/**",
		"vendor/tinyxml2",
		"vendor/AntTweakBar/include",
		"vendor/FastNoiseSIMD/FastNoiseSIMD/"
	}
	
	includedirs {
		"$(PLUGIN_SDK_DIR)/shared/",
		"$(PLUGIN_SDK_DIR)/shared/game/",
		"$(PLUGIN_SDK_DIR)/plugin_sa/",
		"$(PLUGIN_SDK_DIR)/plugin_sa/game_sa/",
	}
	
	libdirs { 
		"$(PLUGIN_SDK_DIR)/output/lib/",
		"vendor/AntTweakBar/lib",
	}
	
	kind "SharedLib"
	language "C++"
	targetdir "output/asi/"
	objdir ("output/obj")
	targetextension ".asi"
	characterset ("MBCS")
	linkoptions "/SAFESEH:NO"
	cppdialect "C++17"
	buildoptions { "/permissive" }
	defines { "_CRT_SECURE_NO_WARNINGS", "_CRT_NON_CONFORMING_SWPRINTFS", "_USE_MATH_DEFINES", "USE_ANTTWEAKBAR", "NOASM", "VK_USE_PLATFORM_WIN32_KHR", }
	disablewarnings { "4244", "4800", "4305", "4073", "4838", "4996", "4221", "4430", "26812", "26495", "6031" }
	defines { "GTASA", "GTA_SA", "PLUGIN_SGV_10US", "_WINDOWS", "_USRDLL", "GTARENDERHOOK_EXPORTS" }

	filter "configurations:Debug"		
		links { "plugin_d", "AntTweakBar", "dxgi", "d3d11", "d3dcompiler", "dxguid", "user32" }
		targetname "gtaRenderHook"
		defines { "DEBUG", "_DEBUG" }
		symbols "on"
		staticruntime "on"
		debugdir "$(GTA_SA_DIR)"
		debugcommand "$(GTA_SA_DIR)/gta-sa.exe"
		postbuildcommands "copy /y \"$(TargetPath)\" \"$(GTA_SA_DIR)\\gtaRenderHook.asi\""

	filter "configurations:Release"
		links { "plugin", "AntTweakBar", "dxgi", "d3d11", "d3dcompiler", "dxguid", "user32" }
		targetname "gtaRenderHook"
		defines { "NDEBUG" }
		symbols "off"
		optimize "On"
		staticruntime "on"
		debugdir "$(GTA_SA_DIR)"
		debugcommand "$(GTA_SA_DIR)/gta-sa.exe"
		postbuildcommands "copy /y \"$(TargetPath)\" \"$(GTA_SA_DIR)\\gtaRenderHook.asi\""
