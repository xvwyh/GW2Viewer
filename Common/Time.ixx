export module GW2Viewer.Common.Time;
import GW2Viewer.Common;
import std;
import <corecrt.h>;

export using namespace std::chrono_literals;

export namespace GW2Viewer::Time
{

using Us = std::chrono::microseconds;
using Ms = std::chrono::milliseconds;
using Secs = std::chrono::seconds;
using Mins = std::chrono::minutes;
using Hours = std::chrono::hours;
using Days = std::chrono::days;
using Weeks = std::chrono::weeks;
using Months = std::chrono::months;
using Years = std::chrono::years;

using Timestamp = time_t;
using TimestampMs = int64;

using Clock = std::chrono::system_clock;
using Point = Clock::time_point;
using PointMs = decltype(std::chrono::floor<Ms>(Point { }));
using PointSecs = decltype(std::chrono::floor<Secs>(Point { }));
using Duration = Clock::duration;

using FileClock = std::chrono::file_clock;
using FilePoint = FileClock::time_point;
using FileDuration = FileClock::duration;

using PreciseClock = std::chrono::high_resolution_clock;
using PrecisePoint = PreciseClock::time_point;
using PreciseDuration = PreciseClock::duration;

Point Now() { return Clock::now(); }
PrecisePoint PreciseNow() { return PreciseClock::now(); }

template<typename Dur, typename Clock, typename FromDur> std::chrono::time_point<Clock, Dur> Cast(std::chrono::time_point<Clock, FromDur> time) { return std::chrono::floor<Dur>(time); }
template<typename Dur, typename Rep, typename Ratio> Dur Cast(std::chrono::duration<Rep, Ratio> duration) { return std::chrono::duration_cast<Dur>(duration); }

template<typename Rep, typename Ratio> Ms ToMs(std::chrono::duration<Rep, Ratio> duration) { return Cast<Ms>(duration); }
template<typename Rep, typename Ratio> Secs ToSecs(std::chrono::duration<Rep, Ratio> duration) { return Cast<Secs>(duration); }

auto ToMs(Point time) { return Cast<Ms>(time); }
auto ToSecs(Point time) { return Cast<Secs>(time); }

Point FromFileTime(FilePoint fileTime)
{
    try { return std::chrono::utc_clock::to_sys(FileClock::to_utc(fileTime)); }
    catch (...) { return Point { fileTime.time_since_epoch() - ToSecs(FileDuration { 0x19DB1DED53E8000LL }) }; }
}

Timestamp ToTimestamp(Point time) { return Clock::to_time_t(time); }
Timestamp ToTimestamp(FilePoint fileTime) { return ToTimestamp(FromFileTime(fileTime)); }
PointSecs FromTimestamp(Timestamp time) { return ToSecs(Clock::from_time_t(time)); }

TimestampMs ToTimestampMs(Point time) { return ToMs(time.time_since_epoch()).count(); }
Point FromTimestampMs(TimestampMs ms) { return Point { Ms { ms } }; }

template<typename Dur, typename From, typename To> Dur Between(From from, To to) { return Cast<Dur>(to - from); }
Duration Between(Point from, Point to) { return Between<Duration>(from, to); }
Secs BetweenSecs(Point from, Point to) { return Between<Secs>(from, to); }

template<typename Dur, typename From> Dur UntilNow(From from) { return Between<Dur>(from, Now()); }
Duration UntilNow(Point from) { return UntilNow<Duration>(from); }
Secs UntilNowSecs(Point from) { return UntilNow<Secs>(from); }

}
