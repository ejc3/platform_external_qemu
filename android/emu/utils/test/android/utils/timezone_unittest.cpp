// Copyright 2026 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/utils/timezone.h"

#include <gtest/gtest.h>

namespace android {
namespace utils {

// Names returned by strftime("%Z") on localized Windows installations. They
// are localized display names, not timezone key names.
// "Tokyo Standard Time" in Japanese (UTF-8).
constexpr char kJapaneseTokyoStandardName[] =
        "\xe6\x9d\xb1\xe4\xba\xac (\xe6\xa8\x99\xe6\xba\x96\xe6\x99\x82)";
// "W. Europe Standard Time" in Norwegian.
constexpr char kNorwegianWEuropeStandardName[] = "Vest-Europa (normaltid)";

TEST(TimezoneTest, Win32ToZoneinfoMapsKeyNames) {
    EXPECT_STREQ("Asia/Tokyo",
                 timezone_win32_to_zoneinfo("Tokyo Standard Time", nullptr));
    EXPECT_STREQ("Europe/Berlin", timezone_win32_to_zoneinfo(
                                          "W. Europe Standard Time", nullptr));
    EXPECT_STREQ("America/Los_Angeles",
                 timezone_win32_to_zoneinfo("Pacific Standard Time", nullptr));
    EXPECT_STREQ("Etc/GMT", timezone_win32_to_zoneinfo("UTC", nullptr));

    // Key names added by Windows updates (see CLDR windowsZones.xml).
    EXPECT_STREQ(
            "America/Punta_Arenas",
            timezone_win32_to_zoneinfo("Magallanes Standard Time", nullptr));
    EXPECT_STREQ("Asia/Omsk",
                 timezone_win32_to_zoneinfo("Omsk Standard Time", nullptr));
    EXPECT_STREQ("Asia/Qyzylorda", timezone_win32_to_zoneinfo(
                                           "Qyzylorda Standard Time", nullptr));
    EXPECT_STREQ("Africa/Sao_Tome",
                 timezone_win32_to_zoneinfo("Sao Tome Standard Time", nullptr));
    EXPECT_STREQ("Europe/Saratov",
                 timezone_win32_to_zoneinfo("Saratov Standard Time", nullptr));
    EXPECT_STREQ("Africa/Juba", timezone_win32_to_zoneinfo(
                                        "South Sudan Standard Time", nullptr));
    EXPECT_STREQ("Africa/Khartoum",
                 timezone_win32_to_zoneinfo("Sudan Standard Time", nullptr));
    EXPECT_STREQ("Etc/GMT-13", timezone_win32_to_zoneinfo("UTC+13", nullptr));
    EXPECT_STREQ(
            "Europe/Volgograd",
            timezone_win32_to_zoneinfo("Volgograd Standard Time", nullptr));
    EXPECT_STREQ("America/Whitehorse",
                 timezone_win32_to_zoneinfo("Yukon Standard Time", nullptr));
}

TEST(TimezoneTest, Win32ToZoneinfoMapsKeyNamesToZonesWithMatchingOffset) {
    // Asia/Almaty moved to UTC+5 in 2024 and America/Chihuahua to UTC-6 in
    // 2022, so they no longer match these UTC+6 and UTC-7 timezones.
    EXPECT_STREQ(
            "Asia/Bishkek",
            timezone_win32_to_zoneinfo("Central Asia Standard Time", nullptr));
    EXPECT_STREQ("America/Mazatlan",
                 timezone_win32_to_zoneinfo("Mountain Standard Time (Mexico)",
                                            nullptr));
}

TEST(TimezoneTest, Win32ToZoneinfoUsesKeyNameOnLocalizedWindows) {
    // b/574875267: Japanese Windows.
    EXPECT_STREQ("Asia/Tokyo",
                 timezone_win32_to_zoneinfo("Tokyo Standard Time",
                                            kJapaneseTokyoStandardName));
    // b/532790747: Norwegian Windows.
    EXPECT_STREQ("Europe/Berlin",
                 timezone_win32_to_zoneinfo("W. Europe Standard Time",
                                            kNorwegianWEuropeStandardName));
}

TEST(TimezoneTest, Win32ToZoneinfoPrefersKeyNameOverStandardName) {
    EXPECT_STREQ("Asia/Tokyo",
                 timezone_win32_to_zoneinfo("Tokyo Standard Time",
                                            "Pacific Standard Time"));
    // Even on English Windows, the standard name of the "UTC" key differs from
    // the key name.
    EXPECT_STREQ("Etc/GMT", timezone_win32_to_zoneinfo(
                                    "UTC", "Coordinated Universal Time"));
}

TEST(TimezoneTest, Win32ToZoneinfoFallsBackToStandardName) {
    EXPECT_STREQ("Asia/Tokyo",
                 timezone_win32_to_zoneinfo(nullptr, "Tokyo Standard Time"));
    EXPECT_STREQ("Asia/Tokyo",
                 timezone_win32_to_zoneinfo("", "Tokyo Standard Time"));
    EXPECT_STREQ("Asia/Tokyo",
                 timezone_win32_to_zoneinfo("Unmapped Standard Time",
                                            "Tokyo Standard Time"));
}

TEST(TimezoneTest, Win32ToZoneinfoReturnsNullForUnknownNames) {
    EXPECT_EQ(nullptr, timezone_win32_to_zoneinfo(nullptr, nullptr));
    EXPECT_EQ(nullptr, timezone_win32_to_zoneinfo("", ""));
    // Localized names cannot be mapped on their own.
    EXPECT_EQ(nullptr,
              timezone_win32_to_zoneinfo(nullptr, kJapaneseTokyoStandardName));
    EXPECT_EQ(nullptr, timezone_win32_to_zoneinfo(
                               nullptr, kNorwegianWEuropeStandardName));
}

}  // namespace utils
}  // namespace android
