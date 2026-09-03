# The block container that carries LCW data through the pipe and straw chains. It brings the
# base classes with it and, like the codec, needs nothing else from the engine.
add_executable(LCWStreamTest
    "${CMAKE_CURRENT_SOURCE_DIR}/lcwstreamtest.cpp"
    "${CMAKE_SOURCE_DIR}/code/lcw.cpp"
    "${CMAKE_SOURCE_DIR}/code/lcwpipe.cpp"
    "${CMAKE_SOURCE_DIR}/code/lcwstraw.cpp"
    "${CMAKE_SOURCE_DIR}/code/pipe.cpp"
    "${CMAKE_SOURCE_DIR}/code/straw.cpp"
)

foreach(target LCWStreamTest)
    target_compile_features(${target} PRIVATE cxx_std_20)

    target_include_directories(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}/code"
    )

    if(MSVC)
        target_compile_definitions(${target} PRIVATE WIN32 _WINDOWS _MBCS)

        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
            $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
        )
    endif()

    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endforeach()

add_test(NAME lcwstream COMMAND LCWStreamTest)
