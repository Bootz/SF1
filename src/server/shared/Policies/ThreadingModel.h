/*
 * Copyright (C) 2010-2012 Project SkyFire <http://www.projectskyfire.org/>
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

#ifndef TRINITY_THREADINGMODEL_H
#define TRINITY_THREADINGMODEL_H

#include "Define.h"

namespace Trinity
{
    inline void Guard(void *) {}

    template<typename MUTEX> class GeneralLock
    {
        public:
            GeneralLock(MUTEX &m) : i_mutex(m)
            {
                i_mutex.acquire();
            }

            ~GeneralLock()
            {
                i_mutex.release();
            }
        private:
            GeneralLock(const GeneralLock &);
            GeneralLock& operator=(const GeneralLock &);
            MUTEX &i_mutex;
    };

    template <class T>
        class SingleThreaded
    {
        public:

            struct Lock                                     // empty object
            {
                Lock() {}
                Lock(const T &) {}
                Lock(const SingleThreaded<T> &)             // for single threaded we ignore this
                {
                }
            };

            typedef T VolatileType;
    };

    // object level lockable
    template<class T, class MUTEX>
        class ObjectLevelLockable
    {
        public:
            ObjectLevelLockable() : i_mtx() {}

            friend class Lock;

            class Lock
            {
                public:
                    Lock(ObjectLevelLockable<T, MUTEX> &host) : i_lock(host.i_mtx)
                    {
                    }

                private:
                    GeneralLock<MUTEX> i_lock;
            };

            typedef volatile T VolatileType;

        private:
            // prevent the compiler creating a copy construct
            ObjectLevelLockable(const ObjectLevelLockable<T, MUTEX> &);
            ObjectLevelLockable<T, MUTEX>& operator=(const ObjectLevelLockable<T, MUTEX> &);

            MUTEX i_mtx;
    };

    template<class T, class MUTEX>
        class ClassLevelLockable
    {
        public:
            class Lock;
            friend class Lock;
            typedef volatile T VolatileType;

            ClassLevelLockable() {}

            class Lock
            {
                public:
                    Lock(T& /*host*/) { ClassLevelLockable<T, MUTEX>::si_mtx.acquire(); }
                    Lock(ClassLevelLockable<T, MUTEX> &) { ClassLevelLockable<T, MUTEX>::si_mtx.acquire(); }
                    Lock() { ClassLevelLockable<T, MUTEX>::si_mtx.acquire(); }
                    ~Lock() { ClassLevelLockable<T, MUTEX>::si_mtx.release(); }
            };

        private:
            static MUTEX si_mtx;
    };
}

template<class T, class MUTEX> MUTEX Trinity::ClassLevelLockable<T, MUTEX>::si_mtx;

#define INSTANTIATE_CLASS_MUTEX(CTYPE,MUTEX) \
    template class Trinity::ClassLevelLockable<CTYPE, MUTEX >
#endif

