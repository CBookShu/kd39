add_library(compiler_options INTERFACE)

target_compile_options(compiler_options INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wpedantic -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 /wd4100 /wd4458 /utf-8>
)

target_compile_definitions(compiler_options INTERFACE
    $<$<CONFIG:Debug>:KD39_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)
