/// @file
/// @brief Tests for FileSystem: mounts, path rules, reading, atomic writing, listing, UTF-8 names, concurrent reads.

#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using Meow2::ErrorCode;
using Meow2::FileSystem::DirectoryEntry;
using Meow2::FileSystem::FileSystem;
using Meow2::FileSystem::MountAccess;

/// @brief A fresh folder per test with `assets/` (read-only) and `save/` (writable) mounted.
class FileSystemTest : public ::testing::Test
{
protected:
    FileSystemTest()
        : m_root(makeRoot())
        , m_logSystem({.appName = "Test", .directory = m_root / "logs", .console = false})
        , m_log(m_logSystem.createLogger("Engine"))
        , m_files(m_log)
    {
        fs::create_directories(m_root / "assets" / "textures");
        fs::create_directories(m_root / "save");
        writeNative(m_root / "assets" / "hello.txt", "Hello, Meow2!");
        writeNative(m_root / "assets" / "textures" / "cat.png", std::string(1000, 'x'));
    }

    void SetUp() override
    {
        ASSERT_TRUE(m_files.mountDirectory("assets", m_root / "assets").has_value());
        ASSERT_TRUE(m_files.mountDirectory("save", m_root / "save", MountAccess::ReadWrite).has_value());
    }

    static void writeNative(const fs::path& path, const std::string& text)
    {
        std::ofstream stream{path, std::ios::binary};
        stream << text;
    }

    fs::path m_root;
    Meow2::Log::LogSystem m_logSystem;
    Meow2::Log::Logger m_log;
    FileSystem m_files;

private:
    static fs::path makeRoot()
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const fs::path root =
            fs::temp_directory_path() / std::format("meow2-vfs-{}-{}", Meow2::c_configName, info->name());
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root);
        return root;
    }
};
} // namespace

TEST_F(FileSystemTest, ReadsFromMountedDirectory)
{
    const Meow2::Result<std::string> text = m_files.readText("assets/hello.txt");
    ASSERT_TRUE(text.has_value()) << std::format("{}", text.error());
    EXPECT_EQ(*text, "Hello, Meow2!");

    const Meow2::Result<std::vector<std::byte>> bytes = m_files.readFile("assets/textures/cat.png");
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(bytes->size(), 1000U);
    EXPECT_TRUE(m_files.exists("assets/textures/cat.png"));
    EXPECT_TRUE(m_files.exists("assets/textures")) << "folders exist too";
    EXPECT_EQ(m_files.fileSize("assets/textures/cat.png").value_or(0), 1000U);
    EXPECT_TRUE(m_files.isMounted("assets"));
}

TEST_F(FileSystemTest, RejectsBadPaths)
{
    for (const char* bad :
         {"", "assets/../secret.txt", "../x", "/assets/hello.txt", "assets\\hello.txt", "C:/Windows/win.ini",
          "assets//hello.txt", "assets/./hello.txt", "assets/hello.txt/", "as sets/hello.txt"})
    {
        const Meow2::Result<std::string> text = m_files.readText(bad);
        ASSERT_FALSE(text.has_value()) << "'" << bad << "' was accepted";
        EXPECT_EQ(text.error().code, ErrorCode::InvalidArgument) << bad;
        EXPECT_FALSE(m_files.exists(bad)) << bad;
    }
    const Meow2::Result<std::string> unknownMount = m_files.readText("nomount/hello.txt");
    ASSERT_FALSE(unknownMount.has_value());
    EXPECT_EQ(unknownMount.error().code, ErrorCode::NotFound);
}

TEST_F(FileSystemTest, MissingFileIsNotFound)
{
    EXPECT_FALSE(m_files.exists("assets/missing.txt"));
    const Meow2::Result<std::vector<std::byte>> bytes = m_files.readFile("assets/missing.txt");
    ASSERT_FALSE(bytes.has_value());
    EXPECT_EQ(bytes.error().code, ErrorCode::NotFound);
    const Meow2::Result<std::uint64_t> size = m_files.fileSize("assets/textures");
    ASSERT_FALSE(size.has_value()) << "a folder has no file size";
    EXPECT_EQ(size.error().code, ErrorCode::NotFound);
}

TEST_F(FileSystemTest, WriteNeedsWritableMount)
{
    const Meow2::Result<void> written = m_files.writeText("assets/new.txt", "nope");
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error().code, ErrorCode::AccessDenied);
    EXPECT_FALSE(fs::exists(m_root / "assets" / "new.txt"));
}

TEST_F(FileSystemTest, WriteCreatesFoldersAndReplaces)
{
    ASSERT_TRUE(m_files.writeText("save/slots/1/game.sav", "first").has_value());
    EXPECT_EQ(m_files.readText("save/slots/1/game.sav").value_or(""), "first");
    ASSERT_TRUE(m_files.writeText("save/slots/1/game.sav", "second, longer").has_value());
    EXPECT_EQ(m_files.readText("save/slots/1/game.sav").value_or(""), "second, longer");

    const Meow2::Result<std::vector<DirectoryEntry>> entries = m_files.list("save/slots/1");
    ASSERT_TRUE(entries.has_value());
    const std::vector<DirectoryEntry> expected{{.name = "game.sav", .isDirectory = false}};
    EXPECT_EQ(*entries, expected) << "no .tmp file is left behind";
}

TEST_F(FileSystemTest, ListsSorted)
{
    writeNative(m_root / "assets" / "b.txt", "b");
    writeNative(m_root / "assets" / "a.txt", "a");
    const Meow2::Result<std::vector<DirectoryEntry>> root = m_files.list("assets");
    ASSERT_TRUE(root.has_value()) << std::format("{}", root.error());
    const std::vector<DirectoryEntry> expected{{.name = "a.txt", .isDirectory = false},
                                               {.name = "b.txt", .isDirectory = false},
                                               {.name = "hello.txt", .isDirectory = false},
                                               {.name = "textures", .isDirectory = true}};
    EXPECT_EQ(*root, expected);

    EXPECT_EQ(m_files.list("assets/hello.txt").error().code, ErrorCode::NotFound) << "a file isn't a folder";
    EXPECT_EQ(m_files.list("assets/nothing").error().code, ErrorCode::NotFound);
}

TEST_F(FileSystemTest, MountErrors)
{
    EXPECT_EQ(m_files.mountDirectory("other", m_root / "missing").error().code, ErrorCode::NotFound);
    EXPECT_EQ(m_files.mountDirectory("assets", m_root / "save").error().code, ErrorCode::AlreadyExists);
    EXPECT_EQ(m_files.mountDirectory("bad/name", m_root / "save").error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(m_files.mountDirectory("", m_root / "save").error().code, ErrorCode::InvalidArgument);

    EXPECT_TRUE(m_files.unmount("assets"));
    EXPECT_FALSE(m_files.unmount("assets")) << "already gone";
    EXPECT_EQ(m_files.readText("assets/hello.txt").error().code, ErrorCode::NotFound);
}

TEST_F(FileSystemTest, NonAsciiNames)
{
    const std::string name = "caf\xC3\xA9.txt"; // "cafe" with an acute e, in UTF-8
    ASSERT_TRUE(m_files.writeText("save/" + name, "ok").has_value());
    EXPECT_EQ(m_files.readText("save/" + name).value_or(""), "ok");
    const Meow2::Result<std::vector<DirectoryEntry>> entries = m_files.list("save");
    ASSERT_TRUE(entries.has_value());
    ASSERT_EQ(entries->size(), 1U);
    EXPECT_EQ((*entries)[0].name, name) << "names come back as UTF-8";
    EXPECT_TRUE(fs::exists(m_root / "save" / fs::path{u8"caf\u00e9.txt"})) << "the native name is the real one";
}

TEST_F(FileSystemTest, ConcurrentReads)
{
    std::vector<int> failures(4, 0);
    {
        std::vector<std::jthread> threads;
        for (std::size_t t = 0; t < failures.size(); ++t)
        {
            threads.emplace_back(
                [this, &failed = failures[t]]
                {
                    for (int i = 0; i < 200; ++i)
                    {
                        const bool ok = m_files.readText("assets/hello.txt").value_or("") == "Hello, Meow2!" &&
                                        m_files.fileSize("assets/textures/cat.png").value_or(0) == 1000U &&
                                        m_files.exists("assets/textures");
                        failed += ok ? 0 : 1;
                    }
                });
        }
    }
    for (const int failed : failures)
    {
        EXPECT_EQ(failed, 0);
    }
}

TEST_F(FileSystemTest, NativePath)
{
    const Meow2::Result<fs::path> native = m_files.nativePath("assets/textures/cat.png");
    ASSERT_TRUE(native.has_value());
    EXPECT_TRUE(fs::equivalent(*native, m_root / "assets" / "textures" / "cat.png"));
    EXPECT_TRUE(fs::equivalent(m_files.nativePath("assets").value_or(fs::path{}), m_root / "assets"))
        << "the mount name alone is the mount's root";
}
