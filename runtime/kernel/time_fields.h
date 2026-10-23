#pragma once

// Conversion between Xbox kernel times (100 ns intervals since 1601-01-01 UTC,
// the Windows FILETIME epoch) and calendar fields, with the same validation and
// results as Win32 FileTimeToSystemTime and SystemTimeToFileTime. Pure
// arithmetic on the proleptic Gregorian calendar, so it is host independent.

#include <cstdint>
#include <optional>

namespace mojorecomp::kernel {

struct CalendarTime
{
    uint16_t year = 0;
    uint16_t month = 0;        // 1..12
    uint16_t day = 0;          // 1..31
    uint16_t hour = 0;
    uint16_t minute = 0;
    uint16_t second = 0;
    uint16_t milliseconds = 0;
    uint16_t weekday = 0;      // 0 = Sunday; ignored by CalendarToTime
};

namespace time_fields_detail {

constexpr int64_t kTicksPerMillisecond = 10'000;
constexpr int64_t kMillisecondsPerDay = 86'400'000;
// Days from 1601-01-01 to 1970-01-01.
constexpr int64_t kDaysFrom1601To1970 = 134'774;
constexpr uint16_t kMinimumYear = 1601;
constexpr uint16_t kMaximumYear = 30827;

constexpr bool IsLeapYear(int64_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

constexpr uint16_t DaysInMonth(int64_t year, uint16_t month)
{
    constexpr uint16_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && IsLeapYear(year) ? 29 : kDays[month - 1];
}

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's
// days_from_civil).
constexpr int64_t DaysFromCivil(int64_t year, int64_t month, int64_t day)
{
    year -= month <= 2 ? 1 : 0;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t yearOfEra = year - era * 400;
    const int64_t dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146'097 + dayOfEra - 719'468;
}

// Inverse of DaysFromCivil (H. Hinnant's civil_from_days).
constexpr void CivilFromDays(int64_t days, int64_t& year, int64_t& month, int64_t& day)
{
    days += 719'468;
    const int64_t era = (days >= 0 ? days : days - 146'096) / 146'097;
    const int64_t dayOfEra = days - era * 146'097;
    const int64_t yearOfEra =
        (dayOfEra - dayOfEra / 1460 + dayOfEra / 36'524 - dayOfEra / 146'096) / 365;
    const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const int64_t monthIndex = (5 * dayOfYear + 2) / 153;
    day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
    month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
    year = yearOfEra + era * 400 + (month <= 2 ? 1 : 0);
}

} // namespace time_fields_detail

// Fails, like FileTimeToSystemTime, when the top bit of the time is set.
constexpr std::optional<CalendarTime> TimeToCalendar(uint64_t time)
{
    using namespace time_fields_detail;
    if (time > uint64_t(INT64_MAX))
        return std::nullopt;

    const int64_t totalMilliseconds = int64_t(time) / kTicksPerMillisecond;
    const int64_t days = totalMilliseconds / kMillisecondsPerDay;
    int64_t millisecondOfDay = totalMilliseconds % kMillisecondsPerDay;

    int64_t year = 0, month = 0, day = 0;
    CivilFromDays(days - kDaysFrom1601To1970, year, month, day);

    CalendarTime result;
    result.year = uint16_t(year);
    result.month = uint16_t(month);
    result.day = uint16_t(day);
    result.hour = uint16_t(millisecondOfDay / 3'600'000);
    millisecondOfDay %= 3'600'000;
    result.minute = uint16_t(millisecondOfDay / 60'000);
    millisecondOfDay %= 60'000;
    result.second = uint16_t(millisecondOfDay / 1000);
    result.milliseconds = uint16_t(millisecondOfDay % 1000);
    // 1601-01-01 was a Monday.
    result.weekday = uint16_t((days + 1) % 7);
    return result;
}

// Fails, like SystemTimeToFileTime, when any field is out of range. The
// weekday is ignored.
constexpr std::optional<uint64_t> CalendarToTime(const CalendarTime& fields)
{
    using namespace time_fields_detail;
    if (fields.year < kMinimumYear || fields.year > kMaximumYear ||
        fields.month < 1 || fields.month > 12 ||
        fields.day < 1 || fields.day > DaysInMonth(fields.year, fields.month) ||
        fields.hour > 23 || fields.minute > 59 || fields.second > 59 ||
        fields.milliseconds > 999)
        return std::nullopt;

    const int64_t days =
        DaysFromCivil(fields.year, fields.month, fields.day) + kDaysFrom1601To1970;
    const int64_t milliseconds = days * kMillisecondsPerDay +
        int64_t(fields.hour) * 3'600'000 + int64_t(fields.minute) * 60'000 +
        int64_t(fields.second) * 1000 + fields.milliseconds;
    return uint64_t(milliseconds * kTicksPerMillisecond);
}

} // namespace mojorecomp::kernel
