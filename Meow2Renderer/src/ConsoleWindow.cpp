/// @file
/// @brief ConsoleWindow (ConsoleWindow.h).

#include "ConsoleWindow.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <utility>
#include <vector>

namespace Meow2::RendererApp
{
namespace
{
/// @brief Level names as the log file writes them, in Log::LogLevel's order.
constexpr std::array<std::string_view, 5> c_levelNames{"debug", "info", "warn", "error", "fatal"};

/// @brief Whether `text` contains `part`, ignoring ASCII case.
bool containsIgnoringCase(std::string_view text, std::string_view part)
{
    const auto lower = [](char c)
    {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    return std::ranges::search(text, part, {}, lower, lower).begin() != text.end() || part.empty();
}

/// @brief A level's colour: grey for debug, text for info, the accent for warnings, the bright accent for errors.
ImVec4 levelColor(Log::LogLevel level)
{
    switch (level)
    {
        case Log::LogLevel::Debug:
            return UiColors::c_textDim;
        case Log::LogLevel::Info:
            return UiColors::c_text;
        case Log::LogLevel::Warn:
            return UiColors::c_accent;
        case Log::LogLevel::Error:
        case Log::LogLevel::Fatal:
            break;
    }
    return UiColors::c_accentBright;
}

/// @brief A line as the log file has it: `[time]<level> logger: message`.
std::string formatLine(const Log::LogLine& line)
{
    return std::format("[{}]<{}> {}: {}", line.time, c_levelNames[static_cast<std::size_t>(line.level)], line.logger,
                       line.message);
}
} // namespace

bool consoleLineMatches(const Log::LogLine& line, const ConsoleFilter& filter)
{
    const auto level = static_cast<std::size_t>(line.level);
    if (level < filter.levels.size() && !filter.levels[level])
    {
        return false;
    }
    return containsIgnoringCase(line.message, filter.text) || containsIgnoringCase(line.logger, filter.text);
}

ConsoleWindow::ConsoleWindow(const Log::ConsoleBuffer& buffer)
    : m_buffer(buffer)
{
}

void ConsoleWindow::update()
{
    std::vector<Log::LogLine> fresh;
    m_missed += m_buffer.linesSince(m_next, fresh);
    if (!fresh.empty())
    {
        m_next = fresh.back().sequence + 1;
    }
    for (Log::LogLine& line : fresh)
    {
        m_lines.push_back(std::move(line));
    }
    while (m_lines.size() > m_buffer.capacity())
    {
        m_lines.pop_front();
    }
}

void ConsoleWindow::clear()
{
    m_lines.clear();
    m_missed = 0;
}

void ConsoleWindow::draw(const UiFonts& fonts)
{
    update();
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2{10.0F * scale, display.y - 260.0F * scale}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2{std::min(900.0F * scale, display.x - 20.0F * scale), 250.0F * scale},
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("CONSOLE###Console"))
    {
        ImGui::End();
        return;
    }
    drawWindowAccents();

    // Toolbar: a toggle per level, the text filter, then the actions.
    for (std::size_t i = 0; i < c_levelNames.size() - 1; ++i) // fatal ends the program: no toggle
    {
        std::string label = std::string{c_levelNames[i]};
        std::ranges::transform(label, label.begin(), [](char c) { return static_cast<char>(std::toupper(c)); });
        ImGui::Checkbox(label.c_str(), &m_filter.levels[i]);
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(180.0F * scale);
    if (ImGui::InputTextWithHint("##filter", "FILTER", m_filterText.data(), m_filterText.size()))
    {
        m_filter.text = m_filterText.data();
    }
    ImGui::SameLine();
    if (ImGui::Button("CLEAR"))
    {
        clear();
    }
    ImGui::SameLine();
    const bool copy = ImGui::Button("COPY");
    ImGui::SameLine();
    ImGui::Checkbox("FOLLOW", &m_followNewest);

    // The lines that pass, by index, so the clipper only formats what's on screen.
    std::vector<std::size_t> shown;
    shown.reserve(m_lines.size());
    for (std::size_t i = 0; i < m_lines.size(); ++i)
    {
        if (consoleLineMatches(m_lines[i], m_filter))
        {
            shown.push_back(i);
        }
    }
    if (copy)
    {
        std::string text;
        for (const std::size_t i : shown)
        {
            text += formatLine(m_lines[i]);
            text += '\n';
        }
        ImGui::SetClipboardText(text.c_str());
    }

    ImGui::Separator();
    if (ImGui::BeginChild("lines", ImVec2{0.0F, 0.0F}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
    {
        pushMonoFont(fonts);
        if (m_missed > 0)
        {
            ImGui::TextColored(UiColors::c_textDim, "(%llu older lines dropped)",
                               static_cast<unsigned long long>(m_missed));
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const Log::LogLine& line = m_lines[shown[static_cast<std::size_t>(row)]];
                ImGui::TextColored(levelColor(line.level), "%s", formatLine(line).c_str());
            }
        }
        // Follow the newest line unless the user scrolled up to read.
        if (m_followNewest && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetTextLineHeight())
        {
            ImGui::SetScrollHereY(1.0F);
        }
        popMonoFont();
    }
    ImGui::EndChild();
    ImGui::End();
}
} // namespace Meow2::RendererApp
