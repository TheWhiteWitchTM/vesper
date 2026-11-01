# Release builds use this file. scripts/build.sh still uses qmake (vesper.pro).
cmake_minimum_required(VERSION 3.21)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VESPER_VERSION_TEXT)
string(STRIP "${VESPER_VERSION_TEXT}" VESPER_VERSION_TEXT)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" VESPER_VERSION_TRIPLE "${VESPER_VERSION_TEXT}")
if(NOT VESPER_VERSION_TRIPLE)
    message(FATAL_ERROR "VERSION must start with major.minor.patch")
endif()

string(TOLOWER "${VESPER_VERSION_TEXT}" VESPER_VERSION_SLUG)
string(REGEX REPLACE "[ \t_]+" "-" VESPER_VERSION_SLUG "${VESPER_VERSION_SLUG}")
string(REGEX REPLACE "[^a-z0-9.-]" "" VESPER_VERSION_SLUG "${VESPER_VERSION_SLUG}")

project(vesper VERSION ${VESPER_VERSION_TRIPLE} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network Sql Concurrent)

set(VESPER_DBUS OFF)
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.2 REQUIRED COMPONENTS DBus)
    set(VESPER_DBUS ON)
endif()

file(GLOB_RECURSE VESPER_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
if(NOT VESPER_DBUS)
    list(FILTER VESPER_SOURCES EXCLUDE REGEX "/mpris\\.(cpp|h)$")
endif()
add_executable(vesper ${VESPER_SOURCES} resources.qrc)
# The Linux host rcc would emit a zstd hook a cross-built Qt may not load.
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL CMAKE_SYSTEM_NAME)
    set_target_properties(vesper PROPERTIES AUTORCC_OPTIONS "--no-zstd")
endif()
target_include_directories(vesper PRIVATE src)
target_compile_options(vesper PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(vesper PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Sql Qt6::Concurrent)
if(VESPER_DBUS)
    target_link_libraries(vesper PRIVATE Qt6::DBus)
endif()

# Headers are the system copy of libVLC 3.0. The Windows and Mac binaries
# are the matching VideoLAN builds, staged by scripts/fetch-release-deps.sh.
set(VESPER_VLC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/release/vlc-headers")
if(WIN32 OR APPLE)
    if(NOT VESPER_VLC_ROOT OR NOT VESPER_TAGLIB_ROOT)
        message(FATAL_ERROR "vesper: set VESPER_VLC_ROOT and VESPER_TAGLIB_ROOT")
    endif()
    target_include_directories(vesper PRIVATE
        "${VESPER_VLC_HEADERS}"
        "${VESPER_TAGLIB_ROOT}/include")
    # Static TagLib. Without this the headers dllimport every symbol.
    target_compile_definitions(vesper PRIVATE TAGLIB_STATIC)
    if(WIN32)
        target_link_libraries(vesper PRIVATE
            "${VESPER_VLC_ROOT}/libvlc.dll.a"
            "${VESPER_VLC_ROOT}/libvlccore.dll.a"
            "${VESPER_TAGLIB_ROOT}/lib/libtag.a")
    else()
        target_link_libraries(vesper PRIVATE
            "${VESPER_VLC_ROOT}/lib/libvlc.dylib"
            "${VESPER_TAGLIB_ROOT}/lib/libtag.a")
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBVLC REQUIRED libvlc)
    pkg_check_modules(TAGLIB REQUIRED taglib)
    target_include_directories(vesper PRIVATE ${LIBVLC_INCLUDE_DIRS} ${TAGLIB_INCLUDE_DIRS})
    target_link_directories(vesper PRIVATE ${LIBVLC_LIBRARY_DIRS} ${TAGLIB_LIBRARY_DIRS})
    target_link_libraries(vesper PRIVATE ${LIBVLC_LIBRARIES} ${TAGLIB_LIBRARIES})
endif()

# dist/bin/vesper finds dist/lib first, then the Qt it was built with.
# The Mac app finds Frameworks next to Contents/MacOS.
if(WIN32)
    set_target_properties(vesper PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    set_target_properties(vesper PROPERTIES
        BUILD_RPATH "@executable_path/../Frameworks"
        INSTALL_RPATH "@executable_path/../Frameworks")
else()
    set_target_properties(vesper PROPERTIES
        BUILD_RPATH "\$ORIGIN/../lib;${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}"
        INSTALL_RPATH "\$ORIGIN/../lib")
    target_link_options(vesper PRIVATE -Wl,--disable-new-dtags)
endif()

include(GNUInstallDirs)
install(TARGETS vesper RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

set(CPACK_PACKAGE_NAME "vesper")
set(CPACK_PACKAGE_VENDOR "Mira (The White Witch)")
set(CPACK_PACKAGE_VERSION "${VESPER_VERSION_TRIPLE}")
if(WIN32)
    set(CPACK_PACKAGE_FILE_NAME "vesper-${VESPER_VERSION_SLUG}-windows-x86_64")
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_PACKAGE_FILE_NAME "vesper-${VESPER_VERSION_SLUG}-macos-arm64")
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_PACKAGE_FILE_NAME "vesper-${VESPER_VERSION_SLUG}-linux-x86_64")
    set(CPACK_GENERATOR "TGZ")
endif()
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Music and video for the quiet hours")
set(CPACK_PACKAGE_DESCRIPTION "Vesper ${VESPER_VERSION_TEXT}.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
include(CPack)
