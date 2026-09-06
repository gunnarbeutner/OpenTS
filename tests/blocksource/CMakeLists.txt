# The block-source reader is compiled straight into the harness. It lives outside code/ so
# that the recursive glob building OpenTS cannot pick this target's entry point up.
set(BLOCKSOURCE_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/blocksourcetest.cpp"
    "${CMAKE_SOURCE_DIR}/code/blocksource.cpp"
)

add_executable(BlockSourceTest ${BLOCKSOURCE_TEST_SOURCES})

target_compile_features(BlockSourceTest PRIVATE cxx_std_20)

target_include_directories(BlockSourceTest PRIVATE "${CMAKE_SOURCE_DIR}/code")

if(MSVC)
    target_compile_definitions(BlockSourceTest PRIVATE WIN32 _WINDOWS _MBCS)

    target_compile_options(BlockSourceTest PRIVATE
        $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
        $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
    )
endif()

add_test(NAME blocksource COMMAND BlockSourceTest "${CMAKE_CURRENT_BINARY_DIR}")
