cmake_minimum_required(VERSION 3.20)

project(CrispyPandasModdingTools VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)

add_executable(crispy-pandas WIN32
    src/main.c
    src/hoi4_map.c
    src/path_utils.c
    src/state_edit.c
    src/province_transfer.c
    src/character_creator.c
    src/portrait_image.c
    src/character_ui.c
)

target_include_directories(crispy-pandas PRIVATE src)
target_compile_definitions(crispy-pandas PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_compile_options(crispy-pandas PRIVATE
    $<$<C_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Wshadow>
)
target_link_libraries(crispy-pandas PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    shell32
    ole32
    comdlg32
    windowscodecs
    uuid
    m
)

if(WIN32)
    add_custom_command(TARGET crispy-pandas POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3-shared>"
                "$<TARGET_FILE_DIR:crispy-pandas>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:SDL3_ttf::SDL3_ttf-shared>"
                "$<TARGET_FILE_DIR:crispy-pandas>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/assets/advisor_small_template.png"
                "$<TARGET_FILE_DIR:crispy-pandas>/advisor_small_template.png"
        VERBATIM
    )
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(hoi4-parser-tests
        tests/parser_tests.c
        src/hoi4_map.c
        src/path_utils.c
        src/state_edit.c
        src/province_transfer.c
        src/character_creator.c
        src/portrait_image.c
    )
    target_include_directories(hoi4-parser-tests PRIVATE src)
    target_compile_definitions(hoi4-parser-tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(hoi4-parser-tests PRIVATE shell32 ole32 comdlg32 windowscodecs uuid m)
    add_custom_command(TARGET hoi4-parser-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/assets/advisor_small_template.png"
                "$<TARGET_FILE_DIR:hoi4-parser-tests>/advisor_small_template.png"
        VERBATIM
    )
    add_test(NAME hoi4-parser-tests COMMAND hoi4-parser-tests)
endif()
