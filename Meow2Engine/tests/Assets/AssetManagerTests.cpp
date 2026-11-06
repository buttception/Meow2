/// @file
/// @brief Tests for AssetManager: `.meta` files and stable IDs, moves, duplicates, loading once, reload, jobs.

#include <Meow2/Assets/AssetManager.h>
#include <Meow2/Core/Jobs/JobSystem.h>
#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Core/Platform.h>
#include <Meow2/Platform/FileSystem/FileSystem.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>

namespace Meow2::Assets
{
namespace
{
namespace fs = std::filesystem;

/// @brief The simplest asset: a text file's contents.
struct TextAsset
{
    std::string text; ///< The contents.

    static Result<TextAsset> loadAsset(AssetManager& assets, AssetId id)
    {
        const std::optional<AssetRecord> record = assets.find(id);
        if (!record)
        {
            return fail(ErrorCode::NotFound, id.uuid().toString());
        }
        Result<std::string> text = assets.files().readText(record->sourcePath);
        if (!text)
        {
            return Unexpected<Error>{text.error()};
        }
        return TextAsset{std::move(*text)};
    }
};

/// @brief A read-write `assets` mount in a fresh folder, and a manager.
class AssetManagerTest : public ::testing::Test
{
protected:
    AssetManagerTest()
        : m_directory(makeDirectory())
        , m_logSystem({.appName = "Test", .directory = m_directory / "logs", .console = false})
        , m_log(m_logSystem.createLogger("Engine"))
        , m_files(m_log)
        , m_assets(m_files, m_ids, m_log)
    {
    }

    void SetUp() override
    {
        fs::create_directories(m_directory / "root");
        ASSERT_TRUE(
            m_files.mountDirectory("assets", m_directory / "root", FileSystem::MountAccess::ReadWrite).has_value());
    }

    void write(const std::string& path, const std::string& text)
    {
        ASSERT_TRUE(m_files.writeText(path, text).has_value());
    }

    fs::path m_directory;
    Log::LogSystem m_logSystem;
    Log::Logger m_log;
    FileSystem::FileSystem m_files;
    UuidGenerator m_ids;
    AssetManager m_assets;

private:
    static fs::path makeDirectory()
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const fs::path directory =
            fs::temp_directory_path() / std::format("meow2-assets-{}-{}", c_configName, info->name());
        std::error_code ignored;
        fs::remove_all(directory, ignored);
        return directory;
    }
};
} // namespace

TEST_F(AssetManagerTest, ImportCreatesMetaOnce)
{
    write("assets/Sky.hlsl", "source");
    const Result<AssetId> first = m_assets.importSource("assets/Sky.hlsl");
    ASSERT_TRUE(first.has_value()) << first.error().message;
    EXPECT_TRUE(first->isValid());
    EXPECT_TRUE(m_files.exists("assets/Sky.hlsl.meta"));

    const Result<AssetId> second = m_assets.importSource("assets/Sky.hlsl");
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, *second) << "the .meta keeps the ID";

    EXPECT_EQ(m_assets.importSource("assets/Nothing.hlsl").error().code, ErrorCode::NotFound);
}

TEST_F(AssetManagerTest, ScanFindsNestedAssets)
{
    write("assets/A.txt", "a");
    write("assets/deep/er/B.txt", "b");
    const AssetId a = *m_assets.importSource("assets/A.txt");
    const AssetId b = *m_assets.importSource("assets/deep/er/B.txt");

    AssetManager fresh{m_files, m_ids, m_log};
    const Result<std::size_t> count = fresh.scan("assets");
    ASSERT_TRUE(count.has_value()) << count.error().message;
    EXPECT_EQ(*count, 2U);
    EXPECT_EQ(fresh.records().size(), 2U);
    ASSERT_TRUE(fresh.find(b).has_value());
    EXPECT_EQ(fresh.find(b)->sourcePath, "assets/deep/er/B.txt");
    EXPECT_EQ(fresh.findBySource("assets/A.txt"), a);
    EXPECT_FALSE(fresh.find(m_ids.next<IdKind::Asset>()).has_value());
}

TEST_F(AssetManagerTest, MovingAFileKeepsItsId)
{
    write("assets/A.txt", "a");
    const AssetId id = *m_assets.importSource("assets/A.txt");
    fs::create_directories(m_directory / "root" / "moved");
    fs::rename(m_directory / "root" / "A.txt", m_directory / "root" / "moved" / "A.txt");
    fs::rename(m_directory / "root" / "A.txt.meta", m_directory / "root" / "moved" / "A.txt.meta");

    ASSERT_TRUE(m_assets.scan("assets").has_value());
    ASSERT_TRUE(m_assets.find(id).has_value());
    EXPECT_EQ(m_assets.find(id)->sourcePath, "assets/moved/A.txt");
}

TEST_F(AssetManagerTest, BadMetaFilesAreErrors)
{
    write("assets/A.txt", "a");
    ASSERT_TRUE(m_assets.importSource("assets/A.txt").has_value());
    const std::string meta = *m_files.readText("assets/A.txt.meta");
    write("assets/Copy.txt", "copy");
    write("assets/Copy.txt.meta", meta);
    const Result<std::size_t> duplicate = m_assets.scan("assets");
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().code, ErrorCode::AlreadyExists) << "a copied .meta must not go unnoticed";

    write("assets/Copy.txt.meta", "this is not = = toml");
    EXPECT_EQ(m_assets.scan("assets").error().code, ErrorCode::ParseError);
    write("assets/Copy.txt.meta", "id = \"not-a-uuid\"\n");
    EXPECT_EQ(m_assets.scan("assets").error().code, ErrorCode::ParseError);
    write("assets/Copy.txt.meta", "other = 1\n");
    EXPECT_EQ(m_assets.scan("assets").error().code, ErrorCode::ParseError);
}

TEST_F(AssetManagerTest, LoadsOnce)
{
    write("assets/A.txt", "hello");
    const AssetId id = *m_assets.importSource("assets/A.txt");
    Result<AssetHandle<TextAsset>> first = m_assets.load<TextAsset>(id);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    Result<AssetHandle<TextAsset>> second = m_assets.load<TextAsset>(id);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->get()->text, "hello");
    EXPECT_EQ(first->get(), second->get()) << "one loaded copy";
    EXPECT_EQ(first->version(), 1U);

    EXPECT_EQ(m_assets.load<TextAsset>(m_ids.next<IdKind::Asset>()).error().code, ErrorCode::NotFound);
}

TEST_F(AssetManagerTest, ReloadReachesEveryHandle)
{
    write("assets/A.txt", "old");
    const AssetId id = *m_assets.importSource("assets/A.txt");
    AssetHandle<TextAsset> handle = *m_assets.load<TextAsset>(id);
    const std::shared_ptr<const TextAsset> held = handle.get();

    write("assets/A.txt", "new");
    ASSERT_TRUE(m_assets.reload<TextAsset>(id).has_value());
    EXPECT_EQ(handle.get()->text, "new");
    EXPECT_EQ(handle.version(), 2U);
    EXPECT_EQ(held->text, "old") << "values already handed out stay valid";

    fs::remove(m_directory / "root" / "A.txt");
    EXPECT_FALSE(m_assets.reload<TextAsset>(id).has_value());
    EXPECT_EQ(handle.get()->text, "new") << "a failed reload keeps the old value";
    EXPECT_EQ(handle.version(), 2U);
}

TEST_F(AssetManagerTest, JobsLoadTheSameAsset)
{
    write("assets/A.txt", "shared");
    const AssetId id = *m_assets.importSource("assets/A.txt");
    Jobs::JobSystem jobs{{.threadCount = 4}, m_log};
    std::atomic<int> failures{0};
    jobs.wait(jobs.parallelFor(256, 1,
                               [&](std::uint32_t begin, std::uint32_t end, std::uint32_t /*threadIndex*/)
                               {
                                   for (std::uint32_t i = begin; i < end; ++i)
                                   {
                                       const Result<AssetHandle<TextAsset>> handle = m_assets.load<TextAsset>(id);
                                       if (!handle || handle->get()->text != "shared")
                                       {
                                           ++failures;
                                       }
                                   }
                               }));
    EXPECT_EQ(failures.load(), 0);
}
} // namespace Meow2::Assets
