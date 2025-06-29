export module GW2Viewer.Utils.Async;
import GW2Viewer.Common;
import std;

export namespace Utils::Async
{

struct Scheduler
{
    struct ProgressContext
    {
        bool Cancelled = false;
        uint32 Current = 0;
        uint32 Total = 1;
        std::mutex Lock;
        ProgressContext(bool cancelled = false) : Cancelled(cancelled) { }
        ProgressContext(ProgressContext const& source) : Cancelled(source.Cancelled), Current(source.Current), Total(source.Total) { }
        operator bool() const { return !Cancelled; }

        void Cancel() { Cancelled = true; }
        void Finish() { Cancel(); }
        void Clear() { Current = 0; }
        void Increment(uint32 amount = 1) { Current += amount; }
        void InterlockedIncrement(uint32 amount = 1) { std::scoped_lock _(Lock); Current += amount; }
        void SetTotal(uint32 total) { Total = total; }
        void SetIndeterminate() { SetTotal(0); }
        bool IsIndeterminate() const { return !Total; }
        float Progress() const { return (float)Current / Total; }
        float ProgressPercent() const { return 100.0f * Progress(); }
    };
    using Context = std::shared_ptr<ProgressContext>;

    Scheduler(bool allowParallelTasks = false) : m_allowParallelTasks(allowParallelTasks) { }

    ProgressContext Current() const
    {
        std::scoped_lock _(m_lock);
        return m_current && !m_current->Cancelled ? *m_current : ProgressContext { true };
    }
    void Run(auto&& task)
    {
        std::scoped_lock _(m_lock);
        if (auto const previous = std::exchange(m_current, std::make_shared<Context::element_type>()))
            previous->Cancel();

        if (m_allowParallelTasks)
            m_runningTasks.remove_if([](std::future<void> const& future) { return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
        else
            m_runningTasks.clear(); // This joins the ongoing task thread and waits for its completion, which should be fast thanks to many cancellation checks

        m_runningTasks.emplace_back(std::async(std::launch::async, std::move(task), m_current));
    }

private:
    bool m_allowParallelTasks = false;
    std::list<std::future<void>> m_runningTasks;
    Context m_current = nullptr;
    mutable std::mutex m_lock;
};
using Context = Scheduler::Context;

}
