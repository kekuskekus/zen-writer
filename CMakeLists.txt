cmake_minimum_required(VERSION 3.20)

project(zen-writer VERSION 0.1.0 LANGUAGES CXX)

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
    target_compile_definitions(zen-writer PRIVATE ZEN_WRITER_HAS_HUNSPELL=1)
    target_link_libraries(zen-writer PRIVATE PkgConfig::HUNSPELL)
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
endif()
