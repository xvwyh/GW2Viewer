module;
#include <assert.h>

export module GW2Viewer.Utils.Async.ProgressBarContext;
import GW2Viewer.Utils.Encoding;
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
    void Start(size_t total, size_t current)
    {
        std::scoped_lock _(m_mutex);
        m_total = total;
        m_current = current;
    }
    void Start(size_t total = 0)
    {
        std::scoped_lock _(m_mutex);
        m_total = total;
    }
    void SetDescription(std::string_view description)
    {
        std::scoped_lock _(m_mutex);
        m_description = description;
    }

    ProgressBarContext& operator=(size_t current)
    {
        std::scoped_lock _(m_mutex);
        m_current = current;
        return *this;
    }
    ProgressBarContext& operator+=(size_t increment) { return *this = m_current + increment; }
    ProgressBarContext& operator++() { return *this += 1; }

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
        m_task = std::async(std::launch::async, [this, func = std::move(func)](ProgressBarContext& context)
        {
            try
            {
                func(context);
            }
            catch (std::exception const& ex)
            {
                _wassert(Encoding::ToWString(ex.what()).c_str(), _CRT_WIDE(__FILE__), __LINE__);
            }
            catch (...)
            {
                assert(false && "ProgressBarContext task threw an unknown exception");
            }
        }, std::ref(*this));
    }
};

}
