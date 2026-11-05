#pragma once

// Microsoft's source-code annotation language (SAL) for compilers that don't have it.
//
// DirectXMath includes "sal.h", which on Windows comes with the SDK and lets MSVC's code analysis check buffer sizes.
// GCC and Clang don't use these annotations, so on Linux (and later Android) this file defines the ones DirectXMath
// uses as nothing. CMake puts this folder on DirectXMath's include path only off Windows (cmake/Meow2External.cmake).
// If a DirectXMath update starts using another annotation, the Linux build fails on it: add it here.

#define _In_
#define _Out_
#define _Out_opt_
#define _In_reads_(size)
#define _In_reads_bytes_(size)
#define _Out_writes_(size)
#define _Out_writes_bytes_(size)
#define _Use_decl_annotations_
#define _Analysis_assume_(expression)
#define _Success_(expression)
