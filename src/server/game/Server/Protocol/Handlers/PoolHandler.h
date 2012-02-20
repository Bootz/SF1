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

#ifndef MANGOS_POOLHANDLER_H
#define MANGOS_POOLHANDLER_H

#include "Define.h"
#include "Creature.h"
#include "GameObject.h"

struct PoolTemplateData
{
    uint32  MaxLimit;
};

struct PoolObject
{
    uint32  guid;
    float   chance;
    bool    spawned;
    PoolObject(uint32 _guid, float _chance): guid(_guid), chance(fabs(_chance)), spawned(false) {}
};

template <class T>
class PoolGroup
{
    typedef std::vector<PoolObject> PoolObjectList;
    public:
        PoolGroup() : m_SpawnedPoolAmount(0) {}
        ~PoolGroup() {};
        bool isEmpty() { return ExplicitlyChanced.empty() && EqualChanced.empty(); }
        void AddEntry(PoolObject& poolitem, uint32 maxentries);
        bool CheckPool(void);
        void RollOne(int32& index, PoolObjectList** store, uint32 triggerFrom);
        bool IsSpawnedObject(uint32 guid);
        void DespawnObject(uint32 guid=0);
        void Despawn1Object(uint32 guid);
        void SpawnObject(uint32 limit, uint32 triggerFrom);
        bool Spawn1Object(uint32 guid);
        bool ReSpawn1Object(uint32 guid);
        void RemoveOneRelation(uint16 child_pool_id);
    private:
        PoolObjectList ExplicitlyChanced;
        PoolObjectList EqualChanced;
        uint32 m_SpawnedPoolAmount;                         // Used to know the number of spawned objects
};

class Pool                                                  // for Pool of Pool case
{
};

class PoolHandler
{
    public:
        PoolHandler();
        ~PoolHandler() {};
        void LoadFromDB();
        uint16 IsPartOfAPool(uint32 guid, uint32 type);
        bool IsSpawnedObject(uint16 pool_id, uint32 guid, uint32 type);
        bool CheckPool(uint16 pool_id);
        void SpawnPool(uint16 pool_id, uint32 guid, uint32 type);
        void DespawnPool(uint16 pool_id);
        void UpdatePool(uint16 pool_id, uint32 guid, uint32 type);
        void Initialize();

    protected:
        bool m_IsPoolSystemStarted;
        uint16 max_pool_id;
        typedef std::vector<PoolTemplateData> PoolTemplateDataMap;
        typedef std::vector<PoolGroup<Creature> >   PoolGroupCreatureMap;
        typedef std::vector<PoolGroup<GameObject> > PoolGroupGameObjectMap;
        typedef std::vector<PoolGroup<Pool> >       PoolGroupPoolMap;
        typedef std::pair<uint32, uint16> SearchPair;
        typedef std::map<uint32, uint16> SearchMap;

        PoolTemplateDataMap mPoolTemplate;
        PoolGroupCreatureMap mPoolCreatureGroups;
        PoolGroupGameObjectMap mPoolGameobjectGroups;
        PoolGroupPoolMap mPoolPoolGroups;
        SearchMap mCreatureSearchMap;
        SearchMap mGameobjectSearchMap;
        SearchMap mPoolSearchMap;
};

#define poolhandler Oregon::Singleton<PoolHandler>::Instance()
#endif

