export module GW2Viewer.UI.Notifications;
import GW2Viewer.Common;
import GW2Viewer.Common.Time;
import GW2Viewer.UI.ImGui;
import GW2Viewer.Utils.Math;
import GW2Viewer.Utils.Visitor;
import std;
#include "Macros.h"

namespace GW2Viewer::UI
{

export
{

struct Notification
{
    struct Handle
    {
        uint32 ID = -1;

        Handle(uint32 id);
        Handle(Handle&& source) noexcept : ID(std::exchange(source.ID, -1)) { } // No RefCounter changes
        ~Handle();

        Handle(Handle const&) = delete;
        Handle& operator=(Handle const&) = delete;
        Handle& operator=(Handle&& source) noexcept { ID = std::exchange(source.ID, -1); return *this; } // No RefCounter changes

        void Close() const;
        bool HasClosed() const;
    };

    std::string Text;
    float WidthMin = 0;
    float WidthMax = FLT_MAX;
    std::function<void(Handle handle)> Draw;
};

}

struct NotificationManager
{
    Notification::Handle AddCloseable(Notification notification)
    {
        std::scoped_lock lock(m_mutex);
        return m_active.try_emplace(m_nextID++, notification, ActiveNotification::Closeable { }).first->first;
    }
    Notification::Handle AddTimed(Time::Duration duration, Notification notification)
    {
        std::scoped_lock lock(m_mutex);
        return m_active.try_emplace(m_nextID++, notification, ActiveNotification::Timed { duration, duration }).first->first;
    }
    Notification::Handle AddPersistent(Notification notification)
    {
        std::scoped_lock lock(m_mutex);
        return m_active.try_emplace(m_nextID++, notification, ActiveNotification::Persistent { }).first->first;
    }

    void Draw()
    {
        std::scoped_lock lock(m_mutex);

        std::erase_if(m_active, [](auto const& pair) { return !pair.second.RefCounter; });

        auto const viewport = I::GetMainViewport();
        auto positionStart = viewport->WorkPos + ImVec2(viewport->WorkSize.x, 0) + ImVec2(-10, 10);
        auto position = positionStart;
        auto rawPosition = position;
        for (auto&& [id, active] : m_active)
        {
            if (active.HasClosed())
                continue;

            auto const& notification = active.Notification;

            if (active.OffsetY == FLT_MAX)
                active.OffsetY = rawPosition.y - positionStart.y;
            Utils::Math::ExpDecayChase(active.OffsetY, rawPosition.y - positionStart.y, 3, 0.1f);
            Utils::Math::ExpDecayChase(active.Alpha, active.Closing ? 0.0f : 1.0f, active.Closing ? 5 : 15, 0.001f);

            position.y = positionStart.y + active.OffsetY;

            I::SetNextWindowPos(position, ImGuiCond_Always, { 1, 0 });
            I::SetNextWindowViewport(viewport->ID);
            I::SetNextWindowSizeConstraints({ notification.WidthMin, 0 }, { notification.WidthMax, FLT_MAX });
            I::SetNextWindowBgAlpha(active.Alpha * 0.9f);
            if (scoped::WithStyleVar(ImGuiStyleVar_Alpha, active.Alpha))
            if (scoped::Window(std::format("###Notification-{}", id).c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | (active.Closing ? ImGuiWindowFlags_NoInputs : 0)))
            {
                bool needsSeparator = false;
                if (!notification.Text.empty())
                {
                    I::TextUnformatted(notification.Text.data());
                    needsSeparator = true;
                }
                if (std::visit(Utils::Visitor::Overloaded
                {
                    [&](ActiveNotification::Closeable& closeable)
                    {
                        I::PushClipRect(viewport->WorkPos, viewport->WorkPos + viewport->WorkSize, false);
                        if (I::CloseButton(I::GetID("##CLOSE"), position + ImVec2(-I::GetTextLineHeight() * 4 / 5, -I::GetTextLineHeight() / 5)))
                            closeable.CloseRequested = true;
                        I::PopClipRect();
                        return closeable.CloseRequested;
                    },
                    [&](ActiveNotification::Timed& timed)
                    {
                        timed.Remaining -= Time::Delta;
                        I::PushClipRect(I::GetWindowPos(), I::GetWindowPos() + ImVec2(I::GetWindowWidth() * timed.Remaining.count() / timed.Total.count(), 4), true);
                        I::GetWindowDrawList()->AddRectFilled(I::GetWindowPos(), I::GetWindowPos() + I::GetWindowSize(), I::GetColorU32(ImGuiCol_ButtonHovered), I::GetStyle().WindowRounding, ImDrawFlags_RoundCornersTop);
                        I::PopClipRect();
                        I::PushClipRect(viewport->WorkPos, viewport->WorkPos + viewport->WorkSize, false);
                        if (I::CloseButton(I::GetID("##CLOSE"), position + ImVec2(-I::GetTextLineHeight() * 4 / 5, -I::GetTextLineHeight() / 5)))
                            timed.Remaining = 0s;
                        I::PopClipRect();
                        return timed.Remaining <= 0s;
                    },
                    [](ActiveNotification::Persistent const& persistent) { return persistent.CloseRequested; }
                }, active.Type) && active.Alpha >= 1) // Alpha check is not necessary, it's there to delay the notification from closing too fast, preventing it from being shown entirely
                    active.Closing = true;

                if (active.Notification.Draw)
                {
                    if (std::exchange(needsSeparator, false))
                        I::Separator();

                    active.Notification.Draw(id);
                }

                if (!active.Closing)
                {
                    auto const offset = I::GetCurrentWindow()->DC.CursorMaxPos.y + I::GetStyle().WindowPadding.y - I::GetCurrentWindow()->Pos.y + 5;
                    position.y += offset;
                    rawPosition.y += offset;
                }
            }

            if (active.HasClosed())
                --active.RefCounter;
        }
    }

private:
    std::recursive_mutex m_mutex;

    struct ActiveNotification
    {
        struct Closeable
        {
            bool CloseRequested = false;
        };
        struct Timed
        {
            Time::PreciseDuration Total;
            Time::PreciseDuration Remaining;
        };
        struct Persistent
        {
            bool CloseRequested = false;
        };

        Notification const Notification;
        std::variant<Closeable, Timed, Persistent> Type;
        float OffsetY = FLT_MAX;
        float Alpha = 0.0f;
        bool Closing = false;
        std::atomic<uint32> RefCounter = 1;

        bool HasClosed() const { return Closing && Alpha <= 0; }
    };
    std::map<uint32, ActiveNotification> m_active;
    uint32 m_nextID = 0;
    [[nodiscard]] auto GetByID(uint32 id)
    {
        std::pair<std::unique_lock<decltype(m_mutex)>, ActiveNotification*> result { m_mutex, nullptr };
        if (auto const itr = m_active.find(id); itr != m_active.end())
            result.second = &itr->second;
        return result;
    }

    friend Notification::Handle;
};

}

export namespace GW2Viewer::G { UI::NotificationManager Notifications; }

namespace GW2Viewer::UI
{

Notification::Handle::Handle(uint32 id) : ID(id)
{
    if (auto [lock, active] = G::Notifications.GetByID(ID); active)
        ++active->RefCounter;
}
Notification::Handle::~Handle()
{
    if (auto [lock, active] = G::Notifications.GetByID(ID); active)
        --active->RefCounter;
}
void Notification::Handle::Close() const
{
    if (auto [lock, active] = G::Notifications.GetByID(ID); active)
    {
        std::visit(Utils::Visitor::Overloaded
        {
            [](NotificationManager::ActiveNotification::Closeable& closeable) { closeable.CloseRequested = true; },
            [](NotificationManager::ActiveNotification::Timed& timed) { timed.Remaining = { }; },
            [](NotificationManager::ActiveNotification::Persistent& persistent) { persistent.CloseRequested = true; },
        }, active->Type);
    }
}
bool Notification::Handle::HasClosed() const
{
    if (auto [lock, active] = G::Notifications.GetByID(ID); active)
        return active->HasClosed();

    return false;
}

}
