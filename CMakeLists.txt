# ZIMTV IPTV (formerly PS4 IPTV) - production application (title ZIMT00001).
#
# Playback foundation (Milestone B+): the pPlay stack proven on hardware in Phase 2 - libmpv 0.34.1,
# FFmpeg 5.0, SDL2 2.0.18 PS4 port, libcross2d - found and linked exactly like upstream pPlay.
# Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/ps4-windows.cmake (see scripts/build-ps4iptv.ps1).

cmake_minimum_required(VERSION 3.22)

project(ps4iptv C CXX)

if (NOT PS4)
    message(FATAL_ERROR "PS4 only: configure with -DCMAKE_TOOLCHAIN_FILE=cmake/ps4-windows.cmake")
endif ()

# The application version: written here only. Everything else is derived from it - the in-app "v0.4.0" (About,
# Settings, Diagnostics, log), the User-Agent, and the PS4 APP_VER / VERSION of param.sfo ("XX.YY": MAJOR as two
# digits, then MINOR and PATCH as one digit each: 0.3.0 -> 00.30, 0.4.0 -> 00.40, 1.2.3 -> 01.23). APP_VER must
# grow with every release (the console refuses to install an older APP_VER over a newer one).
set(APP_VERSION "0.4.0")
include(cmake/app_version.cmake)
app_pkg_version("${APP_VERSION}" APP_PKG_VERSION)   # 0.4.0 -> 00.40 (v0.3.0 was published with 00.30)
set(APP_TITLE_ID "ZIMT00001")      # unique: not PPLA00001 (pPlay), ZIMT00000 (old experiment), ZIMT00002 (test)
# shown on the PS4 home screen. Only the display name changed with the ZIMTV IPTV brand: the Title ID, content ID and
# /data/ZIMTV/ stay, so the PKG installs over earlier versions and keeps profiles, settings, history and downloads.
set(APP_PKG_TITLE "ZIMTV IPTV")
# The content ID's last part stays the one v0.2.0 was published with (IV0001-ZIMT00001_00-ZIMT000010002000): the
# release PKG keeps its name and installs over v0.2.0 as an update (same content ID, higher APP_VER). add_pkg would
# otherwise derive it from APP_PKG_VERSION.
set(PKG_CONTENT_VERSION "0002000")

set(PLATFORM_PS4 ON CACHE BOOL "Sony PS4 target platform" FORCE)
set(PPLAY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/pplay-reference" CACHE PATH "pPlay checkout")
if (NOT EXISTS "${PPLAY_DIR}/libcross2d/CMakeLists.txt")
    message(FATAL_ERROR "pPlay checkout not found at ${PPLAY_DIR} - run scripts/build-pplay-reference.ps1 first")
endif ()

add_subdirectory("${PPLAY_DIR}/libcross2d" "${CMAKE_BINARY_DIR}/libcross2d")

list(APPEND CMAKE_MODULE_PATH "${PPLAY_DIR}/cmake")
find_package(BZip2 REQUIRED)
find_package(MPV REQUIRED)
# FFmpeg 5.0 with HTTPS (mbedTLS 2.16-built, kept for playback TLS which already works on console; see build-ffmpeg-tls.sh).
# otherwise, so the prebuilt libmpv 0.34.1 links against it unchanged. Its libraries are found first
# (Findffmpeg keeps preset cache values).
set(ZIMTV_FFMPEG_TLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/toolchain/ffmpeg-5.0-tls" CACHE PATH "FFmpeg 5.0 + TLS prefix")
# TLS stack: the SDK's original PacBrew sysroot (mbedTLS 2.16.6 + libcurl 7.80.0, Jan 2022). The project-local
# 2.28.10 rebuild (Nov 2026) passed my offline x509 chain verification test with the PS4's own sources, yet on
# the console EVERY TLS handshake failed with BADCERT_NOT_TRUSTED (0x8) since the rebuild; the sysroot stack
# is the configuration the official PKG uses and it is verified working on the same console. Do not switch back
# without re-testing HTTPS on hardware.
set(ZIMTV_TLS_STACK_DIR "/root/zimtv-arquivos/opt/pacbrew/ps4/openorbis/usr" CACHE PATH "PacBrew sysroot (mbedTLS 2.16.6)")
set(ZIMTV_CURL_STACK_DIR "/root/zimtv-arquivos/opt/pacbrew/ps4/openorbis/usr" CACHE PATH "PacBrew sysroot (libcurl 7.80.0)")
if (NOT EXISTS "${ZIMTV_FFMPEG_TLS_DIR}/lib/libavformat.a")
    message(FATAL_ERROR "FFmpeg with HTTPS not found at ${ZIMTV_FFMPEG_TLS_DIR} - run scripts/build-ffmpeg-tls.sh")
endif ()
if (NOT EXISTS "${ZIMTV_CURL_STACK_DIR}/lib/libcurl.a" OR NOT EXISTS "${ZIMTV_TLS_STACK_DIR}/lib/libmbedtls.a")
    message(FATAL_ERROR "TLS stack not found at ${ZIMTV_TLS_STACK_DIR} / ${ZIMTV_CURL_STACK_DIR} - run scripts/build-curl-tls3.sh")
endif ()
foreach (_comp avfilter avcodec avformat avutil swscale swresample)
    set(${_comp}_INCLUDE_DIR "${ZIMTV_FFMPEG_TLS_DIR}/include" CACHE PATH "" FORCE)
    set(${_comp}_LIBRARY "${ZIMTV_FFMPEG_TLS_DIR}/lib/lib${_comp}.a" CACHE FILEPATH "" FORCE)
endforeach ()
find_package(ffmpeg COMPONENTS avfilter avcodec avformat avutil swscale swresample)
find_package(ass REQUIRED)
# FreeType (font coverage for the fallback fonts) and FriBidi (right-to-left text): already linked for libass
find_package(PkgConfig REQUIRED)
pkg_search_module(FREETYPE2 REQUIRED freetype2)

# build info: regenerated at every build (cmake/build_info.cmake), so the commit shown in About / Diagnostics / the log
# is always the one compiled - also after a commit without a new CMake configure run
set(BUILD_INFO_H "${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.h")
add_custom_target(${PROJECT_NAME}_build_info
        COMMAND ${CMAKE_COMMAND} "-DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}" "-DOUT_FILE=${BUILD_INFO_H}"
                "-DAPP_VERSION=${APP_VERSION}" "-DAPP_TITLE_ID=${APP_TITLE_ID}" "-DBUILD_TYPE=${CMAKE_BUILD_TYPE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_info.cmake"
        BYPRODUCTS "${BUILD_INFO_H}"
        VERBATIM)
execute_process(COMMAND ${CMAKE_COMMAND} "-DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}" "-DOUT_FILE=${BUILD_INFO_H}"
        "-DAPP_VERSION=${APP_VERSION}" "-DAPP_TITLE_ID=${APP_TITLE_ID}" "-DBUILD_TYPE=${CMAKE_BUILD_TYPE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_info.cmake")

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS src/*.cpp)
# unmodified pPlay sources needed by the pPlay Mpv wrapper (src/player/pplay)
set(PPLAY_UNMODIFIED_SOURCES "${PPLAY_DIR}/src/media_info.cpp" "${PPLAY_DIR}/src/utility.cpp")

add_executable(${PROJECT_NAME} ${APP_SOURCES} ${PPLAY_UNMODIFIED_SOURCES})
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_build_info)
set_source_files_properties(${PPLAY_UNMODIFIED_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wno-extra")
# ETAPO 2 (CORRIGIDO10): o app inclui <mbedtls/x509_crt.h> (src/images/image_loader.cpp) e headers
# do libcurl; o sysroot da toolchain ainda carrega os headers da stack OLD (mbedTLS 2.16.6 — struct
# mbedtls_x509_crt diferente). O prefix 2.28.10 precisa sombrear os headers OLD para todos os
# fontes do app (BEFORE, antes dos -isysroot/-I da toolchain).
target_include_directories(${PROJECT_NAME} BEFORE PRIVATE
        ${ZIMTV_TLS_STACK_DIR}/include
        ${ZIMTV_CURL_STACK_DIR}/include
        src
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${PPLAY_DIR}/src"
        ${MPV_INCLUDE_DIRS} ${ASS_INCLUDE_DIRS} ${FFMPEG_INCLUDE_DIRS} ${BZIP2_INCLUDE_DIR} ${FREETYPE2_INCLUDE_DIRS})
target_compile_options(${PROJECT_NAME} PRIVATE -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
# same language level as libcross2d (CMAKE_CXX_STANDARD 17) and the host tests (/std:c++17)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
# text rendering test screen at launch: a Debug-only diagnostic (hardware Checkpoint 1). Release builds never
# open it by themselves; it stays available in Settings > Diagnostics. scripts/verify-ps4iptv.ps1 checks that
# a release build was configured without it.
option(ZIMTV_DRAW_TRACE "Debug builds only: log every drawn object (pointer, type, bounds) once per frame" OFF)

if (ZIMTV_DRAW_TRACE)
    # the app sets the trace hook; the cross2d subdirectory reads the cache variable
    target_compile_definitions(${PROJECT_NAME} PRIVATE ZIMTV_DRAW_TRACE=1)
    set(ZIMTV_DRAW_TRACE "${ZIMTV_DRAW_TRACE}" CACHE BOOL "passes into the cross2d subdirectory" FORCE)
endif ()

option(ZIMTV_TEXT_TEST_AT_START "Debug builds only: show the text rendering test screen at launch" OFF)
if (ZIMTV_TEXT_TEST_AT_START)
    if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(FATAL_ERROR "ZIMTV_TEXT_TEST_AT_START is a Debug-only diagnostic and cannot be used in a "
                "${CMAKE_BUILD_TYPE} build")
    endif ()
    target_compile_definitions(${PROJECT_NAME} PRIVATE ZIMTV_TEXT_TEST_AT_START=1)
endif ()
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE ZIMTV_DEBUG=1)
endif ()

# pPlay PS4 link libraries (playback stack) + libcurl/mbedTLS for the Xtream API
target_link_libraries(${PROJECT_NAME}
        cross2d
        ${MPV_LIBRARY} ${ASS_LIBRARIES} ${FFMPEG_LIBRARIES} ${BZIP2_LIBRARIES}
        opus freetype fribidi png bz2
        ${ZIMTV_CURL_STACK_DIR}/lib/libcurl.a
        ${ZIMTV_TLS_STACK_DIR}/lib/libmbedtls.a
        ${ZIMTV_TLS_STACK_DIR}/lib/libmbedx509.a
        ${ZIMTV_TLS_STACK_DIR}/lib/libmbedcrypto.a
        z
        SceNet
        --whole-archive -lc --no-whole-archive)

###################
# PKG
###################
set(PKG_ROMFS "${CMAKE_BINARY_DIR}/pkg_romfs")
add_custom_target(${PROJECT_NAME}_data
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${PKG_ROMFS}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PKG_ROMFS}/sce_sys" "${PKG_ROMFS}/assets/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PPLAY_DIR}/data/ps4/romfs/sce_module" "${PKG_ROMFS}/sce_module"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PPLAY_DIR}/data/ps4/romfs/sce_sys/about" "${PKG_ROMFS}/sce_sys/about"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon0.png" "${PKG_ROMFS}/sce_sys/icon0.png"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/zimtv_hero.png" "${PKG_ROMFS}/assets/zimtv_hero.png"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/zimtv_logo.png" "${PKG_ROMFS}/assets/zimtv_logo.png"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/onboarding_bg.png" "${PKG_ROMFS}/assets/onboarding_bg.png"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/inter/Inter-Regular.ttf"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/inter/Inter-SemiBold.ttf"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts-fallback/DejaVuSans.ttf"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts-fallback/DroidSansFallback.ttf"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts-subtitles/ZIMSubtitles.ttf"
                "${PKG_ROMFS}/assets/fonts/"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cacert/cacert.pem" "${PKG_ROMFS}/assets/cacert.pem"
        VERBATIM)

add_self(${PROJECT_NAME})

# credential guard: runs after the executable and the romfs exist, before the PKG is built
add_custom_target(${PROJECT_NAME}_credential_guard
        COMMAND ${CMAKE_COMMAND}
                "-DSCAN_DIR=${PKG_ROMFS}"
                "-DSCAN_FILES=${CMAKE_BINARY_DIR}/eboot.bin|${CMAKE_BINARY_DIR}/${PROJECT_NAME}"
                "-DSECRET_SOURCES=${CMAKE_CURRENT_SOURCE_DIR}/config/test_streams.txt|${CMAKE_CURRENT_SOURCE_DIR}/config/forbidden_strings.txt"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/scan_credentials.cmake"
        VERBATIM)
add_dependencies(${PROJECT_NAME}_credential_guard ${PROJECT_NAME}_data ${PROJECT_NAME}_self)

add_pkg(${PROJECT_NAME} "${PKG_ROMFS}" ${APP_TITLE_ID} "${APP_PKG_TITLE}" ${APP_PKG_VERSION})
add_dependencies(${PROJECT_NAME}_pkg ${PROJECT_NAME}_credential_guard)
