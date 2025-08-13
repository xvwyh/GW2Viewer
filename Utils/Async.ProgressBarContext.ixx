module;
#include <assert.h>

export module GW2Viewer.Utils.Async.ProgressBarContext;
import GW2Viewer.Common.Time;
import GW2Viewer.UI.ImGui;
import GW2Viewer.UI.Notifications;
import GW2Viewer.Utils.Encoding;
import std;
#include "Macros.h"

export namespace GW2Viewer::Utils::Async
{

class ProgressBarContext
{
    std::string m_description;
    size_t m_current = 0;
    size_t m_total = 0;
    mutable std::recursive_mutex m_mutex;
    std::future<void> m_task;
    std::optional<UI::Notification::Handle> m_notification;
    mutable std::recursive_mutex m_notificationMutex;

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

    ProgressBarContext& Run(std::function<void(ProgressBarContext&)>&& func)
    {
        m_task = std::async(std::launch::async, [this, func = std::move(func)](ProgressBarContext& context)
        {
            try
            {
                func(context);

                std::scoped_lock _(m_notificationMutex);
                if (m_notification)
                {
                    m_notification->Close();
                    while (!m_notification->HasClosed())
                        std::this_thread::sleep_for(10ms);
                    m_notification.reset();
                }
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
        return *this;
    }
    ProgressBarContext& ShowNotification()
    {
        std::scoped_lock _(m_notificationMutex);
        if (!m_notification)
        {
            m_notification.emplace(G::Notifications.AddPersistent({
                .WidthMin = 300,
                .WidthMax = 300,
                .Draw = [this](UI::Notification::Handle const& notification)
                {
                    if (auto lock = Lock(); IsRunning())
                    {
                        I::TextWrapped("%s", GetDescription().c_str());
                        if (IsIndeterminate())
                        {
                            I::ProgressBar(-I::GetTime(), { -FLT_MIN, 8 }, "");
                        }
                        else if (scoped::WithStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2()))
                        {
                            auto [p, current, total] = GetProgress();
                            I::Text("<c=#8>%zu / %zu</c>", current, total);
                            I::ProgressBar(p, { -FLT_MIN, 8 }, "");
                        }
                    }
                }
            }));
        }
        return *this;
    }
};

}
