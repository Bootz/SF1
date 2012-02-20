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
SDName: Karazhan
SD%Complete: 100
SDComment: Support for Barnes (Opera controller) and Berthold (Doorman), Support for Quest 9645.
SDCategory: Karazhan
EndScriptData */

/* ContentData
npc_barnes
npc_berthold
npc_image_of_medivh
EndContentData */

#include "ScriptPCH.h"
#include "karazhan.h"
#include "ScriptedEscortAI.h"

/*######
# npc_barnesAI
######*/

#define GOSSIP_READY        "I'm not an actor."

#define SAY_READY           "Splendid, I'm going to get the audience ready. Break a leg!"
#define SAY_OZ_INTRO1       "Finally, everything is in place. Are you ready for your big stage debut?"
#define OZ_GOSSIP1          "I'm not an actor."
#define SAY_OZ_INTRO2       "Don't worry, you'll be fine. You look like a natural!"
#define OZ_GOSSIP2          "Ok, I'll give it a try, then."

#define SAY_RAJ_INTRO1      "The romantic plays are really tough, but you'll do better this time. You have TALENT. Ready?"
#define RAJ_GOSSIP1         "I've never been more ready."

#define OZ_GM_GOSSIP1       "[GM] Change event to EVENT_OZ"
#define OZ_GM_GOSSIP2       "[GM] Change event to EVENT_HOOD"
#define OZ_GM_GOSSIP3       "[GM] Change event to EVENT_RAJ"

struct Dialogue
{
    int32 textid;
    uint32 timer;
};

static Dialogue OzDialogue[]=
{
    {-1532103, 6000},
    {-1532104, 18000},
    {-1532105, 9000},
    {-1532106, 15000}
};

static Dialogue HoodDialogue[]=
{
    {-1532107, 6000},
    {-1532108, 10000},
    {-1532109, 14000},
    {-1532110, 15000}
};

static Dialogue RAJDialogue[]=
{
    {-1532111, 5000},
    {-1532112, 7000},
    {-1532113, 14000},
    {-1532114, 14000}
};

float GameObjects_OZ[5][4]=
{
    {183496, -10909.5f, -1761.79f, 90.477f}, //Bush
    {183496, -10906.7f, -1750.10f, 90.476f},
    {183496, -10883.0f, -1751.81f, 90.476f},
    {183496, -10877.7f, -1763.18f, 90.477f},
    {183442, -10890.9f, -1744.06f, 90.476f} //Scene Picture
};

float GameObjects_RomeJulia[1][4]=
{
    {183443, -10890.9f, -1744.06f, 90.476f}
};

float GameObjects_Wolf[5][4]=
{
    {183492, -10908.9f, -1762.55f, 90.478f},
    {183492, -10902.4f, -1753.36f, 90.477f},
    {183492, -10876.2f, -1761.21f, 90.477f},
    {183493, -10885.2f, -1756.47f, 90.477f},
    {183491, -10890.9f, -1744.06f, 90.476f}
};

// Entries and spawn locations for creatures in Oz event
float Spawns[6][2]=
{
    {17535, -10896},                                        // Dorothee
    {17546, -10891},                                        // Roar
    {17547, -10884},                                        // Tinhead
    {17543, -10902},                                        // Strawman
    {17603, -10892},                                        // Grandmother
    {17534, -10900},                                        // Julianne
};

#define CREATURE_SPOTLIGHT  19525

#define SPELL_SPOTLIGHT     25824
#define SPELL_TUXEDO        32616

#define SPAWN_Z             90.5f
#define SPAWN_Y             -1758
#define SPAWN_O             4.738f

struct npc_barnesAI : public npc_escortAI
{
    npc_barnesAI(Creature* c) : npc_escortAI(c)
    {
        RaidWiped = false;
        m_uiEventId = 0;
        pInstance = c->GetInstanceData();
        SpellEntry *TempSpell = GET_SPELL(29683);
        TempSpell->Effect[2] = 27;
        TempSpell->EffectBaseDice[2] = 1;
        TempSpell->EffectDieSides[2] = 1;
        TempSpell->EffectBasePoints[2] = 49;
        TempSpell->EffectImplicitTargetA[2] = 18;
        TempSpell->EffectImplicitTargetB[2] = 16;
        TempSpell->EffectRadiusIndex[2] = 7;
        TempSpell->EffectApplyAuraName[2] = 79;
        TempSpell->EffectMiscValue[2] = 127;
    }

    ScriptedInstance* pInstance;

    uint64 m_uiSpotlightGUID;

    uint32 TalkCount;
    uint32 TalkTimer;
    uint32 WipeTimer;
    uint32 m_uiEventId;

    bool PerformanceReady;
    bool RaidWiped;

    void Reset()
    {
        m_uiSpotlightGUID = 0;

        TalkCount = 0;
        TalkTimer = 2000;
        WipeTimer = 5000;

        PerformanceReady = false;

        if (pInstance)
            m_uiEventId = pInstance->GetData(DATA_OPERA_PERFORMANCE);
    }

    void StartEvent()
    {
        if (!pInstance)
            return;

        pInstance->SetData(TYPE_OPERA, IN_PROGRESS);

        //resets count for this event, in case earlier failed
        if (m_uiEventId == EVENT_OZ)
            pInstance->SetData(DATA_OPERA_OZ_DEATHCOUNT, IN_PROGRESS);

        Start(false, false);
    }

    void EnterCombat(Unit* /*who*/) {}

    void WaypointReached(uint32 i)
    {
        if (!pInstance)
            return;

        switch(i)
        {
            case 0:
                DoCast(me, SPELL_TUXEDO, false);
                pInstance->DoUseDoorOrButton(pInstance->GetData64(DATA_GO_STAGEDOORLEFT));
                break;
            case 2:
                TalkCount = 0;
                SetEscortPaused(true);

                if (Creature* pSpotlight = me->SummonCreature(CREATURE_SPOTLIGHT,
                    me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0.0f,
                    TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 60000))
                {
                    pSpotlight->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
                    pSpotlight->CastSpell(pSpotlight, SPELL_SPOTLIGHT, false);
                    m_uiSpotlightGUID = pSpotlight->GetGUID();
                }
                switch(m_uiEventId)
                {
                    case EVENT_OZ:
                        for (int i = 0; i<5; i++)
                            me->SummonGameObject(GameObjects_OZ[i][0],GameObjects_OZ[i][1],GameObjects_OZ[i][2],GameObjects_OZ[i][3],4.63f,0,0,0.73f,-0.68f,60000);
                        break;
                    case EVENT_HOOD:
                        for (int i = 0; i<5; i++)
                            me->SummonGameObject(GameObjects_Wolf[i][0],GameObjects_Wolf[i][1],GameObjects_Wolf[i][2],GameObjects_Wolf[i][3],4.63f,0,0,0.73f,-0.68f,60000);
                        break;
                    case EVENT_RAJ:
                        for (int i = 0; i<1; i++)
                            me->SummonGameObject(GameObjects_RomeJulia[i][0],GameObjects_RomeJulia[i][1],GameObjects_RomeJulia[i][2],GameObjects_RomeJulia[i][3],4.63f,0,0,0.73f,-0.68f,60000);
                        break;
                }
                break;
            case 5:
                pInstance->DoUseDoorOrButton(pInstance->GetData64(DATA_GO_STAGEDOORLEFT));
                PerformanceReady = true;
                PrepareEncounter();
                pInstance->DoUseDoorOrButton(pInstance->GetData64(DATA_GO_CURTAINS));
                break;
        }
    }

    void Talk(uint32 count)
    {
        int32 text = 0;

        switch(m_uiEventId)
        {
            case EVENT_OZ:
                if (OzDialogue[count].textid)
                     text = OzDialogue[count].textid;
                if (OzDialogue[count].timer)
                    TalkTimer = OzDialogue[count].timer;
                break;

            case EVENT_HOOD:
                if (HoodDialogue[count].textid)
                    text = HoodDialogue[count].textid;
                if (HoodDialogue[count].timer)
                    TalkTimer = HoodDialogue[count].timer;
                break;

            case EVENT_RAJ:
                 if (RAJDialogue[count].textid)
                     text = RAJDialogue[count].textid;
                if (RAJDialogue[count].timer)
                    TalkTimer = RAJDialogue[count].timer;
                break;
        }

        if (text)
             DoScriptText(text, me);
    }

    void PrepareEncounter()
    {
        debug_log("OSCR: Barnes Opera Event - Introduction complete - preparing encounter %d", m_uiEventId);
        uint8 index = 0;
        uint8 count = 0;

        switch(m_uiEventId)
        {
            case EVENT_OZ:
                index = 0;
                count = 4;
                break;
            case EVENT_HOOD:
                index = 4;
                count = index+1;
                break;
            case EVENT_RAJ:
                index = 5;
                count = index+1;
                break;
        }

        for (; index < count; ++index)
        {
            uint32 entry = ((uint32)Spawns[index][0]);
            float PosX = Spawns[index][1];

            if (Creature* pCreature = me->SummonCreature(entry, PosX, SPAWN_Y, SPAWN_Z, SPAWN_O, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, HOUR*2*IN_MILLISECONDS))
            {
                // In case database has bad flags
                pCreature->SetUInt32Value(UNIT_FIELD_FLAGS, 0);
                pCreature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
            }
        }

        RaidWiped = false;
    }

    void UpdateAI(const uint32 diff)
    {
        npc_escortAI::UpdateAI(diff);

        if (HasEscortState(STATE_ESCORT_PAUSED))
        {
            if (TalkTimer <= diff)
            {
                if (TalkCount > 3)
                {
                    if (Creature* pSpotlight = Unit::GetCreature(*me, m_uiSpotlightGUID))
                        pSpotlight->ForcedDespawn();

                    SetEscortPaused(false);
                    return;
                }

                Talk(TalkCount);
                ++TalkCount;
            } else TalkTimer -= diff;
        }

        if (PerformanceReady)
        {
            if (!RaidWiped)
            {
                if (WipeTimer <= diff)
                {
                    Map* pMap = me->GetMap();
                    if (!pMap->IsDungeon())
                        return;

                    Map::PlayerList const &PlayerList = pMap->GetPlayers();
                    if (PlayerList.isEmpty())
                        return;

                    RaidWiped = true;
                    for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
                    {
                        if (i->getSource()->isAlive() && !i->getSource()->isGameMaster())
                        {
                            RaidWiped = false;
                            break;
                        }
                    }

                    if (RaidWiped)
                    {
                        RaidWiped = true;
                        EnterEvadeMode();
                        return;
                    }

                    WipeTimer = 15000;
                } else WipeTimer -= diff;
            }
        }
    }
};

CreatureAI* GetAI_npc_barnesAI(Creature* pCreature)
{
    return new npc_barnesAI(pCreature);
}

bool GossipHello_npc_barnes(Player* pPlayer, Creature* pCreature)
{
    if (ScriptedInstance* pInstance = pCreature->GetInstanceData())
    {
        // Check for death of Moroes and if opera event is not done already
        if (pInstance->GetData(TYPE_MOROES) == DONE && pInstance->GetData(TYPE_OPERA) != DONE)
        {
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, OZ_GOSSIP1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

            if (pPlayer->isGameMaster())
            {
                pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_DOT, OZ_GM_GOSSIP1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 3);
                pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_DOT, OZ_GM_GOSSIP2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 4);
                pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_DOT, OZ_GM_GOSSIP3, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 5);
            }

            if (npc_barnesAI* pBarnesAI = CAST_AI(npc_barnesAI,pCreature->AI()))
            {
                if (!pBarnesAI->RaidWiped)
                    pPlayer->SEND_GOSSIP_MENU(8970, pCreature->GetGUID());
                else
                    pPlayer->SEND_GOSSIP_MENU(8975, pCreature->GetGUID());

                return true;
            }
        }
    }

    pPlayer->SEND_GOSSIP_MENU(8978, pCreature->GetGUID());
    return true;
}

bool GossipSelect_npc_barnes(Player* pPlayer, Creature* pCreature, uint32 /*uiSender*/, uint32 uiAction)
{
    npc_barnesAI* pBarnesAI = CAST_AI(npc_barnesAI, pCreature->AI());

    switch(uiAction)
    {
        case GOSSIP_ACTION_INFO_DEF+1:
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, OZ_GOSSIP2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+2);
            pPlayer->SEND_GOSSIP_MENU(8971, pCreature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+2:
            pPlayer->CLOSE_GOSSIP_MENU();
            pBarnesAI->StartEvent();
            break;
        case GOSSIP_ACTION_INFO_DEF+3:
            pPlayer->CLOSE_GOSSIP_MENU();
            pBarnesAI->m_uiEventId = EVENT_OZ;
            outstring_log("OSCR: player (GUID %i) manually set Opera event to EVENT_OZ",pPlayer->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+4:
            pPlayer->CLOSE_GOSSIP_MENU();
            pBarnesAI->m_uiEventId = EVENT_HOOD;
            outstring_log("OSCR: player (GUID %i) manually set Opera event to EVENT_HOOD",pPlayer->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+5:
            pPlayer->CLOSE_GOSSIP_MENU();
            pBarnesAI->m_uiEventId = EVENT_RAJ;
            outstring_log("OSCR: player (GUID %i) manually set Opera event to EVENT_RAJ",pPlayer->GetGUID());
            break;
    }

    return true;
}

/*###
# npc_berthold
####*/

enum eBerthold
{
    SPELL_TELEPORT           = 39567
};

#define GOSSIP_ITEM_TELEPORT    "Teleport me to the Guardian's Library"

bool GossipHello_npc_berthold(Player* pPlayer, Creature* pCreature)
{
    if (ScriptedInstance* pInstance = pCreature->GetInstanceData())
    {
        // Check if Shade of Aran event is done
        if (pInstance->GetData(TYPE_ARAN) == DONE)
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_ITEM_TELEPORT, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
    }

    pPlayer->SEND_GOSSIP_MENU(pPlayer->GetGossipTextId(pCreature), pCreature->GetGUID());
    return true;
}

bool GossipSelect_npc_berthold(Player* pPlayer, Creature* /*pCreature*/, uint32 /*uiSender*/, uint32 uiAction)
{
    if (uiAction == GOSSIP_ACTION_INFO_DEF + 1)
        pPlayer->CastSpell(pPlayer, SPELL_TELEPORT, true);

    pPlayer->CLOSE_GOSSIP_MENU();
    return true;
}

/*###
# npc_image_of_medivh
####*/

#define SAY_DIALOG_MEDIVH_1         "You've got my attention, dragon. You'll find I'm not as easily scared as the villagers below."
#define SAY_DIALOG_ARCANAGOS_2      "Your dabbling in the arcane has gone too far, Medivh. You've attracted the attention of powers beyond your understanding. You must leave Karazhan at once!"
#define SAY_DIALOG_MEDIVH_3         "You dare challenge me at my own dwelling? Your arrogance is astounding, even for a dragon!"
#define SAY_DIALOG_ARCANAGOS_4      "A dark power seeks to use you, Medivh! If you stay, dire days will follow. You must hurry, we don't have much time!"
#define SAY_DIALOG_MEDIVH_5         "I do not know what you speak of, dragon... but I will not be bullied by this display of insolence. I'll leave Karazhan when it suits me!"
#define SAY_DIALOG_ARCANAGOS_6      "You leave me no alternative. I will stop you by force if you won't listen to reason!"
#define EMOTE_DIALOG_MEDIVH_7       "begins to cast a spell of great power, weaving his own essence into the magic."
#define SAY_DIALOG_ARCANAGOS_8      "What have you done, wizard? This cannot be! I'm burning from... within!"
#define SAY_DIALOG_MEDIVH_9         "He should not have angered me. I must go... recover my strength now..."

#define MOB_ARCANAGOS               17652
#define SPELL_FIRE_BALL             30967
#define SPELL_UBER_FIREBALL         30971
#define SPELL_CONFLAGRATION_BLAST   30977
#define SPELL_MANA_SHIELD           31635

static float MedivPos[4] = {-11161.49f,-1902.24f,91.48f,1.94f};
static float ArcanagosPos[4] = {-11169.75f,-1881.48f,95.39f,4.83f};

struct npc_image_of_medivhAI : public ScriptedAI
{
    npc_image_of_medivhAI(Creature* c) : ScriptedAI(c)
    {
        pInstance = c->GetInstanceData();
    }

    ScriptedInstance *pInstance;

    uint64 ArcanagosGUID;

    uint32 YellTimer;
    uint32 Step;
    uint32 FireMedivhTimer;
    uint32 FireArcanagosTimer;

    bool EventStarted;

    void Reset()
    {
        ArcanagosGUID = 0;

        if (pInstance && pInstance->GetData64(DATA_IMAGE_OF_MEDIVH) == 0)
        {
            pInstance->SetData64(DATA_IMAGE_OF_MEDIVH, me->GetGUID());
            (*me).GetMotionMaster()->MovePoint(1,MedivPos[0],MedivPos[1],MedivPos[2]);
            Step = 0;
        } else
        {
            me->DealDamage(me,me->GetHealth(),NULL, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, NULL, false);
            me->RemoveCorpse();
        }
    }
    void EnterCombat(Unit* /*who*/) {}

    void MovementInform(uint32 type, uint32 id)
    {
        if (type != POINT_MOTION_TYPE)
            return;
        if (id == 1)
        {
            StartEvent();
            me->SetOrientation(MedivPos[3]);
            me->SetOrientation(MedivPos[3]);
        }
    }

    void StartEvent()
    {
        Step = 1;
        EventStarted = true;
        Creature* Arcanagos = me->SummonCreature(MOB_ARCANAGOS,ArcanagosPos[0],ArcanagosPos[1],ArcanagosPos[2],0,TEMPSUMMON_CORPSE_TIMED_DESPAWN,20000);
        if (!Arcanagos)
            return;
        ArcanagosGUID = Arcanagos->GetGUID();
        Arcanagos->AddUnitMovementFlag(MOVEFLAG_ONTRANSPORT | MOVEFLAG_LEVITATING);
        (*Arcanagos).GetMotionMaster()->MovePoint(0,ArcanagosPos[0],ArcanagosPos[1],ArcanagosPos[2]);
        Arcanagos->SetOrientation(ArcanagosPos[3]);
        me->SetOrientation(MedivPos[3]);
        YellTimer = 10000;
    }

    uint32 NextStep(uint32 Step)
    {
        Unit* arca = Unit::GetUnit((*me),ArcanagosGUID);
        Map* pMap = me->GetMap();
        switch(Step)
        {
        case 0: return 9999999;
        case 1:
            me->MonsterYell(SAY_DIALOG_MEDIVH_1,LANG_UNIVERSAL,NULL);
            return 10000;
        case 2:
            if (arca)
                CAST_CRE(arca)->MonsterYell(SAY_DIALOG_ARCANAGOS_2,LANG_UNIVERSAL,NULL);
            return 20000;
        case 3:
            me->MonsterYell(SAY_DIALOG_MEDIVH_3,LANG_UNIVERSAL,NULL);
            return 10000;
        case 4:
            if (arca)
                CAST_CRE(arca)->MonsterYell(SAY_DIALOG_ARCANAGOS_4, LANG_UNIVERSAL, NULL);
            return 20000;
        case 5:
            me->MonsterYell(SAY_DIALOG_MEDIVH_5, LANG_UNIVERSAL, NULL);
            return 20000;
        case 6:
            if (arca)
                CAST_CRE(arca)->MonsterYell(SAY_DIALOG_ARCANAGOS_6, LANG_UNIVERSAL, NULL);
            return 10000;
        case 7:
            FireArcanagosTimer = 500;
            return 5000;
        case 8:
            FireMedivhTimer = 500;
            DoCast(me, SPELL_MANA_SHIELD);
            return 10000;
        case 9:
            me->MonsterTextEmote(EMOTE_DIALOG_MEDIVH_7, 0, false);
            return 10000;
        case 10:
            if (arca)
                DoCast(arca, SPELL_CONFLAGRATION_BLAST, false);
            return 1000;
        case 11:
            if (arca)
                CAST_CRE(arca)->MonsterYell(SAY_DIALOG_ARCANAGOS_8, LANG_UNIVERSAL, NULL);
            return 5000;
        case 12:
            arca->GetMotionMaster()->MovePoint(0, -11010.82f,-1761.18f, 156.47f);
            arca->setActive(true);
            arca->InterruptNonMeleeSpells(true);
            arca->SetSpeed(MOVE_FLIGHT, 2.0f);
            return 10000;
        case 13:
            me->MonsterYell(SAY_DIALOG_MEDIVH_9, LANG_UNIVERSAL, NULL);
            return 10000;
        case 14:
            me->SetVisibility(VISIBILITY_OFF);
            me->ClearInCombat();

            if (pMap->IsDungeon())
            {
                InstanceMap::PlayerList const &PlayerList = pMap->GetPlayers();
                for (InstanceMap::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
                {
                    if (i->getSource()->isAlive())
                    {
                        if (i->getSource()->GetQuestStatus(9645) == QUEST_STATUS_INCOMPLETE)
                            i->getSource()->CompleteQuest(9645);
                    }
                }
            }
            return 50000;
        case 15:
            arca->DealDamage(arca,arca->GetHealth(),NULL, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, NULL, false);
            return 5000;
        default : return 9999999;
        }
    }

    void UpdateAI(const uint32 diff)
    {
        if (YellTimer <= diff)
        {
            if (EventStarted)
                YellTimer = NextStep(Step++);
        } else YellTimer -= diff;

        if (Step >= 7 && Step <= 12)
        {
            Unit* arca = Unit::GetUnit((*me),ArcanagosGUID);

            if (FireArcanagosTimer <= diff)
            {
                if (arca)
                    arca->CastSpell(me, SPELL_FIRE_BALL, false);
                FireArcanagosTimer = 6000;
            } else FireArcanagosTimer -= diff;

            if (FireMedivhTimer <= diff)
            {
                if (arca)
                    DoCast(arca, SPELL_FIRE_BALL);
                FireMedivhTimer = 5000;
            } else FireMedivhTimer -= diff;
        }
    }
};

CreatureAI* GetAI_npc_image_of_medivh(Creature* pCreature)
{
    return new npc_image_of_medivhAI(pCreature);
}

void AddSC_karazhan()
{
    Script* newscript;

    newscript = new Script;
    newscript->GetAI = &GetAI_npc_barnesAI;
    newscript->Name = "npc_barnes";
    newscript->pGossipHello = &GossipHello_npc_barnes;
    newscript->pGossipSelect = &GossipSelect_npc_barnes;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "npc_berthold";
    newscript->pGossipHello = &GossipHello_npc_berthold;
    newscript->pGossipSelect = &GossipSelect_npc_berthold;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "npc_image_of_medivh";
    newscript->GetAI = &GetAI_npc_image_of_medivh;
    newscript->RegisterSelf();
}

