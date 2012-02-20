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
SDName: Boss_Harbinger_Skyriss
SD%Complete: 45
SDComment: CombatAI not fully implemented. Timers will need adjustments. Need more docs on how event fully work. Reset all event and force start over if fail at one point?
SDCategory: Tempest Keep, The Arcatraz
EndScriptData */

/* ContentData
boss_harbinger_skyriss
boss_harbinger_skyriss_illusion
EndContentData */

#include "ScriptPCH.h"
#include "arcatraz.h"

#define SAY_INTRO               -1552000
#define SAY_AGGRO               -1552001
#define SAY_KILL_1              -1552002
#define SAY_KILL_2              -1552003
#define SAY_MIND_1              -1552004
#define SAY_MIND_2              -1552005
#define SAY_FEAR_1              -1552006
#define SAY_FEAR_2              -1552007
#define SAY_IMAGE               -1552008
#define SAY_DEATH               -1552009

#define SPELL_FEAR              39415

#define SPELL_MIND_REND         36924
#define H_SPELL_MIND_REND       39017

#define SPELL_DOMINATION        37162
#define H_SPELL_DOMINATION      39019

#define H_SPELL_MANA_BURN       39020

#define SPELL_66_ILLUSION       36931                       //entry 21466
#define SPELL_33_ILLUSION       36932                       //entry 21467

struct boss_harbinger_skyrissAI : public ScriptedAI
{
    boss_harbinger_skyrissAI(Creature *c) : ScriptedAI(c)
    {
        pInstance = c->GetInstanceData();
        HeroicMode = me->GetMap()->IsHeroic();
        Intro = false;
    }

    ScriptedInstance *pInstance;
    bool HeroicMode;

    bool Intro;
    bool IsImage33;
    bool IsImage66;

    uint32 Intro_Phase;
    uint32 Intro_Timer;
    uint32 MindRend_Timer;
    uint32 Fear_Timer;
    uint32 Domination_Timer;
    uint32 ManaBurn_Timer;

    void Reset()
    {
        if (!Intro)
            me->SetFlag(UNIT_FIELD_FLAGS,UNIT_FLAG_OOC_NOT_ATTACKABLE);

        IsImage33 = false;
        IsImage66 = false;

        Intro_Phase = 1;
        Intro_Timer = 5000;
        MindRend_Timer = 3000;
        Fear_Timer = 15000;
        Domination_Timer = 30000;
        ManaBurn_Timer = 25000;
    }

    void MoveInLineOfSight(Unit *who)
    {
        if (!Intro)
        {
            return;
        }
        ScriptedAI::MoveInLineOfSight(who);
    }

    void EnterCombat(Unit *who) {}

    void JustDied(Unit* Killer)
    {
        DoScriptText(SAY_DEATH, me);
        if (pInstance)
            pInstance->SetData(TYPE_HARBINGERSKYRISS,DONE);
    }

    void JustSummoned(Creature *summon)
    {
        if (!summon)
            return;
        if (IsImage66)
            summon->SetHealth((summon->GetMaxHealth()*33)/100);
        else
            summon->SetHealth((summon->GetMaxHealth()*66)/100);
        if (me->getVictim())
            if (Unit *pTarget = SelectUnit(SELECT_TARGET_RANDOM, 0))
                summon->AI()->AttackStart(pTarget);
     }

    void KilledUnit(Unit* victim)
    {
        //won't yell killing pet/other unit
        if (victim->GetEntry() == 21436)
            return;

        switch(rand()%2)
        {
        case 0: DoScriptText(SAY_KILL_1, me); break;
        case 1: DoScriptText(SAY_KILL_2, me); break;
        }
    }

    void DoSplit(uint32 val)
    {
        if (me->IsNonMeleeSpellCasted(false))
            me->InterruptNonMeleeSpells(false);

        DoScriptText(SAY_IMAGE, me);

        if (val == 66)
            DoCast(me, SPELL_66_ILLUSION);
        else
            DoCast(me, SPELL_33_ILLUSION);
    }

    void UpdateAI(const uint32 diff)
    {
        if (!Intro)
        {
            if (!pInstance)
                return;

            if (Intro_Timer <= diff)
            {
                switch(Intro_Phase)
                {
                    case 1:
                         DoScriptText(SAY_INTRO, me);
                        if (GameObject* Sphere = GameObject::GetGameObject(*me,pInstance->GetData64(DATA_SPHERE_SHIELD)))
                            Sphere->SetGoState(GO_STATE_ACTIVE);
                        ++Intro_Phase;
                        Intro_Timer = 25000;
                        break;
                    case 2:
                        DoScriptText(SAY_AGGRO, me);
                        if (Unit *mellic = Unit::GetUnit(*me,pInstance->GetData64(DATA_MELLICHAR)))
                        {
                            //should have a better way to do this. possibly spell exist.
                            mellic->setDeathState(JUST_DIED);
                            mellic->SetHealth(0);
                            pInstance->SetData(TYPE_SHIELD_OPEN,IN_PROGRESS);
                        }
                        ++Intro_Phase;
                        Intro_Timer = 3000;
                        break;
                    case 3:
                        me->RemoveFlag(UNIT_FIELD_FLAGS,UNIT_FLAG_OOC_NOT_ATTACKABLE);
                        Intro = true;
                        break;
                }
            } else Intro_Timer -=diff;
        }

        if (!UpdateVictim())
            return;

        if (!IsImage66 && ((me->GetHealth()*100) / me->GetMaxHealth() <= 66))
        {
            DoSplit(66);
            IsImage66 = true;
        }
        if (!IsImage33 && ((me->GetHealth()*100) / me->GetMaxHealth() <= 33))
        {
            DoSplit(33);
            IsImage33 = true;
        }

        if (MindRend_Timer <= diff)
        {
            if (Unit *pTarget = SelectUnit(SELECT_TARGET_RANDOM,1))
                DoCast(pTarget,HeroicMode ? H_SPELL_MIND_REND : SPELL_MIND_REND);
            else
                DoCast(me->getVictim(),HeroicMode ? H_SPELL_MIND_REND : SPELL_MIND_REND);

            MindRend_Timer = 8000;
        } else MindRend_Timer -=diff;

        if (Fear_Timer <= diff)
        {
            if (me->IsNonMeleeSpellCasted(false))
                return;

            switch(rand()%2)
            {
            case 0: DoScriptText(SAY_FEAR_1, me); break;
            case 1: DoScriptText(SAY_FEAR_2, me); break;
            }

            if (Unit *pTarget = SelectUnit(SELECT_TARGET_RANDOM,1))
                DoCast(pTarget,SPELL_FEAR);
            else
                DoCast(me->getVictim(),SPELL_FEAR);

            Fear_Timer = 25000;
        } else Fear_Timer -=diff;

        if (Domination_Timer <= diff)
        {
            if (me->IsNonMeleeSpellCasted(false))
                return;

            switch(rand()%2)
            {
            case 0: DoScriptText(SAY_MIND_1, me); break;
            case 1: DoScriptText(SAY_MIND_2, me); break;
            }

            if (Unit *pTarget = SelectUnit(SELECT_TARGET_RANDOM,1))
                DoCast(pTarget,HeroicMode ? H_SPELL_DOMINATION : SPELL_DOMINATION);
            else
                DoCast(me->getVictim(),HeroicMode ? H_SPELL_DOMINATION : SPELL_DOMINATION);

            Domination_Timer = 16000+rand()%16000;
        } else Domination_Timer -=diff;

        if (HeroicMode)
        {
            if (ManaBurn_Timer <= diff)
            {
                if (me->IsNonMeleeSpellCasted(false))
                    return;

                if (Unit *pTarget = SelectUnit(SELECT_TARGET_RANDOM,1))
                    DoCast(pTarget,H_SPELL_MANA_BURN);

                ManaBurn_Timer = 16000+rand()%16000;
            } else ManaBurn_Timer -=diff;
        }

        DoMeleeAttackIfReady();
    }
};

CreatureAI* GetAI_boss_harbinger_skyriss(Creature* pCreature)
{
    return new boss_harbinger_skyrissAI (pCreature);
}

#define SPELL_MIND_REND_IMAGE   36929
#define H_SPELL_MIND_REND_IMAGE 39021

struct boss_harbinger_skyriss_illusionAI : public ScriptedAI
{
    boss_harbinger_skyriss_illusionAI(Creature *c) : ScriptedAI(c)
    {
        pInstance = c->GetInstanceData();
        HeroicMode = me->GetMap()->IsHeroic();
    }

    ScriptedInstance *pInstance;
    bool HeroicMode;

    void Reset() { }

    void EnterCombat(Unit *who) { }
};

CreatureAI* GetAI_boss_harbinger_skyriss_illusion(Creature* pCreature)
{
    return new boss_harbinger_skyriss_illusionAI (pCreature);
}

void AddSC_boss_harbinger_skyriss()
{
    Script *newscript;

    newscript = new Script;
    newscript->Name = "boss_harbinger_skyriss";
    newscript->GetAI = &GetAI_boss_harbinger_skyriss;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "boss_harbinger_skyriss_illusion";
    newscript->GetAI = &GetAI_boss_harbinger_skyriss_illusion;
    newscript->RegisterSelf();
}

