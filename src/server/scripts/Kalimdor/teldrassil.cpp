 /*
  * Copyright (C) 2010-2012 Project SkyFire <http://www.projectskyfire.org/>
  * Copyright (C) 2010-2012 Oregon <http://www.oregoncore.com/>
  * Copyright (C) 2006-2008 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
  * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
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

/* ScriptData
SDName: Teldrassil
SD%Complete: 100
SDComment: Quest support: 938
SDCategory: Teldrassil
EndScriptData */

/* ContentData
npc_mist
EndContentData */

#include "ScriptPCH.h"
#include "ScriptedFollowerAI.h"

/*####
# npc_mist
####*/

enum eMist
{
    SAY_AT_HOME             = -1000323,
    EMOTE_AT_HOME           = -1000324,
    QUEST_MIST              = 938,
    NPC_ARYNIA              = 3519,

    POINT_ID_TO_FOREST      = 1,
    FACTION_DARNASSUS       = 79
};

const float m_afToForestLoc[] = {10648.7f, 1790.63f, 1324.08f};

struct npc_mistAI : public FollowerAI
{
    npc_mistAI(Creature* creature) : FollowerAI(creature) { }

    uint32 m_uiPostEventTimer;
    uint32 m_uiPhasePostEvent;

    uint64 AryniaGUID;

    void Reset()
    {
        m_uiPostEventTimer = 1000;
        m_uiPhasePostEvent = 0;

        AryniaGUID = 0;
    }

    void MoveInLineOfSight(Unit *pWho)
    {
        FollowerAI::MoveInLineOfSight(pWho);

        if (!me->getVictim() && !HasFollowState(STATE_FOLLOW_COMPLETE | STATE_FOLLOW_POSTEVENT) && pWho->GetEntry() == NPC_ARYNIA)
        {
            if (me->IsWithinDistInMap(pWho, INTERACTION_DISTANCE))
            {
                if (Player* player = GetLeaderForFollower())
                {
                    if (player->GetQuestStatus(QUEST_MIST) == QUEST_STATUS_INCOMPLETE)
                        player->GroupEventHappens(QUEST_MIST, me);
                }

                AryniaGUID = pWho->GetGUID();
                SetFollowComplete(true);
            }
        }
    }

    void MovementInform(uint32 uiMotionType, uint32 uiPointId)
    {
        FollowerAI::MovementInform(uiMotionType, uiPointId);

        if (uiMotionType != POINT_MOTION_TYPE)
            return;

        if (uiPointId == POINT_ID_TO_FOREST)
            SetFollowComplete();
    }

    void UpdateFollowerAI(const uint32 uiDiff)
    {
        if (!UpdateVictim())
        {
            if (HasFollowState(STATE_FOLLOW_POSTEVENT))
            {
                if (m_uiPostEventTimer <= uiDiff)
                {
                    m_uiPostEventTimer = 3000;

                    Unit *pArynia = Unit::GetUnit(*me, AryniaGUID);
                    if (!pArynia || !pArynia->isAlive())
                    {
                        SetFollowComplete();
                        return;
                    }

                    switch (m_uiPhasePostEvent)
                    {
                    case 0:
                        DoScriptText(SAY_AT_HOME, pArynia);
                        break;
                    case 1:
                        DoScriptText(EMOTE_AT_HOME, me);
                        me->GetMotionMaster()->MovePoint(POINT_ID_TO_FOREST, m_afToForestLoc[0], m_afToForestLoc[1], m_afToForestLoc[2]);
                        break;
                    }

                    ++m_uiPhasePostEvent;
                }
                else
                    m_uiPostEventTimer -= uiDiff;
            }

            return;
        }

        DoMeleeAttackIfReady();
    }
};

CreatureAI* GetAI_npc_mist(Creature* creature)
{
    return new npc_mistAI(creature);
}

bool QuestAccept_npc_mist(Player* player, Creature* creature, Quest const* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_MIST)
    {
        if (npc_mistAI* pMistAI = CAST_AI(npc_mistAI, creature->AI()))
            pMistAI->StartFollow(player, FACTION_DARNASSUS, pQuest);
    }

    return true;
}

void AddSC_teldrassil()
{
    Script *newscript;

    newscript = new Script;
    newscript->Name = "npc_mist";
    newscript->GetAI = &GetAI_npc_mist;
    newscript->pQuestAccept = &QuestAccept_npc_mist;
    newscript->RegisterSelf();
}
