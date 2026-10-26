# meow2_set_warnings(<target>)
#
# Strict warnings for Meow2's own targets. Never call it on third-party targets
# (googletest, SDL3, ...): we can't fix their warnings, and their headers are
# included as SYSTEM so their warnings don't reach our code either.
#
# Warnings are errors by default (MEOW2_WARNINGS_AS_ERRORS). For a one-off
# local build you can override that without editing anything:
#   cmake --preset <p> --compile-no-warning-as-error

option(MEOW2_WARNINGS_AS_ERRORS "Treat compiler warnings in Meow2 code as errors" ON)

function(meow2_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4            # high warning level (not /Wall: that includes thousands of STL-internal notes)
            /permissive-   # standards-conforming mode: reject non-standard MSVC extensions
            /utf-8         # sources and literals are UTF-8 (avoids C4819 on non-English Windows locales)
            /w14062        # enumerator not handled in switch (made level 1 = always on)
            /w14242        # conversion, possible loss of data
            /w14265        # class has virtual functions but non-virtual destructor
            /w14296        # expression is always true/false (unsigned comparisons)
            /w14826        # sign-extending conversion
            /w14905        # wide string literal cast to LPSTR
            /w14906)       # string literal cast to LPWSTR
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow                 # a local hides another variable/member
            -Wnon-virtual-dtor       # polymorphic class without virtual destructor
            -Wold-style-cast         # (T)x instead of static_cast<T>(x)
            -Wcast-align
            -Woverloaded-virtual     # a function hides a base-class virtual
            -Wconversion             # implicit narrowing (float->int, int64->int32, ...)
            -Wnull-dereference
            -Wdouble-promotion       # float silently promoted to double
            -Wformat=2               # printf-style format checks
            -Wimplicit-fallthrough   # switch case falls through without [[fallthrough]]
            -Wundef)                 # #if on an undefined (e.g. misspelled) macro
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE
                -Wduplicated-cond -Wduplicated-branches -Wlogical-op)
        endif()
    endif()

    # CMake turns this into /WX or -Werror, and honours --compile-no-warning-as-error.
    set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ${MEOW2_WARNINGS_AS_ERRORS})
endfunction()
