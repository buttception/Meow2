# Writes the release notes of one version: its section of CHANGELOG.md (AGENTS.md §2.7), without the heading.
#
# Usage: cmake -DMEOW2_VERSION=0.2.0 -DMEOW2_CHANGELOG=CHANGELOG.md -DMEOW2_OUTPUT=notes.md -P ReleaseNotes.cmake
#
# The section starts at the line "## [<version>]" (e.g. "## [0.2.0] — 2026-11-02") and ends before the next "## ["
# line or at the end of the file. Fails if the version has no section (or an empty one), so a tag without notes
# never gets released. Works on the whole text, not line by line, so blank lines and Markdown stay as written.

if(NOT MEOW2_VERSION OR NOT EXISTS "${MEOW2_CHANGELOG}" OR NOT MEOW2_OUTPUT)
    message(FATAL_ERROR "usage: -DMEOW2_VERSION=x.y.z -DMEOW2_CHANGELOG=<file> -DMEOW2_OUTPUT=<file>")
endif()

file(READ "${MEOW2_CHANGELOG}" content)
string(REPLACE "\r\n" "\n" content "${content}")
string(REPLACE "." "\\." versionPattern "${MEOW2_VERSION}")

# The heading line of the section, including its line break.
string(REGEX MATCH "(^|\n)## \\[${versionPattern}\\][^\n]*\n" heading "${content}")
if(heading STREQUAL "")
    message(FATAL_ERROR "CHANGELOG has no section '## [${MEOW2_VERSION}]'")
endif()
string(FIND "${content}" "${heading}" start)
string(LENGTH "${heading}" headingLength)
math(EXPR bodyStart "${start} + ${headingLength}")
string(SUBSTRING "${content}" ${bodyStart} -1 notes)

# Up to the next version's heading, if there is one.
string(FIND "${notes}" "\n## [" end)
if(end GREATER_EQUAL 0)
    string(SUBSTRING "${notes}" 0 ${end} notes)
endif()

string(STRIP "${notes}" notes)
if(notes STREQUAL "")
    message(FATAL_ERROR "the CHANGELOG section for ${MEOW2_VERSION} is empty")
endif()
file(WRITE "${MEOW2_OUTPUT}" "${notes}\n")
message("Release notes for ${MEOW2_VERSION}: ${MEOW2_OUTPUT}")
