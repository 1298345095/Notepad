cmake_minimum_required(VERSION 3.24)

project(Notepad VERSION 0.1.0 LANGUAGES CXX)

option(NOTEPAD_BUILD_GUI "Build the wxWidgets desktop application" ON)
option(NOTEPAD_FETCH_WX "Build the pinned wxWidgets source release" OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Match the CRT used by the statically linked wxWidgets build on Windows.
if(MSVC AND NOTEPAD_BUILD_GUI AND NOTEPAD_FETCH_WX)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(CTest)

add_library(notepad_core STATIC
    src/DocumentIO.cpp
    src/DiffLexer.cpp
)
target_include_directories(notepad_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(notepad_core PUBLIC cxx_std_20)
set_target_properties(notepad_core PROPERTIES CXX_EXTENSIONS OFF)

function(notepad_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
notepad_warnings(notepad_core)

if(BUILD_TESTING)
    foreach(test_name IN ITEMS document_io_test diff_lexer_test)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE notepad_core)
        notepad_warnings(${test_name})
        # Keep assert-based checks active in the Release configuration too.
        if(MSVC)
            target_compile_options(${test_name} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${test_name} PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()

if(NOTEPAD_BUILD_GUI)
    if(NOTEPAD_FETCH_WX)
        include(FetchContent)
        set(wxBUILD_SHARED OFF CACHE BOOL "Static wxWidgets" FORCE)
        set(wxBUILD_MONOLITHIC OFF CACHE BOOL "Separate wx components" FORCE)
        set(wxBUILD_INSTALL OFF CACHE BOOL "Do not install the wx development tree" FORCE)
        set(wxBUILD_SAMPLES OFF CACHE STRING "No wx samples" FORCE)
        set(wxBUILD_TESTS OFF CACHE STRING "No wx tests" FORCE)
        set(wxBUILD_DEMOS OFF CACHE BOOL "No wx demos" FORCE)
        set(wxBUILD_BENCHMARKS OFF CACHE BOOL "No wx benchmarks" FORCE)
        set(wxUSE_STC ON CACHE BOOL "Scintilla editing control" FORCE)
        set(wxUSE_SYS_LIBS OFF CACHE BOOL "Use bundled optional codec libraries" FORCE)
        foreach(feature IN ITEMS WEBVIEW MEDIACTRL RICHTEXT RIBBON AUI PROPGRID XRC HTML WXHTML_HELP OPENGL)
            set(wxUSE_${feature} OFF CACHE BOOL "Unused by this editor" FORCE)
        endforeach()
        if(MSVC)
            set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "Static MSVC runtime" FORCE)
        endif()
        # The official release archive includes third-party submodules; GitHub's
        # automatically generated tag archives do not. Digest: release asset API.
        FetchContent_Declare(wxWidgets
            URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.12/wxWidgets-3.2.12.tar.bz2
            URL_HASH SHA256=a62719bb5e1dcc41c1a6fc1eecd499c7ee8521f70402c7b1bb3342a1e16344e0
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(wxWidgets)
        set(NOTEPAD_WX_LIBRARIES wx::stc wx::core wx::base)
    else()
        find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base stc)
        include(${wxWidgets_USE_FILE})
        set(NOTEPAD_WX_LIBRARIES ${wxWidgets_LIBRARIES})
    endif()

    add_executable(Notepad WIN32 MACOSX_BUNDLE
        src/App.cpp
        src/MainFrame.cpp
        src/EditorCtrl.cpp
        src/SmokeTest.cpp
    )
    target_link_libraries(Notepad PRIVATE notepad_core ${NOTEPAD_WX_LIBRARIES})
    target_compile_definitions(Notepad PRIVATE NOTEPAD_VERSION="${PROJECT_VERSION}")
    notepad_warnings(Notepad)
    set_target_properties(Notepad PROPERTIES
        CXX_EXTENSIONS OFF
        MACOSX_BUNDLE_BUNDLE_NAME Notepad
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.notepad.prototype
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )

    if(WIN32)
        set(NOTEPAD_RUNTIME_DESTINATION .)
    else()
        set(NOTEPAD_RUNTIME_DESTINATION bin)
    endif()
    install(TARGETS Notepad
        RUNTIME DESTINATION "${NOTEPAD_RUNTIME_DESTINATION}"
        BUNDLE DESTINATION .
    )
    if(UNIX AND NOT APPLE)
        set(NOTEPAD_DOC_DESTINATION share/doc/notepad-prototype)
    else()
        set(NOTEPAD_DOC_DESTINATION .)
    endif()
    install(FILES README.md docs/BUILDING.md DESTINATION "${NOTEPAD_DOC_DESTINATION}")
    foreach(notice IN ITEMS LICENSE LICENSE.txt THIRD_PARTY_NOTICES.md)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${notice}")
            install(FILES "${notice}" DESTINATION "${NOTEPAD_DOC_DESTINATION}")
        endif()
    endforeach()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/NOTEPAD4_MIGRATION.md")
        install(FILES docs/NOTEPAD4_MIGRATION.md DESTINATION "${NOTEPAD_DOC_DESTINATION}")
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/scintilla/License.txt")
        install(FILES third_party/scintilla/License.txt
            DESTINATION "${NOTEPAD_DOC_DESTINATION}/licenses/notepad4-scintilla")
    endif()
    if(NOTEPAD_FETCH_WX)
        install(FILES
            "${wxwidgets_SOURCE_DIR}/docs/licence.txt"
            "${wxwidgets_SOURCE_DIR}/docs/lgpl.txt"
            DESTINATION "${NOTEPAD_DOC_DESTINATION}/licenses/wxWidgets")
        install(FILES "${wxwidgets_SOURCE_DIR}/src/stc/scintilla/License.txt"
            DESTINATION "${NOTEPAD_DOC_DESTINATION}/licenses/Scintilla")
        # Preserve notices for bundled wx dependencies without shipping their
        # source trees. Some vendors place their license in the root README.
        foreach(vendor IN ITEMS jpeg png tiff zlib expat regex nanosvg)
            file(GLOB_RECURSE vendor_notices LIST_DIRECTORIES FALSE
                "${wxwidgets_SOURCE_DIR}/src/${vendor}/LICENSE*"
                "${wxwidgets_SOURCE_DIR}/src/${vendor}/LICENCE*"
                "${wxwidgets_SOURCE_DIR}/src/${vendor}/COPYING*"
                "${wxwidgets_SOURCE_DIR}/src/${vendor}/COPYRIGHT*"
                "${wxwidgets_SOURCE_DIR}/src/${vendor}/Copyright*")
            if(EXISTS "${wxwidgets_SOURCE_DIR}/src/${vendor}/README")
                list(APPEND vendor_notices "${wxwidgets_SOURCE_DIR}/src/${vendor}/README")
            endif()
            foreach(notice IN LISTS vendor_notices)
                file(RELATIVE_PATH notice_relative "${wxwidgets_SOURCE_DIR}/src" "${notice}")
                get_filename_component(notice_directory "${notice_relative}" DIRECTORY)
                install(FILES "${notice}"
                    DESTINATION "${NOTEPAD_DOC_DESTINATION}/licenses/wx-dependencies/${notice_directory}")
            endforeach()
        endforeach()
    endif()

    set(CPACK_PACKAGE_NAME notepad-prototype)
    set(CPACK_PACKAGE_VENDOR "Notepad contributors")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Small C++ and wxWidgets text editor prototype")
    set(CPACK_PACKAGE_FILE_NAME "Notepad-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
    if(UNIX AND NOT APPLE)
        set(CPACK_GENERATOR DEB)
        set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Notepad contributors")
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
        set(CPACK_DEBIAN_PACKAGE_SECTION editors)
    else()
        set(CPACK_GENERATOR ZIP)
    endif()
    include(CPack)
endif()
