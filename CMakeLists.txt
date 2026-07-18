cmake_minimum_required(VERSION 3.22)

# project() より前に、生成先として指定された CPU が x64 か確認する。
if(CMAKE_GENERATOR_PLATFORM)
    string(TOUPPER "${CMAKE_GENERATOR_PLATFORM}" AENGINE_GENERATOR_PLATFORM)
    if(NOT AENGINE_GENERATOR_PLATFORM STREQUAL "X64")
        message(FATAL_ERROR "aEngine 0.1 supports the x64 architecture only.")
    endif()
endif()

project(aEngine VERSION 0.1.0 LANGUAGES C CXX)

# 対応する OS と CPU か確認する。
if(NOT WIN32)
    message(FATAL_ERROR "aEngine 0.1 supports Windows only.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "aEngine 0.1 supports x64 builds only.")
endif()
if(MSVC)
    string(TOUPPER "${MSVC_CXX_ARCHITECTURE_ID}" AENGINE_MSVC_ARCHITECTURE_ID)
    if(NOT AENGINE_MSVC_ARCHITECTURE_ID STREQUAL "X64")
        message(FATAL_ERROR "aEngine 0.1 supports the x64 architecture only.")
    endif()
else()
    string(TOUPPER "${CMAKE_SYSTEM_PROCESSOR}" AENGINE_SYSTEM_PROCESSOR)
    if(NOT AENGINE_SYSTEM_PROCESSOR MATCHES "^(AMD64|X86_64)$")
        message(FATAL_ERROR "aEngine 0.1 supports the x64 architecture only.")
    endif()
endif()

include(CMakePackageConfigHelpers)

option(AENGINE_BUILD_MINIAUDIO "Build the miniaudio backend" ON)
option(AENGINE_BUILD_XAUDIO2 "Build the XAudio2 backend" ON)
option(AENGINE_BUILD_OPENAL "Build the OpenAL backend" ON)
option(AENGINE_BUILD_SHARED_LIBS "Build aEngine public component libraries as shared libraries" OFF)
option(AENGINE_FETCH_OPENAL_RUNTIME "Download verified OpenAL Soft binary/source archives and reconstruct the runtime tree" ON)

if(AENGINE_BUILD_SHARED_LIBS)
    set(AENGINE_PUBLIC_LIBRARY_TYPE SHARED)
else()
    set(AENGINE_PUBLIC_LIBRARY_TYPE STATIC)
endif()

set(AENGINE_BUILD_SAMPLES_DEFAULT OFF)
if(PROJECT_IS_TOP_LEVEL)
    set(AENGINE_BUILD_SAMPLES_DEFAULT ON)
endif()
option(AENGINE_BUILD_SAMPLES "Build aEngine samples" ${AENGINE_BUILD_SAMPLES_DEFAULT})

# 外部ライブラリと実行用ファイルの場所をまとめる。
set(AENGINE_MINIAUDIO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/miniaudio" CACHE PATH "Path to the pinned miniaudio source tree")
set(AENGINE_OPENAL_RUNTIME_ROOT "${CMAKE_CURRENT_BINARY_DIR}/ThirdPartyRuntime/OpenAL" CACHE PATH "Path to the OpenAL Soft runtime tree")
set(AENGINE_OPENAL_RUNTIME_VERSION "1.25.2" CACHE INTERNAL "Pinned OpenAL Soft runtime version" FORCE)
set(AENGINE_OPENAL_RUNTIME_URL "https://openal-soft.org/openal-binaries/openal-soft-${AENGINE_OPENAL_RUNTIME_VERSION}-bin.zip")
set(AENGINE_OPENAL_RUNTIME_SHA256 "67A0C4B800BD860C93C04F38CAF8CBE4875F9C84700AC430EFC451F70E265434")
set(AENGINE_OPENAL_RUNTIME_DLL_SHA256 "3963B06E319180700BE0BCC0D027336664BB3A4975876B4A5442F10746FAB5B4")
set(AENGINE_OPENAL_SOURCE_URL "https://openal-soft.org/openal-releases/openal-soft-${AENGINE_OPENAL_RUNTIME_VERSION}.tar.bz2")
set(AENGINE_OPENAL_SOURCE_SHA256 "1DBAAC44E7579D5BC8847CA8DB4B2E8B9FD3961041F35EE20DEF4958301E1089")
set(AENGINE_OPENAL_RUNTIME_DLL "${AENGINE_OPENAL_RUNTIME_ROOT}/Win64/OpenAL32.dll")
set(AENGINE_OPENAL_SOURCE_ARCHIVE "${AENGINE_OPENAL_RUNTIME_ROOT}/Source/openal-soft-${AENGINE_OPENAL_RUNTIME_VERSION}.tar.bz2")

# OpenAL の配布ファイルを検証して用意する。
function(aengine_prepare_openal_runtime)
    if(NOT AENGINE_FETCH_OPENAL_RUNTIME)
        if(EXISTS "${AENGINE_OPENAL_RUNTIME_DLL}"
            AND EXISTS "${AENGINE_OPENAL_RUNTIME_ROOT}/COPYING"
            AND EXISTS "${AENGINE_OPENAL_RUNTIME_ROOT}/LICENSE-pffft"
            AND EXISTS "${AENGINE_OPENAL_RUNTIME_ROOT}/include/AL/al.h"
            AND EXISTS "${AENGINE_OPENAL_RUNTIME_ROOT}/include/AL/alc.h"
            AND EXISTS "${AENGINE_OPENAL_SOURCE_ARCHIVE}")
            file(SHA256 "${AENGINE_OPENAL_SOURCE_ARCHIVE}" supplied_source_sha256)
            string(TOUPPER "${supplied_source_sha256}" supplied_source_sha256)
            if(NOT supplied_source_sha256 STREQUAL AENGINE_OPENAL_SOURCE_SHA256)
                message(FATAL_ERROR "The supplied OpenAL Soft source archive does not match the pinned SHA-256 value.")
            endif()
            file(SHA256 "${AENGINE_OPENAL_RUNTIME_DLL}" supplied_runtime_dll_sha256)
            string(TOUPPER "${supplied_runtime_dll_sha256}" supplied_runtime_dll_sha256)
            if(NOT supplied_runtime_dll_sha256 STREQUAL AENGINE_OPENAL_RUNTIME_DLL_SHA256)
                message(FATAL_ERROR "The supplied OpenAL Soft runtime DLL does not match the pinned unmodified binary.")
            endif()
            return()
        endif()
        message(FATAL_ERROR "OpenAL Soft runtime was not found at ${AENGINE_OPENAL_RUNTIME_ROOT}.")
    endif()

    set(download_directory "${CMAKE_CURRENT_BINARY_DIR}/ThirdPartyDownloads")
    set(download_file "${download_directory}/openal-soft-${AENGINE_OPENAL_RUNTIME_VERSION}-bin.zip")
    set(source_download_file "${download_directory}/openal-soft-${AENGINE_OPENAL_RUNTIME_VERSION}.tar.bz2")
    set(extract_directory "${CMAKE_CURRENT_BINARY_DIR}/ThirdPartyRuntime/OpenALExtract")
    set(extracted_root "${extract_directory}/openal-soft-${AENGINE_OPENAL_RUNTIME_VERSION}-bin")
    file(MAKE_DIRECTORY
        "${download_directory}"
        "${AENGINE_OPENAL_RUNTIME_ROOT}/Win64"
        "${AENGINE_OPENAL_RUNTIME_ROOT}/Source")

    if(EXISTS "${download_file}")
        file(SHA256 "${download_file}" cached_archive_sha256)
        string(TOUPPER "${cached_archive_sha256}" cached_archive_sha256)
        if(NOT cached_archive_sha256 STREQUAL AENGINE_OPENAL_RUNTIME_SHA256)
            message(WARNING "Removing an OpenAL Soft archive whose SHA-256 does not match the pinned value.")
            file(REMOVE "${download_file}")
        endif()
    endif()

    if(NOT EXISTS "${download_file}")
        message(STATUS "Downloading OpenAL Soft runtime ${AENGINE_OPENAL_RUNTIME_VERSION}")
        file(DOWNLOAD "${AENGINE_OPENAL_RUNTIME_URL}" "${download_file}"
            EXPECTED_HASH "SHA256=${AENGINE_OPENAL_RUNTIME_SHA256}"
            SHOW_PROGRESS
            STATUS download_status)
        list(GET download_status 0 download_result)
        if(NOT download_result EQUAL 0)
            list(GET download_status 1 download_message)
            message(FATAL_ERROR "Failed to download OpenAL Soft runtime: ${download_message}")
        endif()
    endif()

    if(EXISTS "${source_download_file}")
        file(SHA256 "${source_download_file}" cached_source_sha256)
        string(TOUPPER "${cached_source_sha256}" cached_source_sha256)
        if(NOT cached_source_sha256 STREQUAL AENGINE_OPENAL_SOURCE_SHA256)
            message(WARNING "Removing an OpenAL Soft source archive whose SHA-256 does not match the pinned value.")
            file(REMOVE "${source_download_file}")
        endif()
    endif()

    if(NOT EXISTS "${source_download_file}")
        message(STATUS "Downloading OpenAL Soft source ${AENGINE_OPENAL_RUNTIME_VERSION}")
        file(DOWNLOAD "${AENGINE_OPENAL_SOURCE_URL}" "${source_download_file}"
            EXPECTED_HASH "SHA256=${AENGINE_OPENAL_SOURCE_SHA256}"
            SHOW_PROGRESS
            STATUS source_download_status)
        list(GET source_download_status 0 source_download_result)
        if(NOT source_download_result EQUAL 0)
            list(GET source_download_status 1 source_download_message)
            message(FATAL_ERROR "Failed to download OpenAL Soft source: ${source_download_message}")
        endif()
    endif()

    file(SHA256 "${download_file}" verified_archive_sha256)
    string(TOUPPER "${verified_archive_sha256}" verified_archive_sha256)
    if(NOT verified_archive_sha256 STREQUAL AENGINE_OPENAL_RUNTIME_SHA256)
        message(FATAL_ERROR "The cached OpenAL Soft archive does not match the pinned SHA-256 value.")
    endif()
    file(SHA256 "${source_download_file}" verified_source_sha256)
    string(TOUPPER "${verified_source_sha256}" verified_source_sha256)
    if(NOT verified_source_sha256 STREQUAL AENGINE_OPENAL_SOURCE_SHA256)
        message(FATAL_ERROR "The cached OpenAL Soft source archive does not match the pinned SHA-256 value.")
    endif()

    file(REMOVE_RECURSE "${extract_directory}")
    file(MAKE_DIRECTORY "${extract_directory}")
    file(ARCHIVE_EXTRACT INPUT "${download_file}" DESTINATION "${extract_directory}")
    file(COPY_FILE "${extracted_root}/bin/Win64/soft_oal.dll" "${AENGINE_OPENAL_RUNTIME_DLL}" ONLY_IF_DIFFERENT)
    file(COPY "${extracted_root}/include/" DESTINATION "${AENGINE_OPENAL_RUNTIME_ROOT}/include")
    file(COPY_FILE "${extracted_root}/COPYING" "${AENGINE_OPENAL_RUNTIME_ROOT}/COPYING" ONLY_IF_DIFFERENT)
    file(COPY_FILE "${extracted_root}/LICENSE-pffft" "${AENGINE_OPENAL_RUNTIME_ROOT}/LICENSE-pffft" ONLY_IF_DIFFERENT)
    file(COPY_FILE "${extracted_root}/readme.txt" "${AENGINE_OPENAL_RUNTIME_ROOT}/readme.txt" ONLY_IF_DIFFERENT)
    file(COPY_FILE "${source_download_file}" "${AENGINE_OPENAL_SOURCE_ARCHIVE}" ONLY_IF_DIFFERENT)
    file(REMOVE_RECURSE "${extract_directory}")
endfunction()

if(AENGINE_BUILD_OPENAL)
    aengine_prepare_openal_runtime()
endif()

# 実行ファイルパスへ DLL とライセンスをコピーする。
function(aengine_stage_runtime_dependencies target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "aengine_stage_runtime_dependencies requires an existing target.")
    endif()

    add_custom_command(TARGET "${target_name}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target_name}>/ThirdParty"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/LICENSE" "$<TARGET_FILE_DIR:${target_name}>/ThirdParty/aEngine-LICENSE.txt"
        VERBATIM)

    if(AENGINE_BUILD_SHARED_LIBS)
        add_custom_command(TARGET "${target_name}" POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target_name}>
                "$<TARGET_FILE_DIR:${target_name}>"
            COMMAND_EXPAND_LISTS
            VERBATIM)
    endif()

    if(AENGINE_BUILD_MINIAUDIO)
        add_custom_command(TARGET "${target_name}" POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${AENGINE_MINIAUDIO_ROOT}/LICENSE" "$<TARGET_FILE_DIR:${target_name}>/ThirdParty/miniaudio-LICENSE.txt"
            VERBATIM)
    endif()

    set(aengine_openal_runtime_dll "${AENGINE_OPENAL_RUNTIME_ROOT}/Win64/OpenAL32.dll")
    set(aengine_openal_source_archive "${AENGINE_OPENAL_RUNTIME_ROOT}/Source/openal-soft-${AENGINE_OPENAL_RUNTIME_VERSION}.tar.bz2")
    if(AENGINE_BUILD_OPENAL AND EXISTS "${aengine_openal_runtime_dll}")
        add_custom_command(TARGET "${target_name}" POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${aengine_openal_runtime_dll}" "$<TARGET_FILE_DIR:${target_name}>/ThirdParty/OpenAL32.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${AENGINE_OPENAL_RUNTIME_ROOT}/COPYING" "$<TARGET_FILE_DIR:${target_name}>/ThirdParty/OpenAL-COPYING.txt"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${AENGINE_OPENAL_RUNTIME_ROOT}/LICENSE-pffft" "$<TARGET_FILE_DIR:${target_name}>/ThirdParty/OpenAL-LICENSE-pffft.txt"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${aengine_openal_source_archive}" "$<TARGET_FILE_DIR:${target_name}>/ThirdParty/openal-soft-${AENGINE_OPENAL_RUNTIME_VERSION}-source.tar.bz2"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ThirdParty/OpenAL-SOURCE.txt" "$<TARGET_FILE_DIR:${target_name}>/ThirdParty/OpenAL-SOURCE.txt"
            VERBATIM)
    endif()
endfunction()

# 各ライブラリで共通する C++ の設定を適用する。
function(aengine_configure_target target_name)
    target_compile_features("${target_name}" PUBLIC cxx_std_23)
    target_include_directories("${target_name}"
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options("${target_name}" PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    endif()
endfunction()

function(aengine_configure_public_library target_name export_definition)
    aengine_configure_target("${target_name}")
    if(AENGINE_BUILD_SHARED_LIBS)
        target_compile_definitions("${target_name}" PRIVATE "${export_definition}=1")
    else()
        target_compile_definitions("${target_name}" PUBLIC AENGINE_STATIC=1)
    endif()
endfunction()

# Core と選択されたバックエンドのライブラリを作る。
add_library(aEngineCore ${AENGINE_PUBLIC_LIBRARY_TYPE}
    src/Core/AudioEngineBase.cpp
    src/Core/BackendRegistry.cpp
    src/Core/Types.cpp)
add_library(aEngine::Core ALIAS aEngineCore)
set_target_properties(aEngineCore PROPERTIES EXPORT_NAME Core DEBUG_POSTFIX "d")
aengine_configure_public_library(aEngineCore AENGINE_CORE_EXPORTS)

if(AENGINE_BUILD_MINIAUDIO)
    if(NOT EXISTS "${AENGINE_MINIAUDIO_ROOT}/miniaudio.c" OR NOT EXISTS "${AENGINE_MINIAUDIO_ROOT}/miniaudio.h")
        message(FATAL_ERROR "miniaudio is required at ${AENGINE_MINIAUDIO_ROOT}. Initialize aEngine submodules recursively.")
    endif()

    add_library(aEngineMiniaudio ${AENGINE_PUBLIC_LIBRARY_TYPE}
        src/Backends/Miniaudio/MiniaudioBackend.cpp
        "${AENGINE_MINIAUDIO_ROOT}/miniaudio.c")
    add_library(aEngine::Miniaudio ALIAS aEngineMiniaudio)
    set_target_properties(aEngineMiniaudio PROPERTIES EXPORT_NAME Miniaudio DEBUG_POSTFIX "d")
    aengine_configure_public_library(aEngineMiniaudio AENGINE_MINIAUDIO_EXPORTS)
    target_include_directories(aEngineMiniaudio PRIVATE "${AENGINE_MINIAUDIO_ROOT}")
    target_compile_definitions(aEngineMiniaudio PRIVATE MA_NO_NULL)
    target_link_libraries(aEngineMiniaudio PUBLIC aEngineCore)
    set_source_files_properties("${AENGINE_MINIAUDIO_ROOT}/miniaudio.c" PROPERTIES
        COMPILE_OPTIONS "$<$<C_COMPILER_ID:MSVC>:/W0>")
endif()

if(AENGINE_BUILD_XAUDIO2 OR AENGINE_BUILD_OPENAL)
    add_library(aEngineWindowsSupport STATIC
        src/Platform/Windows/DynamicLibrary.cpp
        src/Platform/Windows/MediaFoundationDecoder.cpp)
    set_target_properties(aEngineWindowsSupport PROPERTIES EXPORT_NAME WindowsSupport DEBUG_POSTFIX "d")
    aengine_configure_target(aEngineWindowsSupport)
    target_compile_definitions(aEngineWindowsSupport PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(aEngineWindowsSupport
        PUBLIC aEngineCore
        PRIVATE ole32 mfplat mfreadwrite mfuuid)
endif()

if(AENGINE_BUILD_XAUDIO2)
    add_library(aEngineXAudio2 ${AENGINE_PUBLIC_LIBRARY_TYPE}
        src/Backends/XAudio2/RegisterXAudio2Backend.cpp
        src/Backends/XAudio2/XAudio2Backend.cpp
        src/Backends/XAudio2/XAudio2Backend.h)
    add_library(aEngine::XAudio2 ALIAS aEngineXAudio2)
    set_target_properties(aEngineXAudio2 PROPERTIES EXPORT_NAME XAudio2 DEBUG_POSTFIX "d")
    aengine_configure_public_library(aEngineXAudio2 AENGINE_XAUDIO2_EXPORTS)
    target_compile_definitions(aEngineXAudio2 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(aEngineXAudio2 PUBLIC aEngineCore PRIVATE aEngineWindowsSupport)
endif()

if(AENGINE_BUILD_OPENAL)
    add_library(aEngineOpenAL ${AENGINE_PUBLIC_LIBRARY_TYPE}
        src/Backends/OpenAL/OpenALAudioEngine.cpp
        src/Backends/OpenAL/OpenALAudioEngine.h
        src/Backends/OpenAL/RegisterOpenALBackend.cpp)
    add_library(aEngine::OpenAL ALIAS aEngineOpenAL)
    set_target_properties(aEngineOpenAL PROPERTIES EXPORT_NAME OpenAL DEBUG_POSTFIX "d")
    aengine_configure_public_library(aEngineOpenAL AENGINE_OPENAL_EXPORTS)
    target_include_directories(aEngineOpenAL PRIVATE "${AENGINE_OPENAL_RUNTIME_ROOT}/include")
    target_compile_definitions(aEngineOpenAL PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(aEngineOpenAL PUBLIC aEngineCore PRIVATE aEngineWindowsSupport)
endif()

add_library(aEngineAllBackends ${AENGINE_PUBLIC_LIBRARY_TYPE} src/Backends/All/RegisterAllBackends.cpp)
add_library(aEngine::AllBackends ALIAS aEngineAllBackends)
set_target_properties(aEngineAllBackends PROPERTIES EXPORT_NAME AllBackends DEBUG_POSTFIX "d")
aengine_configure_public_library(aEngineAllBackends AENGINE_ALL_BACKENDS_EXPORTS)
target_link_libraries(aEngineAllBackends PUBLIC aEngineCore)
if(AENGINE_BUILD_MINIAUDIO)
    target_compile_definitions(aEngineAllBackends PRIVATE AENGINE_HAS_MINIAUDIO=1)
    target_link_libraries(aEngineAllBackends PUBLIC aEngineMiniaudio)
endif()
if(AENGINE_BUILD_XAUDIO2)
    target_compile_definitions(aEngineAllBackends PRIVATE AENGINE_HAS_XAUDIO2=1)
    target_link_libraries(aEngineAllBackends PUBLIC aEngineXAudio2)
endif()
if(AENGINE_BUILD_OPENAL)
    target_compile_definitions(aEngineAllBackends PRIVATE AENGINE_HAS_OPENAL=1)
    target_link_libraries(aEngineAllBackends PUBLIC aEngineOpenAL)
endif()

# 動作確認用のサンプルを作る。
if(AENGINE_BUILD_SAMPLES)
    add_executable(aEngineBasicPlayback samples/BasicPlayback/main.cpp)
    target_compile_features(aEngineBasicPlayback PRIVATE cxx_std_23)
    target_link_libraries(aEngineBasicPlayback PRIVATE aEngineAllBackends)
    aengine_stage_runtime_dependencies(aEngineBasicPlayback)
endif()

# find_package() で読み込める CMake パッケージを作る。
set(AENGINE_INSTALL_CMAKEDIR "lib/cmake/aEngine")
configure_package_config_file(
    cmake/aEngineConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/aEngineConfig.cmake"
    INSTALL_DESTINATION "${AENGINE_INSTALL_CMAKEDIR}")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/aEngineConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion)

# ライブラリ、ヘッダー、ドキュメント、ライセンスをインストールする。
set(AENGINE_INSTALL_TARGETS aEngineCore aEngineAllBackends)
if(AENGINE_BUILD_MINIAUDIO)
    list(APPEND AENGINE_INSTALL_TARGETS aEngineMiniaudio)
endif()
if(AENGINE_BUILD_XAUDIO2 OR AENGINE_BUILD_OPENAL)
    list(APPEND AENGINE_INSTALL_TARGETS aEngineWindowsSupport)
endif()
if(AENGINE_BUILD_XAUDIO2)
    list(APPEND AENGINE_INSTALL_TARGETS aEngineXAudio2)
endif()
if(AENGINE_BUILD_OPENAL)
    list(APPEND AENGINE_INSTALL_TARGETS aEngineOpenAL)
endif()
install(TARGETS ${AENGINE_INSTALL_TARGETS} EXPORT aEngineTargets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT aEngineTargets
    FILE aEngineTargets.cmake
    NAMESPACE aEngine::
    DESTINATION "${AENGINE_INSTALL_CMAKEDIR}")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/aEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/aEngineConfigVersion.cmake"
    DESTINATION "${AENGINE_INSTALL_CMAKEDIR}")
install(FILES LICENSE DESTINATION share/aEngine)
install(FILES README.md CHANGELOG.md DESTINATION share/aEngine)
install(DIRECTORY docs/ DESTINATION share/aEngine/docs)
install(FILES ThirdParty/README.md DESTINATION share/aEngine/ThirdParty)
if(AENGINE_BUILD_MINIAUDIO)
    install(FILES "${AENGINE_MINIAUDIO_ROOT}/LICENSE" DESTINATION share/aEngine/ThirdParty RENAME miniaudio-LICENSE.txt)
endif()
if(AENGINE_BUILD_OPENAL)
    install(FILES "${AENGINE_OPENAL_RUNTIME_DLL}" DESTINATION bin)
    install(FILES
        "${AENGINE_OPENAL_RUNTIME_ROOT}/COPYING"
        "${AENGINE_OPENAL_RUNTIME_ROOT}/LICENSE-pffft"
        DESTINATION share/aEngine/ThirdParty)
    install(FILES "${AENGINE_OPENAL_SOURCE_ARCHIVE}"
        DESTINATION share/aEngine/ThirdParty
        RENAME "openal-soft-${AENGINE_OPENAL_RUNTIME_VERSION}-source.tar.bz2")
    install(FILES ThirdParty/OpenAL-SOURCE.txt DESTINATION share/aEngine/ThirdParty)
endif()
