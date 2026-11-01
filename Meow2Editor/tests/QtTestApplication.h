#pragma once

/// @file
/// @brief Test fixture with a QApplication on Qt's offscreen platform and a log in a fresh folder.

#include <Meow2/Core/Log/LogSystem.h>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <QApplication>
#include <string>
#include <system_error>

namespace Meow2::Tests
{
/// @brief Base fixture for tests that need Qt widgets: a QApplication (offscreen, so no desktop is needed) that
///        lives for one test, plus a logger.
class QtTest : public ::testing::Test
{
protected:
    QtTest()
        : m_logSystem({.appName = "Test", .directory = logDirectory(), .console = false})
        , m_log(m_logSystem.createLogger("Engine"))
        , m_application(m_argumentCount, m_arguments.data())
    {
    }

    /// @brief Everything logged so far.
    std::string logText()
    {
        m_logSystem.flush();
        std::ifstream file(m_logSystem.filePath(), std::ios::binary);
        return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    Log::LogSystem m_logSystem;
    Log::Logger m_log;

private:
    static std::filesystem::path logDirectory()
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() /
            std::format("meow2-editor-{}-{}-{}", c_configName, info->test_suite_name(), info->name());
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        return directory;
    }

    // QApplication keeps references to the argument count and array, so they're members declared before it.
    std::array<char, 5> m_program{"test"};
    std::array<char, 10> m_platformOption{"-platform"};
    std::array<char, 10> m_platformName{"offscreen"};
    std::array<char*, 4> m_arguments{m_program.data(), m_platformOption.data(), m_platformName.data(), nullptr};
    int m_argumentCount = 3;
    QApplication m_application;
};
} // namespace Meow2::Tests
