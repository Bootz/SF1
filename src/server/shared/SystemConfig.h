/*
 * Copyright (C) 2010-2012 Oregon <http://www.oregoncore.com/>
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2012 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// THIS FILE IS DEPRECATED

#ifndef OREGON_SYSTEMCONFIG_H
#define OREGON_SYSTEMCONFIG_H

#include "Define.h"
#include "revision.h"

#define _PACKAGENAME "SkyFireEMU"
#define _CODENAME "Project TBC"

#if OREGON_ENDIAN == OREGON_BIGENDIAN
# define _ENDIAN_STRING "big-endian"
#else
# define _ENDIAN_STRING "little-endian"
#endif

#if PLATFORM == PLATFORM_WINDOWS
# ifdef _WIN64
#  define _FULLVERSION _PACKAGENAME "Rev: " _REVISION  " Hash: " _HASH " (Win64," _ENDIAN_STRING ")"
# else
#  define _FULLVERSION _PACKAGENAME "Rev: " _REVISION  " Hash: " _HASH " (Win32," _ENDIAN_STRING ")"
# endif
#else
#  define _FULLVERSION _PACKAGENAME "Rev: " _REVISION  " Hash: " _HASH " (Unix," _ENDIAN_STRING ")"
#endif

#define DEFAULT_PLAYER_LIMIT 100
#define DEFAULT_WORLDSERVER_PORT 8085                       //8129
#define DEFAULT_REALMSERVER_PORT 3724
#define DEFAULT_SOCKET_SELECT_TIME 10000
#endif

