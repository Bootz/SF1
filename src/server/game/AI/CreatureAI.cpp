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

#include "CreatureAI.h"
#include "CreatureAIImpl.h"
#include "Creature.h"
#include "World.h"
#include "SpellMgr.h"

//Disable CreatureAI when charmed
void CreatureAI::OnCharmed(bool /*apply*/)
{
    //me->IsAIEnabled = !apply;*/
    me->NeedChangeAI = true;
    me->IsAIEnabled = false;
}

AISpellInfoType * UnitAI::AISpellInfo;
AISpellInfoType * GetAISpellInfo(uint32 i) { return &CreatureAI::AISpellInfo[i]; }

void CreatureAI::DoZoneInCombat(Creature* creature)
{
    if (!creature)
        creature = me;

    if (!creature->CanHaveThreatList())
        return;

    Map *map = creature->GetMap();
    if (!map->IsDungeon())                                  //use IsDungeon instead of Instanceable, in case battlegrounds will be instantiated
    {
        sLog.outError("DoZoneInCombat call for map that isn't an instance (creature entry = %d)", creature->GetTypeId() == TYPEID_UNIT ? creature->ToCreature()->GetEntry() : 0);
        return;
    }

    if (!creature->HasReactState(REACT_PASSIVE) && !creature->getVictim())
    {
        if (Unit *target = creature->SelectNearestTarget(50))
            creature->AI()->AttackStart(target);
        else if (creature->isSummon())
        {
            if (Unit *summoner = ((TempSummon*)creature)->GetSummoner())
            {
                Unit *target = summoner->getAttackerForHelper();
                if (!target && summoner->CanHaveThreatList() && !summoner->getThreatManager().isThreatListEmpty())
                    target = summoner->getThreatManager().getHostileTarget();
                if (target && (creature->IsFriendlyTo(summoner) || creature->IsHostileTo(target)))
                    creature->AI()->AttackStart(target);
            }
        }
    }

    if (!creature->HasReactState(REACT_PASSIVE) && !creature->getVictim())
    {
        sLog.outError("DoZoneInCombat called for creature that has empty threat list (creature entry = %u)", creature->GetEntry());
        return;
    }

    Map::PlayerList const &PlList = map->GetPlayers();

    if (PlList.isEmpty())
        return;

    for (Map::PlayerList::const_iterator i = PlList.begin(); i != PlList.end(); ++i)
    {
        if (Player* pPlayer = i->getSource())
        {
            if (pPlayer->isGameMaster())
                continue;

            if (pPlayer->isAlive())
            {
                creature->SetInCombatWith(pPlayer);
                pPlayer->SetInCombatWith(creature);
                creature->AddThreat(pPlayer, 0.0f);
            }

            /* Causes certain things to never leave the threat list (Priest Lightwell, etc):
            for (Unit::ControlList::const_iterator itr = pPlayer->m_Controlled.begin(); itr != pPlayer->m_Controlled.end(); ++itr)
            {
                creature->SetInCombatWith(*itr);
                (*itr)->SetInCombatWith(creature);
                creature->AddThreat(*itr, 0.0f);
            }*/
        }
    }
}

// scripts does not take care about MoveInLineOfSight loops
// MoveInLineOfSight can be called inside another MoveInLineOfSight and cause stack overflow
void CreatureAI::MoveInLineOfSight_Safe(Unit *who)
{
    if (m_MoveInLineOfSight_locked == true)
        return;
    m_MoveInLineOfSight_locked = true;
    MoveInLineOfSight(who);
    m_MoveInLineOfSight_locked = false;
}

void CreatureAI::MoveInLineOfSight(Unit *who)
{
    if (me->getVictim())
        return;

    if (me->canStartAttack(who))
        AttackStart(who);
    //else if (who->getVictim() && me->IsFriendlyTo(who)
    //    && me->IsWithinDistInMap(who, sWorld.getConfig(CONFIG_CREATURE_FAMILY_ASSISTANCE_RADIUS))
    //    && me->canAttack(who->getVictim()))
    //    AttackStart(who->getVictim());
}

bool CreatureAI::UpdateVictimByReact()
{
    if (!me->isInCombat())
        return false;

    if (me->HasReactState(REACT_AGGRESSIVE))
    {
        if (Unit *victim = me->SelectVictim())
            AttackStart(victim);
        return me->getVictim();
    }
    else if (me->getThreatManager().isThreatListEmpty())
    {
        EnterEvadeMode();
        return false;
    }

    return true;
}

void CreatureAI::SelectNearestTarget(Unit *who)
{
    if (me->getVictim() && me->GetDistanceOrder(who, me->getVictim()) && me->canAttack(who))
    {
        me->getThreatManager().modifyThreatPercent(me->getVictim(), -100);
        me->AddThreat(who, 1000000.0f);
    }
}

void CreatureAI::EnterEvadeMode()
{
    if (!_EnterEvadeMode())
        return;

    sLog.outDebug("Creature %u enters evade mode.", me->GetEntry());

    if (Unit *owner = me->GetCharmerOrOwner())
    {
        me->GetMotionMaster()->Clear(false);
        me->GetMotionMaster()->MoveFollow(owner, PET_FOLLOW_DIST, me->GetFollowAngle(), MOTION_SLOT_ACTIVE);
    }
    else
        me->GetMotionMaster()->MoveTargetedHome();

    Reset();
}

/*void CreatureAI::AttackedBy(Unit* attacker)
{
    if (!me->getVictim())
        AttackStart(attacker);
}*/

