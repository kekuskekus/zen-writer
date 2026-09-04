cmake_minimum_required(VERSION 3.20)

project(zen-writer VERSION 0.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HUNSPELL IMPORTED_TARGET hunspell)
endif()

add_executable(zen-writer
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
    src/editor_highlighter.cpp
    src/editor_highlighter.h
    src/text_utils.cpp
    src/text_utils.h
)

target_link_libraries(zen-writer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

if(HUNSPELL_FOUND)
    if(NOT WIN32)
        target_compile_definitions(zen-writer PRIVATE ZEN_WRITER_HAS_HUNSPELL=1)
        target_link_libraries(zen-writer PRIVATE PkgConfig::HUNSPELL)
    endif()
endif()

if(WIN32)
    target_compile_definitions(zen-writer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(zen-writer PRIVATE ole32)
    set_target_properties(zen-writer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_compile_definitions(zen-writer PRIVATE
    ZEN_WRITER_VERSION="${PROJECT_VERSION}"
)

install(TARGETS zen-writer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES resources/zen-writer.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

option(ZEN_WRITER_BUILD_TESTS "Build Zen Writer tests" ON)
if(ZEN_WRITER_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    add_executable(zen-writer-tests
        tests/text_utils_test.cpp
        src/text_utils.cpp
        src/text_utils.h
    )
    target_include_directories(zen-writer-tests PRIVATE src)
    target_link_libraries(zen-writer-tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME zen-writer-tests COMMAND zen-writer-tests)

    if(UNIX AND NOT APPLE)
        add_executable(zen-writer-startup-tests tests/startup_test.cpp)
        target_link_libraries(zen-writer-startup-tests PRIVATE Qt6::Core Qt6::Test)
        target_compile_definitions(zen-writer-startup-tests PRIVATE
            ZEN_WRITER_EXECUTABLE="$<TARGET_FILE:zen-writer>"
            ZEN_WRITER_DESKTOP_FILE="${CMAKE_CURRENT_SOURCE_DIR}/resources/zen-writer.desktop"
        )
        add_dependencies(zen-writer-startup-tests zen-writer)
        add_test(NAME zen-writer-startup-tests COMMAND zen-writer-startup-tests)
        set_tests_properties(zen-writer-startup-tests PROPERTIES TIMEOUT 45)
    endif()
endif()
