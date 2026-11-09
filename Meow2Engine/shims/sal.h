#pragma once

// Microsoft's source-code annotation language (SAL) for compilers that don't have it.
//
// DirectXMath, DirectXTex and DirectX-Headers include "sal.h", which on Windows comes with the SDK and lets MSVC's code
// analysis check buffer sizes. GCC and Clang don't use these annotations, so on Linux (and later Android) this file
// defines the ones those libraries use as nothing. CMake puts this folder on DirectXMath's include path only off
// Windows (cmake/Meow2External.cmake), so DirectXTex, which links DirectXMath, sees it too.
//
// Each is guarded: DirectX-Headers' WSL stubs (basetsd.h) define some of them as well. Annotations with arguments are
// variadic, so the argument count never matters. If a library update starts using another annotation, the Linux build
// fails on it: add it here.

#ifndef _Always_
#define _Always_(...)
#endif
#ifndef _Analysis_assume_
#define _Analysis_assume_(...)
#endif
#ifndef _COM_Outptr_
#define _COM_Outptr_
#endif
#ifndef _COM_Outptr_opt_
#define _COM_Outptr_opt_
#endif
#ifndef _COM_Outptr_result_maybenull_
#define _COM_Outptr_result_maybenull_
#endif
#ifndef _Field_size_
#define _Field_size_(...)
#endif
#ifndef _Field_size_bytes_
#define _Field_size_bytes_(...)
#endif
#ifndef _Field_size_bytes_full_
#define _Field_size_bytes_full_(...)
#endif
#ifndef _Field_size_bytes_full_opt_
#define _Field_size_bytes_full_opt_(...)
#endif
#ifndef _Field_size_bytes_opt_
#define _Field_size_bytes_opt_(...)
#endif
#ifndef _Field_size_full_
#define _Field_size_full_(...)
#endif
#ifndef _Field_size_full_opt_
#define _Field_size_full_opt_(...)
#endif
#ifndef _Field_size_opt_
#define _Field_size_opt_(...)
#endif
#ifndef _Field_z_
#define _Field_z_
#endif
#ifndef _In_
#define _In_
#endif
#ifndef _In_count_
#define _In_count_(...)
#endif
#ifndef _In_opt_
#define _In_opt_
#endif
#ifndef _In_opt_count_
#define _In_opt_count_(...)
#endif
#ifndef _In_range_
#define _In_range_(...)
#endif
#ifndef _In_reads_
#define _In_reads_(...)
#endif
#ifndef _In_reads_bytes_
#define _In_reads_bytes_(...)
#endif
#ifndef _In_reads_bytes_opt_
#define _In_reads_bytes_opt_(...)
#endif
#ifndef _In_reads_opt_
#define _In_reads_opt_(...)
#endif
#ifndef _In_z_
#define _In_z_
#endif
#ifndef _Inexpressible_
#define _Inexpressible_(...)
#endif
#ifndef _Inout_
#define _Inout_
#endif
#ifndef _Inout_count_
#define _Inout_count_(...)
#endif
#ifndef _Inout_opt_
#define _Inout_opt_
#endif
#ifndef _Inout_updates_all_
#define _Inout_updates_all_(...)
#endif
#ifndef _Inout_updates_all_opt_
#define _Inout_updates_all_opt_(...)
#endif
#ifndef _Inout_updates_bytes_
#define _Inout_updates_bytes_(...)
#endif
#ifndef _Maybenull_
#define _Maybenull_
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _Out_opt_
#define _Out_opt_
#endif
#ifndef _Out_writes_
#define _Out_writes_(...)
#endif
#ifndef _Out_writes_all_
#define _Out_writes_all_(...)
#endif
#ifndef _Out_writes_bytes_
#define _Out_writes_bytes_(...)
#endif
#ifndef _Out_writes_bytes_opt_
#define _Out_writes_bytes_opt_(...)
#endif
#ifndef _Out_writes_bytes_to_opt_
#define _Out_writes_bytes_to_opt_(...)
#endif
#ifndef _Out_writes_opt_
#define _Out_writes_opt_(...)
#endif
#ifndef _Outptr_
#define _Outptr_
#endif
#ifndef _Outptr_opt_result_bytebuffer_
#define _Outptr_opt_result_bytebuffer_(...)
#endif
#ifndef _Outptr_opt_result_maybenull_
#define _Outptr_opt_result_maybenull_
#endif
#ifndef _Reserved_
#define _Reserved_
#endif
#ifndef _Success_
#define _Success_(...)
#endif
#ifndef _Use_decl_annotations_
#define _Use_decl_annotations_
#endif
#ifndef _When_
#define _When_(...)
#endif
#ifndef _Out_writes_to_
#define _Out_writes_to_(...)
#endif
#ifndef _Out_writes_to_opt_
#define _Out_writes_to_opt_(...)
#endif
