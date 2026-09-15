# The shape headers carry MSVC's __declspec, which clang parses only when asked.

opents_add_test(ShapeMagnify
    NAME shapemagnify
    SOURCES shapemagnifytest.cpp
    ENGINE shapemagnify.cpp
)

if(NOT MSVC)
    target_compile_options(ShapeMagnify PRIVATE -fdeclspec)
endif()
