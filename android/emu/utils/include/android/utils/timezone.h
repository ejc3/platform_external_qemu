/* Copyright (C) 2007-2008 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#pragma once

#include "android/utils/compiler.h"
#include <time.h>

ANDROID_BEGIN_HEADER

/* Try to set the default android OS timezone. This operation will affect the
 * emulated networked time in virtual modem
 * returns 0 on success, or -1 if 'tzname' is not in zoneinfo format (e.g. Area/Location)
 */
extern int  timezone_set( const char*  tzname );

/* append the current host "zoneinfo" timezone name to a given buffer. note
 * that this is something like "America/Los_Angeles", and not the human-friendly "PST"
 * this is required by the Android emulated system...
 *
 * the implementation of this function is really tricky and is OS-dependent
 * on Unix systems, it needs to cater to the TZ environment variable, uhhh
 *
 * if TZ is defined to something like "CET" or "PST", this will return the name "Unknown/Unknown"
 */
extern char*  bufprint_zoneinfo_timezone( char*  buffer, char*  end );

/* Return the timezone offset including day light saving in seconds with respect
 * to UTC in host OS
 * + positive if the timezone is east of GMT
 * - negative if the timezone is west of GMT
 */
extern long android_tzoffset_in_seconds(time_t* utc_time);

/* Return the zoneinfo timezone name (e.g. "Asia/Tokyo") of a Windows
 * timezone, or NULL if it is not known.
 *
 * |key_name| is the timezone's registry key name, as reported by
 * GetDynamicTimeZoneInformation() (e.g. "Tokyo Standard Time"). It is never
 * localized, so it is looked up first.
 *
 * |standard_name| is the timezone name reported by strftime("%Z"). It is only
 * used as a fallback because it is localized on non-English Windows
 * installations.
 *
 * Both names can be NULL or empty. This function is available on all hosts so
 * that it can be unit tested.
 */
extern const char* timezone_win32_to_zoneinfo(const char* key_name,
                                              const char* standard_name);

/* Return non-zero if |tzname|, as returned by bufprint_zoneinfo_timezone(),
 * should be sent to the guest with the qemu.timezone boot property. |tzname|
 * is the timezone requested with -timezone if QEMU accepted it (see
 * timezone_set()), otherwise the host timezone.
 *
 * When this property is set, the guest copies it into persist.sys.timezone,
 * overriding the timezone the user selected in the guest (b/532790747). It is
 * therefore only sent if |tzname| is a valid zoneinfo name (not
 * "Unknown/Unknown") and either:
 *   - it is the timezone requested with -timezone, passed in
 *     |requested_tzname| (NULL if the option was not used), or
 *   - the userdata partition is initialized, i.e. |wipe_data| is non-zero.
 *     This happens with -wipe-data and on the first boot of an AVD: the
 *     emulator sets wipe_data when the AVD has no userdata-qemu.img yet.
 */
extern int timezone_should_inject_boot_property(const char* tzname,
                                                int wipe_data,
                                                const char* requested_tzname);

ANDROID_END_HEADER
