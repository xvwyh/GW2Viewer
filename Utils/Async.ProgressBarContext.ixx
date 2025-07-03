export module GW2Viewer.Utils.Async.ProgressBarContext;
import std;

export namespace GW2Viewer::Utils::Async
{

class ProgressBarContext
{
    std::string m_description;
    size_t m_current = 0;
    size_t m_total = 0;
    mutable std::recursive_mutex m_mutex;
    std::future<void> m_task;

public:
    void Start(std::string_view description, size_t total = 0, size_t current = 0)
    {
        std::scoped_lock _(m_mutex);
        m_description = description;
        m_total = total;
        m_current = current;
    }

    ProgressBarContext& operator=(size_t current)
    {
        std::scoped_lock _(m_mutex);
        m_current = current;
        return *this;
    }
    ProgressBarContext& operator++()
    {
        std::scoped_lock _(m_mutex);
        ++m_current;
        return *this;
    }

    [[nodiscard]] auto Lock() const { return std::scoped_lock(m_mutex); }
    [[nodiscard]] bool IsRunning() const
    {
        return m_task.valid() && m_task.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    }
    [[nodiscard]] bool IsIndeterminate() const
    {
        std::scoped_lock _(m_mutex);
        return !m_total;
    }
    [[nodiscard]] std::string GetDescription() const
    {
        std::scoped_lock _(m_mutex);
        return m_description;
    }
    [[nodiscard]] std::tuple<float, size_t, size_t> GetProgress() const
    {
        std::scoped_lock _(m_mutex);
        return { IsIndeterminate() ? 0.0f : (float)m_current / (float)m_total, m_current, m_total };
    }

    void Run(std::function<void(ProgressBarContext&)>&& func)
    {
        m_task = std::async(std::launch::async, std::move(func), std::ref(*this));
    }
};

}
