add_library(disruptor-disc-import STATIC disc_import.cpp)
target_compile_features(disruptor-disc-import PUBLIC cxx_std_20)
target_include_directories(disruptor-disc-import PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(disruptor-disc-import PUBLIC
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_link_libraries(disruptor-disc-import PUBLIC bcrypt)

add_executable(disruptor-launcher WIN32 main.cpp)
set_target_properties(disruptor-launcher PROPERTIES
    OUTPUT_NAME DisruptorLauncher
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(disruptor-launcher PRIVATE
    disruptor-disc-import comctl32 comdlg32 shell32)
target_include_directories(disruptor-launcher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(MINGW)
    target_sources(disruptor-launcher PRIVATE launcher.rc)
    target_link_options(disruptor-launcher PRIVATE -municode -static)
endif()
if(MSVC)
    target_sources(disruptor-launcher PRIVATE launcher.manifest)
    # Setup itself must open even before the VC++ redistributable is installed.
    set_property(TARGET disruptor-disc-import disruptor-launcher
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(disruptor-disc-import PRIVATE /W4 /utf-8)
    target_compile_options(disruptor-launcher PRIVATE /W4 /utf-8)
endif()

# A normal local Windows build is launchable as well as the packaged release.
# Preserve local bindings and preferences if they already exist.
add_custom_command(TARGET disruptor-launcher POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE=${PROJECT_SOURCE_DIR}"
        "-DDESTINATION=$<TARGET_FILE_DIR:disruptor-launcher>"
        -P "${PROJECT_SOURCE_DIR}/tools/stage_launcher.cmake"
    VERBATIM)
set_property(TARGET disruptor-launcher APPEND PROPERTY LINK_DEPENDS
    "${PROJECT_SOURCE_DIR}/tools/stage_launcher.cmake"
    "${PROJECT_SOURCE_DIR}/game.toml"
    "${PROJECT_SOURCE_DIR}/mouse-aim.ini"
    "${PROJECT_SOURCE_DIR}/docs/PLAYING.md"
    "${PROJECT_SOURCE_DIR}/release/windows/Play Disruptor.cmd"
    "${PROJECT_SOURCE_DIR}/keybinds-modern.ini"
    "${PROJECT_SOURCE_DIR}/release/windows/settings.toml")

if(BUILD_TESTING)
    add_executable(disruptor-disc-import-test "${PROJECT_SOURCE_DIR}/tests/disc_import_test.cpp")
    set_target_properties(disruptor-disc-import-test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_link_libraries(disruptor-disc-import-test PRIVATE disruptor-disc-import)
    if(MSVC)
        set_property(TARGET disruptor-disc-import-test
            PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(disruptor-disc-import-test PRIVATE /utf-8)
    endif()
    add_test(NAME disruptor_disc_import COMMAND disruptor-disc-import-test)
endif()
