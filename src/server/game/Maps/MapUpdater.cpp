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

#include "MapUpdater.h"
#include "DelayExecutor.h"
#include "Map.h"
#include "Database/DatabaseEnv.h"

#include <ace/Guard_T.h>
#include <ace/Method_Request.h>

//the reason this things are here is that i want to make
//the netcode patch and the multithreaded maps independant
//once they are merged 1 class should be used
class WDBThreadStartReq1 : public ACE_Method_Request
{
    public:
        WDBThreadStartReq1(){}
        virtual int

    call (void)
    {
        WorldDatabase.ThreadStart();
        return 0;
    }
};

class WDBThreadEndReq1 : public ACE_Method_Request
{
    public:
        WDBThreadEndReq1(){}
        virtual int

    call (void)
    {
        WorldDatabase.ThreadEnd();
        return 0;
    }
};

class MapUpdateRequest : public ACE_Method_Request
{
    public:
        Map& m_map;
        MapUpdater& m_updater;
        ACE_UINT32 m_diff;
        MapUpdateRequest(Map& m, MapUpdater& u, ACE_UINT32 d) : m_map(m), m_updater(u), m_diff(d){}
        virtual int

    call (void)
    {
        m_map.Update (m_diff);
        m_updater.update_finished ();
        return 0;
    }
};

MapUpdater::MapUpdater() :
m_mutex(),
m_condition(m_mutex),
m_executor(),
pedning_requests(0)
{
    return;
}

MapUpdater::~MapUpdater()
{
    this->deactivate();
}

int MapUpdater::activate(size_t num_threads)
{
    return this->m_executor.activate(static_cast<int> (num_threads), new WDBThreadStartReq1, new WDBThreadEndReq1);
}

int MapUpdater::deactivate(void)
{
    this->wait();

    return this->m_executor.deactivate();
}

int MapUpdater::wait()
{
    ACE_GUARD_RETURN(ACE_Thread_Mutex, guard, this->m_mutex, -1);

    while(this->pedning_requests > 0)
        this->m_condition.wait();

    return 0;
}

int MapUpdater::schedule_update(Map& map, ACE_UINT32 diff)
{
    ACE_GUARD_RETURN(ACE_Thread_Mutex, guard, this->m_mutex, -1);

    ++this->pedning_requests;

    if (this->m_executor.execute(new MapUpdateRequest(map, *this, diff)) == -1)
    {
        ACE_DEBUG((LM_ERROR, ACE_TEXT("(%t) \n"), ACE_TEXT("Failed to schedule Map Update")));

        --this->pedning_requests;
        return -1;
    }

    return 0;
}

bool MapUpdater::activated()
{
    return m_executor.activated();
}

void MapUpdater::update_finished()
{
    ACE_GUARD(ACE_Thread_Mutex, guard, this->m_mutex);

    if (this->pedning_requests == 0)
    {
        ACE_ERROR((LM_ERROR,ACE_TEXT("(%t)\n"), ACE_TEXT("MapUpdater::update_finished BUG, report to devs")));
        return;
    }

    --this->pedning_requests;

    this->m_condition.broadcast();
}

