set(MOVIE_FORMAT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/movieformattest.cpp"
    "${CMAKE_SOURCE_DIR}/code/movieformat.cpp"
)

add_executable(MovieFormatVQATest ${MOVIE_FORMAT_TEST_SOURCES})
add_executable(MovieFormatMP4Test ${MOVIE_FORMAT_TEST_SOURCES})

foreach(target MovieFormatVQATest MovieFormatMP4Test)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/code")

    if(MSVC)
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
            $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
        )
    endif()

    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endforeach()

target_compile_definitions(MovieFormatMP4Test PRIVATE OPENTS_MP4_MOVIES)

add_test(NAME movieformat-vqa COMMAND MovieFormatVQATest .VQA)
add_test(NAME movieformat-mp4 COMMAND MovieFormatMP4Test .MP4)
