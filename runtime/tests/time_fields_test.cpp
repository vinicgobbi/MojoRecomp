// Kernel time <-> calendar field conversion. Checks known dates, validation,
// and round trips everywhere; on Windows also compares many values against
// FileTimeToSystemTime and SystemTimeToFileTime, which the runtime used before.
#include "../kernel/time_fields.h"

#include <cstdint>
#include <cstdio>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using mojorecomp::kernel::CalendarTime;
using mojorecomp::kernel::CalendarToTime;
using mojorecomp::kernel::TimeToCalendar;

namespace {

int g_failures = 0;

void Check(bool condition, const char* description)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition)
        ++g_failures;
}

bool Matches(const std::optional<CalendarTime>& value, uint16_t year, uint16_t month,
             uint16_t day, uint16_t hour, uint16_t minute, uint16_t second,
             uint16_t milliseconds, uint16_t weekday)
{
    return value && value->year == year && value->month == month && value->day == day &&
           value->hour == hour && value->minute == minute && value->second == second &&
           value->milliseconds == milliseconds && value->weekday == weekday;
}

CalendarTime Fields(uint16_t year, uint16_t month, uint16_t day, uint16_t hour = 0,
                    uint16_t minute = 0, uint16_t second = 0, uint16_t milliseconds = 0)
{
    CalendarTime fields;
    fields.year = year;
    fields.month = month;
    fields.day = day;
    fields.hour = hour;
    fields.minute = minute;
    fields.second = second;
    fields.milliseconds = milliseconds;
    return fields;
}

uint64_t NextRandom(uint64_t& state)
{
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return state;
}

void TestKnownValues()
{
    constexpr uint64_t kUnixEpoch = 116'444'736'000'000'000ull;
    Check(Matches(TimeToCalendar(0), 1601, 1, 1, 0, 0, 0, 0, 1),
          "time 0 is Monday 1601-01-01");
    Check(Matches(TimeToCalendar(kUnixEpoch), 1970, 1, 1, 0, 0, 0, 0, 4),
          "Unix epoch is Thursday 1970-01-01");
    Check(Matches(TimeToCalendar(kUnixEpoch + 951'782'400'0000000ull + 12'345'678'9ull),
                  2000, 2, 29, 0, 0, 12, 345, 2),
          "leap day 2000-02-29 with sub-second ticks");
    Check(Matches(TimeToCalendar(uint64_t(INT64_MAX)), 30828, 9, 14, 2, 48, 5, 477, 4),
          "largest valid time is 30828-09-14 02:48:05.477");
    Check(!TimeToCalendar(uint64_t(INT64_MAX) + 1), "times with the top bit set fail");

    Check(CalendarToTime(Fields(1601, 1, 1)) == 0u, "1601-01-01 converts to 0");
    Check(CalendarToTime(Fields(1970, 1, 1)) == kUnixEpoch, "1970-01-01 converts to the Unix epoch");
}

void TestValidation()
{
    Check(!CalendarToTime(Fields(1600, 12, 31)), "year before 1601 fails");
    Check(!CalendarToTime(Fields(30828, 1, 1)), "year after 30827 fails");
    Check(CalendarToTime(Fields(30827, 12, 31, 23, 59, 59, 999)).has_value(),
          "last day of 30827 converts");
    Check(!CalendarToTime(Fields(2024, 0, 1)) && !CalendarToTime(Fields(2024, 13, 1)),
          "month outside 1..12 fails");
    Check(!CalendarToTime(Fields(2001, 2, 29)), "Feb 29 in a common year fails");
    Check(CalendarToTime(Fields(2000, 2, 29)).has_value(), "Feb 29 in a leap year converts");
    Check(!CalendarToTime(Fields(1900, 2, 29)), "Feb 29 in 1900 fails");
    Check(!CalendarToTime(Fields(2024, 4, 31)), "April 31 fails");
    Check(!CalendarToTime(Fields(2024, 1, 0)), "day 0 fails");
    Check(!CalendarToTime(Fields(2024, 1, 1, 24)), "hour 24 fails");
    Check(!CalendarToTime(Fields(2024, 1, 1, 0, 60)), "minute 60 fails");
    Check(!CalendarToTime(Fields(2024, 1, 1, 0, 0, 60)), "second 60 fails");
    Check(!CalendarToTime(Fields(2024, 1, 1, 0, 0, 0, 1000)), "millisecond 1000 fails");
}

void TestRoundTrips()
{
    uint64_t state = 0x4D6F6A6F52656330ull;
    bool roundTrips = true;
    for (int i = 0; i < 100'000; ++i)
    {
        // Whole milliseconds, because calendar fields drop sub-millisecond ticks.
        const uint64_t time = (NextRandom(state) % (uint64_t(INT64_MAX) / 10'000)) * 10'000;
        const auto calendar = TimeToCalendar(time);
        if (!calendar || (calendar->year <= 30827 && CalendarToTime(*calendar) != time))
            roundTrips = false;
    }
    Check(roundTrips, "random times round-trip through calendar fields");
}

#if defined(_WIN32)
void TestAgainstWindows()
{
    uint64_t state = 0x57696E33325F5443ull;
    bool toFieldsMatches = true;
    bool toTimeMatches = true;
    for (int i = 0; i < 200'000; ++i)
    {
        uint64_t time = NextRandom(state);
        if (i % 4 == 0)
            time &= uint64_t(INT64_MAX);

        FILETIME fileTime{static_cast<DWORD>(time), static_cast<DWORD>(time >> 32)};
        SYSTEMTIME systemTime{};
        const bool windowsOk = FileTimeToSystemTime(&fileTime, &systemTime) != FALSE;
        const auto ours = TimeToCalendar(time);
        if (windowsOk != ours.has_value() ||
            (windowsOk && !Matches(ours, systemTime.wYear, systemTime.wMonth, systemTime.wDay,
                                   systemTime.wHour, systemTime.wMinute, systemTime.wSecond,
                                   systemTime.wMilliseconds, systemTime.wDayOfWeek)))
            toFieldsMatches = false;

        SYSTEMTIME fields{};
        fields.wYear = uint16_t(1590 + NextRandom(state) % 29'250);
        fields.wMonth = uint16_t(NextRandom(state) % 14);
        fields.wDay = uint16_t(NextRandom(state) % 33);
        fields.wHour = uint16_t(NextRandom(state) % 26);
        fields.wMinute = uint16_t(NextRandom(state) % 62);
        fields.wSecond = uint16_t(NextRandom(state) % 62);
        fields.wMilliseconds = uint16_t(NextRandom(state) % 1002);
        FILETIME converted{};
        const bool windowsTimeOk = SystemTimeToFileTime(&fields, &converted) != FALSE;
        const auto ourTime = CalendarToTime(Fields(fields.wYear, fields.wMonth, fields.wDay,
                                                   fields.wHour, fields.wMinute,
                                                   fields.wSecond, fields.wMilliseconds));
        const uint64_t windowsTime = uint64_t(converted.dwLowDateTime) |
                                     (uint64_t(converted.dwHighDateTime) << 32);
        if (windowsTimeOk != ourTime.has_value() || (windowsTimeOk && *ourTime != windowsTime))
            toTimeMatches = false;
    }
    Check(toFieldsMatches, "TimeToCalendar matches FileTimeToSystemTime");
    Check(toTimeMatches, "CalendarToTime matches SystemTimeToFileTime");
}
#endif

} // namespace

int main()
{
    TestKnownValues();
    TestValidation();
    TestRoundTrips();
#if defined(_WIN32)
    TestAgainstWindows();
#endif
    return g_failures == 0 ? 0 : 1;
}
