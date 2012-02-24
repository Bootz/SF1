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

#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "Config.h"
#include "SystemConfig.h"
#include "Log.h"
#include "Opcodes.h"
#include "WorldSession.h"
#include "WorldPacket.h"
#include "Weather.h"
#include "Player.h"
#include "SkillExtraItems.h"
#include "SkillDiscovery.h"
#include "World.h"
#include "AccountMgr.h"
#include "AuctionHouseMgr.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Chat.h"
#include "DBCStores.h"
#include "LootMgr.h"
#include "ItemEnchantmentMgr.h"
#include "MapManager.h"
#include "CreatureAIRegistry.h"
#include "Policies/SingletonImp.h"
#include "BattlegroundMgr.h"
#include "OutdoorPvPMgr.h"
#include "TemporarySummon.h"
#include "AuctionHouseBot.h"
#include "WaypointMovementGenerator.h"
#include "VMapFactory.h"
#include "GameEventMgr.h"
#include "PoolHandler.h"
#include "Database/DatabaseImpl.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "InstanceSaveMgr.h"
#include "TicketMgr.h"
#include "Util.h"
#include "Language.h"
#include "CreatureFormations.h"
#include "CreatureGroups.h"
#include "Transport.h"
#include "CreatureEventAIMgr.h"
#include "ScriptMgr.h"

INSTANTIATE_SINGLETON_1(World);

volatile bool World::m_stopEvent = false;
uint8 World::m_ExitCode = SHUTDOWN_EXIT_CODE;
volatile uint32 World::m_worldLoopCounter = 0;

float World::m_MaxVisibleDistanceOnContinents = DEFAULT_VISIBILITY_DISTANCE;
float World::m_MaxVisibleDistanceInInstances  = DEFAULT_VISIBILITY_INSTANCE;
float World::m_MaxVisibleDistanceInBGArenas   = DEFAULT_VISIBILITY_BGARENAS;
float World::m_MaxVisibleDistanceForObject    = DEFAULT_VISIBILITY_DISTANCE;

float World::m_MaxVisibleDistanceInFlight     = DEFAULT_VISIBILITY_DISTANCE;
float World::m_VisibleUnitGreyDistance        = 0;
float World::m_VisibleObjectGreyDistance      = 0;

int32 World::m_visibility_notify_periodOnContinents = DEFAULT_VISIBILITY_NOTIFY_PERIOD;
int32 World::m_visibility_notify_periodInInstances  = DEFAULT_VISIBILITY_NOTIFY_PERIOD;
int32 World::m_visibility_notify_periodInBGArenas   = DEFAULT_VISIBILITY_NOTIFY_PERIOD;

// World constructor
World::World()
{
    m_playerLimit = 0;
    m_allowedSecurityLevel = SEC_PLAYER;
    m_allowMovement = true;
    m_ShutdownMask = 0;
    m_ShutdownTimer = 0;
    m_gameTime=time(NULL);
    m_startTime=m_gameTime;
    m_maxActiveSessionCount = 0;
    m_maxQueuedSessionCount = 0;
    m_resultQueue = NULL;
    m_NextDailyQuestReset = 0;
    m_scheduledScripts = 0;

    m_defaultDbcLocale = LOCALE_enUS;
    m_availableDbcLocaleMask = 0;

    m_updateTimeSum = 0;
    m_updateTimeCount = 0;
}

// World destructor
World::~World()
{
    // Empty the kicked session set
    while (!m_sessions.empty())
    {
        // not remove from queue, prevent loading new sessions
        delete m_sessions.begin()->second;
        m_sessions.erase(m_sessions.begin());
    }

    // Empty the WeatherMap
    for (WeatherMap::iterator itr = m_weathers.begin(); itr != m_weathers.end(); ++itr)
        delete itr->second;

    m_weathers.clear();

    CliCommandHolder* command;
    while (cliCmdQueue.next(command))
        delete command;

    VMAP::VMapFactory::clear();

    delete m_resultQueue;

    //TODO free addSessQueue
}

// Find a player in a specified zone
Player* World::FindPlayerInZone(uint32 zone)
{
    // circle through active sessions and return the first player found in the zone
    SessionMap::iterator itr;
    for (itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
    {
        if (!itr->second)
            continue;
        Player *player = itr->second->GetPlayer();
        if (!player)
            continue;
        if (player->IsInWorld() && player->GetZoneId() == zone)
        {
            // Used by the weather system. We return the player to broadcast the change weather message to him and all players in the zone.
            return player;
        }
    }
    return NULL;
}

// Find a session by its id
WorldSession* World::FindSession(uint32 id) const
{
    SessionMap::const_iterator itr = m_sessions.find(id);

    if (itr != m_sessions.end())
        return itr->second;                                 // also can return NULL for kicked session
    else
        return NULL;
}

// Remove a given session
bool World::RemoveSession(uint32 id)
{
    // Find the session, kick the user, but we can't delete session at this moment to prevent iterator invalidation
    SessionMap::iterator itr = m_sessions.find(id);

    if (itr != m_sessions.end() && itr->second)
    {
        if (itr->second->PlayerLoading())
            return false;
        itr->second->KickPlayer();
    }

    return true;
}

void World::AddSession(WorldSession* s)
{
    addSessQueue.add(s);
}

void
World::AddSession_ (WorldSession* s)
{
    ASSERT (s);

    //NOTE - Still there is race condition in WorldSession* being used in the Sockets

    // kick already loaded player with same account (if any) and remove session
    // if player is in loading and want to load again, return
    if (!RemoveSession (s->GetAccountId ()))
    {
        s->KickPlayer ();
        delete s;                                           // session not added yet in session list, so not listed in queue
        return;
    }

    // decrease session counts only at not reconnection case
    bool decrease_session = true;

    // if session already exist, prepare to it deleting at next world update
    // NOTE - KickPlayer() should be called on "old" in RemoveSession()
    {
        SessionMap::const_iterator old = m_sessions.find(s->GetAccountId ());

        if (old != m_sessions.end())
        {
            // prevent decrease sessions count if session queued
            if (RemoveQueuedPlayer(old->second))
                decrease_session = false;
            // not remove replaced session form queue if listed
            delete old->second;
        }
    }

    m_sessions[s->GetAccountId ()] = s;

    uint32 Sessions = GetActiveAndQueuedSessionCount ();
    uint32 pLimit = GetPlayerAmountLimit ();
    uint32 QueueSize = GetQueueSize ();                     //number of players in the queue

    //so we don't count the user trying to
    //login as a session and queue the socket that we are using
    if (decrease_session)
        --Sessions;

    if (pLimit > 0 && Sessions >= pLimit && s->GetSecurity () == SEC_PLAYER && !HasRecentlyDisconnected(s))
    {
        AddQueuedPlayer (s);
        UpdateMaxSessionCounters ();
        sLog->outDetail ("PlayerQueue: Account id %u is in Queue Position (%u).", s->GetAccountId (), ++QueueSize);
        return;
    }

    WorldPacket packet(SMSG_AUTH_RESPONSE, 1 + 4 + 1 + 4 + 1);
    packet << uint8 (AUTH_OK);
    packet << uint32 (0);                                   // BillingTimeRemaining
    packet << uint8 (0);                                    // BillingPlanFlags
    packet << uint32 (0);                                   // BillingTimeRested
    packet << uint8 (s->Expansion());                       // 0 - normal, 1 - TBC, must be set in database manually for each account
    s->SendPacket (&packet);

    UpdateMaxSessionCounters ();

    // Updates the population
    if (pLimit > 0)
    {
        float popu = GetActiveSessionCount ();              // updated number of users on the server
        popu /= pLimit;
        popu *= 2;
        LoginDatabase.PExecute ("UPDATE realmlist SET population = '%f' WHERE id = '%d'", popu, realmID);
        sLog->outDetail ("Server Population (%f).", popu);
    }
}

bool World::HasRecentlyDisconnected(WorldSession* session)
{
    if (!session) return false;

    if (uint32 tolerance = getConfig(CONFIG_INTERVAL_DISCONNECT_TOLERANCE))
    {
        for (DisconnectMap::iterator i = m_disconnects.begin(); i != m_disconnects.end();)
        {
            if (difftime(i->second, time(NULL)) < tolerance)
            {
                if (i->first == session->GetAccountId())
                    return true;
                ++i;
            }
            else
                m_disconnects.erase(i);
        }
    }
    return false;
 }

int32 World::GetQueuePos(WorldSession* sess)
{
    uint32 position = 1;

    for (Queue::iterator iter = m_QueuedPlayer.begin(); iter != m_QueuedPlayer.end(); ++iter, ++position)
        if ((*iter) == sess)
            return position;

    return 0;
}

void World::AddQueuedPlayer(WorldSession* sess)
{
    sess->SetInQueue(true);
    m_QueuedPlayer.push_back(sess);

    // The 1st SMSG_AUTH_RESPONSE needs to contain other info too.
    WorldPacket packet(SMSG_AUTH_RESPONSE, 1+4+1+4+1);
    packet << uint8(AUTH_WAIT_QUEUE);
    packet << uint32(0);                                    // BillingTimeRemaining
    packet << uint8(0);                                     // BillingPlanFlags
    packet << uint32(0);                                    // BillingTimeRested
    packet << uint8(sess->Expansion());                     // 0 - normal, 1 - TBC, must be set in database manually for each account
    packet << uint32(GetQueuePos(sess));                    // Queue position
    sess->SendPacket(&packet);

    //sess->SendAuthWaitQue (GetQueuePos (sess));
}

bool World::RemoveQueuedPlayer(WorldSession* sess)
{
    // sessions count including queued to remove (if removed_session set)
    uint32 sessions = GetActiveSessionCount();

    uint32 position = 1;
    Queue::iterator iter = m_QueuedPlayer.begin();

    // search to remove and count skipped positions
    bool found = false;

    for (; iter != m_QueuedPlayer.end(); ++iter, ++position)
    {
        if (*iter == sess)
        {
            sess->SetInQueue(false);
            iter = m_QueuedPlayer.erase(iter);
            found = true;                                   // removing queued session
            break;
        }
    }

    // iter point to next socked after removed or end()
    // position store position of removed socket and then new position next socket after removed

    // if session not queued then we need decrease sessions count
    if (!found && sessions)
        --sessions;

    // accept first in queue
    if ((!m_playerLimit || sessions < m_playerLimit) && !m_QueuedPlayer.empty())
    {
        WorldSession* pop_sess = m_QueuedPlayer.front();
        pop_sess->SetInQueue(false);
        pop_sess->SendAuthWaitQue(0);
        m_QueuedPlayer.pop_front();

        // update iter to point first queued socket or end() if queue is empty now
        iter = m_QueuedPlayer.begin();
        position = 1;
    }

    // update position from iter to end()
    // iter point to first not updated socket, position store new position
    for (; iter != m_QueuedPlayer.end(); ++iter, ++position)
        (*iter)->SendAuthWaitQue(position);

    return found;
}

// Find a Weather object by the given zoneid
Weather* World::FindWeather(uint32 id) const
{
    WeatherMap::const_iterator itr = m_weathers.find(id);

    if (itr != m_weathers.end())
        return itr->second;
    else
        return 0;
}

// Remove a Weather object for the given zoneid
void World::RemoveWeather(uint32 id)
{
    // not called at the moment. Kept for completeness
    WeatherMap::iterator itr = m_weathers.find(id);

    if (itr != m_weathers.end())
    {
        delete itr->second;
        m_weathers.erase(itr);
    }
}

// Add a Weather object to the list
Weather* World::AddWeather(uint32 zone_id)
{
    WeatherZoneChances const* weatherChances = objmgr.GetWeatherChances(zone_id);

    // zone not have weather, ignore
    if (!weatherChances)
        return NULL;

    Weather* w = new Weather(zone_id, weatherChances);
    m_weathers[w->GetZone()] = w;
    w->ReGenerate();
    w->UpdateWeather();
    return w;
}

// Initialize config values
void World::LoadConfigSettings(bool reload)
{
    if (reload)
    {
        if (!ConfigMgr::Load())
        {
            sLog->outError("World settings reload fail: can't read settings from %s.",ConfigMgr::GetFilename().c_str());
            return;
        }
    }

    // Read the player limit and the Message of the day from the config file
    SetPlayerLimit(ConfigMgr::GetIntDefault("PlayerLimit", DEFAULT_PLAYER_LIMIT), true);
    SetMotd(ConfigMgr::GetStringDefault("Motd", "Welcome to a Trinity Core Server."));

    // Get string for new logins (newly created characters)
    SetNewCharString(ConfigMgr::GetStringDefault("PlayerStart.String", ""));

    // Send server info on login?
    m_configs[CONFIG_ENABLE_SINFO_LOGIN] = ConfigMgr::GetIntDefault("Server.LoginInfo", 0);

    // Read all rates from the config file
    rate_values[RATE_HEALTH]      = ConfigMgr::GetFloatDefault("Rate.Health", 1);
    if (rate_values[RATE_HEALTH] < 0)
    {
        sLog->outError("Rate.Health (%f) must be > 0. Using 1 instead.",rate_values[RATE_HEALTH]);
        rate_values[RATE_HEALTH] = 1;
    }
    rate_values[RATE_POWER_MANA]  = ConfigMgr::GetFloatDefault("Rate.Mana", 1);
    if (rate_values[RATE_POWER_MANA] < 0)
    {
        sLog->outError("Rate.Mana (%f) must be > 0. Using 1 instead.",rate_values[RATE_POWER_MANA]);
        rate_values[RATE_POWER_MANA] = 1;
    }
    rate_values[RATE_POWER_RAGE_INCOME] = ConfigMgr::GetFloatDefault("Rate.Rage.Income", 1);
    rate_values[RATE_POWER_RAGE_LOSS]   = ConfigMgr::GetFloatDefault("Rate.Rage.Loss", 1);
    if (rate_values[RATE_POWER_RAGE_LOSS] < 0)
    {
        sLog->outError("Rate.Rage.Loss (%f) must be > 0. Using 1 instead.",rate_values[RATE_POWER_RAGE_LOSS]);
        rate_values[RATE_POWER_RAGE_LOSS] = 1;
    }
    rate_values[RATE_POWER_FOCUS] = ConfigMgr::GetFloatDefault("Rate.Focus", 1.0f);
    rate_values[RATE_POWER_ENERGY] = ConfigMgr::GetFloatDefault("Rate.Energy", 1.0f);
    rate_values[RATE_LOYALTY]     = ConfigMgr::GetFloatDefault("Rate.Loyalty", 1.0f);
    rate_values[RATE_SKILL_DISCOVERY] = ConfigMgr::GetFloatDefault("Rate.Skill.Discovery", 1.0f);
    rate_values[RATE_DROP_ITEM_POOR]       = ConfigMgr::GetFloatDefault("Rate.Drop.Item.Poor", 1.0f);
    rate_values[RATE_DROP_ITEM_NORMAL]     = ConfigMgr::GetFloatDefault("Rate.Drop.Item.Normal", 1.0f);
    rate_values[RATE_DROP_ITEM_UNCOMMON]   = ConfigMgr::GetFloatDefault("Rate.Drop.Item.Uncommon", 1.0f);
    rate_values[RATE_DROP_ITEM_RARE]       = ConfigMgr::GetFloatDefault("Rate.Drop.Item.Rare", 1.0f);
    rate_values[RATE_DROP_ITEM_EPIC]       = ConfigMgr::GetFloatDefault("Rate.Drop.Item.Epic", 1.0f);
    rate_values[RATE_DROP_ITEM_LEGENDARY]  = ConfigMgr::GetFloatDefault("Rate.Drop.Item.Legendary", 1.0f);
    rate_values[RATE_DROP_ITEM_ARTIFACT]   = ConfigMgr::GetFloatDefault("Rate.Drop.Item.Artifact", 1.0f);
    rate_values[RATE_DROP_ITEM_REFERENCED] = ConfigMgr::GetFloatDefault("Rate.Drop.Item.Referenced", 1.0f);
    rate_values[RATE_DROP_MONEY]  = ConfigMgr::GetFloatDefault("Rate.Drop.Money", 1.0f);
    rate_values[RATE_XP_KILL]     = ConfigMgr::GetFloatDefault("Rate.XP.Kill", 1.0f);
    rate_values[RATE_XP_QUEST]    = ConfigMgr::GetFloatDefault("Rate.XP.Quest", 1.0f);
    rate_values[RATE_XP_EXPLORE]  = ConfigMgr::GetFloatDefault("Rate.XP.Explore", 1.0f);
    rate_values[RATE_XP_PAST_70]  = ConfigMgr::GetFloatDefault("Rate.XP.PastLevel70", 1.0f);
    rate_values[RATE_REPUTATION_GAIN]  = ConfigMgr::GetFloatDefault("Rate.Reputation.Gain", 1.0f);
    rate_values[RATE_CREATURE_NORMAL_DAMAGE]          = ConfigMgr::GetFloatDefault("Rate.Creature.Normal.Damage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_ELITE_DAMAGE]     = ConfigMgr::GetFloatDefault("Rate.Creature.Elite.Elite.Damage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_RAREELITE_DAMAGE] = ConfigMgr::GetFloatDefault("Rate.Creature.Elite.RAREELITE.Damage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_WORLDBOSS_DAMAGE] = ConfigMgr::GetFloatDefault("Rate.Creature.Elite.WORLDBOSS.Damage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_RARE_DAMAGE]      = ConfigMgr::GetFloatDefault("Rate.Creature.Elite.RARE.Damage", 1.0f);
    rate_values[RATE_CREATURE_NORMAL_HP]          = ConfigMgr::GetFloatDefault("Rate.Creature.Normal.HP", 1.0f);
    rate_values[RATE_CREATURE_ELITE_ELITE_HP]     = ConfigMgr::GetFloatDefault("Rate.Creature.Elite.Elite.HP", 1.0f);
    rate_values[RATE_CREATURE_ELITE_RAREELITE_HP] = ConfigMgr::GetFloatDefault("Rate.Creature.Elite.RAREELITE.HP", 1.0f);
    rate_values[RATE_CREATURE_ELITE_WORLDBOSS_HP] = ConfigMgr::GetFloatDefault("Rate.Creature.Elite.WORLDBOSS.HP", 1.0f);
    rate_values[RATE_CREATURE_ELITE_RARE_HP]      = ConfigMgr::GetFloatDefault("Rate.Creature.Elite.RARE.HP", 1.0f);
    rate_values[RATE_CREATURE_NORMAL_SPELLDAMAGE]          = ConfigMgr::GetFloatDefault("Rate.Creature.Normal.SpellDamage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_ELITE_SPELLDAMAGE]     = ConfigMgr::GetFloatDefault("Rate.Creature.Elite.Elite.SpellDamage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_RAREELITE_SPELLDAMAGE] = ConfigMgr::GetFloatDefault("Rate.Creature.Elite.RAREELITE.SpellDamage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_WORLDBOSS_SPELLDAMAGE] = ConfigMgr::GetFloatDefault("Rate.Creature.Elite.WORLDBOSS.SpellDamage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_RARE_SPELLDAMAGE]      = ConfigMgr::GetFloatDefault("Rate.Creature.Elite.RARE.SpellDamage", 1.0f);
    rate_values[RATE_CREATURE_AGGRO]  = ConfigMgr::GetFloatDefault("Rate.Creature.Aggro", 1.0f);
    rate_values[RATE_REST_INGAME]                    = ConfigMgr::GetFloatDefault("Rate.Rest.InGame", 1.0f);
    rate_values[RATE_REST_OFFLINE_IN_TAVERN_OR_CITY] = ConfigMgr::GetFloatDefault("Rate.Rest.Offline.InTavernOrCity", 1.0f);
    rate_values[RATE_REST_OFFLINE_IN_WILDERNESS]     = ConfigMgr::GetFloatDefault("Rate.Rest.Offline.InWilderness", 1.0f);
    rate_values[RATE_DAMAGE_FALL]  = ConfigMgr::GetFloatDefault("Rate.Damage.Fall", 1.0f);
    rate_values[RATE_AUCTION_TIME]  = ConfigMgr::GetFloatDefault("Rate.Auction.Time", 1.0f);
    rate_values[RATE_AUCTION_DEPOSIT] = ConfigMgr::GetFloatDefault("Rate.Auction.Deposit", 1.0f);
    rate_values[RATE_AUCTION_CUT] = ConfigMgr::GetFloatDefault("Rate.Auction.Cut", 1.0f);
    rate_values[RATE_HONOR] = ConfigMgr::GetFloatDefault("Rate.Honor",1.0f);
    rate_values[RATE_MINING_AMOUNT] = ConfigMgr::GetFloatDefault("Rate.Mining.Amount",1.0f);
    rate_values[RATE_MINING_NEXT]   = ConfigMgr::GetFloatDefault("Rate.Mining.Next",1.0f);
    rate_values[RATE_INSTANCE_RESET_TIME] = ConfigMgr::GetFloatDefault("Rate.InstanceResetTime",1.0f);
    rate_values[RATE_TALENT] = ConfigMgr::GetFloatDefault("Rate.Talent",1.0f);
    if (rate_values[RATE_TALENT] < 0.0f)
    {
        sLog->outError("Rate.Talent (%f) must be > 0. Using 1 instead.",rate_values[RATE_TALENT]);
        rate_values[RATE_TALENT] = 1.0f;
    }
    rate_values[RATE_CORPSE_DECAY_LOOTED] = ConfigMgr::GetFloatDefault("Rate.Corpse.Decay.Looted",0.5f);

    rate_values[RATE_TARGET_POS_RECALCULATION_RANGE] = ConfigMgr::GetFloatDefault("TargetPosRecalculateRange",1.5f);
    if (rate_values[RATE_TARGET_POS_RECALCULATION_RANGE] < CONTACT_DISTANCE)
    {
        sLog->outError("TargetPosRecalculateRange (%f) must be >= %f. Using %f instead.",rate_values[RATE_TARGET_POS_RECALCULATION_RANGE],CONTACT_DISTANCE, CONTACT_DISTANCE);
        rate_values[RATE_TARGET_POS_RECALCULATION_RANGE] = CONTACT_DISTANCE;
    }
    else if (rate_values[RATE_TARGET_POS_RECALCULATION_RANGE] > NOMINAL_MELEE_RANGE)
    {
        sLog->outError("TargetPosRecalculateRange (%f) must be <= %f. Using %f instead.",
            rate_values[RATE_TARGET_POS_RECALCULATION_RANGE],NOMINAL_MELEE_RANGE, NOMINAL_MELEE_RANGE);
        rate_values[RATE_TARGET_POS_RECALCULATION_RANGE] = NOMINAL_MELEE_RANGE;
    }

    rate_values[RATE_DURABILITY_LOSS_DAMAGE] = ConfigMgr::GetFloatDefault("DurabilityLossChance.Damage",0.5f);
    if (rate_values[RATE_DURABILITY_LOSS_DAMAGE] < 0.0f)
    {
        sLog->outError("DurabilityLossChance.Damage (%f) must be >=0. Using 0.0 instead.",rate_values[RATE_DURABILITY_LOSS_DAMAGE]);
        rate_values[RATE_DURABILITY_LOSS_DAMAGE] = 0.0f;
    }
    rate_values[RATE_DURABILITY_LOSS_ABSORB] = ConfigMgr::GetFloatDefault("DurabilityLossChance.Absorb",0.5f);
    if (rate_values[RATE_DURABILITY_LOSS_ABSORB] < 0.0f)
    {
        sLog->outError("DurabilityLossChance.Absorb (%f) must be >=0. Using 0.0 instead.",rate_values[RATE_DURABILITY_LOSS_ABSORB]);
        rate_values[RATE_DURABILITY_LOSS_ABSORB] = 0.0f;
    }
    rate_values[RATE_DURABILITY_LOSS_PARRY] = ConfigMgr::GetFloatDefault("DurabilityLossChance.Parry",0.05f);
    if (rate_values[RATE_DURABILITY_LOSS_PARRY] < 0.0f)
    {
        sLog->outError("DurabilityLossChance.Parry (%f) must be >=0. Using 0.0 instead.",rate_values[RATE_DURABILITY_LOSS_PARRY]);
        rate_values[RATE_DURABILITY_LOSS_PARRY] = 0.0f;
    }
    rate_values[RATE_DURABILITY_LOSS_BLOCK] = ConfigMgr::GetFloatDefault("DurabilityLossChance.Block",0.05f);
    if (rate_values[RATE_DURABILITY_LOSS_BLOCK] < 0.0f)
    {
        sLog->outError("DurabilityLossChance.Block (%f) must be >=0. Using 0.0 instead.",rate_values[RATE_DURABILITY_LOSS_BLOCK]);
        rate_values[RATE_DURABILITY_LOSS_BLOCK] = 0.0f;
    }

    // Read other configuration items from the config file

    m_configs[CONFIG_COMPRESSION] = ConfigMgr::GetIntDefault("Compression", 1);
    if (m_configs[CONFIG_COMPRESSION] < 1 || m_configs[CONFIG_COMPRESSION] > 9)
    {
        sLog->outError("Compression level (%i) must be in range 1..9. Using default compression level (1).",m_configs[CONFIG_COMPRESSION]);
        m_configs[CONFIG_COMPRESSION] = 1;
    }
    m_configs[CONFIG_ADDON_CHANNEL] = ConfigMgr::GetBoolDefault("AddonChannel", true);
    m_configs[CONFIG_GRID_UNLOAD] = ConfigMgr::GetBoolDefault("GridUnload", true);
    m_configs[CONFIG_INTERVAL_SAVE] = ConfigMgr::GetIntDefault("PlayerSaveInterval", 900000);
    m_configs[CONFIG_INTERVAL_DISCONNECT_TOLERANCE] = ConfigMgr::GetIntDefault("DisconnectToleranceInterval", 0);

    m_configs[CONFIG_INTERVAL_GRIDCLEAN] = ConfigMgr::GetIntDefault("GridCleanUpDelay", 300000);
    if (m_configs[CONFIG_INTERVAL_GRIDCLEAN] < MIN_GRID_DELAY)
    {
        sLog->outError("GridCleanUpDelay (%i) must be greater %u. Use this minimal value.",m_configs[CONFIG_INTERVAL_GRIDCLEAN],MIN_GRID_DELAY);
        m_configs[CONFIG_INTERVAL_GRIDCLEAN] = MIN_GRID_DELAY;
    }
    if (reload)
        MapManager::Instance().SetGridCleanUpDelay(m_configs[CONFIG_INTERVAL_GRIDCLEAN]);

    m_configs[CONFIG_INTERVAL_MAPUPDATE] = ConfigMgr::GetIntDefault("MapUpdateInterval", 100);
    if (m_configs[CONFIG_INTERVAL_MAPUPDATE] < MIN_MAP_UPDATE_DELAY)
    {
        sLog->outError("MapUpdateInterval (%i) must be greater %u. Use this minimal value.",m_configs[CONFIG_INTERVAL_MAPUPDATE],MIN_MAP_UPDATE_DELAY);
        m_configs[CONFIG_INTERVAL_MAPUPDATE] = MIN_MAP_UPDATE_DELAY;
    }
    if (reload)
        MapManager::Instance().SetMapUpdateInterval(m_configs[CONFIG_INTERVAL_MAPUPDATE]);

    m_configs[CONFIG_INTERVAL_CHANGEWEATHER] = ConfigMgr::GetIntDefault("ChangeWeatherInterval", 10 * MINUTE * IN_MILLISECONDS);

    if (reload)
    {
        uint32 val = ConfigMgr::GetIntDefault("WorldServerPort", DEFAULT_WORLDSERVER_PORT);
        if (val != m_configs[CONFIG_PORT_WORLD])
            sLog->outError("WorldServerPort option can't be changed at Trinityd.conf reload, using current value (%u).",m_configs[CONFIG_PORT_WORLD]);
    }
    else
        m_configs[CONFIG_PORT_WORLD] = ConfigMgr::GetIntDefault("WorldServerPort", DEFAULT_WORLDSERVER_PORT);

    if (reload)
    {
        uint32 val = ConfigMgr::GetIntDefault("SocketSelectTime", DEFAULT_SOCKET_SELECT_TIME);
        if (val != m_configs[CONFIG_SOCKET_SELECTTIME])
            sLog->outError("SocketSelectTime option can't be changed at Trinityd.conf reload, using current value (%u).",m_configs[DEFAULT_SOCKET_SELECT_TIME]);
    }
    else
        m_configs[CONFIG_SOCKET_SELECTTIME] = ConfigMgr::GetIntDefault("SocketSelectTime", DEFAULT_SOCKET_SELECT_TIME);

    m_configs[CONFIG_SOCKET_TIMEOUTTIME] = ConfigMgr::GetIntDefault("SocketTimeOutTime", 900000);
    m_configs[CONFIG_SESSION_ADD_DELAY] = ConfigMgr::GetIntDefault("SessionAddDelay", 10000);

    m_configs[CONFIG_GROUP_XP_DISTANCE] = ConfigMgr::GetIntDefault("MaxGroupXPDistance", 74);
    // todo Add MonsterSight and GuarderSight (with meaning) in Trinityd.conf or put them as define
    m_configs[CONFIG_SIGHT_MONSTER] = ConfigMgr::GetIntDefault("MonsterSight", 50);
    m_configs[CONFIG_SIGHT_GUARDER] = ConfigMgr::GetIntDefault("GuarderSight", 50);

    if (reload)
    {
        uint32 val = ConfigMgr::GetIntDefault("GameType", 0);
        if (val != m_configs[CONFIG_GAME_TYPE])
            sLog->outError("GameType option can't be changed at Trinityd.conf reload, using current value (%u).",m_configs[CONFIG_GAME_TYPE]);
    }
    else
        m_configs[CONFIG_GAME_TYPE] = ConfigMgr::GetIntDefault("GameType", 0);

    if (reload)
    {
        uint32 val = ConfigMgr::GetIntDefault("RealmZone", REALM_ZONE_DEVELOPMENT);
        if (val != m_configs[CONFIG_REALM_ZONE])
            sLog->outError("RealmZone option can't be changed at Trinityd.conf reload, using current value (%u).",m_configs[CONFIG_REALM_ZONE]);
    }
    else
        m_configs[CONFIG_REALM_ZONE] = ConfigMgr::GetIntDefault("RealmZone", REALM_ZONE_DEVELOPMENT);

    m_configs[CONFIG_ALLOW_TWO_SIDE_ACCOUNTS]            = ConfigMgr::GetBoolDefault("AllowTwoSide.Accounts", false);
    m_configs[CONFIG_ALLOW_TWO_SIDE_INTERACTION_CHAT]    = ConfigMgr::GetBoolDefault("AllowTwoSide.Interaction.Chat",false);
    m_configs[CONFIG_ALLOW_TWO_SIDE_INTERACTION_CHANNEL] = ConfigMgr::GetBoolDefault("AllowTwoSide.Interaction.Channel",false);
    m_configs[CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP]   = ConfigMgr::GetBoolDefault("AllowTwoSide.Interaction.Group",false);
    m_configs[CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD]   = ConfigMgr::GetBoolDefault("AllowTwoSide.Interaction.Guild",false);
    m_configs[CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION] = ConfigMgr::GetBoolDefault("AllowTwoSide.Interaction.Auction",false);
    m_configs[CONFIG_ALLOW_TWO_SIDE_INTERACTION_MAIL]    = ConfigMgr::GetBoolDefault("AllowTwoSide.Interaction.Mail",false);
    m_configs[CONFIG_ALLOW_TWO_SIDE_WHO_LIST]            = ConfigMgr::GetBoolDefault("AllowTwoSide.WhoList", false);
    m_configs[CONFIG_ALLOW_TWO_SIDE_ADD_FRIEND]          = ConfigMgr::GetBoolDefault("AllowTwoSide.AddFriend", false);
    m_configs[CONFIG_ALLOW_TWO_SIDE_TRADE]               = ConfigMgr::GetBoolDefault("AllowTwoSide.trade", false);
    m_configs[CONFIG_STRICT_PLAYER_NAMES]                = ConfigMgr::GetIntDefault ("StrictPlayerNames", 0);
    m_configs[CONFIG_STRICT_CHARTER_NAMES]               = ConfigMgr::GetIntDefault ("StrictCharterNames", 0);
    m_configs[CONFIG_STRICT_PET_NAMES]                   = ConfigMgr::GetIntDefault ("StrictPetNames",    0);

    m_configs[CONFIG_CHARACTERS_CREATING_DISABLED]       = ConfigMgr::GetIntDefault ("CharactersCreatingDisabled", 0);

    m_configs[CONFIG_CHARACTERS_PER_REALM] = ConfigMgr::GetIntDefault("CharactersPerRealm", 10);
    if (m_configs[CONFIG_CHARACTERS_PER_REALM] < 1 || m_configs[CONFIG_CHARACTERS_PER_REALM] > 10)
    {
        sLog->outError("CharactersPerRealm (%i) must be in range 1..10. Set to 10.",m_configs[CONFIG_CHARACTERS_PER_REALM]);
        m_configs[CONFIG_CHARACTERS_PER_REALM] = 10;
    }

    // must be after CONFIG_CHARACTERS_PER_REALM
    m_configs[CONFIG_CHARACTERS_PER_ACCOUNT] = ConfigMgr::GetIntDefault("CharactersPerAccount", 50);
    if (m_configs[CONFIG_CHARACTERS_PER_ACCOUNT] < m_configs[CONFIG_CHARACTERS_PER_REALM])
    {
        sLog->outError("CharactersPerAccount (%i) can't be less than CharactersPerRealm (%i).",m_configs[CONFIG_CHARACTERS_PER_ACCOUNT],m_configs[CONFIG_CHARACTERS_PER_REALM]);
        m_configs[CONFIG_CHARACTERS_PER_ACCOUNT] = m_configs[CONFIG_CHARACTERS_PER_REALM];
    }

    m_configs[CONFIG_SKIP_CINEMATICS] = ConfigMgr::GetIntDefault("SkipCinematics", 0);
    if (int32(m_configs[CONFIG_SKIP_CINEMATICS]) < 0 || m_configs[CONFIG_SKIP_CINEMATICS] > 2)
    {
        sLog->outError("SkipCinematics (%i) must be in range 0..2. Set to 0.",m_configs[CONFIG_SKIP_CINEMATICS]);
        m_configs[CONFIG_SKIP_CINEMATICS] = 0;
    }

    if (reload)
    {
        uint32 val = ConfigMgr::GetIntDefault("MaxPlayerLevel", DEFAULT_MAX_LEVEL);
        if (val != m_configs[CONFIG_MAX_PLAYER_LEVEL])
            sLog->outError("MaxPlayerLevel option can't be changed at config reload, using current value (%u).",m_configs[CONFIG_MAX_PLAYER_LEVEL]);
    }
    else
        m_configs[CONFIG_MAX_PLAYER_LEVEL] = ConfigMgr::GetIntDefault("MaxPlayerLevel", DEFAULT_MAX_LEVEL);

    if (m_configs[CONFIG_MAX_PLAYER_LEVEL] > MAX_LEVEL)
    {
        sLog->outError("MaxPlayerLevel (%i) must be in range 1..%u. Set to %u.",m_configs[CONFIG_MAX_PLAYER_LEVEL],MAX_LEVEL, MAX_LEVEL);
        m_configs[CONFIG_MAX_PLAYER_LEVEL] = MAX_LEVEL;
    }

    m_configs[CONFIG_START_PLAYER_LEVEL] = ConfigMgr::GetIntDefault("StartPlayerLevel", 1);
    if (m_configs[CONFIG_START_PLAYER_LEVEL] < 1)
    {
        sLog->outError("StartPlayerLevel (%i) must be in range 1..MaxPlayerLevel(%u). Set to 1.",m_configs[CONFIG_START_PLAYER_LEVEL],m_configs[CONFIG_MAX_PLAYER_LEVEL]);
        m_configs[CONFIG_START_PLAYER_LEVEL] = 1;
    }
    else if (m_configs[CONFIG_START_PLAYER_LEVEL] > m_configs[CONFIG_MAX_PLAYER_LEVEL])
    {
        sLog->outError("StartPlayerLevel (%i) must be in range 1..MaxPlayerLevel(%u). Set to %u.",m_configs[CONFIG_START_PLAYER_LEVEL],m_configs[CONFIG_MAX_PLAYER_LEVEL],m_configs[CONFIG_MAX_PLAYER_LEVEL]);
        m_configs[CONFIG_START_PLAYER_LEVEL] = m_configs[CONFIG_MAX_PLAYER_LEVEL];
    }

    m_configs[CONFIG_START_PLAYER_MONEY] = ConfigMgr::GetIntDefault("StartPlayerMoney", 0);
    if (int32(m_configs[CONFIG_START_PLAYER_MONEY]) < 0)
    {
        sLog->outError("StartPlayerMoney (%i) must be in range 0..%u. Set to %u.",m_configs[CONFIG_START_PLAYER_MONEY],MAX_MONEY_AMOUNT, 0);
        m_configs[CONFIG_START_PLAYER_MONEY] = 0;
    }
    else if (m_configs[CONFIG_START_PLAYER_MONEY] > MAX_MONEY_AMOUNT)
    {
        sLog->outError("StartPlayerMoney (%i) must be in range 0..%u. Set to %u.",
            m_configs[CONFIG_START_PLAYER_MONEY],MAX_MONEY_AMOUNT, MAX_MONEY_AMOUNT);
        m_configs[CONFIG_START_PLAYER_MONEY] = MAX_MONEY_AMOUNT;
    }

    m_configs[CONFIG_MAX_HONOR_POINTS] = ConfigMgr::GetIntDefault("MaxHonorPoints", 75000);
    if (int32(m_configs[CONFIG_MAX_HONOR_POINTS]) < 0)
    {
        sLog->outError("MaxHonorPoints (%i) can't be negative. Set to 0.",m_configs[CONFIG_MAX_HONOR_POINTS]);
        m_configs[CONFIG_MAX_HONOR_POINTS] = 0;
    }

    m_configs[CONFIG_START_HONOR_POINTS] = ConfigMgr::GetIntDefault("StartHonorPoints", 0);
    if (int32(m_configs[CONFIG_START_HONOR_POINTS]) < 0)
    {
        sLog->outError("StartHonorPoints (%i) must be in range 0..MaxHonorPoints(%u). Set to %u.",
            m_configs[CONFIG_START_HONOR_POINTS],m_configs[CONFIG_MAX_HONOR_POINTS],0);
        m_configs[CONFIG_START_HONOR_POINTS] = 0;
    }
    else if (m_configs[CONFIG_START_HONOR_POINTS] > m_configs[CONFIG_MAX_HONOR_POINTS])
    {
        sLog->outError("StartHonorPoints (%i) must be in range 0..MaxHonorPoints(%u). Set to %u.",
            m_configs[CONFIG_START_HONOR_POINTS],m_configs[CONFIG_MAX_HONOR_POINTS],m_configs[CONFIG_MAX_HONOR_POINTS]);
        m_configs[CONFIG_START_HONOR_POINTS] = m_configs[CONFIG_MAX_HONOR_POINTS];
    }

    m_configs[CONFIG_MAX_ARENA_POINTS] = ConfigMgr::GetIntDefault("MaxArenaPoints", 5000);
    if (int32(m_configs[CONFIG_MAX_ARENA_POINTS]) < 0)
    {
        sLog->outError("MaxArenaPoints (%i) can't be negative. Set to 0.",m_configs[CONFIG_MAX_ARENA_POINTS]);
        m_configs[CONFIG_MAX_ARENA_POINTS] = 0;
    }

    m_configs[CONFIG_START_ARENA_POINTS] = ConfigMgr::GetIntDefault("StartArenaPoints", 0);
    if (int32(m_configs[CONFIG_START_ARENA_POINTS]) < 0)
    {
        sLog->outError("StartArenaPoints (%i) must be in range 0..MaxArenaPoints(%u). Set to %u.",
            m_configs[CONFIG_START_ARENA_POINTS],m_configs[CONFIG_MAX_ARENA_POINTS],0);
        m_configs[CONFIG_START_ARENA_POINTS] = 0;
    }
    else if (m_configs[CONFIG_START_ARENA_POINTS] > m_configs[CONFIG_MAX_ARENA_POINTS])
    {
        sLog->outError("StartArenaPoints (%i) must be in range 0..MaxArenaPoints(%u). Set to %u.",
            m_configs[CONFIG_START_ARENA_POINTS],m_configs[CONFIG_MAX_ARENA_POINTS],m_configs[CONFIG_MAX_ARENA_POINTS]);
        m_configs[CONFIG_START_ARENA_POINTS] = m_configs[CONFIG_MAX_ARENA_POINTS];
    }

    m_configs[CONFIG_ALL_TAXI_PATHS] = ConfigMgr::GetBoolDefault("AllFlightPaths", false);

    m_configs[CONFIG_INSTANCE_IGNORE_LEVEL] = ConfigMgr::GetBoolDefault("Instance.IgnoreLevel", false);
    m_configs[CONFIG_INSTANCE_IGNORE_RAID]  = ConfigMgr::GetBoolDefault("Instance.IgnoreRaid", false);

    m_configs[CONFIG_CAST_UNSTUCK] = ConfigMgr::GetBoolDefault("CastUnstuck", true);
    m_configs[CONFIG_INSTANCE_RESET_TIME_HOUR]  = ConfigMgr::GetIntDefault("Instance.ResetTimeHour", 4);
    m_configs[CONFIG_INSTANCE_UNLOAD_DELAY] = ConfigMgr::GetIntDefault("Instance.UnloadDelay", 30 * MINUTE * IN_MILLISECONDS);

    m_configs[CONFIG_MAX_PRIMARY_TRADE_SKILL] = ConfigMgr::GetIntDefault("MaxPrimaryTradeSkill", 2);
    m_configs[CONFIG_MIN_PETITION_SIGNS] = ConfigMgr::GetIntDefault("MinPetitionSigns", 9);
    if (m_configs[CONFIG_MIN_PETITION_SIGNS] > 9)
    {
        sLog->outError("MinPetitionSigns (%i) must be in range 0..9. Set to 9.", m_configs[CONFIG_MIN_PETITION_SIGNS]);
        m_configs[CONFIG_MIN_PETITION_SIGNS] = 9;
    }

    m_configs[CONFIG_GM_LOGIN_STATE]       = ConfigMgr::GetIntDefault("GM.LoginState", 2);
    m_configs[CONFIG_GM_VISIBLE_STATE]     = ConfigMgr::GetIntDefault("GM.Visible", 2);
    m_configs[CONFIG_GM_CHAT]              = ConfigMgr::GetIntDefault("GM.Chat", 2);
    m_configs[CONFIG_GM_WISPERING_TO]      = ConfigMgr::GetIntDefault("GM.WhisperingTo", 2);
    m_configs[CONFIG_GM_IN_GM_LIST]        = ConfigMgr::GetBoolDefault("GM.InGMList", false);
    m_configs[CONFIG_GM_IN_WHO_LIST]       = ConfigMgr::GetBoolDefault("GM.InWhoList", false);
    m_configs[CONFIG_GM_MAIL]              = ConfigMgr::GetBoolDefault("GM.Mail", false);
    m_configs[CONFIG_GM_LOG_TRADE]         = ConfigMgr::GetBoolDefault("GM.LogTrade", false);
    m_configs[CONFIG_START_GM_LEVEL]       = ConfigMgr::GetIntDefault("GM.StartLevel", 1);
    m_configs[CONFIG_ALLOW_GM_GROUP]       = ConfigMgr::GetBoolDefault("GM.AllowInvite", false);
    m_configs[CONFIG_ALLOW_GM_FRIEND]      = ConfigMgr::GetBoolDefault("GM.AllowFriend", false);
    if (m_configs[CONFIG_START_GM_LEVEL] < m_configs[CONFIG_START_PLAYER_LEVEL])
    {
        sLog->outError("GM.StartLevel (%i) must be in range StartPlayerLevel(%u)..%u. Set to %u.",
            m_configs[CONFIG_START_GM_LEVEL],m_configs[CONFIG_START_PLAYER_LEVEL], MAX_LEVEL, m_configs[CONFIG_START_PLAYER_LEVEL]);
        m_configs[CONFIG_START_GM_LEVEL] = m_configs[CONFIG_START_PLAYER_LEVEL];
    }
    else if (m_configs[CONFIG_START_GM_LEVEL] > MAX_LEVEL)
    {
        sLog->outError("GM.StartLevel (%i) must be in range 1..%u. Set to %u.", m_configs[CONFIG_START_GM_LEVEL], MAX_LEVEL, MAX_LEVEL);
        m_configs[CONFIG_START_GM_LEVEL] = MAX_LEVEL;
    }

    m_configs[CONFIG_CHANCE_OF_GM_SURVEY] = ConfigMgr::GetFloatDefault("GM.TicketSystem.ChanceOfGMSurvey", 0.0f);

    m_configs[CONFIG_GROUP_VISIBILITY] = ConfigMgr::GetIntDefault("Visibility.GroupMode", 1);

    m_configs[CONFIG_MAIL_DELIVERY_DELAY] = ConfigMgr::GetIntDefault("MailDeliveryDelay", HOUR);

    m_configs[CONFIG_EXTERNAL_MAIL] = ConfigMgr::GetIntDefault("ExternalMail", 0);
    m_configs[CONFIG_EXTERNAL_MAIL_INTERVAL] = ConfigMgr::GetIntDefault("ExternalMailInterval", 1);

    m_configs[CONFIG_UPTIME_UPDATE] = ConfigMgr::GetIntDefault("UpdateUptimeInterval", 10);
    if (int32(m_configs[CONFIG_UPTIME_UPDATE]) <= 0)
    {
        sLog->outError("UpdateUptimeInterval (%i) must be > 0, set to default 10.",m_configs[CONFIG_UPTIME_UPDATE]);
        m_configs[CONFIG_UPTIME_UPDATE] = 10;
    }
    if (reload)
    {
        m_timers[WUPDATE_UPTIME].SetInterval(m_configs[CONFIG_UPTIME_UPDATE]*MINUTE*IN_MILLISECONDS);
        m_timers[WUPDATE_UPTIME].Reset();
    }

    // log db cleanup interval
    m_configs[CONFIG_LOGDB_CLEARINTERVAL] = ConfigMgr::GetIntDefault("LogDB.Opt.ClearInterval", 10);
    if (int32(m_configs[CONFIG_LOGDB_CLEARINTERVAL]) <= 0)
    {
        sLog->outError("LogDB.Opt.ClearInterval (%i) must be > 0, set to default 10.", m_configs[CONFIG_LOGDB_CLEARINTERVAL]);
        m_configs[CONFIG_LOGDB_CLEARINTERVAL] = 10;
    }
    if (reload)
    {
        m_timers[WUPDATE_CLEANDB].SetInterval(m_configs[CONFIG_LOGDB_CLEARINTERVAL] * MINUTE * IN_MILLISECONDS);
        m_timers[WUPDATE_CLEANDB].Reset();
    }
    m_configs[CONFIG_LOGDB_CLEARTIME] = ConfigMgr::GetIntDefault("LogDB.Opt.ClearTime", 1209600); // 14 days default
    sLog->outString("Will clear `logs` table of entries older than %i seconds every %u minutes.",
        m_configs[CONFIG_LOGDB_CLEARTIME], m_configs[CONFIG_LOGDB_CLEARINTERVAL]);

    m_configs[CONFIG_SKILL_CHANCE_ORANGE] = ConfigMgr::GetIntDefault("SkillChance.Orange",100);
    m_configs[CONFIG_SKILL_CHANCE_YELLOW] = ConfigMgr::GetIntDefault("SkillChance.Yellow",75);
    m_configs[CONFIG_SKILL_CHANCE_GREEN]  = ConfigMgr::GetIntDefault("SkillChance.Green",25);
    m_configs[CONFIG_SKILL_CHANCE_GREY]   = ConfigMgr::GetIntDefault("SkillChance.Grey",0);

    m_configs[CONFIG_SKILL_CHANCE_MINING_STEPS]  = ConfigMgr::GetIntDefault("SkillChance.MiningSteps",75);
    m_configs[CONFIG_SKILL_CHANCE_SKINNING_STEPS]   = ConfigMgr::GetIntDefault("SkillChance.SkinningSteps",75);

    m_configs[CONFIG_SKILL_PROSPECTING] = ConfigMgr::GetBoolDefault("SkillChance.Prospecting",false);

    m_configs[CONFIG_SKILL_GAIN_CRAFTING]  = ConfigMgr::GetIntDefault("SkillGain.Crafting", 1);
    if (m_configs[CONFIG_SKILL_GAIN_CRAFTING] < 0)
    {
        sLog->outError("SkillGain.Crafting (%i) can't be negative. Set to 1.",m_configs[CONFIG_SKILL_GAIN_CRAFTING]);
        m_configs[CONFIG_SKILL_GAIN_CRAFTING] = 1;
    }

    m_configs[CONFIG_SKILL_GAIN_DEFENSE]  = ConfigMgr::GetIntDefault("SkillGain.Defense", 1);
    if (m_configs[CONFIG_SKILL_GAIN_DEFENSE] < 0)
    {
        sLog->outError("SkillGain.Defense (%i) can't be negative. Set to 1.",m_configs[CONFIG_SKILL_GAIN_DEFENSE]);
        m_configs[CONFIG_SKILL_GAIN_DEFENSE] = 1;
    }

    m_configs[CONFIG_SKILL_GAIN_GATHERING]  = ConfigMgr::GetIntDefault("SkillGain.Gathering", 1);
    if (m_configs[CONFIG_SKILL_GAIN_GATHERING] < 0)
    {
        sLog->outError("SkillGain.Gathering (%i) can't be negative. Set to 1.",m_configs[CONFIG_SKILL_GAIN_GATHERING]);
        m_configs[CONFIG_SKILL_GAIN_GATHERING] = 1;
    }

    m_configs[CONFIG_SKILL_GAIN_WEAPON]  = ConfigMgr::GetIntDefault("SkillGain.Weapon", 1);
    if (m_configs[CONFIG_SKILL_GAIN_WEAPON] < 0)
    {
        sLog->outError("SkillGain.Weapon (%i) can't be negative. Set to 1.",m_configs[CONFIG_SKILL_GAIN_WEAPON]);
        m_configs[CONFIG_SKILL_GAIN_WEAPON] = 1;
    }

    m_configs[CONFIG_MAX_OVERSPEED_PINGS] = ConfigMgr::GetIntDefault("MaxOverspeedPings",2);
    if (m_configs[CONFIG_MAX_OVERSPEED_PINGS] != 0 && m_configs[CONFIG_MAX_OVERSPEED_PINGS] < 2)
    {
        sLog->outError("MaxOverspeedPings (%i) must be in range 2..infinity (or 0 to disable check). Set to 2.",m_configs[CONFIG_MAX_OVERSPEED_PINGS]);
        m_configs[CONFIG_MAX_OVERSPEED_PINGS] = 2;
    }

    m_configs[CONFIG_SAVE_RESPAWN_TIME_IMMEDIATELY] = ConfigMgr::GetBoolDefault("SaveRespawnTimeImmediately",true);
    m_configs[CONFIG_WEATHER] = ConfigMgr::GetBoolDefault("ActivateWeather",true);

    m_configs[CONFIG_DISABLE_BREATHING] = ConfigMgr::GetIntDefault("DisableWaterBreath", SEC_CONSOLE);

    m_configs[CONFIG_ALWAYS_MAX_SKILL_FOR_LEVEL] = ConfigMgr::GetBoolDefault("AlwaysMaxSkillForLevel", false);

    if (reload)
    {
        uint32 val = ConfigMgr::GetIntDefault("Expansion",1);
        if (val != m_configs[CONFIG_EXPANSION])
            sLog->outError("Expansion option can't be changed at Trinityd.conf reload, using current value (%u).",m_configs[CONFIG_EXPANSION]);
    }
    else
        m_configs[CONFIG_EXPANSION] = ConfigMgr::GetIntDefault("Expansion",1);

    m_configs[CONFIG_CHATFLOOD_MESSAGE_COUNT] = ConfigMgr::GetIntDefault("ChatFlood.MessageCount",10);
    m_configs[CONFIG_CHATFLOOD_MESSAGE_DELAY] = ConfigMgr::GetIntDefault("ChatFlood.MessageDelay",1);
    m_configs[CONFIG_CHATFLOOD_MUTE_TIME]     = ConfigMgr::GetIntDefault("ChatFlood.MuteTime",10);

    m_configs[CONFIG_EVENT_ANNOUNCE] = ConfigMgr::GetIntDefault("Event.Announce",0);

    m_configs[CONFIG_CREATURE_FAMILY_FLEE_ASSISTANCE_RADIUS] = ConfigMgr::GetIntDefault("CreatureFamilyFleeAssistanceRadius",30);
    m_configs[CONFIG_CREATURE_FAMILY_ASSISTANCE_RADIUS] = ConfigMgr::GetIntDefault("CreatureFamilyAssistanceRadius",10);
    m_configs[CONFIG_CREATURE_FAMILY_ASSISTANCE_DELAY]  = ConfigMgr::GetIntDefault("CreatureFamilyAssistanceDelay",1500);
    m_configs[CONFIG_CREATURE_FAMILY_FLEE_DELAY]        = ConfigMgr::GetIntDefault("CreatureFamilyFleeDelay",7000);

    m_configs[CONFIG_WORLD_BOSS_LEVEL_DIFF] = ConfigMgr::GetIntDefault("WorldBossLevelDiff",3);

    // note: disable value (-1) will assigned as 0xFFFFFFF, to prevent overflow at calculations limit it to max possible player level MAX_LEVEL(100)
    m_configs[CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF] = ConfigMgr::GetIntDefault("Quests.LowLevelHideDiff", 4);
    if (m_configs[CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF] > MAX_LEVEL)
        m_configs[CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF] = MAX_LEVEL;
    m_configs[CONFIG_QUEST_HIGH_LEVEL_HIDE_DIFF] = ConfigMgr::GetIntDefault("Quests.HighLevelHideDiff", 7);
    if (m_configs[CONFIG_QUEST_HIGH_LEVEL_HIDE_DIFF] > MAX_LEVEL)
        m_configs[CONFIG_QUEST_HIGH_LEVEL_HIDE_DIFF] = MAX_LEVEL;

    m_configs[CONFIG_DETECT_POS_COLLISION] = ConfigMgr::GetBoolDefault("DetectPosCollision", true);

    m_configs[CONFIG_RESTRICTED_LFG_CHANNEL]      = ConfigMgr::GetBoolDefault("Channel.RestrictedLfg", true);
    m_configs[CONFIG_SILENTLY_GM_JOIN_TO_CHANNEL] = ConfigMgr::GetBoolDefault("Channel.SilentlyGMJoin", false);

    m_configs[CONFIG_TALENTS_INSPECTING]           = ConfigMgr::GetBoolDefault("TalentsInspecting", true);
    m_configs[CONFIG_CHAT_FAKE_MESSAGE_PREVENTING] = ConfigMgr::GetBoolDefault("ChatFakeMessagePreventing", false);
    m_configs[CONFIG_CHAT_STRICT_LINK_CHECKING_SEVERITY] = ConfigMgr::GetIntDefault("ChatStrictLinkChecking.Severity", 0);
    m_configs[CONFIG_CHAT_STRICT_LINK_CHECKING_KICK] = ConfigMgr::GetIntDefault("ChatStrictLinkChecking.Kick", 0);

    m_configs[CONFIG_CORPSE_DECAY_NORMAL]    = ConfigMgr::GetIntDefault("Corpse.Decay.NORMAL", 60);
    m_configs[CONFIG_CORPSE_DECAY_RARE]      = ConfigMgr::GetIntDefault("Corpse.Decay.RARE", 300);
    m_configs[CONFIG_CORPSE_DECAY_ELITE]     = ConfigMgr::GetIntDefault("Corpse.Decay.ELITE", 300);
    m_configs[CONFIG_CORPSE_DECAY_RAREELITE] = ConfigMgr::GetIntDefault("Corpse.Decay.RAREELITE", 300);
    m_configs[CONFIG_CORPSE_DECAY_WORLDBOSS] = ConfigMgr::GetIntDefault("Corpse.Decay.WORLDBOSS", 3600);

    m_configs[CONFIG_DEATH_SICKNESS_LEVEL]           = ConfigMgr::GetIntDefault ("Death.SicknessLevel", 11);
    m_configs[CONFIG_DEATH_CORPSE_RECLAIM_DELAY_PVP] = ConfigMgr::GetBoolDefault("Death.CorpseReclaimDelay.PvP", true);
    m_configs[CONFIG_DEATH_CORPSE_RECLAIM_DELAY_PVE] = ConfigMgr::GetBoolDefault("Death.CorpseReclaimDelay.PvE", true);
    m_configs[CONFIG_DEATH_BONES_WORLD]              = ConfigMgr::GetBoolDefault("Death.Bones.World", true);
    m_configs[CONFIG_DEATH_BONES_BG_OR_ARENA]        = ConfigMgr::GetBoolDefault("Death.Bones.BattlegroundOrArena", true);

    m_configs[CONFIG_THREAT_RADIUS] = ConfigMgr::GetIntDefault("ThreatRadius", 60);

    // always use declined names in the russian client
    m_configs[CONFIG_DECLINED_NAMES_USED] =
        (m_configs[CONFIG_REALM_ZONE] == REALM_ZONE_RUSSIAN) ? true : ConfigMgr::GetBoolDefault("DeclinedNames", false);

    m_configs[CONFIG_LISTEN_RANGE_SAY]       = ConfigMgr::GetIntDefault("ListenRange.Say", 25);
    m_configs[CONFIG_LISTEN_RANGE_TEXTEMOTE] = ConfigMgr::GetIntDefault("ListenRange.TextEmote", 25);
    m_configs[CONFIG_LISTEN_RANGE_YELL]      = ConfigMgr::GetIntDefault("ListenRange.Yell", 300);

    m_configs[CONFIG_BATTLEGROUND_CAST_DESERTER]                = ConfigMgr::GetBoolDefault("Battleground.CastDeserter", true);
    m_configs[CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_ENABLE]       = ConfigMgr::GetBoolDefault("Battleground.QueueAnnouncer.Enable", false);
    m_configs[CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_PLAYERONLY]   = ConfigMgr::GetBoolDefault("Battleground.QueueAnnouncer.PlayerOnly", false);
    m_configs[CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_ONSTART]      = ConfigMgr::GetBoolDefault("Battleground.QueueAnnouncer.OnStart", false);
    m_configs[CONFIG_BATTLEGROUND_PREMATURE_REWARD]             = ConfigMgr::GetBoolDefault("Battleground.PrematureReward", true);
    m_configs[CONFIG_BATTLEGROUND_PREMATURE_FINISH_TIMER]       = ConfigMgr::GetIntDefault("BattleGround.PrematureFinishTimer", 5 * MINUTE * IN_MILLISECONDS);
    m_configs[CONFIG_BATTLEGROUND_WRATH_LEAVE_MODE]             = ConfigMgr::GetBoolDefault("Battleground.LeaveWrathMode", false);
    m_configs[CONFIG_ARENA_MAX_RATING_DIFFERENCE]               = ConfigMgr::GetIntDefault("Arena.MaxRatingDifference", 0);
    m_configs[CONFIG_ARENA_RATING_DISCARD_TIMER]                = ConfigMgr::GetIntDefault("Arena.RatingDiscardTimer", 10 * MINUTE * IN_MILLISECONDS);
    m_configs[CONFIG_ARENA_AUTO_DISTRIBUTE_POINTS]              = ConfigMgr::GetBoolDefault("Arena.AutoDistributePoints", false);
    m_configs[CONFIG_ARENA_AUTO_DISTRIBUTE_INTERVAL_DAYS]       = ConfigMgr::GetIntDefault("Arena.AutoDistributeInterval", 7);
    m_configs[CONFIG_ARENA_LOG_EXTENDED_INFO]                   = ConfigMgr::GetBoolDefault("ArenaLogExtendedInfo", false);
    m_configs[CONFIG_INSTANT_LOGOUT]                            = ConfigMgr::GetIntDefault("InstantLogout", SEC_MODERATOR);

    m_VisibleUnitGreyDistance = ConfigMgr::GetFloatDefault("Visibility.Distance.Grey.Unit", 1);
    if (m_VisibleUnitGreyDistance >  MAX_VISIBILITY_DISTANCE)
    {
        sLog->outError("Visibility.Distance.Grey.Unit can't be greater %f",MAX_VISIBILITY_DISTANCE);
        m_VisibleUnitGreyDistance = MAX_VISIBILITY_DISTANCE;
    }
    m_VisibleObjectGreyDistance = ConfigMgr::GetFloatDefault("Visibility.Distance.Grey.Object", 10);
    if (m_VisibleObjectGreyDistance >  MAX_VISIBILITY_DISTANCE)
    {
        sLog->outError("Visibility.Distance.Grey.Object can't be greater %f",MAX_VISIBILITY_DISTANCE);
        m_VisibleObjectGreyDistance = MAX_VISIBILITY_DISTANCE;
    }

    //visibility on continents
    m_MaxVisibleDistanceOnContinents = ConfigMgr::GetFloatDefault("Visibility.Distance.Continents", DEFAULT_VISIBILITY_DISTANCE);
    if (m_MaxVisibleDistanceOnContinents < 45*sWorld.getRate(RATE_CREATURE_AGGRO))
    {
        sLog->outError("Visibility.Distance.Continents can't be less max aggro radius %f", 45*sWorld.getRate(RATE_CREATURE_AGGRO));
        m_MaxVisibleDistanceOnContinents = 45*sWorld.getRate(RATE_CREATURE_AGGRO);
    }
    else if (m_MaxVisibleDistanceOnContinents + m_VisibleUnitGreyDistance > MAX_VISIBILITY_DISTANCE)
    {
        sLog->outError("Visibility.Distance.Continents can't be greater %f",MAX_VISIBILITY_DISTANCE - m_VisibleUnitGreyDistance);
        m_MaxVisibleDistanceOnContinents = MAX_VISIBILITY_DISTANCE - m_VisibleUnitGreyDistance;
    }

    //visibility in instances
    m_MaxVisibleDistanceInInstances = ConfigMgr::GetFloatDefault("Visibility.Distance.Instances", DEFAULT_VISIBILITY_INSTANCE);
    if (m_MaxVisibleDistanceInInstances < 45*sWorld.getRate(RATE_CREATURE_AGGRO))
    {
        sLog->outError("Visibility.Distance.Instances can't be less max aggro radius %f",45*sWorld.getRate(RATE_CREATURE_AGGRO));
        m_MaxVisibleDistanceInInstances = 45*sWorld.getRate(RATE_CREATURE_AGGRO);
    }
    else if (m_MaxVisibleDistanceInInstances + m_VisibleUnitGreyDistance > MAX_VISIBILITY_DISTANCE)
    {
        sLog->outError("Visibility.Distance.Instances can't be greater %f",MAX_VISIBILITY_DISTANCE - m_VisibleUnitGreyDistance);
        m_MaxVisibleDistanceInInstances = MAX_VISIBILITY_DISTANCE - m_VisibleUnitGreyDistance;
    }

    //visibility in BG/Arenas
    m_MaxVisibleDistanceInBGArenas = ConfigMgr::GetFloatDefault("Visibility.Distance.BGArenas", DEFAULT_VISIBILITY_BGARENAS);
    if (m_MaxVisibleDistanceInBGArenas < 45*sWorld.getRate(RATE_CREATURE_AGGRO))
    {
        sLog->outError("Visibility.Distance.BGArenas can't be less max aggro radius %f",45*sWorld.getRate(RATE_CREATURE_AGGRO));
        m_MaxVisibleDistanceInBGArenas = 45*sWorld.getRate(RATE_CREATURE_AGGRO);
    }
    else if (m_MaxVisibleDistanceInBGArenas + m_VisibleUnitGreyDistance > MAX_VISIBILITY_DISTANCE)
    {
        sLog->outError("Visibility.Distance.BGArenas can't be greater %f",MAX_VISIBILITY_DISTANCE - m_VisibleUnitGreyDistance);
        m_MaxVisibleDistanceInBGArenas = MAX_VISIBILITY_DISTANCE - m_VisibleUnitGreyDistance;
    }

    m_MaxVisibleDistanceForObject = ConfigMgr::GetFloatDefault("Visibility.Distance.Object", DEFAULT_VISIBILITY_DISTANCE);
    if (m_MaxVisibleDistanceForObject < INTERACTION_DISTANCE)
    {
        sLog->outError("Visibility.Distance.Object can't be less max aggro radius %f",float(INTERACTION_DISTANCE));
        m_MaxVisibleDistanceForObject = INTERACTION_DISTANCE;
    }
    else if (m_MaxVisibleDistanceForObject + m_VisibleObjectGreyDistance > MAX_VISIBILITY_DISTANCE)
    {
        sLog->outError("Visibility.Distance.Object can't be greater %f",MAX_VISIBILITY_DISTANCE-m_VisibleObjectGreyDistance);
        m_MaxVisibleDistanceForObject = MAX_VISIBILITY_DISTANCE - m_VisibleObjectGreyDistance;
    }
    m_MaxVisibleDistanceInFlight = ConfigMgr::GetFloatDefault("Visibility.Distance.InFlight", DEFAULT_VISIBILITY_DISTANCE);
    if (m_MaxVisibleDistanceInFlight + m_VisibleObjectGreyDistance > MAX_VISIBILITY_DISTANCE)
    {
        sLog->outError("Visibility.Distance.InFlight can't be greater %f",MAX_VISIBILITY_DISTANCE-m_VisibleObjectGreyDistance);
        m_MaxVisibleDistanceInFlight = MAX_VISIBILITY_DISTANCE - m_VisibleObjectGreyDistance;
    }

    ///- Load the CharDelete related config options
    m_configs[CONFIG_CHARDELETE_METHOD] = ConfigMgr::GetIntDefault("CharDelete.Method", 0);
    m_configs[CONFIG_CHARDELETE_MIN_LEVEL] = ConfigMgr::GetIntDefault("CharDelete.MinLevel", 0);
    m_configs[CONFIG_CHARDELETE_KEEP_DAYS] = ConfigMgr::GetIntDefault("CharDelete.KeepDays", 30);

    m_visibility_notify_periodOnContinents = ConfigMgr::GetIntDefault("Visibility.Notify.Period.OnContinents", DEFAULT_VISIBILITY_NOTIFY_PERIOD);
    m_visibility_notify_periodInInstances = ConfigMgr::GetIntDefault("Visibility.Notify.Period.InInstances",  DEFAULT_VISIBILITY_NOTIFY_PERIOD);
    m_visibility_notify_periodInBGArenas = ConfigMgr::GetIntDefault("Visibility.Notify.Period.InBGArenas",   DEFAULT_VISIBILITY_NOTIFY_PERIOD);

    // Read the "Data" directory from the config file
    std::string dataPath = ConfigMgr::GetStringDefault("DataDir","./");
    if (dataPath.at(dataPath.length()-1) != '/' && dataPath.at(dataPath.length()-1) != '\\')
        dataPath.append("/");

    if (reload)
    {
        if (dataPath != m_dataPath)
            sLog->outError("DataDir option can't be changed at trinitycore.conf reload, using current value (%s).",m_dataPath.c_str());
    }
    else
    {
        m_dataPath = dataPath;
        sLog->outString("Using DataDir %s",m_dataPath.c_str());
    }

    bool enableIndoor = ConfigMgr::GetBoolDefault("vmap.enableIndoorCheck", true);
    bool enableLOS = ConfigMgr::GetBoolDefault("vmap.enableLOS", true);
    bool enableHeight = ConfigMgr::GetBoolDefault("vmap.enableHeight", true);
    bool enablePetLOS = ConfigMgr::GetBoolDefault("vmap.petLOS", true);
    std::string ignoreSpellIds = ConfigMgr::GetStringDefault("vmap.ignoreSpellIds", "");

    if (!enableHeight)
        sLog->outError("VMap height checking disabled! Creatures movements and other various things WILL be broken! Expect no support.");

    VMAP::VMapFactory::createOrGetVMapManager()->setEnableLineOfSightCalc(enableLOS);
    VMAP::VMapFactory::createOrGetVMapManager()->setEnableHeightCalc(enableHeight);
    VMAP::VMapFactory::preventSpellsFromBeingTestedForLoS(ignoreSpellIds.c_str());
    sLog->outString("WORLD: VMap support included. LineOfSight:%i, getHeight:%i, indoorCheck:%i, PetLOS:%i", enableLOS, enableHeight, enableIndoor, enablePetLOS);
    sLog->outString("WORLD: VMap data directory is: %svmaps",m_dataPath.c_str());

    m_configs[CONFIG_VMAP_INDOOR_CHECK] = enableIndoor;
    m_configs[CONFIG_PET_LOS] = enablePetLOS;
    m_configs[CONFIG_VMAP_TOTEM] = ConfigMgr::GetBoolDefault("vmap.totem", false);
    m_configs[CONFIG_MAX_WHO] = ConfigMgr::GetIntDefault("MaxWhoListReturns", 49);

    m_configs[CONFIG_BG_START_MUSIC] = ConfigMgr::GetBoolDefault("MusicInBattleground", false);
    m_configs[CONFIG_START_ALL_SPELLS] = ConfigMgr::GetBoolDefault("PlayerStart.AllSpells", false);
    m_configs[CONFIG_HONOR_AFTER_DUEL] = ConfigMgr::GetIntDefault("HonorPointsAfterDuel", 0);
    if (m_configs[CONFIG_HONOR_AFTER_DUEL] < 0)
        m_configs[CONFIG_HONOR_AFTER_DUEL]= 0;
    m_configs[CONFIG_START_ALL_EXPLORED] = ConfigMgr::GetBoolDefault("PlayerStart.MapsExplored", false);
    m_configs[CONFIG_START_ALL_REP] = ConfigMgr::GetBoolDefault("PlayerStart.AllReputation", false);
    m_configs[CONFIG_ALWAYS_MAXSKILL] = ConfigMgr::GetBoolDefault("AlwaysMaxWeaponSkill", false);
    m_configs[CONFIG_PVP_TOKEN_ENABLE] = ConfigMgr::GetBoolDefault("PvPToken.Enable", false);
    m_configs[CONFIG_PVP_TOKEN_MAP_TYPE] = ConfigMgr::GetIntDefault("PvPToken.MapAllowType", 4);
    m_configs[CONFIG_PVP_TOKEN_ID] = ConfigMgr::GetIntDefault("PvPToken.ItemID", 29434);
    m_configs[CONFIG_PVP_TOKEN_COUNT] = ConfigMgr::GetIntDefault("PvPToken.ItemCount", 1);
    if (m_configs[CONFIG_PVP_TOKEN_COUNT] < 1)
        m_configs[CONFIG_PVP_TOKEN_COUNT] = 1;
    m_configs[CONFIG_NO_RESET_TALENT_COST] = ConfigMgr::GetBoolDefault("NoResetTalentsCost", false);
    m_configs[CONFIG_SHOW_KICK_IN_WORLD] = ConfigMgr::GetBoolDefault("ShowKickInWorld", false);
    m_configs[CONFIG_INTERVAL_LOG_UPDATE] = ConfigMgr::GetIntDefault("RecordUpdateTimeDiffInterval", 60000);
    m_configs[CONFIG_MIN_LOG_UPDATE] = ConfigMgr::GetIntDefault("MinRecordUpdateTimeDiff", 100);
    m_configs[CONFIG_NUMTHREADS] = ConfigMgr::GetIntDefault("MapUpdate.Threads",1);
    m_configs[CONFIG_DUEL_MOD] = ConfigMgr::GetBoolDefault("DuelMod.Enable", false);
    m_configs[CONFIG_DUEL_CD_RESET] = ConfigMgr::GetBoolDefault("DuelMod.Cooldowns", false);
    m_configs[CONFIG_AUTOBROADCAST_TIMER] = ConfigMgr::GetIntDefault("AutoBroadcast.Timer", 60000);
    m_configs[CONFIG_AUTOBROADCAST_ENABLED] = ConfigMgr::GetIntDefault("AutoBroadcast.On", 0);
    m_configs[CONFIG_AUTOBROADCAST_CENTER] = ConfigMgr::GetIntDefault("AutoBroadcast.Center", 0);

    std::string forbiddenmaps = ConfigMgr::GetStringDefault("ForbiddenMaps", "");
    char * forbiddenMaps = new char[forbiddenmaps.length() + 1];
    forbiddenMaps[forbiddenmaps.length()] = 0;
    strncpy(forbiddenMaps, forbiddenmaps.c_str(), forbiddenmaps.length());
    const char * delim = ",";
    char * token = strtok(forbiddenMaps, delim);
    while (token != NULL)
    {
        int32 mapid = strtol(token, NULL, 10);
        m_forbiddenMapIds.insert(mapid);
        token = strtok(NULL, delim);
    }
    delete[] forbiddenMaps;

    // chat logging
    m_configs[CONFIG_CHATLOG_CHANNEL] = ConfigMgr::GetBoolDefault("ChatLogs.Channel", false);
    m_configs[CONFIG_CHATLOG_WHISPER] = ConfigMgr::GetBoolDefault("ChatLogs.Whisper", false);
    m_configs[CONFIG_CHATLOG_SYSCHAN] = ConfigMgr::GetBoolDefault("ChatLogs.SysChan", false);
    m_configs[CONFIG_CHATLOG_PARTY] = ConfigMgr::GetBoolDefault("ChatLogs.Party", false);
    m_configs[CONFIG_CHATLOG_RAID] = ConfigMgr::GetBoolDefault("ChatLogs.Raid", false);
    m_configs[CONFIG_CHATLOG_GUILD] = ConfigMgr::GetBoolDefault("ChatLogs.Guild", false);
    m_configs[CONFIG_CHATLOG_PUBLIC] = ConfigMgr::GetBoolDefault("ChatLogs.Public", false);
    m_configs[CONFIG_CHATLOG_ADDON] = ConfigMgr::GetBoolDefault("ChatLogs.Addon", false);
    m_configs[CONFIG_CHATLOG_BGROUND] = ConfigMgr::GetBoolDefault("ChatLogs.BattleGround", false);
}

// Initialize the World
void World::SetInitialWorldSettings()
{
    // Initialize the random number generator
    srand((unsigned int)time(NULL));

    // Initialize config settings
    LoadConfigSettings();

    // Init highest guids before any table loading to prevent using not initialized guids in some code.
    objmgr.SetHighestGuids();

    // Check the existence of the map files for all races' startup areas.
    if (!MapManager::ExistMapAndVMap(0,-6240.32f, 331.033f)
        ||!MapManager::ExistMapAndVMap(0,-8949.95f,-132.493f)
        ||!MapManager::ExistMapAndVMap(1,-618.518f,-4251.67f)
        ||!MapManager::ExistMapAndVMap(0, 1676.35f, 1677.45f)
        ||!MapManager::ExistMapAndVMap(1, 10311.3f, 832.463f)
        ||!MapManager::ExistMapAndVMap(1,-2917.58f,-257.98f)
        ||m_configs[CONFIG_EXPANSION] && (
        !MapManager::ExistMapAndVMap(530, 10349.6f,-6357.29f) || !MapManager::ExistMapAndVMap(530,-3961.64f,-13931.2f)))
    {
        sLog->outError("Correct *.map files not found in path '%smaps' or *.vmtree/*.vmtile files in '%svmaps'. Please place *.map/*.vmtree/*.vmtile files in appropriate directories or correct the DataDir value in the trinitycore.conf file.",m_dataPath.c_str(),m_dataPath.c_str());
        exit(1);
    }

    // Loading strings. Getting no records means core load has to be canceled because no error message can be output.
    sLog->outString();
    sLog->outString("Loading Trinity strings...");
    if (!objmgr.LoadTrinityStrings())
        exit(1);                                            // Error message displayed in function already

    // Update the realm entry in the database with the realm type from the config file
    //No SQL injection as values are treated as integers

    // not send custom type REALM_FFA_PVP to realm list
    uint32 server_type = IsFFAPvPRealm() ? REALM_TYPE_PVP : getConfig(CONFIG_GAME_TYPE);
    uint32 realm_zone = getConfig(CONFIG_REALM_ZONE);
    LoginDatabase.PExecute("UPDATE realmlist SET icon = %u, timezone = %u WHERE id = '%d'", server_type, realm_zone, realmID);

    // Remove the bones (they should not exist in DB though) and old corpses after a restart
    CharacterDatabase.PExecute("DELETE FROM corpse WHERE corpse_type = '0' OR time < (UNIX_TIMESTAMP()-'%u')", 3 * DAY);

    // Load the DBC files
    sLog->outString("Initialize data stores...");
    LoadDBCStores(m_dataPath);
    DetectDBCLang();

    sLog->outString("Loading Script Names...");
    objmgr.LoadScriptNames();

    sLog->outString("Loading Instance Template...");
    objmgr.LoadInstanceTemplate();

    sLog->outString("Loading SkillLineAbilityMultiMap Data...");
    spellmgr.LoadSkillLineAbilityMap();

    // Clean up and pack instances
    sLog->outString("Cleaning up instances...");
    sInstanceSaveManager.CleanupInstances();                // must be called before `creature_respawn`/`gameobject_respawn` tables

    sLog->outString("Packing instances...");
    sInstanceSaveManager.PackInstances();

    sLog->outString("Loading Localization strings...");
    objmgr.LoadCreatureLocales();
    objmgr.LoadGameObjectLocales();
    objmgr.LoadItemLocales();
    objmgr.LoadQuestLocales();
    objmgr.LoadNpcTextLocales();
    objmgr.LoadPageTextLocales();
    objmgr.LoadGossipMenuItemsLocales();
    objmgr.SetDBCLocaleIndex(GetDefaultDbcLocale());        // Get once for all the locale index of DBC language (console/broadcasts)
    sLog->outString(">>> Localization strings loaded");
    sLog->outString();

    sLog->outString("Loading Page Texts...");
    objmgr.LoadPageTexts();

    sLog->outString("Loading Game Object Templates...");     // must be after LoadPageTexts
    objmgr.LoadGameobjectInfo();

    sLog->outString("Loading Spell Chain Data...");
    spellmgr.LoadSpellChains();

    sLog->outString("Loading Spell Required Data...");
    spellmgr.LoadSpellRequired();

    sLog->outString("Loading Spell Elixir types...");
    spellmgr.LoadSpellElixirs();

    sLog->outString("Loading Spell Learn Skills...");
    spellmgr.LoadSpellLearnSkills();                        // must be after LoadSpellChains

    sLog->outString("Loading Spell Learn Spells...");
    spellmgr.LoadSpellLearnSpells();

    sLog->outString("Loading Spell Proc Event conditions...");
    spellmgr.LoadSpellProcEvents();

    sLog->outString("Loading Aggro Spells Definitions...");
    spellmgr.LoadSpellThreats();

    sLog->outString("Loading NPC Texts...");
    objmgr.LoadGossipText();

    sLog->outString("Loading Enchant Spells Proc datas...");
    spellmgr.LoadSpellEnchantProcData();

    sLog->outString("Loading Item Random Enchantments Table...");
    LoadRandomEnchantmentsTable();

    sLog->outString("Loading Items...");                     // must be after LoadRandomEnchantmentsTable and LoadPageTexts
    objmgr.LoadItemPrototypes();

    sLog->outString("Loading Item Texts...");
    objmgr.LoadItemTexts();

    sLog->outString("Loading Creature Model Based Info Data...");
    objmgr.LoadCreatureModelInfo();

    sLog->outString("Loading Equipment templates...");
    objmgr.LoadEquipmentTemplates();

    sLog->outString("Loading Creature templates...");
    objmgr.LoadCreatureTemplates();

    sLog->outString("Loading SpellsScriptTarget...");
    spellmgr.LoadSpellScriptTarget();                       // must be after LoadCreatureTemplates and LoadGameobjectInfo

    sLog->outString("Loading Creature Reputation OnKill Data...");
    objmgr.LoadReputationOnKill();

    sLog->outString("Loading Pet Create Spells...");
    objmgr.LoadPetCreateSpells();

    sLog->outString("Loading Creature Data...");
    objmgr.LoadCreatures();

    sLog->outString("Loading Creature Linked Respawn...");
    objmgr.LoadCreatureLinkedRespawn();                     // must be after LoadCreatures()

    sLog->outString();
    sLog->outString("Loading Creature Addon Data...");
    objmgr.LoadCreatureAddons();                            // must be after LoadCreatureTemplates() and LoadCreatures()
    sLog->outString(">>> Creature Addon Data loaded");
    sLog->outString();

    sLog->outString("Loading Creature Respawn Data...");   // must be after PackInstances()
    objmgr.LoadCreatureRespawnTimes();

    sLog->outString("Loading Gameobject Data...");
    objmgr.LoadGameobjects();

    sLog->outString("Loading Gameobject Respawn Data...");   // must be after PackInstances()
    objmgr.LoadGameobjectRespawnTimes();

    sLog->outString("Loading Objects Pooling Data...");
    poolhandler.LoadFromDB();

    sLog->outString("Loading Game Event Data...");
    gameeventmgr.LoadFromDB();

    sLog->outString("Loading Weather Data...");
    objmgr.LoadWeatherZoneChances();

    sLog->outString("Loading Quests...");
    objmgr.LoadQuests();                                    // must be loaded after DBCs, creature_template, item_template, gameobject tables

    sLog->outString("Loading Quests Relations...");
    objmgr.LoadQuestRelations();                            // must be after quest load

    sLog->outString("Loading AreaTrigger definitions...");
    objmgr.LoadAreaTriggerTeleports();

    sLog->outString("Loading Access Requirements...");
    objmgr.LoadAccessRequirements();                        // must be after item template load

    sLog->outString("Loading Quest Area Triggers...");
    objmgr.LoadQuestAreaTriggers();                         // must be after LoadQuests

    sLog->outString("Loading Tavern Area Triggers...");
    objmgr.LoadTavernAreaTriggers();

    sLog->outString("Loading AreaTrigger script names...");
    objmgr.LoadAreaTriggerScripts();

    sLog->outString("Loading Graveyard-zone links...");
    objmgr.LoadGraveyardZones();

    sLog->outString("Loading Spell target coordinates...");
    spellmgr.LoadSpellTargetPositions();

    sLog->outString("Loading SpellAffect definitions...");
    spellmgr.LoadSpellAffects();

    sLog->outString("Loading spell pet auras...");
    spellmgr.LoadSpellPetAuras();

    sLog->outString("Loading spell extra attributes...");
    spellmgr.LoadSpellCustomAttr();

    sLog->outString("Loading linked spells...");
    spellmgr.LoadSpellLinked();

    sLog->outString("Loading Player Create Data...");
    objmgr.LoadPlayerInfo();

    sLog->outString("Loading Exploration BaseXP Data...");
    objmgr.LoadExplorationBaseXP();

    sLog->outString("Loading Pet Name Parts...");
    objmgr.LoadPetNames();

    sLog->outString("Loading the max pet number...");
    objmgr.LoadPetNumber();

    sLog->outString("Loading pet level stats...");
    objmgr.LoadPetLevelInfo();

    sLog->outString("Loading Player Corpses...");
    objmgr.LoadCorpses();

    sLog->outString("Loading Disabled Spells...");
    objmgr.LoadSpellDisabledEntrys();

    sLog->outString("Loading Loot Tables...");
    LoadLootTables();

    sLog->outString("Loading Skill Discovery Table...");
    LoadSkillDiscoveryTable();

    sLog->outString("Loading Skill Extra Item Table...");
    LoadSkillExtraItemTable();

    sLog->outString("Loading Skill Fishing base level requirements...");
    objmgr.LoadFishingBaseSkillLevel();

    // Load dynamic data tables from the database
    sLog->outString("Loading Item Auctions...");
    sAuctionMgr->LoadAuctionItems();
    sLog->outString("Loading Auctions...");
    sAuctionMgr->LoadAuctions();

    sLog->outString("Loading Guilds...");
    objmgr.LoadGuilds();

    sLog->outString("Loading ArenaTeams...");
    objmgr.LoadArenaTeams();

    sLog->outString("Loading Groups...");
    objmgr.LoadGroups();

    sLog->outString("Loading ReservedNames...");
    objmgr.LoadReservedPlayersNames();

    sLog->outString("Loading GameObjects for quests...");
    objmgr.LoadGameObjectForQuests();

    sLog->outString("Loading BattleMasters...");
    objmgr.LoadBattleMastersEntry();

    sLog->outString("Loading GameTeleports...");
    objmgr.LoadGameTele();

    sLog->outString("Loading Npc Text Id...");
    objmgr.LoadNpcTextId();                                 // must be after load Creature and NpcText

    sLog->outString( "Loading Gossip scripts...");
    objmgr.LoadGossipScripts();                             // must be before gossip menu options

    sLog->outString("Loading Gossip menu...");
    objmgr.LoadGossipMenu();

    sLog->outString("Loading Gossip menu options...");
    objmgr.LoadGossipMenuItems();

    sLog->outString("Loading Vendors...");
    objmgr.LoadVendors();                                   // must be after load CreatureTemplate and ItemTemplate

    sLog->outString("Loading Trainers...");
    objmgr.LoadTrainerSpell();                              // must be after load CreatureTemplate

    sLog->outString("Loading Waypoints...");
    sWaypointMgr->Load();

    sLog->outString("Loading Creature Formations...");
    formation_mgr.LoadCreatureFormations();

    sLog->outString("Loading Creature Groups...");
    group_mgr.LoadCreatureGroups();

    sLog->outString("Loading GM tickets...");
    ticketmgr.LoadGMTickets();

    sLog->outString("Loading GM surveys...");
    ticketmgr.LoadGMSurveys();

    // Handle outdated emails (delete/return)
    sLog->outString("Returning old mails...");
    objmgr.ReturnOrDeleteOldMails(false);

    sLog->outString("Loading Autobroadcasts...");
    LoadAutobroadcasts();

    // Load and initialize scripts
    sLog->outString("Loading Scripts...");
    sLog->outString();
    objmgr.LoadQuestStartScripts();                         // must be after load Creature/Gameobject(Template/Data) and QuestTemplate
    objmgr.LoadQuestEndScripts();                           // must be after load Creature/Gameobject(Template/Data) and QuestTemplate
    objmgr.LoadSpellScripts();                              // must be after load Creature/Gameobject(Template/Data)
    objmgr.LoadGameObjectScripts();                         // must be after load Creature/Gameobject(Template/Data)
    objmgr.LoadEventScripts();                              // must be after load Creature/Gameobject(Template/Data)
    objmgr.LoadWaypointScripts();
    sLog->outString(">>> Scripts loaded");
    sLog->outString();

    sLog->outString("Loading Scripts text locales...");      // must be after Load*Scripts calls
    objmgr.LoadDbScriptStrings();

    sLog->outString("Loading CreatureEventAI Texts...");
    CreatureEAI_Mgr.LoadCreatureEventAI_Texts(false);       // false, will checked in LoadCreatureEventAI_Scripts

    sLog->outString("Loading CreatureEventAI Summons...");
    CreatureEAI_Mgr.LoadCreatureEventAI_Summons(false);     // false, will checked in LoadCreatureEventAI_Scripts

    sLog->outString("Loading CreatureEventAI Scripts...");
    CreatureEAI_Mgr.LoadCreatureEventAI_Scripts();

    sLog->outString("Initializing Scripts...");
    sScriptMgr.ScriptsInit();

    // Initialize game time and timers
    sLog->outDebug("DEBUG:: Initialize game time and timers");
    m_gameTime = time(NULL);
    m_startTime=m_gameTime;

    tm local;
    time_t curr;
    time(&curr);
    local=*(localtime(&curr));                              // dereference and assign
    char isoDate[128];
    sprintf(isoDate, "%04d-%02d-%02d %02d:%02d:%02d",
        local.tm_year+1900, local.tm_mon+1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);

    WorldDatabase.PExecute("INSERT INTO uptime (startstring, starttime, uptime) VALUES('%s', " UI64FMTD ", 0)",
        isoDate, uint64(m_startTime));

    m_timers[WUPDATE_AUTOBROADCAST].SetInterval(m_configs[CONFIG_AUTOBROADCAST_TIMER]);
    m_timers[WUPDATE_OBJECTS].SetInterval(IN_MILLISECONDS/2);
    m_timers[WUPDATE_SESSIONS].SetInterval(0);
    m_timers[WUPDATE_WEATHERS].SetInterval(IN_MILLISECONDS);
    m_timers[WUPDATE_AUCTIONS].SetInterval(MINUTE*IN_MILLISECONDS);
    m_timers[WUPDATE_UPTIME].SetInterval(m_configs[CONFIG_UPTIME_UPDATE]*MINUTE*IN_MILLISECONDS);
                                                            //Update "uptime" table based on configuration entry in minutes.
    m_timers[WUPDATE_CORPSES].SetInterval(20 * MINUTE * IN_MILLISECONDS);
                                                            //erase corpses every 20 minutes
    m_timers[WUPDATE_CLEANDB].SetInterval(m_configs[CONFIG_LOGDB_CLEARINTERVAL]*MINUTE*IN_MILLISECONDS);
                                                            // clean logs table every 14 days by default

    m_timers[WUPDATE_DELETECHARS].SetInterval(DAY*IN_MILLISECONDS); // check for chars to delete every day

    //to set mailtimer to return mails every day between 4 and 5 am
    //mailtimer is increased when updating auctions
    //one second is 1000 -(tested on win system)

    // handle timer for external mail
    extmail_timer.SetInterval(m_configs[CONFIG_EXTERNAL_MAIL_INTERVAL] * MINUTE * IN_MILLISECONDS);

    mail_timer = ((((localtime(&m_gameTime)->tm_hour + 20) % 24)* HOUR * IN_MILLISECONDS) / m_timers[WUPDATE_AUCTIONS].GetInterval());
                                                            //1440
    mail_timer_expires = ((DAY * IN_MILLISECONDS) / (m_timers[WUPDATE_AUCTIONS].GetInterval()));
    sLog->outDebug("Mail timer set to: %u, mail return is called every %u minutes", mail_timer, mail_timer_expires);

    // Initilize static helper structures
    AIRegistry::Initialize();
    Player::InitVisibleBits();

    // Initialize MapManager
    sLog->outString("Starting Map System");
    MapManager::Instance().Initialize();

    sLog->outString("Starting Game Event system...");
    uint32 nextGameEvent = gameeventmgr.Initialize();
    m_timers[WUPDATE_EVENTS].SetInterval(nextGameEvent);    //depend on next event

    // Initialize Battlegrounds
    sLog->outString("Starting BattleGround System");
    sBattleGroundMgr.CreateInitialBattleGrounds();
    sBattleGroundMgr.InitAutomaticArenaPointDistribution();

    // Initialize outdoor pvp
    sLog->outString("Starting Outdoor PvP System");
    sOutdoorPvPMgr.InitOutdoorPvP();

    //Not sure if this can be moved up in the sequence (with static data loading) as it uses MapManager
    sLog->outString("Loading Transports...");
    MapManager::Instance().LoadTransports();

    sLog->outString("Loading Transports Events...");
    objmgr.LoadTransportEvents();

    sLog->outString("Deleting expired bans...");
    LoginDatabase.Execute("DELETE FROM ip_banned WHERE unbandate <= UNIX_TIMESTAMP() AND unbandate<>bandate");

    sLog->outString("Starting objects Pooling system...");
    poolhandler.Initialize();

    sLog->outString("Calculate next daily quest reset time...");
    InitDailyQuestResetTime();

    sLog->outString("Initialize AuctionHouseBot...");
    auctionbot.Initialize();

    // possibly enable db logging; avoid massive startup spam by doing it here.
    if (sLog->GetLogDBLater())
    {
        sLog->outString("Enabling database logging...");
        sLog->SetLogDBLater(false);
        sLog->SetLogDB(true);
    }
    else
        sLog->SetLogDB(false);

    // Delete all characters which have been deleted X days before
    Player::DeleteOldCharacters();

    sLog->outString("WORLD: World initialized");
}

void World::DetectDBCLang()
{
    uint8 m_lang_confid = ConfigMgr::GetIntDefault("DBC.Locale", 255);

    if (m_lang_confid != 255 && m_lang_confid >= MAX_LOCALE)
    {
        sLog->outError("Incorrect DBC.Locale! Must be >= 0 and < %d (set to 0)",MAX_LOCALE);
        m_lang_confid = LOCALE_enUS;
    }

    ChrRacesEntry const* race = sChrRacesStore.LookupEntry(1);

    std::string availableLocalsStr;

    uint8 default_locale = MAX_LOCALE;
    for (uint8 i = default_locale-1; i < MAX_LOCALE; --i)  // -1 will be 255 due to uint8
    {
        if (strlen(race->name[i]) > 0)                     // check by race names
        {
            default_locale = i;
            m_availableDbcLocaleMask |= (1 << i);
            availableLocalsStr += localeNames[i];
            availableLocalsStr += " ";
        }
    }

    if (default_locale != m_lang_confid && m_lang_confid < MAX_LOCALE &&
        (m_availableDbcLocaleMask & (1 << m_lang_confid)))
    {
        default_locale = m_lang_confid;
    }

    if (default_locale >= MAX_LOCALE)
    {
        sLog->outError("Unable to determine your DBC Locale! (corrupt DBC?)");
        exit(1);
    }

    m_defaultDbcLocale = LocaleConstant(default_locale);

    sLog->outString("Using %s DBC Locale as default. All available DBC locales: %s",localeNames[m_defaultDbcLocale],availableLocalsStr.empty() ? "<none>" : availableLocalsStr.c_str());
    sLog->outString();
}

void World::RecordTimeDiff(const char *text, ...)
{
    if (m_updateTimeCount != 1)
        return;
    if (!text)
    {
        m_currentTime = getMSTime();
        return;
    }

    uint32 thisTime = getMSTime();
    uint32 diff = getMSTimeDiff(m_currentTime, thisTime);

    if (diff >= m_configs[CONFIG_MIN_LOG_UPDATE])
    {
        va_list ap;
        char str[256];
        va_start(ap, text);
        vsnprintf(str, 256, text, ap);
        va_end(ap);
        sLog->outDetail("Difftime %s: %u.", str, diff);
    }

    m_currentTime = thisTime;
}

void World::LoadAutobroadcasts()
{
    m_Autobroadcasts.clear();

    QueryResult_AutoPtr result = WorldDatabase.Query("SELECT text FROM autobroadcast");

    if (!result)
    {
        sLog->outString();
        sLog->outString(">> Loaded 0 autobroadcasts definitions");
        return;
    }

    uint32 count = 0;

    do
    {
        Field *fields = result->Fetch();
        std::string message = fields[0].GetCppString();
        m_Autobroadcasts.push_back(message);
        ++count;
    }

    while (result->NextRow());

    sLog->outString();
    sLog->outString( ">> Loaded %u autobroadcasts definitions", count);
}

// Update the World !
void World::Update(time_t diff)
{
    m_updateTime = uint32(diff);
    if (m_configs[CONFIG_INTERVAL_LOG_UPDATE])
    {
        if (m_updateTimeSum > m_configs[CONFIG_INTERVAL_LOG_UPDATE] && uint32(diff) >= m_configs[CONFIG_MIN_LOG_UPDATE])
        {
            sLog->outBasic("Update time diff: %u. Players online: %u.", m_updateTimeSum / m_updateTimeCount, GetActiveSessionCount());
            m_updateTimeSum = m_updateTime;
            m_updateTimeCount = 1;
        }
        else
        {
            m_updateTimeSum += m_updateTime;
            ++m_updateTimeCount;
        }
    }

    // Update the different timers
    for (int i = 0; i < WUPDATE_COUNT; ++i)
    {
        if (m_timers[i].GetCurrent() >= 0)
            m_timers[i].Update(diff);
        else
            m_timers[i].SetCurrent(0);
    }

    // Update the game time and check for shutdown time
    _UpdateGameTime();

    // Handle daily quests reset time
    if (m_gameTime > m_NextDailyQuestReset)
    {
        ResetDailyQuests();
        m_NextDailyQuestReset += DAY;
    }

    // Handle external mail
    if (m_configs[CONFIG_EXTERNAL_MAIL] != 0)
    {
        extmail_timer.Update(diff);
        if (extmail_timer.Passed())
        {
            WorldSession::SendExternalMails();
            extmail_timer.Reset();
        }
    }

    // Handle auctions when the timer has passed
    if (m_timers[WUPDATE_AUCTIONS].Passed())
    {
        auctionbot.Update();
        m_timers[WUPDATE_AUCTIONS].Reset();

        // Update mails (return old mails with item, or delete them)
        //(tested... works on win)
        if (++mail_timer > mail_timer_expires)
        {
            mail_timer = 0;
            objmgr.ReturnOrDeleteOldMails(true);
        }

        // Handle expired auctions
        sAuctionMgr->Update();
    }

    // Handle session updates when the timer has passed
    RecordTimeDiff(NULL);
    UpdateSessions(diff);
    RecordTimeDiff("UpdateSessions");

    // Handle weather updates when the timer has passed
    if (m_timers[WUPDATE_WEATHERS].Passed())
    {
        m_timers[WUPDATE_WEATHERS].Reset();

        // Send an update signal to Weather objects
        WeatherMap::iterator itr, next;
        for (itr = m_weathers.begin(); itr != m_weathers.end(); itr = next)
        {
            next = itr;
            ++next;

            // and remove Weather objects for zones with no player
                                                            //As interval > WorldTick
            if (!itr->second->Update(m_timers[WUPDATE_WEATHERS].GetInterval()))
            {
                delete itr->second;
                m_weathers.erase(itr);
            }
        }
    }

    // Update uptime table
    if (m_timers[WUPDATE_UPTIME].Passed())
    {
        uint32 tmpDiff = (m_gameTime - m_startTime);
        uint32 maxClientsNum = sWorld.GetMaxActiveSessionCount();

        m_timers[WUPDATE_UPTIME].Reset();
        WorldDatabase.PExecute("UPDATE uptime SET uptime = %d, maxplayers = %d WHERE starttime = " UI64FMTD, tmpDiff, maxClientsNum, uint64(m_startTime));
    }

    // Clean logs table
    if (sWorld.getConfig(CONFIG_LOGDB_CLEARTIME) > 0) // if not enabled, ignore the timer
    {
        if (m_timers[WUPDATE_CLEANDB].Passed())
        {
            m_timers[WUPDATE_CLEANDB].Reset();
            LoginDatabase.PExecute("DELETE FROM logs WHERE (time + %u) < "UI64FMTD";",
                sWorld.getConfig(CONFIG_LOGDB_CLEARTIME), uint64(time(0)));
        }
    }

    // Handle all other objects
    // Update objects when the timer has passed (maps, transport, creatures,...)
    MapManager::Instance().Update(diff);                // As interval = 0

    if (m_configs[CONFIG_AUTOBROADCAST_ENABLED])
    {
       if (m_timers[WUPDATE_AUTOBROADCAST].Passed())
       {
          m_timers[WUPDATE_AUTOBROADCAST].Reset();
          SendAutoBroadcast();
       }
    }

    sBattleGroundMgr.Update(diff);
    RecordTimeDiff("UpdateBattleGroundMgr");

    sOutdoorPvPMgr.Update(diff);
    RecordTimeDiff("UpdateOutdoorPvPMgr");

    ///- Delete all characters which have been deleted X days before
    if (m_timers[WUPDATE_DELETECHARS].Passed())
    {
        m_timers[WUPDATE_DELETECHARS].Reset();
        Player::DeleteOldCharacters();
    }

    // execute callbacks from sql queries that were queued recently
    UpdateResultQueue();
    RecordTimeDiff("UpdateResultQueue");

    // Erase corpses once every 20 minutes
    if (m_timers[WUPDATE_CORPSES].Passed())
    {
        m_timers[WUPDATE_CORPSES].Reset();
        ObjectAccessor::Instance().RemoveOldCorpses();
    }

    // Process Game events when necessary
    if (m_timers[WUPDATE_EVENTS].Passed())
    {
        m_timers[WUPDATE_EVENTS].Reset();                   // to give time for Update() to be processed
        uint32 nextGameEvent = gameeventmgr.Update();
        m_timers[WUPDATE_EVENTS].SetInterval(nextGameEvent);
        m_timers[WUPDATE_EVENTS].Reset();
    }

    // update the instance reset times
    sInstanceSaveManager.Update();

    // And last, but not least handle the issued cli commands
    ProcessCliCommands();
}

void World::ForceGameEventUpdate()
{
    m_timers[WUPDATE_EVENTS].Reset();                   // to give time for Update() to be processed
    uint32 nextGameEvent = gameeventmgr.Update();
    m_timers[WUPDATE_EVENTS].SetInterval(nextGameEvent);
    m_timers[WUPDATE_EVENTS].Reset();
}

// Send a packet to all players (except self if mentioned)
void World::SendGlobalMessage(WorldPacket *packet, WorldSession *self, uint32 team)
{
    SessionMap::iterator itr;
    for (itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
    {
        if (itr->second &&
            itr->second->GetPlayer() &&
            itr->second->GetPlayer()->IsInWorld() &&
            itr->second != self &&
            (team == 0 || itr->second->GetPlayer()->GetTeam() == team))
        {
            itr->second->SendPacket(packet);
        }
    }
}

// Send a packet to all GMs (except self if mentioned)
void World::SendGlobalGMMessage(WorldPacket *packet, WorldSession *self, uint32 team)
{
    SessionMap::iterator itr;
    for (itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
    {
        if (itr->second &&
            itr->second->GetPlayer() &&
            itr->second->GetPlayer()->IsInWorld() &&
            itr->second != self &&
            itr->second->GetSecurity() > SEC_PLAYER &&
            (team == 0 || itr->second->GetPlayer()->GetTeam() == team))
        {
            itr->second->SendPacket(packet);
        }
    }
}

// Send a System Message to all players (except self if mentioned)
void World::SendWorldText(int32 string_id, ...)
{
    std::vector<std::vector<WorldPacket*> > data_cache;     // 0 = default, i => i-1 locale index

    for (SessionMap::iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
    {
        if (!itr->second || !itr->second->GetPlayer() || !itr->second->GetPlayer()->IsInWorld())
            continue;

        uint32 loc_idx = itr->second->GetSessionDbLocaleIndex();
        uint32 cache_idx = loc_idx+1;

        std::vector<WorldPacket*>* data_list;

        // create if not cached yet
        if (data_cache.size() < cache_idx+1 || data_cache[cache_idx].empty())
        {
            if (data_cache.size() < cache_idx+1)
                data_cache.resize(cache_idx+1);

            data_list = &data_cache[cache_idx];

            char const* text = objmgr.GetTrinityString(string_id, loc_idx);

            char buf[1000];

            va_list argptr;
            va_start(argptr, string_id);
            vsnprintf(buf, 1000, text, argptr);
            va_end(argptr);

            char* pos = &buf[0];

            while (char* line = ChatHandler::LineFromMessage(pos))
            {
                WorldPacket* data = new WorldPacket();
                ChatHandler::FillMessageData(data, NULL, CHAT_MSG_SYSTEM, LANG_UNIVERSAL, NULL, 0, line, NULL);
                data_list->push_back(data);
            }
        }
        else
            data_list = &data_cache[cache_idx];

        for (int i = 0; i < data_list->size(); ++i)
            itr->second->SendPacket((*data_list)[i]);
    }

    // free memory
    for (int i = 0; i < data_cache.size(); ++i)
        for (int j = 0; j < data_cache[i].size(); ++j)
            delete data_cache[i][j];
}

void World::SendGMText(int32 string_id, ...)
{
    std::vector<std::vector<WorldPacket*> > data_cache;     // 0 = default, i => i-1 locale index

    for (SessionMap::iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
    {
        if (!itr->second || !itr->second->GetPlayer() || !itr->second->GetPlayer()->IsInWorld())
            continue;

        uint32 loc_idx = itr->second->GetSessionDbLocaleIndex();
        uint32 cache_idx = loc_idx+1;

        std::vector<WorldPacket*>* data_list;

        // create if not cached yet
        if (data_cache.size() < cache_idx+1 || data_cache[cache_idx].empty())
        {
            if (data_cache.size() < cache_idx+1)
                data_cache.resize(cache_idx+1);

            data_list = &data_cache[cache_idx];

            char const* text = objmgr.GetTrinityString(string_id, loc_idx);

            char buf[1000];

            va_list argptr;
            va_start(argptr, string_id);
            vsnprintf(buf, 1000, text, argptr);
            va_end(argptr);

            char* pos = &buf[0];

            while (char* line = ChatHandler::LineFromMessage(pos))
            {
                WorldPacket* data = new WorldPacket();
                ChatHandler::FillMessageData(data, NULL, CHAT_MSG_SYSTEM, LANG_UNIVERSAL, NULL, 0, line, NULL);
                data_list->push_back(data);
            }
        }
        else
            data_list = &data_cache[cache_idx];

        for (int i = 0; i < data_list->size(); ++i)
            if (itr->second->GetSecurity() > SEC_PLAYER)
            itr->second->SendPacket((*data_list)[i]);
    }

    // free memory
    for (int i = 0; i < data_cache.size(); ++i)
        for (int j = 0; j < data_cache[i].size(); ++j)
            delete data_cache[i][j];
}

// Send a System Message to all players (except self if mentioned)
void World::SendGlobalText(const char* text, WorldSession *self)
{
    WorldPacket data;

    // need copy to prevent corruption by strtok call in LineFromMessage original string
    char* buf = strdup(text);
    char* pos = buf;

    while (char* line = ChatHandler::LineFromMessage(pos))
    {
        ChatHandler::FillMessageData(&data, NULL, CHAT_MSG_SYSTEM, LANG_UNIVERSAL, NULL, 0, line, NULL);
        SendGlobalMessage(&data, self);
    }

    free(buf);
}

// Send a packet to all players (or players selected team) in the zone (except self if mentioned)
void World::SendZoneMessage(uint32 zone, WorldPacket *packet, WorldSession *self, uint32 team)
{
    SessionMap::iterator itr;
    for (itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
    {
        if (itr->second &&
            itr->second->GetPlayer() &&
            itr->second->GetPlayer()->IsInWorld() &&
            itr->second->GetPlayer()->GetZoneId() == zone &&
            itr->second != self &&
            (team == 0 || itr->second->GetPlayer()->GetTeam() == team))
        {
            itr->second->SendPacket(packet);
        }
    }
}

// Send a System Message to all players in the zone (except self if mentioned)
void World::SendZoneText(uint32 zone, const char* text, WorldSession *self, uint32 team)
{
    WorldPacket data;
    ChatHandler::FillMessageData(&data, NULL, CHAT_MSG_SYSTEM, LANG_UNIVERSAL, NULL, 0, text, NULL);
    SendZoneMessage(zone, &data, self, team);
}

// Kick (and save) all players
void World::KickAll()
{
    m_QueuedPlayer.clear();                                 // prevent send queue update packet and login queued sessions

    // session not removed at kick and will removed in next update tick
    for (SessionMap::iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
        itr->second->KickPlayer();
}

// Kick (and save) all players with security level less `sec`
void World::KickAllLess(AccountTypes sec)
{
    // session not removed at kick and will removed in next update tick
    for (SessionMap::iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
        if (itr->second->GetSecurity() < sec)
            itr->second->KickPlayer();
}

// Kick (and save) the designated player
bool World::KickPlayer(const std::string& playerName)
{
    SessionMap::iterator itr;

    // session not removed at kick and will removed in next update tick
    for (itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
    {
        if (!itr->second)
            continue;
        Player *player = itr->second->GetPlayer();
        if (!player)
            continue;
        if (player->IsInWorld())
        {
            if (playerName == player->GetName())
            {
                itr->second->KickPlayer();
                return true;
            }
        }
    }
    return false;
}

// Ban an account or ban an IP address, duration will be parsed using TimeStringToSecs if it is positive, otherwise permban
BanReturn World::BanAccount(BanMode mode, std::string nameOrIP, std::string duration, std::string reason, std::string author)
{
    LoginDatabase.escape_string(nameOrIP);
    LoginDatabase.escape_string(reason);
    std::string safe_author=author;
    LoginDatabase.escape_string(safe_author);

    uint32 duration_secs = TimeStringToSecs(duration);
    QueryResult_AutoPtr resultAccounts = QueryResult_AutoPtr(NULL);                     //used for kicking

    // Update the database with ban information
    switch (mode)
    {
        case BAN_IP:
            //No SQL injection as strings are escaped
            resultAccounts = LoginDatabase.PQuery("SELECT id FROM account WHERE last_ip = '%s'",nameOrIP.c_str());
            LoginDatabase.PExecute("INSERT INTO ip_banned VALUES ('%s',UNIX_TIMESTAMP(),UNIX_TIMESTAMP()+%u,'%s','%s')",nameOrIP.c_str(),duration_secs, safe_author.c_str(),reason.c_str());
            break;
        case BAN_ACCOUNT:
            //No SQL injection as string is escaped
            resultAccounts = LoginDatabase.PQuery("SELECT id FROM account WHERE username = '%s'",nameOrIP.c_str());
            break;
        case BAN_CHARACTER:
            //No SQL injection as string is escaped
            resultAccounts = CharacterDatabase.PQuery("SELECT account FROM characters WHERE name = '%s'",nameOrIP.c_str());
            break;
        default:
            return BAN_SYNTAX_ERROR;
    }

    if (!resultAccounts)
    {
        if (mode == BAN_IP)
            return BAN_SUCCESS;                             // ip correctly banned but nobody affected (yet)
        else
            return BAN_NOTFOUND;                            // Nobody to ban
    }

    // Disconnect all affected players (for IP it can be several)
    do
    {
        Field* fieldsAccount = resultAccounts->Fetch();
        uint32 account = fieldsAccount->GetUInt32();

        if (mode != BAN_IP)
        {
            //No SQL injection as strings are escaped
            LoginDatabase.PExecute("INSERT INTO account_banned VALUES ('%u', UNIX_TIMESTAMP(), UNIX_TIMESTAMP()+%u, '%s', '%s', '1')",
                account, duration_secs, safe_author.c_str(),reason.c_str());
        }

        if (WorldSession* sess = FindSession(account))
            if (std::string(sess->GetPlayerName()) != author)
                sess->KickPlayer();
    }
    while (resultAccounts->NextRow());

    return BAN_SUCCESS;
}

// Remove a ban from an account or IP address
bool World::RemoveBanAccount(BanMode mode, std::string nameOrIP)
{
    if (mode == BAN_IP)
    {
        LoginDatabase.escape_string(nameOrIP);
        LoginDatabase.PExecute("DELETE FROM ip_banned WHERE ip = '%s'",nameOrIP.c_str());
    }
    else
    {
        uint32 account = 0;
        if (mode == BAN_ACCOUNT)
            account = sAccountMgr->GetId (nameOrIP);
        else if (mode == BAN_CHARACTER)
            account = objmgr.GetPlayerAccountIdByPlayerName (nameOrIP);

        if (!account)
            return false;

        //NO SQL injection as account is uint32
        LoginDatabase.PExecute("UPDATE account_banned SET active = '0' WHERE id = '%u'",account);
    }
    return true;
}

// Update the game time
void World::_UpdateGameTime()
{
    // update the time
    time_t thisTime = time(NULL);
    uint32 elapsed = uint32(thisTime - m_gameTime);
    m_gameTime = thisTime;

    // if there is a shutdown timer
    if (!m_stopEvent && m_ShutdownTimer > 0 && elapsed > 0)
    {
        // ... and it is overdue, stop the world (set m_stopEvent)
        if (m_ShutdownTimer <= elapsed)
        {
            if (!(m_ShutdownMask & SHUTDOWN_MASK_IDLE) || GetActiveAndQueuedSessionCount() == 0)
                m_stopEvent = true;                         // exist code already set
            else
                m_ShutdownTimer = 1;                        // minimum timer value to wait idle state
        }
        // ... else decrease it and if necessary display a shutdown countdown to the users
        else
        {
            m_ShutdownTimer -= elapsed;

            ShutdownMsg();
        }
    }
}

// Shutdown the server
void World::ShutdownServ(uint32 time, uint32 options, uint8 exitcode)
{
    // ignore if server shutdown at next tick
    if (m_stopEvent)
        return;

    m_ShutdownMask = options;
    m_ExitCode = exitcode;

    // If the shutdown time is 0, set m_stopEvent (except if shutdown is 'idle' with remaining sessions)
    if (time == 0)
    {
        if (!(options & SHUTDOWN_MASK_IDLE) || GetActiveAndQueuedSessionCount() == 0)
            m_stopEvent = true;                             // exist code already set
        else
            m_ShutdownTimer = 1;                            //So that the session count is re-evaluated at next world tick
    }
    // Else set the shutdown timer and warn users
    else
    {
        m_ShutdownTimer = time;
        ShutdownMsg(true);
    }
}

// Display a shutdown message to the user(s)
void World::ShutdownMsg(bool show, Player* player)
{
    // not show messages for idle shutdown mode
    if (m_ShutdownMask & SHUTDOWN_MASK_IDLE)
        return;

    // Display a message every 12 hours, hours, 5 minutes, minute, 5 seconds and finally seconds
    if (show ||
        (m_ShutdownTimer < 10) ||
                                                            // < 30 sec; every 5 sec
        (m_ShutdownTimer<30        && (m_ShutdownTimer % 5) == 0) ||
                                                            // < 5 min ; every 1 min
        (m_ShutdownTimer<5*MINUTE  && (m_ShutdownTimer % MINUTE) == 0) ||
                                                            // < 30 min ; every 5 min
        (m_ShutdownTimer<30*MINUTE && (m_ShutdownTimer % (5*MINUTE)) == 0) ||
                                                            // < 12 h ; every 1 h
        (m_ShutdownTimer<12*HOUR   && (m_ShutdownTimer % HOUR) == 0) ||
                                                            // > 12 h ; every 12 h
        (m_ShutdownTimer>12*HOUR   && (m_ShutdownTimer % (12*HOUR)) == 0))
    {
        std::string str = secsToTimeString(m_ShutdownTimer);

        ServerMessageType msgid = (m_ShutdownMask & SHUTDOWN_MASK_RESTART) ? SERVER_MSG_RESTART_TIME : SERVER_MSG_SHUTDOWN_TIME;

        SendServerMessage(msgid, str.c_str(),player);
        DEBUG_LOG("Server is %s in %s",(m_ShutdownMask & SHUTDOWN_MASK_RESTART ? "restart" : "shutting down"),str.c_str());
    }
}

// Cancel a planned server shutdown
void World::ShutdownCancel()
{
    // nothing cancel or too later
    if (!m_ShutdownTimer || m_stopEvent)
        return;

    ServerMessageType msgid = (m_ShutdownMask & SHUTDOWN_MASK_RESTART) ? SERVER_MSG_RESTART_CANCELLED : SERVER_MSG_SHUTDOWN_CANCELLED;

    m_ShutdownMask = 0;
    m_ShutdownTimer = 0;
    m_ExitCode = SHUTDOWN_EXIT_CODE;                       // to default value
    SendServerMessage(msgid);

    DEBUG_LOG("Server %s cancelled.",(m_ShutdownMask & SHUTDOWN_MASK_RESTART ? "restart" : "shutdown"));
}

// Send a server message to the user(s)
void World::SendServerMessage(ServerMessageType type, const char *text, Player* player)
{
    WorldPacket data(SMSG_SERVER_MESSAGE, 50);              // guess size
    data << uint32(type);
    if (type <= SERVER_MSG_STRING)
        data << text;

    if (player)
        player->GetSession()->SendPacket(&data);
    else
        SendGlobalMessage(&data);
}

void World::UpdateSessions(time_t diff)
{
    // Add new sessions
    WorldSession* sess;
    while (addSessQueue.next(sess))
        AddSession_ (sess);

    // Then send an update signal to remaining ones
    for (SessionMap::iterator itr = m_sessions.begin(), next; itr != m_sessions.end(); itr = next)
    {
        next = itr;
        ++next;

        if (!itr->second)
            continue;

        // and remove not active sessions from the list
        if (!itr->second->Update(diff))                      // As interval = 0
        {
            if (!RemoveQueuedPlayer(itr->second) && itr->second && getConfig(CONFIG_INTERVAL_DISCONNECT_TOLERANCE))
                m_disconnects[itr->second->GetAccountId()] = time(NULL);
            delete itr->second;
            m_sessions.erase(itr);
        }
    }
}

// This handles the issued and queued CLI commands
void World::ProcessCliCommands()
{
    CliCommandHolder::Print* zprint = NULL;
    void* callbackArg = NULL;
    CliCommandHolder* command;
    while (cliCmdQueue.next(command))
    {
        sLog->outDebug("CLI command under processing...");
        zprint = command->m_print;
        callbackArg = command->m_callbackArg;
        CliHandler handler(callbackArg, zprint);
        handler.ParseCommands(command->m_command);

        if (command->m_commandFinished)
            command->m_commandFinished(callbackArg, !handler.HasSentErrorMessage());

        delete command;
    }
}

void World::SendAutoBroadcast()
{
    if (m_Autobroadcasts.empty())
        return;

    std::string msg;

    std::list<std::string>::const_iterator itr = m_Autobroadcasts.begin();
    std::advance(itr, rand() % m_Autobroadcasts.size());
    msg = *itr;

    uint32 abcenter = ConfigMgr::GetIntDefault("AutoBroadcast.Center", 0);

    if (abcenter == 0)
        sWorld.SendWorldText(LANG_AUTO_BROADCAST, msg.c_str());

    else if (abcenter == 1)
    {
        WorldPacket data(SMSG_NOTIFICATION, (msg.size()+1));
        data << msg;
        sWorld.SendGlobalMessage(&data);
    }

    else if (abcenter == 2)
    {
        sWorld.SendWorldText(LANG_AUTO_BROADCAST, msg.c_str());

        WorldPacket data(SMSG_NOTIFICATION, (msg.size()+1));
        data << msg;
        sWorld.SendGlobalMessage(&data);
    }

    sLog->outString("AutoBroadcast: '%s'", msg.c_str());
}

void World::InitResultQueue()
{
    m_resultQueue = new SqlResultQueue;
    CharacterDatabase.SetResultQueue(m_resultQueue);
}

void World::UpdateResultQueue()
{
    m_resultQueue->Update();
}

void World::UpdateRealmCharCount(uint32 accountId)
{
    CharacterDatabase.AsyncPQuery(this, &World::_UpdateRealmCharCount, accountId,
        "SELECT COUNT(guid) FROM characters WHERE account = '%u'", accountId);
}

void World::_UpdateRealmCharCount(QueryResult_AutoPtr resultCharCount, uint32 accountId)
{
    if (resultCharCount)
    {
        Field *fields = resultCharCount->Fetch();
        uint32 charCount = fields[0].GetUInt32();

        LoginDatabase.PExecute("DELETE FROM realmcharacters WHERE acctid= '%d' AND realmid = '%d'", accountId, realmID);
        LoginDatabase.PExecute("INSERT INTO realmcharacters (numchars, acctid, realmid) VALUES (%u, %u, %u)", charCount, accountId, realmID);
    }
}

void World::InitDailyQuestResetTime()
{
    time_t mostRecentQuestTime;

    QueryResult_AutoPtr result = CharacterDatabase.Query("SELECT MAX(time) FROM character_queststatus_daily");
    if (result)
    {
        Field *fields = result->Fetch();
        mostRecentQuestTime = (time_t)fields[0].GetUInt64();
    }
    else
        mostRecentQuestTime = 0;

    // client built-in time for reset is 6:00 AM
    // FIX ME: client not show day start time
    time_t curTime = time(NULL);
    tm localTm = *localtime(&curTime);
    localTm.tm_hour = 6;
    localTm.tm_min  = 0;
    localTm.tm_sec  = 0;

    // current day reset time
    time_t curDayResetTime = mktime(&localTm);

    // last reset time before current moment
    time_t resetTime = (curTime < curDayResetTime) ? curDayResetTime - DAY : curDayResetTime;

    // need reset (if we have quest time before last reset time (not processed by some reason)
    if (mostRecentQuestTime && mostRecentQuestTime <= resetTime)
        m_NextDailyQuestReset = mostRecentQuestTime;
    else
    {
        // plan next reset time
        m_NextDailyQuestReset = (curTime >= curDayResetTime) ? curDayResetTime + DAY : curDayResetTime;
    }
}

void World::UpdateAllowedSecurity()
{
     QueryResult_AutoPtr result = LoginDatabase.PQuery("SELECT allowedSecurityLevel from realmlist WHERE id = '%d'", realmID);
     if (result)
     {
        m_allowedSecurityLevel = AccountTypes(result->Fetch()->GetUInt16());
        sLog->outDebug("Allowed Level: %u Result %u", m_allowedSecurityLevel, result->Fetch()->GetUInt16());
     }
}

void World::ResetDailyQuests()
{
    sLog->outDetail("Daily quests reset for all characters.");
    CharacterDatabase.Execute("DELETE FROM character_queststatus_daily");
    for (SessionMap::iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
        if (itr->second->GetPlayer())
            itr->second->GetPlayer()->ResetDailyQuestStatus();
}

void World::SetPlayerLimit(int32 limit, bool needUpdate)
{
    m_playerLimit = limit;
}

void World::UpdateMaxSessionCounters()
{
    m_maxActiveSessionCount = std::max(m_maxActiveSessionCount, uint32(m_sessions.size()-m_QueuedPlayer.size()));
    m_maxQueuedSessionCount = std::max(m_maxQueuedSessionCount, uint32(m_QueuedPlayer.size()));
}

void World::LoadDBVersion()
{
    QueryResult_AutoPtr result = WorldDatabase.Query("SELECT db_version FROM version LIMIT 1");
    if (result)
    {
        Field* fields = result->Fetch();

        m_DBVersion = fields[0].GetString();
    }
    else
        m_DBVersion = "unknown world database";
}

