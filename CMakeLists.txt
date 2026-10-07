# Cross-build for Windows x64 using mingw-w64 (Linux host).
# Usage: cmake -B build-mingw -S . -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-toolchain.cmake && cmake --build build-mingw -j
#
# Source lists are parsed from the existing .vcxproj files so the MSVC and
# mingw builds stay in sync. Output goes to build/ (same as the MSVC build).

cmake_minimum_required(VERSION 3.21)
project(Heretic2R C CXX RC)

if(NOT WIN32)
	message(FATAL_ERROR "This build requires the mingw-w64 toolchain file: -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-toolchain.cmake")
endif()

set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu17 -fcommon -Wno-unknown-pragmas -Wno-multichar -Wno-error=incompatible-pointer-types -Wno-incompatible-pointer-types -Wno-error=implicit-function-declaration -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -DWIN32 -D_WINDOWS -include ${CMAKE_SOURCE_DIR}/cmake/mingw_compat.h -I${CMAKE_SOURCE_DIR}/cmake/compat -I${CMAKE_BINARY_DIR}/case_aliases" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas -Wno-multichar -Wno-error=incompatible-pointer-types -Wno-incompatible-pointer-types -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -DWIN32 -D_WINDOWS -I${CMAKE_SOURCE_DIR}/cmake/compat -I${CMAKE_BINARY_DIR}/case_aliases" CACHE STRING "" FORCE)

set(H2_OUT "${CMAKE_SOURCE_DIR}/build")
set(H2_BASE "${CMAKE_SOURCE_DIR}/build/base")

set(H2_SDL3_LIB   "${CMAKE_SOURCE_DIR}/lib/SDL3/x64/SDL3.lib")
set(H2_OPENAL_LIB "${CMAKE_SOURCE_DIR}/lib/OpenAL/x64/OpenAL32.lib")

# Several sources include headers with a different case than the file on disk
# (works on case-insensitive Windows, fails on Linux). Generate case-alias
# symlinks in the build tree so neither the sources nor MSVC builds are affected.
set(H2_CASE_ALIASES "${CMAKE_BINARY_DIR}/case_aliases")
file(MAKE_DIRECTORY "${H2_CASE_ALIASES}")
set(H2_CASE_PAIRS
	"q_sprite.h|src/qcommon/q_Sprite.h"
	"q_clientserver.h|src/qcommon/q_ClientServer.h"
	"ce_Dlight.h|src/client effects/ce_DLight.h"
	"ce_dlight.h|src/client effects/ce_DLight.h"
	"Decals.h|src/game/decals.h"
	"g_skeletons.h|src/game/g_Skeletons.h"
	"c_victimssithra_anim.h|src/game/c_victimSsithra_anim.h"
	"c_victimssithra_moves.h|src/game/c_victimSsithra_moves.h"
	"m_plaguesSithra.h|src/game/m_plagueSsithra.h"
	"m_plaguesSithra_anim.h|src/game/m_plagueSsithra_anim.h"
	"m_plaguesSithra_moves.h|src/game/m_plagueSsithra_moves.h"
	"m_plaguesSithra_shared.h|src/game/m_plagueSsithra_shared.h"
	"m_plaguessithra_anim.h|src/game/m_plagueSsithra_anim.h")
foreach(pair IN LISTS H2_CASE_PAIRS)
	string(REPLACE "|" ";" pair_list "${pair}")
	list(GET pair_list 0 alias)
	list(GET pair_list 1 real)
	file(CREATE_LINK "${CMAKE_SOURCE_DIR}/${real}" "${H2_CASE_ALIASES}/${alias}" SYMBOLIC)
endforeach()

# Collects <ClCompile Include="..."/> entries from a vcxproj (paths relative to it).
function(h2_vcxproj_sources vcxproj out_var)
	get_filename_component(projdir "${vcxproj}" DIRECTORY)
	file(STRINGS "${vcxproj}" lines)
	set(srcs)
	foreach(line IN LISTS lines)
		if(line MATCHES "ClCompile Include=\"([^\"]+)\"")
			string(REPLACE "\\" "/" rel "${CMAKE_MATCH_1}")
			get_filename_component(abs "${rel}" ABSOLUTE BASE_DIR "${projdir}")
			list(APPEND srcs "${abs}")
		endif()
	endforeach()
	set(${out_var} "${srcs}" PARENT_SCOPE)
endfunction()

function(h2_add_target name)
	cmake_parse_arguments(A "" "VCPROJ;OUTNAME;OUTDIR" "DEFS;INCS;LIBS;SOURCES;EXCLUDE" ${ARGN})
	if(A_VCPROJ)
		h2_vcxproj_sources("${CMAKE_SOURCE_DIR}/${A_VCPROJ}" srcs)
		if(A_EXCLUDE)
			list(REMOVE_ITEM srcs ${A_EXCLUDE})
		endif()
		list(APPEND srcs ${A_SOURCES})
	else()
		set(srcs ${A_SOURCES})
	endif()
	add_library(${name} SHARED ${srcs})
	set_target_properties(${name} PROPERTIES
		PREFIX ""
		OUTPUT_NAME "${A_OUTNAME}"
		RUNTIME_OUTPUT_DIRECTORY "${A_OUTDIR}"
		LIBRARY_OUTPUT_DIRECTORY "${A_OUTDIR}")
	target_compile_definitions(${name} PRIVATE ${A_DEFS} NDEBUG WIN32 _WINDOWS)
	target_include_directories(${name} PRIVATE ${A_INCS})
	target_link_libraries(${name} PRIVATE ${A_LIBS})
endfunction()

# --- H2Common (utility DLL) -------------------------------------------------
h2_add_target(H2Common
	VCPROJ "src/H2Common/H2Common.vcxproj"
	OUTNAME "H2Common" OUTDIR "${H2_OUT}"
	DEFS  H2COMMON
	INCS  "${CMAKE_SOURCE_DIR}/src/qcommon" "${CMAKE_SOURCE_DIR}/src/game" "${CMAKE_SOURCE_DIR}/src/Player")

# --- quake2 (engine DLL) ----------------------------------------------------
h2_add_target(quake2
	VCPROJ "src/win32/quake2.vcxproj"
	OUTNAME "quake2" OUTDIR "${H2_OUT}"
	DEFS  QUAKE2_DLL
	INCS  "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/src/client" "${CMAKE_SOURCE_DIR}/src/server"
	      "${CMAKE_SOURCE_DIR}/src/qcommon" "${CMAKE_SOURCE_DIR}/src/game" "${CMAKE_SOURCE_DIR}/src/Player"
	      "${CMAKE_SOURCE_DIR}/src/win32"
	LIBS  H2Common "${H2_SDL3_LIB}" mf mfplat mfreadwrite mfuuid ole32 winmm wsock32 wininet)

# --- Launcher exe -----------------------------------------------------------
add_executable(Heretic2R WIN32
	"${CMAKE_SOURCE_DIR}/src/launcher/main.c"
	"${CMAKE_SOURCE_DIR}/src/launcher/Heretic2R.rc")
target_compile_definitions(Heretic2R PRIVATE NDEBUG WIN32 _WINDOWS)
target_include_directories(Heretic2R PRIVATE "${CMAKE_SOURCE_DIR}/src/qcommon" "${CMAKE_SOURCE_DIR}/src/win32")
target_link_libraries(Heretic2R PRIVATE quake2)
set_target_properties(Heretic2R PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${H2_OUT}" OUTPUT_NAME "Heretic2R")

# --- Game logic DLL ---------------------------------------------------------
h2_add_target(gamex64
	VCPROJ "src/game/game.vcxproj"
	OUTNAME "gamex64" OUTDIR "${H2_BASE}"
	DEFS  GAME_DLL
	INCS  "${CMAKE_SOURCE_DIR}/src/Player" "${CMAKE_SOURCE_DIR}/src/qcommon" "${CMAKE_SOURCE_DIR}/src/game"
	LIBS  H2Common quake2)

# --- Client effects DLL -----------------------------------------------------
h2_add_target(ClientEffects
	VCPROJ "src/client effects/Client Effects.vcxproj"
	OUTNAME "Client Effects" OUTDIR "${H2_BASE}"
	INCS  "${CMAKE_SOURCE_DIR}/src/Player" "${CMAKE_SOURCE_DIR}/src/client" "${CMAKE_SOURCE_DIR}/src/qcommon" "${CMAKE_SOURCE_DIR}/src/game"
	LIBS  H2Common quake2)

# --- Player DLL -------------------------------------------------------------
h2_add_target(Player
	VCPROJ "src/Player/Player.vcxproj"
	OUTNAME "Player" OUTDIR "${H2_BASE}"
	DEFS  PLAYER_DLL
	INCS  "${CMAKE_SOURCE_DIR}/src/game" "${CMAKE_SOURCE_DIR}/src/qcommon" "${CMAKE_SOURCE_DIR}/src/Player"
	LIBS  H2Common quake2)

# --- Sound DLL --------------------------------------------------------------
h2_add_target(snd_sdl3
	VCPROJ "src/snd_sdl3/snd_sdl3.vcxproj"
	OUTNAME "snd_sdl3" OUTDIR "${H2_OUT}"
	INCS  "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/src/qcommon" "${CMAKE_SOURCE_DIR}/src/client"
	      "${CMAKE_SOURCE_DIR}/src/game" "${CMAKE_SOURCE_DIR}/src/Player"
	LIBS  H2Common "${H2_SDL3_LIB}" "${H2_OPENAL_LIB}"
	SOURCES "${CMAKE_SOURCE_DIR}/src/snd_sdl3/src/snd_efx.c") # present on disk but missing from snd_sdl3.vcxproj

# --- OpenGL 3.3 renderer ----------------------------------------------------
# Note: the vcxproj compiles include/glad-GL3.3/glad.c, but the sources include
# glad-GL4.6/glad.h; src/gl3_Glad.c is the matching GLAD 2.x implementation.
h2_add_target(ref_gl3
	VCPROJ "src/ref_gl3/ref_gl3.vcxproj"
	OUTNAME "ref_gl3" OUTDIR "${H2_OUT}"
	INCS  "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/src/client" "${CMAKE_SOURCE_DIR}/src/qcommon" "${CMAKE_SOURCE_DIR}/src/game"
	LIBS  H2Common "${H2_SDL3_LIB}"
	EXCLUDE "${CMAKE_SOURCE_DIR}/include/glad-GL3.3/glad.c"
	SOURCES "${CMAKE_SOURCE_DIR}/src/ref_gl3/src/gl3_Glad.c")

# --- OpenGL 1.x renderer (legacy) -------------------------------------------
h2_add_target(ref_gl1
	VCPROJ "src/ref_gl1/ref_gl1.vcxproj"
	OUTNAME "ref_gl1" OUTDIR "${H2_OUT}"
	INCS  "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/src/client" "${CMAKE_SOURCE_DIR}/src/qcommon" "${CMAKE_SOURCE_DIR}/src/game"
	LIBS  H2Common "${H2_SDL3_LIB}" opengl32 glu32)
