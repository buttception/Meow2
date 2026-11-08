/// @file
/// @brief Meow2Renderer's UI logic: engine keys to ImGui keys, the sample list, model names, frame times.

#include "ImGuiLayer.h"
#include "RendererUi.h"
#include "SceneView.h"
#include "UiTheme.h"

#include <Meow2/Core/Platform.h>

#include <gtest/gtest.h>
#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace Meow2::RendererApp
{
namespace
{
namespace fs = std::filesystem;

/// A fake samples folder: Models/<name>/glTF/<name>.gltf for each name.
class SampleFolder
{
public:
    explicit SampleFolder(const std::string& test)
        : m_root(fs::temp_directory_path() / std::format("meow2-samples-{}-{}", c_configName, test))
    {
        std::error_code error;
        fs::remove_all(m_root, error);
        fs::create_directories(m_root / "Models");
    }

    ~SampleFolder()
    {
        std::error_code error;
        fs::remove_all(m_root, error);
    }

    SampleFolder(const SampleFolder&) = delete;
    SampleFolder& operator=(const SampleFolder&) = delete;

    void add(const std::string& name, bool binary = false) const
    {
        const fs::path folder = m_root / "Models" / name / (binary ? "glTF-Binary" : "glTF");
        fs::create_directories(folder);
        std::ofstream{folder / (name + (binary ? ".glb" : ".gltf"))} << "{}";
    }

    [[nodiscard]] const fs::path& root() const
    {
        return m_root;
    }

private:
    fs::path m_root;
};

const SampleEntry* findEntry(const std::vector<SampleEntry>& samples, const std::string& name)
{
    const auto found = std::ranges::find(samples, name, &SampleEntry::name);
    return found == samples.end() ? nullptr : &*found;
}
} // namespace

TEST(ImGuiInput, LettersDigitsAndFunctionKeysMap)
{
    EXPECT_EQ(toImGuiKey(Input::Key::A), ImGuiKey_A);
    EXPECT_EQ(toImGuiKey(Input::Key::Z), ImGuiKey_Z);
    EXPECT_EQ(toImGuiKey(Input::Key::Num1), ImGuiKey_1);
    EXPECT_EQ(toImGuiKey(Input::Key::Num0), ImGuiKey_0);
    EXPECT_EQ(toImGuiKey(Input::Key::F1), ImGuiKey_F1);
    EXPECT_EQ(toImGuiKey(Input::Key::F12), ImGuiKey_F12);
    EXPECT_EQ(toImGuiKey(Input::Key::Keypad5), ImGuiKey_Keypad5);
}

TEST(ImGuiInput, EditingKeysMap)
{
    EXPECT_EQ(toImGuiKey(Input::Key::Return), ImGuiKey_Enter);
    EXPECT_EQ(toImGuiKey(Input::Key::Backspace), ImGuiKey_Backspace);
    EXPECT_EQ(toImGuiKey(Input::Key::Left), ImGuiKey_LeftArrow);
    EXPECT_EQ(toImGuiKey(Input::Key::Delete), ImGuiKey_Delete);
    EXPECT_EQ(toImGuiKey(Input::Key::LeftCtrl), ImGuiKey_LeftCtrl);
    EXPECT_EQ(toImGuiKey(Input::Key::RightShift), ImGuiKey_RightShift);
}

TEST(ImGuiInput, UnknownKeysMapToNone)
{
    EXPECT_EQ(toImGuiKey(Input::Key::Unknown), ImGuiKey_None);
    EXPECT_EQ(toImGuiKey(Input::Key::NonUsBackslash), ImGuiKey_None);
}

TEST(ScenePicker, ModelNamesAreChecked)
{
    EXPECT_TRUE(isModelName("DamagedHelmet"));
    EXPECT_TRUE(isModelName("glTF-Sample_1"));
    EXPECT_FALSE(isModelName(""));
    EXPECT_FALSE(isModelName("Box && del"));
    EXPECT_FALSE(isModelName("../Box"));
    EXPECT_FALSE(isModelName("Box\""));
}

TEST(ScenePicker, ListsSuggestedSamplesThenOtherDownloads)
{
    const SampleFolder folder{"lists"};
    folder.add("Duck");
    folder.add("Fox", true);

    const std::vector<SampleEntry> samples = listSamples(folder.root());

    ASSERT_GE(samples.size(), 26U);
    EXPECT_EQ(samples.front().name, "Box") << "the suggested ones first, in their order";
    const SampleEntry* duck = findEntry(samples, "Duck");
    ASSERT_NE(duck, nullptr);
    EXPECT_TRUE(duck->downloaded);
    EXPECT_EQ(duck->group, "First light");
    const SampleEntry* helmet = findEntry(samples, "DamagedHelmet");
    ASSERT_NE(helmet, nullptr);
    EXPECT_FALSE(helmet->downloaded);
    EXPECT_EQ(samples.back().name, "Fox") << "a downloaded model that isn't suggested comes last";
    EXPECT_EQ(samples.back().group, "Downloaded");
}

TEST(ScenePicker, FindsGltfBeforeGlb)
{
    const SampleFolder folder{"finds"};
    folder.add("Duck");
    folder.add("Duck", true);
    folder.add("Fox", true);

    EXPECT_EQ(findSample(folder.root(), "Duck").extension(), ".gltf");
    EXPECT_EQ(findSample(folder.root(), "Fox").extension(), ".glb");
    EXPECT_TRUE(findSample(folder.root(), "Missing").empty());
}

TEST(FrameTimes, AveragesAndKeepsOrder)
{
    FrameTimes times;
    EXPECT_FLOAT_EQ(times.average(), 0.0F);
    times.add(10.0F);
    times.add(20.0F);

    EXPECT_FLOAT_EQ(times.average(), 15.0F);
    EXPECT_FLOAT_EQ(times.worst(), 20.0F);
    const auto ordered = times.ordered();
    EXPECT_FLOAT_EQ(ordered[ordered.size() - 1], 20.0F) << "newest last";
    EXPECT_FLOAT_EQ(ordered[ordered.size() - 2], 10.0F);
}
namespace
{
/// @brief Two colours are the same, channel by channel.
void expectColor(const ImVec4& actual, const ImVec4& expected)
{
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.z, expected.z);
    EXPECT_FLOAT_EQ(actual.w, expected.w);
}
} // namespace

TEST(UiTheme, AccentsAreRedOnDark)
{
    ImGuiStyle style;
    applyUiTheme(style, 1.0F);

    // Near-black panels, light text.
    EXPECT_LT(style.Colors[ImGuiCol_WindowBg].x, 0.06F);
    EXPECT_GT(style.Colors[ImGuiCol_Text].x, 0.85F);
    // Everything active is the red accent.
    for (const ImGuiCol active : {ImGuiCol_CheckMark, ImGuiCol_SliderGrab, ImGuiCol_PlotLines, ImGuiCol_NavCursor,
                                  ImGuiCol_TabSelectedOverline, ImGuiCol_SeparatorActive})
    {
        expectColor(style.Colors[active], UiColors::c_accent);
    }
    // Hover and press are washes of the same red, stronger when pressed.
    EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_ButtonHovered].x, UiColors::c_accent.x);
    EXPECT_GT(style.Colors[ImGuiCol_ButtonActive].w, style.Colors[ImGuiCol_ButtonHovered].w);
    // Square and flat.
    EXPECT_EQ(style.WindowRounding, 0.0F);
    EXPECT_EQ(style.FrameRounding, 0.0F);
    EXPECT_EQ(style.TabRounding, 0.0F);
    EXPECT_EQ(style.FrameBorderSize, 1.0F);
}

TEST(UiTheme, ScalesSizes)
{
    ImGuiStyle normal;
    applyUiTheme(normal, 1.0F);
    ImGuiStyle large;
    applyUiTheme(large, 1.5F);
    EXPECT_FLOAT_EQ(large.WindowPadding.x, normal.WindowPadding.x * 1.5F);
    EXPECT_FLOAT_EQ(large.FramePadding.y, normal.FramePadding.y * 1.5F);
    EXPECT_FLOAT_EQ(large.FontScaleDpi, 1.5F);

    // Applying again for another scale starts over instead of compounding.
    applyUiTheme(large, 1.0F);
    EXPECT_FLOAT_EQ(large.WindowPadding.x, normal.WindowPadding.x);
}
} // namespace Meow2::RendererApp
