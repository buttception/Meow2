# Clang/GCC sanitizers for the whole build (techstack.md §19). Set by the linux-clang-asan / linux-clang-tsan presets.
#
#   MEOW2_SANITIZER=address  AddressSanitizer (out-of-bounds, use-after-free, double free, leaks via LeakSanitizer)
#                            + UndefinedBehaviorSanitizer (signed overflow, misaligned/null access, bad casts, ...)
#   MEOW2_SANITIZER=thread   ThreadSanitizer (data races, lock-order inversions)
#
# Included before the external libraries, so they're instrumented too: ThreadSanitizer only understands
# synchronisation in code it instrumented, and an uninstrumented library (enkiTS's queues, SDL) would show up as
# false races. Memory tracking is turned off: the sanitizers replace operator new/delete themselves, and our
# replacement would hide the allocations from them.
set(MEOW2_SANITIZER "" CACHE STRING "Sanitizer for the whole build: empty, 'address' (+ undefined) or 'thread'")
set_property(CACHE MEOW2_SANITIZER PROPERTY STRINGS "" address thread)

if(MEOW2_SANITIZER STREQUAL "")
    return()
endif()
if(MSVC)
    message(FATAL_ERROR "MEOW2_SANITIZER is for Clang/GCC; use the linux-clang-asan / linux-clang-tsan presets")
endif()

if(MEOW2_SANITIZER STREQUAL "address")
    # -fno-sanitize-recover: undefined behaviour stops the test instead of printing and carrying on, so CI fails.
    set(meow2SanitizerFlags -fsanitize=address,undefined -fno-sanitize-recover=undefined)
elseif(MEOW2_SANITIZER STREQUAL "thread")
    set(meow2SanitizerFlags -fsanitize=thread)
else()
    message(FATAL_ERROR "MEOW2_SANITIZER must be empty, 'address' or 'thread', not '${MEOW2_SANITIZER}'")
endif()

# Frame pointers make the sanitizers' stack traces complete and fast.
add_compile_options(${meow2SanitizerFlags} -fno-omit-frame-pointer)
add_link_options(${meow2SanitizerFlags})
add_compile_definitions(MEOW2_MEMORY_TRACKING=0)
message(STATUS "Sanitizer: ${MEOW2_SANITIZER} (${meow2SanitizerFlags}); memory tracking off")
