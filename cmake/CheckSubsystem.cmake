# Script mode helper for meow2_gui_subsystem_in_release(): checks which Windows subsystem an exe was linked for.
# Usage: cmake -DMEOW2_EXE=<path> -DMEOW2_EXPECT=gui|console -P CheckSubsystem.cmake
#
# A Windows exe (PE file) starts with a DOS header whose 4 bytes at offset 0x3C ("e_lfanew") give the position of
# the PE header: the signature "PE\0\0" (4 bytes), the COFF file header (20 bytes), then the optional header, whose
# 2-byte Subsystem field is at offset 68 in both its 32- and 64-bit forms. So: Subsystem = e_lfanew + 4 + 20 + 68.
# Values: 2 = IMAGE_SUBSYSTEM_WINDOWS_GUI, 3 = IMAGE_SUBSYSTEM_WINDOWS_CUI (console).
# https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#windows-subsystem

# Reads `size` little-endian bytes at `offset` as an unsigned number.
function(meow2_read_le file offset size outVar)
    file(READ "${file}" hex OFFSET ${offset} LIMIT ${size} HEX)
    set(value 0)
    math(EXPR last "${size} - 1")
    foreach(i RANGE ${last} 0 -1) # most significant byte first
        math(EXPR start "${i} * 2")
        string(SUBSTRING "${hex}" ${start} 2 byte)
        math(EXPR value "(${value} << 8) + 0x${byte}")
    endforeach()
    set(${outVar} ${value} PARENT_SCOPE)
endfunction()

if(NOT EXISTS "${MEOW2_EXE}")
    message(FATAL_ERROR "${MEOW2_EXE} doesn't exist")
endif()
file(READ "${MEOW2_EXE}" signature LIMIT 2 HEX)
if(NOT signature STREQUAL "4d5a") # "MZ"
    message(FATAL_ERROR "${MEOW2_EXE} isn't a Windows executable (no MZ header)")
endif()

meow2_read_le("${MEOW2_EXE}" 60 4 peOffset) # 60 = 0x3C, e_lfanew
file(READ "${MEOW2_EXE}" peSignature OFFSET ${peOffset} LIMIT 4 HEX)
if(NOT peSignature STREQUAL "50450000") # "PE\0\0"
    message(FATAL_ERROR "${MEOW2_EXE}: no PE signature at offset ${peOffset}")
endif()
math(EXPR subsystemOffset "${peOffset} + 4 + 20 + 68")
meow2_read_le("${MEOW2_EXE}" ${subsystemOffset} 2 subsystem)

if(subsystem EQUAL 2)
    set(actual gui)
elseif(subsystem EQUAL 3)
    set(actual console)
else()
    set(actual "other (${subsystem})")
endif()
message("${MEOW2_EXE}: subsystem ${actual}")
if(NOT actual STREQUAL MEOW2_EXPECT)
    message(FATAL_ERROR "expected the ${MEOW2_EXPECT} subsystem, found ${actual}")
endif()
