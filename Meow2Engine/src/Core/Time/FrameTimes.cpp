/// @file
/// @brief FrameTimes (FrameTimes.h).

#include <Meow2/Core/Time/FrameTimes.h>

#include <algorithm>
#include <numeric>

namespace Meow2::Time
{
void FrameTimes::add(float milliseconds)
{
    m_times[m_next] = milliseconds;
    m_next = (m_next + 1) % m_times.size();
    m_count = std::min(m_count + 1, m_times.size());
}

float FrameTimes::average() const
{
    if (m_count == 0)
    {
        return 0.0F;
    }
    const std::array<float, c_capacity> times = ordered();
    return std::accumulate(times.end() - static_cast<std::ptrdiff_t>(m_count), times.end(), 0.0F) /
           static_cast<float>(m_count);
}

float FrameTimes::worst() const
{
    return *std::ranges::max_element(m_times);
}

std::array<float, FrameTimes::c_capacity> FrameTimes::ordered() const
{
    std::array<float, c_capacity> ordered{};
    for (std::size_t i = 0; i < m_times.size(); ++i)
    {
        ordered[i] = m_times[(m_next + i) % m_times.size()];
    }
    return ordered;
}
} // namespace Meow2::Time
