#include "global.h"
#include "constants/abilities.h"
#include "constants/game_stat.h"
#include "constants/items.h"
#include "constants/region_map_sections.h"

#include "battle.h"
#include "event_data.h"
#include "data.h"
#include "item.h"
#include "malloc.h"
#include "money.h"
#include "pokedex.h"
#include "string_util.h"

#include "rogue.h"
#include "rogue_adventurepaths.h"
#include "rogue_controller.h"
#include "rogue_gifts.h"
#include "rogue_pokedex.h"
#include "rogue_quest.h"
#include "rogue_settings.h"
#include "rogue_popup.h"

// new quests

struct RogueQuestRewardOutput
{
    u32 moneyCount;
    u16 buildSuppliesCount;
    u16 failedRewardItem;
    u16 failedRewardCount;
};

static EWRAM_DATA struct RogueQuestRewardOutput* sRogueQuestRewardOutput = NULL;

static bool8 QuestCondition_Always(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_DifficultyGreaterThan(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_DifficultyLessThan(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_IsStandardRunActive(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_HasCompletedQuestAND(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_HasCompletedQuestOR(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_PartyContainsType(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_PartyOnlyContainsType(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_PartyContainsLegendary(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_PartyContainsInitialPartner(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_PartyContainsSpecies(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_CurrentlyInMap(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_AreOnlyTheseTrainersActive(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_IsPokedexRegion(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_IsPokedexVariant(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_CanUnlockFinalQuest(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_IsFinalQuestConditionMet(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_PokedexEntryCountGreaterThan(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_InAdventureEncounterType(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_TotalMoneySpentGreaterThan(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_PlayerMoneyGreaterThan(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_RandomanWasUsed(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_RandomanWasActive(u16 questId, struct RogueQuestTrigger const* trigger);
static bool8 QuestCondition_LastRandomanWasFullParty(u16 questId, struct RogueQuestTrigger const* trigger);

static bool8 IsQuestSurpressed(u16 questId);
static bool8 CanSurpressedQuestActivate(u16 questId);

bool8 PartyContainsBaseSpecies(struct Pokemon *party, u8 partyCount, u16 species);

#define COMPOUND_STRING(str) (const u8[]) _(str)

#include "data/rogue/quests.h"

#undef COMPOUND_STRING

// ensure we are serializing the exact correct amount
STATIC_ASSERT(QUEST_SAVE_COUNT == QUEST_ID_COUNT, saveQuestCountMissmatch);

//static u8* RogueQuest_GetExtraState(size_t size)
//{
//    // todo
//    AGB_ASSERT(FALSE);
//    return NULL;
//}

static struct RogueQuestEntry const* RogueQuest_GetEntry(u16 questId)
{
    AGB_ASSERT(questId < QUEST_ID_COUNT);
    if(questId < ARRAY_COUNT(sQuestEntries))
        return &sQuestEntries[questId];
    else
        return NULL;
}

static struct RogueQuestStateNEW* RogueQuest_GetState(u16 questId)
{
    AGB_ASSERT(questId < QUEST_ID_COUNT);
    if(questId < QUEST_ID_COUNT)
        return &gRogueSaveBlock->questStatesNEW[questId];

    return NULL;
}

u8 const* RogueQuest_GetTitle(u16 questId)
{
    struct RogueQuestEntry const* entry = RogueQuest_GetEntry(questId);
    AGB_ASSERT(questId < QUEST_ID_COUNT);
    return entry->title;
}

u8 const* RogueQuest_GetDesc(u16 questId)
{
    struct RogueQuestEntry const* entry = RogueQuest_GetEntry(questId);
    AGB_ASSERT(questId < QUEST_ID_COUNT);
    return entry->desc;
}

bool8 RogueQuest_GetConstFlag(u16 questId, u32 flag)
{
    struct RogueQuestEntry const* entry = RogueQuest_GetEntry(questId);
    AGB_ASSERT(questId < QUEST_ID_COUNT);
    return (entry->flags & flag) != 0;
}

u16 RogueQuest_GetOrderedQuest(u16 index)
{
    AGB_ASSERT(index < ARRAY_COUNT(sQuestDisplayOrder));
    return sQuestDisplayOrder[index];
}

bool8 RogueQuest_GetStateFlag(u16 questId, u32 flag)
{
    struct RogueQuestStateNEW* questState = RogueQuest_GetState(questId);
    return (questState->stateFlags & flag) != 0;
}

void RogueQuest_SetStateFlag(u16 questId, u32 flag, bool8 state)
{
    struct RogueQuestStateNEW* questState = RogueQuest_GetState(questId);

    if(state)
        questState->stateFlags |= flag;
    else
        questState->stateFlags &= ~flag;
}

struct RogueQuestRewardNEW const* RogueQuest_GetReward(u16 questId, u16 i)
{
    struct RogueQuestEntry const* entry = RogueQuest_GetEntry(questId);
    AGB_ASSERT(questId < QUEST_ID_COUNT);
    AGB_ASSERT(i < entry->rewardCount);
    return &entry->rewards[i];
}

u16 RogueQuest_GetRewardCount(u16 questId)
{
    struct RogueQuestEntry const* entry = RogueQuest_GetEntry(questId);
    AGB_ASSERT(questId < QUEST_ID_COUNT);
    return entry->rewardCount;
}

u8 RogueQuest_GetHighestCompleteDifficulty(u16 questId)
{
    if(RogueQuest_GetStateFlag(questId, QUEST_STATE_HAS_COMPLETE))
    {
        struct RogueQuestStateNEW* questState = RogueQuest_GetState(questId);
        return questState->highestCompleteDifficulty;
    }

    return DIFFICULTY_LEVEL_NONE;
}

static bool8 CanActivateQuest(u16 questId)
{
    if(!RogueQuest_IsQuestUnlocked(questId))
        return FALSE;

    // Cannot start quests we have rewards for
    if(RogueQuest_GetStateFlag(questId, QUEST_STATE_PENDING_REWARDS))
        return FALSE;

    // Masteries still work in the background, but challenges don't
    if(IsQuestSurpressed(questId) && !CanSurpressedQuestActivate(questId))
        return FALSE;

    // Challenges can be run again at a higher difficulty
    if(RogueQuest_GetConstFlag(questId, QUEST_CONST_IS_CHALLENGE))
    {
        if(Rogue_ShouldDisableChallengeQuests())
            return FALSE;

        if(RogueQuest_GetStateFlag(questId, QUEST_STATE_HAS_COMPLETE))
        {
            u8 difficultyLevel = Rogue_GetDifficultyRewardLevel();
            struct RogueQuestStateNEW* questState = RogueQuest_GetState(questId);

            if(questState->highestCompleteDifficulty != DIFFICULTY_LEVEL_NONE && difficultyLevel <= questState->highestCompleteDifficulty)
                return FALSE;
        }
    }
    else // QUEST_CONST_IS_MAIN_QUEST || QUEST_CONST_IS_MON_MASTERY
    {
        if(Rogue_ShouldDisableMainQuests())
            return FALSE;

        // Can't repeat main quests
        if(RogueQuest_GetStateFlag(questId, QUEST_STATE_HAS_COMPLETE))
            return FALSE;
    }

    return TRUE;
}

// Surpressed quests are quests which can still activate and be completed, but all UI mentioned are hidden
// i.e. you can technically complete masteries without having them unlocked
static bool8 IsQuestSurpressed(u16 questId)
{
    if(RogueQuest_GetConstFlag(questId, QUEST_CONST_IS_CHALLENGE))
    {
        if(!RogueQuest_HasUnlockedChallenges())
            return TRUE;
    }

    if(RogueQuest_GetConstFlag(questId, QUEST_CONST_IS_MON_MASTERY))
    {
        if(!RogueQuest_HasUnlockedMonMasteries())
            return TRUE;
    }

    return FALSE;
}

static bool8 CanSurpressedQuestActivate(u16 questId)
{
    if(RogueQuest_GetConstFlag(questId, QUEST_CONST_IS_MON_MASTERY))
        return TRUE;

    return FALSE;
}

bool8 RogueQuest_IsQuestUnlocked(u16 questId)
{
    return RogueQuest_GetStateFlag(questId, QUEST_STATE_UNLOCKED);
}

bool8 RogueQuest_IsQuestVisible(u16 questId)
{
    return RogueQuest_IsQuestUnlocked(questId) && !IsQuestSurpressed(questId);
}

bool8 RogueQuest_TryUnlockQuest(u16 questId)
{
    if(!RogueQuest_IsQuestUnlocked(questId))
    {
        RogueQuest_SetStateFlag(questId, QUEST_STATE_UNLOCKED, TRUE);
        RogueQuest_SetStateFlag(questId, QUEST_STATE_NEW_UNLOCK, TRUE);

        // Activate quest now if we can/should (Assuming we only ever call this from within the hub)
        if(RogueQuest_GetConstFlag(questId, QUEST_CONST_ACTIVE_IN_HUB))
        {
            RogueQuest_SetStateFlag(questId, QUEST_STATE_ACTIVE, TRUE);
        }

        return TRUE;
    }

    return FALSE;
}

bool8 RogueQuest_HasPendingNewQuests()
{
    u16 i;

    for(i = 0; i < QUEST_ID_COUNT; ++i)
    {
        if(RogueQuest_IsQuestUnlocked(i) && !IsQuestSurpressed(i) && RogueQuest_GetStateFlag(i, QUEST_STATE_NEW_UNLOCK))
            return TRUE;
    }

    return FALSE;
}

void RogueQuest_ClearNewUnlockQuests()
{
    u16 i;

    for(i = 0; i < QUEST_ID_COUNT; ++i)
    {
        if(RogueQuest_IsQuestUnlocked(i) && !IsQuestSurpressed(i))
            RogueQuest_SetStateFlag(i, QUEST_STATE_NEW_UNLOCK, FALSE);
    }
}

bool8 RogueQuest_HasCollectedRewards(u16 questId)
{
    if(RogueQuest_IsQuestUnlocked(questId) && !IsQuestSurpressed(questId))
    {
        if(RogueQuest_GetStateFlag(questId, QUEST_STATE_HAS_COMPLETE) && !RogueQuest_GetStateFlag(questId, QUEST_STATE_PENDING_REWARDS))
            return TRUE;
    }

    return FALSE;
}

bool8 RogueQuest_HasPendingRewards(u16 questId)
{
    if(RogueQuest_IsQuestUnlocked(questId) && !IsQuestSurpressed(questId))
    {
        if(RogueQuest_GetStateFlag(questId, QUEST_STATE_PENDING_REWARDS))
            return TRUE;
    }

    return FALSE;
}

bool8 RogueQuest_HasAnyPendingRewards()
{
    u16 i;

    for(i = 0; i < QUEST_ID_COUNT; ++i)
    {
        if(RogueQuest_HasPendingRewards(i))
            return TRUE;
    }

    return FALSE;
}

static bool8 GiveRewardInternal(struct RogueQuestRewardNEW const* rewardInfo)
{
    bool8 state = TRUE;

    switch (rewardInfo->type)
    {
    case QUEST_REWARD_POKEMON:
        {
            struct Pokemon* mon = &gEnemyParty[0];
            u32 temp = 0;
            bool8 isCustom = FALSE;

            if(rewardInfo->perType.pokemon.customMonId != CUSTOM_MON_NONE)
            {
                isCustom = TRUE;
                RogueGift_CreateMon(rewardInfo->perType.pokemon.customMonId, mon, STARTER_MON_LEVEL, USE_RANDOM_IVS);
                AGB_ASSERT(rewardInfo->perType.pokemon.species == GetMonData(mon, MON_DATA_SPECIES));
            }
            else
            {
                ZeroMonData(mon);
                CreateMon(mon, rewardInfo->perType.pokemon.species, STARTER_MON_LEVEL, USE_RANDOM_IVS, 0, 0, OT_ID_PLAYER_ID, 0);

                temp = METLOC_FATEFUL_ENCOUNTER;
                SetMonData(mon, MON_DATA_MET_LOCATION, &temp);
            }

            // Update nickname
            if(rewardInfo->perType.pokemon.nickname != NULL)
            {
                SetMonData(mon, MON_DATA_NICKNAME, rewardInfo->perType.pokemon.nickname);
            }

            // Set shiny state
            if(rewardInfo->perType.pokemon.isShiny)
            {
                temp = 1;
                SetMonData(mon, MON_DATA_IS_SHINY, &temp);
            }

            // Give mon
            if(isCustom)
                GiveTradedMonToPlayer(mon);
            else
                GiveMonToPlayer(mon);

            // Set pokedex flag
            GetSetPokedexSpeciesFlag(rewardInfo->perType.pokemon.species, rewardInfo->perType.pokemon.isShiny ? FLAG_SET_CAUGHT_SHINY : FLAG_SET_CAUGHT);

            Rogue_PushPopup_AddPokemon(rewardInfo->perType.pokemon.species, isCustom, rewardInfo->perType.pokemon.isShiny);
        }
        break;

    case QUEST_REWARD_ITEM:
        state = AddBagItem(rewardInfo->perType.item.item, rewardInfo->perType.item.count);

        if(state)
        {
            if(sRogueQuestRewardOutput && rewardInfo->perType.item.item == ITEM_BUILDING_SUPPLIES)
                sRogueQuestRewardOutput->buildSuppliesCount += rewardInfo->perType.item.count;
            else
                Rogue_PushPopup_AddItem(rewardInfo->perType.item.item, rewardInfo->perType.item.count);
        }
        else
        {
            if(sRogueQuestRewardOutput)
            {
                sRogueQuestRewardOutput->failedRewardItem = rewardInfo->perType.item.item;
                sRogueQuestRewardOutput->failedRewardCount = rewardInfo->perType.item.count;
            }
        }
        break;

    case QUEST_REWARD_SHOP_ITEM:
        Rogue_PushPopup_UnlockedShopItem(rewardInfo->perType.shopItem.item);
        break;

    case QUEST_REWARD_MONEY:
        AddMoney(&gSaveBlock1Ptr->money, rewardInfo->perType.money.amount);

        if(sRogueQuestRewardOutput)
            sRogueQuestRewardOutput->moneyCount += rewardInfo->perType.money.amount;
        else
            Rogue_PushPopup_AddMoney(rewardInfo->perType.money.amount);

        break;

    case QUEST_REWARD_QUEST_UNLOCK:
        RogueQuest_TryUnlockQuest(rewardInfo->perType.questUnlock.questId);
        break;
    
    default:
        AGB_ASSERT(FALSE);
        break;
    }

    return state;
}

static void RemoveRewardInternal(struct RogueQuestRewardNEW const* rewardInfo)
{
    switch (rewardInfo->type)
    {
    case QUEST_REWARD_ITEM:
        RemoveBagItem(rewardInfo->perType.item.item, rewardInfo->perType.item.count);

        if(sRogueQuestRewardOutput && rewardInfo->perType.item.item == ITEM_BUILDING_SUPPLIES)
            sRogueQuestRewardOutput->buildSuppliesCount -= rewardInfo->perType.item.count;
        break;

    case QUEST_REWARD_MONEY:
        RemoveMoney(&gSaveBlock1Ptr->money, rewardInfo->perType.money.amount);

        if(sRogueQuestRewardOutput)
            sRogueQuestRewardOutput->moneyCount -= rewardInfo->perType.money.amount;
        break;
    
    default:
        // Cannot refund this type
        AGB_ASSERT(FALSE);
        break;
    }
}

static bool8 IsHighPriorityReward(struct RogueQuestRewardNEW const* rewardInfo)
{
    // High priority rewards can fail and be refunded
    switch (rewardInfo->type)
    {
    case QUEST_REWARD_ITEM:
        return TRUE;
    }

    return FALSE;
}

bool8 RogueQuest_TryCollectRewards(u16 questId)
{
    u16 i;
    bool8 state = TRUE;
    struct RogueQuestRewardNEW const* rewardInfo;
    struct RogueQuestStateNEW* questState = RogueQuest_GetState(questId);
    u16 rewardCount = RogueQuest_GetRewardCount(questId);

    AGB_ASSERT(RogueQuest_HasPendingRewards(questId));

    // Give high pri rewards
    for(i = 0; i < rewardCount; ++i)
    {
        rewardInfo = RogueQuest_GetReward(questId, i);

        if(IsHighPriorityReward(rewardInfo))
        {
            state = GiveRewardInternal(rewardInfo);
            if(!state)
                break;
        }
    }

    if(!state)
    {
        // Failed to give items so refund all we had previously given
        rewardCount = i;
        for(i = 0; i < rewardCount; ++i)
        {
            rewardInfo = RogueQuest_GetReward(questId, i);

            if(IsHighPriorityReward(rewardInfo))
                RemoveRewardInternal(rewardInfo);
        }
    }
    else
    {
        // Give all remaining low pri rewards
        for(i = 0; i < rewardCount; ++i)
        {
            rewardInfo = RogueQuest_GetReward(questId, i);

            if(!IsHighPriorityReward(rewardInfo))
            {
                // We should never be able to fail to give a high pri reward
                state = GiveRewardInternal(RogueQuest_GetReward(questId, i));
                AGB_ASSERT(state);
            }
        }

        // Clear pending rewards
        RogueQuest_SetStateFlag(questId, QUEST_STATE_PENDING_REWARDS, FALSE);

        questState->highestCollectedRewardDifficulty = questState->highestCompleteDifficulty;
        return TRUE;
    }

    return FALSE;
}

bool8 RogueQuest_IsRewardSequenceActive()
{
    return sRogueQuestRewardOutput != NULL;
}

void RogueQuest_BeginRewardSequence()
{
    AGB_ASSERT(sRogueQuestRewardOutput == NULL);
    sRogueQuestRewardOutput = AllocZeroed(sizeof(struct RogueQuestRewardOutput));
}

void RogueQuest_EndRewardSequence()
{
    AGB_ASSERT(sRogueQuestRewardOutput != NULL);

    if(sRogueQuestRewardOutput->buildSuppliesCount)
        Rogue_PushPopup_AddItem(ITEM_BUILDING_SUPPLIES, sRogueQuestRewardOutput->buildSuppliesCount);

    if(sRogueQuestRewardOutput->moneyCount)
        Rogue_PushPopup_AddMoney(sRogueQuestRewardOutput->moneyCount);

    if(sRogueQuestRewardOutput->failedRewardItem != ITEM_NONE)
        Rogue_PushPopup_CannotTakeItem(sRogueQuestRewardOutput->failedRewardItem, sRogueQuestRewardOutput->failedRewardCount);

    Free(sRogueQuestRewardOutput);
    sRogueQuestRewardOutput = NULL;
}

void RogueQuest_ActivateQuestsFor(u32 flags)
{
    u16 i;

    for(i = 0; i < QUEST_ID_COUNT; ++i)
    {
        bool8 desiredState = RogueQuest_GetConstFlag(i, flags) && CanActivateQuest(i);

        if(RogueQuest_GetStateFlag(i, QUEST_STATE_ACTIVE) != desiredState)
        {
            RogueQuest_SetStateFlag(i, QUEST_STATE_ACTIVE, desiredState);
            // TODO - Trigger for state on activate?
        }
    }
}

bool8 RogueQuest_IsQuestActive(u16 questId)
{
    return RogueQuest_IsQuestUnlocked(questId) && RogueQuest_GetStateFlag(questId, QUEST_STATE_ACTIVE);
}

u16 RogueQuest_GetQuestCompletePercFor(u32 constFlag)
{
    u16 i;
    u16 complete = 0;
    u16 total = 0;

    for(i = 0; i < QUEST_ID_COUNT; ++i)
    {
        if(RogueQuest_GetConstFlag(i, constFlag))
        {
            ++total;

            if(RogueQuest_GetStateFlag(i, QUEST_STATE_HAS_COMPLETE))
            {
                ++complete;
            }
        }
    }

    return (complete * 100) / total;
}

void RogueQuest_GetQuestCountsFor(u32 constFlag, u16* activeCount, u16* inactiveCount)
{
    u16 i;
    u16 active = 0;
    u16 inactive = 0;

    for(i = 0; i < QUEST_ID_COUNT; ++i)
    {
        if(RogueQuest_GetConstFlag(i, constFlag))
        {
            if(RogueQuest_IsQuestActive(i))
            {
                active++;
            }
            else
            {
                inactive++;
            }
        }
    }

    *activeCount = active;
    *inactiveCount = inactive;
}

u16 RogueQuest_GetDisplayCompletePerc()
{
    u32 constFlags = QUEST_CONST_IS_MAIN_QUEST;
    u16 maxValue = 100;

    if(RogueQuest_HasUnlockedChallenges())
        constFlags |= QUEST_CONST_IS_CHALLENGE;
    else
        maxValue = 99;

    if(RogueQuest_HasUnlockedMonMasteries())
        constFlags |= QUEST_CONST_IS_MON_MASTERY;
    else
        maxValue = 99;

    return min(maxValue, RogueQuest_GetQuestCompletePercFor(constFlags));

    //u16 questCompletion = RogueQuest_GetQuestCompletePercFor(QUEST_CONST_IS_MAIN_QUEST);
//
    //if(questCompletion == 100)
    //{
    //    // Reach 100% total
    //    return questCompletion + RogueQuest_GetQuestCompletePercFor(QUEST_CONST_IS_CHALLENGE) + RogueQuest_GetQuestCompletePercFor(QUEST_CONST_IS_MON_MASTERY);
    //}
//
    //return questCompletion;
}

static void EnsureUnlockedDefaultQuests()
{
    u16 i;

    for(i = 0; i < QUEST_ID_COUNT; ++i)
    {
        if(RogueQuest_GetConstFlag(i, QUEST_CONST_UNLOCKED_BY_DEFAULT))
            RogueQuest_TryUnlockQuest(i);
    }
}

void RogueQuest_OnNewGame()
{
    memset(gRogueSaveBlock->questStatesNEW, 0, sizeof(gRogueSaveBlock->questStatesNEW));
    EnsureUnlockedDefaultQuests();
}

void RogueQuest_OnLoadGame()
{
    EnsureUnlockedDefaultQuests();
}

static void CompleteQuest(u16 questId)
{
    u8 currentDifficulty = Rogue_GetDifficultyRewardLevel();
    struct RogueQuestStateNEW* questState = RogueQuest_GetState(questId);

    questState->highestCompleteDifficulty = currentDifficulty;
    if(!RogueQuest_GetStateFlag(questId, QUEST_STATE_HAS_COMPLETE))
    {
        questState->highestCollectedRewardDifficulty = DIFFICULTY_LEVEL_NONE;
    }

    RogueQuest_SetStateFlag(questId, QUEST_STATE_ACTIVE, FALSE);
    RogueQuest_SetStateFlag(questId, QUEST_STATE_PENDING_REWARDS, TRUE);
    RogueQuest_SetStateFlag(questId, QUEST_STATE_HAS_COMPLETE, TRUE);

    if(!IsQuestSurpressed(questId))
        Rogue_PushPopup_QuestComplete(questId);
}

void Debug_RogueQuest_CompleteQuest(u16 questId)
{
#ifdef ROGUE_DEBUG
    CompleteQuest(questId);
#endif
}

static void FailQuest(u16 questId)
{
    RogueQuest_SetStateFlag(questId, QUEST_STATE_ACTIVE, FALSE);

    if(RogueQuest_GetStateFlag(questId, QUEST_STATE_PINNED))
        Rogue_PushPopup_QuestFail(questId);
}

static void ExecuteQuestTriggers(u16 questId, u16 triggerFlag)
{
    u16 i;
    struct RogueQuestTrigger const* trigger;

    for(i = 0; i < sQuestEntries[questId].triggerCount; ++i)
    {
        trigger = &sQuestEntries[questId].triggers[i];

        AGB_ASSERT(trigger->callback != NULL);

        if(((trigger->flags & triggerFlag) != 0) && trigger->callback != NULL)
        {
            bool8 condition = trigger->callback(questId, trigger);
            u8 status = condition != FALSE ? trigger->passState : trigger->failState;

            switch (status)
            {
            case QUEST_STATUS_PENDING:
                // Do nothing
                break;

            case QUEST_STATUS_SUCCESS:
                CompleteQuest(questId);
                return;
                break;

            case QUEST_STATUS_FAIL:
                FailQuest(questId);
                return;
                break;

            case QUEST_STATUS_BREAK:
                // Don't execute any more callbacks for this quest here
                return;
                break;
            
            default:
                AGB_ASSERT(FALSE);
                break;
            }
        }
    }
}

void RogueQuest_OnTrigger(u16 triggerFlag)
{
    u16 i;

    // Execute quest callback for any active quests which are listening for this trigger
    for(i = 0; i < QUEST_ID_COUNT; ++i)
    {
        if((sQuestEntries[i].triggerFlags & triggerFlag) != 0)
        {
            if(RogueQuest_GetStateFlag(i, QUEST_STATE_ACTIVE))
                ExecuteQuestTriggers(i, triggerFlag);
        }
    }
}

bool8 RogueQuest_HasUnlockedChallenges()
{
    return FlagGet(FLAG_SYS_CHALLENGES_UNLOCKED);
}

bool8 RogueQuest_HasUnlockedMonMasteries()
{
    return FlagGet(FLAG_SYS_MASTERIES_UNLOCKED);
}

// QuestCondition
//

#define ASSERT_PARAM_COUNT(count) AGB_ASSERT(trigger->paramCount == count)

static bool8 QuestCondition_Always(u16 questId, struct RogueQuestTrigger const* trigger)
{
    return TRUE;
}

static bool8 QuestCondition_DifficultyGreaterThan(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u16 threshold = trigger->params[0];
    ASSERT_PARAM_COUNT(1);
    return Rogue_GetCurrentDifficulty() > threshold;
}

static bool8 QuestCondition_DifficultyLessThan(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u16 threshold = trigger->params[0];
    ASSERT_PARAM_COUNT(1);
    return Rogue_GetCurrentDifficulty() < threshold;
}

static bool8 QuestCondition_IsStandardRunActive(u16 questId, struct RogueQuestTrigger const* trigger)
{
    // TODO
    return TRUE;
}

static bool8 UNUSED QuestCondition_HasCompletedQuestAND(u16 triggerQuestId, struct RogueQuestTrigger const* trigger)
{
    u16 i, questId;

    for(i = 0; i < trigger->paramCount; ++i)
    {
        questId = trigger->params[i];
        if(!RogueQuest_GetStateFlag(questId, QUEST_STATE_HAS_COMPLETE))
            return FALSE;
    }

    return TRUE;
}

static bool8 QuestCondition_HasCompletedQuestOR(u16 triggerQuestId, struct RogueQuestTrigger const* trigger)
{
    u16 i, questId;

    for(i = 0; i < trigger->paramCount; ++i)
    {
        questId = trigger->params[i];
        if(RogueQuest_GetStateFlag(questId, QUEST_STATE_HAS_COMPLETE))
            return TRUE;
    }

    return FALSE;
}

static bool8 UNUSED QuestCondition_PartyContainsType(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u8 i;
    u16 species, targetType;

    ASSERT_PARAM_COUNT(1);
    targetType = trigger->params[0];

    for(i = 0; i < gPlayerPartyCount; ++i)
    {
        species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES);

        if(RoguePokedex_GetSpeciesType(species, 0) == targetType || RoguePokedex_GetSpeciesType(species, 1) == targetType)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static bool8 QuestCondition_PartyOnlyContainsType(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u8 i;
    u16 species, targetType;

    ASSERT_PARAM_COUNT(1);
    targetType = trigger->params[0];

    for(i = 0; i < gPlayerPartyCount; ++i)
    {
        species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES);

        if(RoguePokedex_GetSpeciesType(species, 0) != targetType && RoguePokedex_GetSpeciesType(species, 1) != targetType)
        {
            return FALSE;
        }
    }

    return TRUE;
}

static bool8 QuestCondition_PartyContainsLegendary(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u8 i;
    u16 species;

    for(i = 0; i < gPlayerPartyCount; ++i)
    {
        species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES);

        if(RoguePokedex_IsSpeciesLegendary(species))
            return TRUE;
    }

    return FALSE;
}

static bool8 QuestCondition_PartyContainsInitialPartner(u16 questId, struct RogueQuestTrigger const* trigger)
{
    return Rogue_IsPartnerMonInTeam();
}

static bool8 QuestCondition_PartyContainsSpecies(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u16 i;
    u16 species;

    for(i = 0; i < trigger->paramCount; ++i)
    {
        species = trigger->params[i];

        if(PartyContainsBaseSpecies(gPlayerParty, gPlayerPartyCount, species))
            return TRUE;
    }

    return FALSE;
}


static bool8 CheckSingleTrainerConfigValid(u32 toggleToCheck, u32 currentToggle)
{
    if(toggleToCheck == currentToggle)
        return Rogue_GetConfigToggle(currentToggle) == TRUE;
    else
        return Rogue_GetConfigToggle(currentToggle) == FALSE;
}

bool8 CheckOnlyTheseTrainersEnabled(u32 toggleToCheck)
{
    if(!CheckSingleTrainerConfigValid(toggleToCheck, CONFIG_TOGGLE_TRAINER_ROGUE))
        return FALSE;

    if(!CheckSingleTrainerConfigValid(toggleToCheck, CONFIG_TOGGLE_TRAINER_KANTO))
        return FALSE;

    if(!CheckSingleTrainerConfigValid(toggleToCheck, CONFIG_TOGGLE_TRAINER_JOHTO))
        return FALSE;

    if(!CheckSingleTrainerConfigValid(toggleToCheck, CONFIG_TOGGLE_TRAINER_HOENN))
        return FALSE;

#ifdef ROGUE_EXPANSION
    if(!CheckSingleTrainerConfigValid(toggleToCheck, CONFIG_TOGGLE_TRAINER_SINNOH))
        return FALSE;

    if(!CheckSingleTrainerConfigValid(toggleToCheck, CONFIG_TOGGLE_TRAINER_UNOVA))
        return FALSE;

    if(!CheckSingleTrainerConfigValid(toggleToCheck, CONFIG_TOGGLE_TRAINER_KALOS))
        return FALSE;

    if(!CheckSingleTrainerConfigValid(toggleToCheck, CONFIG_TOGGLE_TRAINER_ALOLA))
        return FALSE;

    if(!CheckSingleTrainerConfigValid(toggleToCheck, CONFIG_TOGGLE_TRAINER_GALAR))
        return FALSE;

    if(!CheckSingleTrainerConfigValid(toggleToCheck, CONFIG_TOGGLE_TRAINER_PALDEA))
        return FALSE;
#endif
    return TRUE;
}

static bool8 QuestCondition_AreOnlyTheseTrainersActive(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u16 trainerConfigToggle = trigger->params[0];
    ASSERT_PARAM_COUNT(1);
    return CheckOnlyTheseTrainersEnabled(trainerConfigToggle);
}

static bool8 QuestCondition_IsPokedexRegion(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u16 region = trigger->params[0];
    ASSERT_PARAM_COUNT(1);
    return RoguePokedex_GetDexRegion() == region;
}

static bool8 UNUSED QuestCondition_IsPokedexVariant(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u16 variant = trigger->params[0];
    ASSERT_PARAM_COUNT(1);
    return RoguePokedex_GetDexVariant() == variant;
}

static bool8 QuestCondition_CurrentlyInMap(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u16 mapId = trigger->params[0];
    u16 mapGroup = (mapId >> 8); // equiv to MAP_GROUP
    u16 mapNum = (mapId & 0xFF); // equiv to MAP_NUM

    ASSERT_PARAM_COUNT(1);
    return gSaveBlock1Ptr->location.mapNum == mapNum || gSaveBlock1Ptr->location.mapGroup == mapGroup;
}

static bool8 QuestCondition_CanUnlockFinalQuest(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u16 i;

    for(i = 0; i < QUEST_ID_COUNT; ++i)
    {
        // Check all other main quests except these 2 have been completed
        if(i == QUEST_ID_ONE_LAST_QUEST || i == QUEST_ID_THE_FINAL_RUN)
            continue;

        if(RogueQuest_GetConstFlag(i, QUEST_CONST_IS_MAIN_QUEST))
        {
            if(!(RogueQuest_IsQuestUnlocked(i) && RogueQuest_HasCollectedRewards(i)))
                return FALSE;
        }
    }

    return TRUE;
}

static bool8 QuestCondition_IsFinalQuestConditionMet(u16 questId, struct RogueQuestTrigger const* trigger)
{
    return Rogue_UseFinalQuestEffects();
}

static bool8 QuestCondition_PokedexEntryCountGreaterThan(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u16 count = trigger->params[0];
    ASSERT_PARAM_COUNT(1);
    return RoguePokedex_CountNationalCaughtMons(FLAG_GET_CAUGHT) > count;
}

static bool8 QuestCondition_InAdventureEncounterType(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u16 i;
    u16 encounterType;

    for(i = 0; i < trigger->paramCount; ++i)
    {
        encounterType = trigger->params[i];

        if(gRogueAdvPath.currentRoomType == encounterType)
            return TRUE;
    }

    return FALSE;
}

static bool8 QuestCondition_TotalMoneySpentGreaterThan(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u16 count = trigger->params[0];
    ASSERT_PARAM_COUNT(1);
    return Rogue_GetTotalSpentOnActiveMap() > count;
}

static bool8 QuestCondition_PlayerMoneyGreaterThan(u16 questId, struct RogueQuestTrigger const* trigger)
{
    u16 count = trigger->params[0];
    ASSERT_PARAM_COUNT(1);
    return GetMoney(&gSaveBlock1Ptr->money) > count;
}

static bool8 QuestCondition_RandomanWasUsed(u16 questId, struct RogueQuestTrigger const* trigger)
{
    ASSERT_PARAM_COUNT(0);
    return FlagGet(FLAG_ROGUE_RANDOM_TRADE_WAS_ACTIVE) && FlagGet(FLAG_ROGUE_RANDOM_TRADE_DISABLED);
}

static bool8 QuestCondition_RandomanWasActive(u16 questId, struct RogueQuestTrigger const* trigger)
{
    ASSERT_PARAM_COUNT(0);
    return !!FlagGet(FLAG_ROGUE_RANDOM_TRADE_WAS_ACTIVE);
}

static bool8 QuestCondition_LastRandomanWasFullParty(u16 questId, struct RogueQuestTrigger const* trigger)
{
    ASSERT_PARAM_COUNT(0);
    return !!FlagGet(FLAG_ROGUE_RANDOM_TRADE_WAS_FULL_PARTY);
}

// old
extern const u8 gText_QuestRewardGive[];
extern const u8 gText_QuestRewardGiveMoney[];
extern const u8 gText_QuestRewardGiveMon[];
extern const u8 gText_QuestRewardGiveShinyMon[];
extern const u8 gText_QuestLogStatusIncomplete[];

static EWRAM_DATA u8 sRewardQuest = 0;
static EWRAM_DATA u8 sRewardParam = 0;
static EWRAM_DATA u8 sPreviousRouteType = 0;

typedef void (*QuestCallbackOLD)(u16 questId, struct OLDRogueQuestState* state);

static const u16 TypeToMonoQuest[NUMBER_OF_MON_TYPES] =
{
    [TYPE_NORMAL] = QUEST_NORMAL_Champion,
    [TYPE_FIGHTING] = QUEST_FIGHTING_Champion,
    [TYPE_FLYING] = QUEST_FLYING_Champion,
    [TYPE_POISON] = QUEST_POISON_Champion,
    [TYPE_GROUND] = QUEST_GROUND_Champion,
    [TYPE_ROCK] = QUEST_ROCK_Champion,
    [TYPE_BUG] = QUEST_BUG_Champion,
    [TYPE_GHOST] = QUEST_GHOST_Champion,
    [TYPE_STEEL] = QUEST_STEEL_Champion,
    [TYPE_MYSTERY] = QUEST_NONE,
    [TYPE_FIRE] = QUEST_FIRE_Champion,
    [TYPE_WATER] = QUEST_WATER_Champion,
    [TYPE_GRASS] = QUEST_GRASS_Champion,
    [TYPE_ELECTRIC] = QUEST_ELECTRIC_Champion,
    [TYPE_PSYCHIC] = QUEST_PSYCHIC_Champion,
    [TYPE_ICE] = QUEST_ICE_Champion,
    [TYPE_DRAGON] = QUEST_DRAGON_Champion,
    [TYPE_DARK] = QUEST_DARK_Champion,
#ifdef ROGUE_EXPANSION
    [TYPE_FAIRY] = QUEST_FAIRY_Champion,
#endif
};

static void UpdateMonoQuests(void);
static void ForEachUnlockedQuest(QuestCallbackOLD callback);
static void ActivateAdventureQuests(u16 questId, struct OLDRogueQuestState* state);
static void ActivateHubQuests(u16 questId, struct OLDRogueQuestState* state);

static void UnlockDefaultQuests()
{
    u16 i;

    for(i = QUEST_FirstAdventure; i <= QUEST_MeetPokabbie; ++i)
    {
        TryUnlockQuest(i);
    }

    TryUnlockQuest(QUEST_Collector1);
    TryUnlockQuest(QUEST_ShoppingSpree);
    TryUnlockQuest(QUEST_Bike1);
    TryUnlockQuest(QUEST_NoFainting1);
    TryUnlockQuest(QUEST_MrRandoman);
    TryUnlockQuest(QUEST_DevilDeal);
    TryUnlockQuest(QUEST_BerryCollector);

    // Make sure following quests are unlocked
    {
        u16 i;
        for(i = QUEST_NONE + 1; i < QUEST_CAPACITY; ++i)
        {
            if(IsQuestCollected(i))
                UnlockFollowingQuests(i);
        }
    }
}

void ResetQuestStateAfter(u16 loadedQuestCapacity)
{
    u16 i;

    if(loadedQuestCapacity < QUEST_CAPACITY)
    {
        if(loadedQuestCapacity == 0)
        {
            // Clear these flags here, as they are quest related so we want to make sure they're setup correctly
            FlagClear(FLAG_ROGUE_MET_POKABBIE);
            FlagClear(FLAG_ROGUE_UNCOVERRED_POKABBIE);
        }

        // Reset the state for any new quests
        for(i = loadedQuestCapacity; i < QUEST_CAPACITY; ++i)
        {
            memset(&gRogueSaveBlock->questStates[i], 0, sizeof(struct OLDRogueQuestState));
        }

        // Always make sure default quests are unlocked
        UnlockDefaultQuests();

        ForEachUnlockedQuest(ActivateHubQuests);
    }
}

bool8 AnyNewQuests(void)
{
    u16 i;
    struct OLDRogueQuestState* state;

    for(i = 0; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueSaveBlock->questStates[i];
        if(state->isUnlocked && state->hasNewMarker)
        {
            return TRUE;
        }
    }

    return FALSE;
}

bool8 AnyQuestRewardsPending(void)
{
    u16 i;
    struct OLDRogueQuestState* state;

    for(i = 0; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueSaveBlock->questStates[i];
        if(state->isUnlocked && state->hasPendingRewards)
        {
            return TRUE;
        }
    }

    return FALSE;
}

bool8 AnyNewQuestsPending(void)
{
    u16 i;
    struct OLDRogueQuestState* state;

    for(i = 0; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueSaveBlock->questStates[i];
        if(state->isUnlocked && state->hasPendingRewards && DoesQuestHaveUnlocks(i))
        {
            return TRUE;
        }
    }

    return FALSE;
}

u16 GetCompletedQuestCount(void)
{
    u16 i;
    struct OLDRogueQuestState* state;
    u16 count = 0;

    for(i = 0; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueSaveBlock->questStates[i];
        if(state->isUnlocked && state->isCompleted)
            ++count;
    }

    return count;
}

u16 GetUnlockedQuestCount(void)
{
    u16 i;
    struct OLDRogueQuestState* state;
    u16 count = 0;

    for(i = 0; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueSaveBlock->questStates[i];
        if(state->isUnlocked)
            ++count;
    }

    return count;
}

u8 GetCompletedQuestPerc(void)
{
    return (GetCompletedQuestCount() * 100) / (QUEST_CAPACITY - 1);
}

bool8 GetQuestState(u16 questId, struct OLDRogueQuestState* outState)
{
    if(questId < QUEST_CAPACITY)
    {
        memcpy(outState, &gRogueSaveBlock->questStates[questId], sizeof(struct OLDRogueQuestState));
        return outState->isUnlocked;
    }

    return FALSE;
}

void SetQuestState(u16 questId, struct OLDRogueQuestState* state)
{
    if(questId < QUEST_CAPACITY)
    {
        memcpy(&gRogueSaveBlock->questStates[questId], state, sizeof(struct OLDRogueQuestState));
    }
}

bool8 IsQuestRepeatable(u16 questId)
{
    return (gRogueQuests[questId].flags & QUEST_FLAGS_REPEATABLE) != 0;
}

bool8 IsQuestCollected(u16 questId)
{
    struct OLDRogueQuestState state;
    if (GetQuestState(questId, &state))
    {
        return state.isCompleted && !state.hasPendingRewards;
    }

    return FALSE;
}

bool8 IsQuestGloballyTracked(u16 questId)
{
    return (gRogueQuests[questId].flags & QUEST_FLAGS_GLOBALALLY_TRACKED) != 0;
}

bool8 IsQuestActive(u16 questId)
{
    struct OLDRogueQuestState state;
    if (GetQuestState(questId, &state))
    {
        return state.isValid;
    }

    return FALSE;
}

bool8 DoesQuestHaveUnlocks(u16 questId)
{
    return gRogueQuests[questId].unlockedQuests[0] != QUEST_NONE;
}


static struct RogueQuestReward const* GetCurrentRewardTarget()
{
    return &gRogueQuests[sRewardQuest].rewards[sRewardParam];
}

static bool8 QueueTargetRewardQuest()
{
    u16 i;
    struct OLDRogueQuestState* state;
    for(i = 0; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueSaveBlock->questStates[i];

        if(state->hasPendingRewards)
        {
            if(sRewardQuest != i)
            {
                sRewardQuest = i;
                sRewardParam = 0;
                UnlockFollowingQuests(sRewardQuest);
                // TODO - Check to see if we have enough space for all of these items
            }
            else
            {
                ++sRewardParam;
            }
            return TRUE;
        }
    }

    sRewardQuest = QUEST_NONE;
    sRewardParam = 0;
    return FALSE;
}

static bool8 QueueNextReward()
{
    while(QueueTargetRewardQuest())
    {
        struct RogueQuestReward const* reward = GetCurrentRewardTarget();

        if(sRewardParam >= QUEST_MAX_REWARD_COUNT || reward->type == QUEST_REWARD_NONE)
        {
            // We've cleared out this quest's rewards
            gRogueSaveBlock->questStates[sRewardQuest].hasPendingRewards = FALSE;

            sRewardQuest = QUEST_NONE;
            sRewardParam = 0;
        }
        else
        {
            return TRUE;
        }
    }

    return FALSE;
}

static bool8 GiveAndGetNextAnnouncedReward()
{
    while(QueueNextReward())
    {
        struct RogueQuestReward const* reward = GetCurrentRewardTarget();
        bool8 shouldAnnounce = reward->giveText != NULL;

        // Actually give the reward here
        switch(reward->type)
        {
            case QUEST_REWARD_SET_FLAG:
                FlagSet(reward->params[0]);
                break;

            case QUEST_REWARD_CLEAR_FLAG:
                FlagClear(reward->params[0]);
                break;

            case QUEST_REWARD_GIVE_ITEM:
                if(!AddBagItem(reward->params[0], reward->params[1]))
                {
                    AddPCItem(reward->params[0], reward->params[1]);
                }
                shouldAnnounce = TRUE;
                break;

            case QUEST_REWARD_GIVE_MONEY:
                AddMoney(&gSaveBlock1Ptr->money, reward->params[0]);
                shouldAnnounce = TRUE;
                break;

            case QUEST_REWARD_GIVE_POKEMON:
                // Force shiny
                if(reward->params[2] == TRUE)
                    CreateMonForcedShiny(&gEnemyParty[0], reward->params[0], reward->params[1], USE_RANDOM_IVS, OT_ID_PLAYER_ID, 0);
                else 
                    CreateMon(&gEnemyParty[0], reward->params[0], reward->params[1], USE_RANDOM_IVS, 0, 0, OT_ID_PLAYER_ID, 0);

                GiveMonToPlayer(&gEnemyParty[0]);
                shouldAnnounce = TRUE;
                break;

            //case QUEST_REWARD_CUSTOM_TEXT:
            //    break;
        }
        
        if(shouldAnnounce)
        {
            return TRUE;
        }
    }

    return FALSE;
}


bool8 GiveNextRewardAndFormat(u8* str, u8* type)
{
    if(GiveAndGetNextAnnouncedReward())
    {
        struct RogueQuestReward const* reward = GetCurrentRewardTarget();
        *type = reward->type;

        if(reward->giveText)
        {
            StringCopy(str, reward->giveText);
        }
        else
        {
            switch(reward->type)
            {
                case QUEST_REWARD_GIVE_ITEM:
                    CopyItemNameHandlePlural(reward->params[0], gStringVar1, reward->params[1]);
                    StringExpandPlaceholders(str, gText_QuestRewardGive);
                    break;

                case QUEST_REWARD_GIVE_MONEY:
                    ConvertUIntToDecimalStringN(gStringVar1, reward->params[0], STR_CONV_MODE_LEFT_ALIGN, 7);
                    StringExpandPlaceholders(str, gText_QuestRewardGiveMoney);
                    break;

                case QUEST_REWARD_GIVE_POKEMON:
                    StringCopy(gStringVar1, RoguePokedex_GetSpeciesName(reward->params[0]));

                    if(reward->params[2] == TRUE)
                        StringExpandPlaceholders(str, gText_QuestRewardGiveShinyMon);
                    else
                        StringExpandPlaceholders(str, gText_QuestRewardGiveMon);
                    break;
                
                default:
                    // Just return an obviously broken message
                    StringCopy(str, gText_QuestLogStatusIncomplete);
                    break;
            }
        }
        return TRUE;
    }

    return FALSE;
}

bool8 TryUnlockQuest(u16 questId)
{
    struct OLDRogueQuestState* state = &gRogueSaveBlock->questStates[questId];

    if(!state->isUnlocked)
    {
        state->isUnlocked = TRUE;
        state->isValid = FALSE;
        state->isCompleted = FALSE;
        state->hasNewMarker = TRUE;

        if(questId == QUEST_Collector1)
        {
            // Instant complete
            u16 caughtCount = GetNationalPokedexCount(FLAG_GET_CAUGHT);
            if(caughtCount >= 15)
            {
                state->isValid = TRUE;
                TryMarkQuestAsComplete(QUEST_Collector1);
            }
        }

        if(questId == QUEST_Collector2)
        {
            // Instant complete
            u16 caughtCount = GetNationalPokedexCount(FLAG_GET_CAUGHT);
            if(caughtCount >= 100)
            {
                state->isValid = TRUE;
                TryMarkQuestAsComplete(QUEST_Collector2);
            }
        }

        return TRUE;
    }

    return FALSE;
}

bool8 TryMarkQuestAsComplete(u16 questId)
{
    struct OLDRogueQuestState* state = &gRogueSaveBlock->questStates[questId];

    if(state->isValid)
    {
        state->isValid = FALSE;

        if(!state->isCompleted)
        {
            // First time finishing
            state->isCompleted = TRUE;
            state->hasPendingRewards = TRUE;

            FlagSet(FLAG_ROGUE_QUESTS_ASK_FOR_RETIRE);
        }
        else if(IsQuestRepeatable(questId))
        {
            // Has already completed this once before so has rewards pending
            state->hasPendingRewards = TRUE;
        }

        //Rogue_PushPopup_QuestComplete(questId);
        return TRUE;
    }

    return FALSE;
}

bool8 TryDeactivateQuest(u16 questId)
{
    struct OLDRogueQuestState* state = &gRogueSaveBlock->questStates[questId];

    if(state->isValid)
    {
        state->isValid = FALSE;

        //if(state->isPinned)
        //    Rogue_PushPopup_QuestFail(questId);

        return TRUE;
    }

    return FALSE;
}

void UnlockFollowingQuests(u16 questId)
{
    u8 i;

    for(i = 0; i < QUEST_MAX_FOLLOWING_QUESTS; ++i)
    {
        if(gRogueQuests[questId].unlockedQuests[i] == QUEST_NONE)
            break;

        TryUnlockQuest(gRogueQuests[questId].unlockedQuests[i]);
    }
}

static void ForEachUnlockedQuest(QuestCallbackOLD callback)
{
    u16 i;
    struct OLDRogueQuestState* state;

    for(i = QUEST_NONE + 1; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueSaveBlock->questStates[i];
        if(state->isUnlocked)
        {
            callback(i, state);
        }
    }
}

static void UNUSED ForEachActiveQuest(QuestCallbackOLD callback)
{
    u16 i;
    struct OLDRogueQuestState* state;

    for(i = QUEST_NONE + 1; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueSaveBlock->questStates[i];
        if(state->isValid && !state->isCompleted)
        {
            callback(i, state);
        }
    }
}

static void TryActivateQuestInternal(u16 questId, struct OLDRogueQuestState* state)
{
    if(state->isUnlocked)
    {
        if(IsQuestRepeatable(questId))
        {
            // Don't reactivate quests if we have rewards pending
            if(!state->hasPendingRewards)
                state->isValid = TRUE;
        }
        else if(!state->isCompleted)
        {
            state->isValid = TRUE;
        }
    }
}


static void ActivateAdventureQuests(u16 questId, struct OLDRogueQuestState* state)
{
    bool8 activeInHub = (gRogueQuests[questId].flags & QUEST_FLAGS_ACTIVE_IN_HUB) != 0;

    if(!activeInHub || IsQuestGloballyTracked(questId))
    {
        TryActivateQuestInternal(questId, state);
    }
    else
    {
        state->isValid = FALSE;
    }
}

static void ActivateGauntletAdventureQuests(u16 questId, struct OLDRogueQuestState* state)
{
    // The quests we allow for gauntlet mode
    switch(questId)
    {
        case QUEST_FirstAdventure:
        case QUEST_GymChallenge:
        case QUEST_GymMaster:
        case QUEST_EliteMaster:
        case QUEST_Champion:
        case QUEST_GauntletMode:
            ActivateAdventureQuests(questId, state);
            break;

        default:
            state->isValid = FALSE;
            break;
    }
}

static void ActivateHubQuests(u16 questId, struct OLDRogueQuestState* state)
{
    bool8 activeInHub = (gRogueQuests[questId].flags & QUEST_FLAGS_ACTIVE_IN_HUB) != 0;

    if(activeInHub || IsQuestGloballyTracked(questId))
    {
        TryActivateQuestInternal(questId, state);
    }
    else
    {
        state->isValid = FALSE;
    }
}

// External callbacks
//

static void UpdateChaosChampion(bool8 enteringPotentialEncounter)
{
    struct OLDRogueQuestState state;

    if(IsQuestActive(QUEST_ChaosChampion) && GetQuestState(QUEST_ChaosChampion, &state))
    {
        bool8 isRandomanDisabled = FlagGet(FLAG_ROGUE_RANDOM_TRADE_DISABLED);

        if(enteringPotentialEncounter)
        {
            state.data.half = isRandomanDisabled ? 0 : 1;
            SetQuestState(QUEST_ChaosChampion, &state);
        }
        else if(state.data.half)
        {
            if(!isRandomanDisabled)
            {
                // Randoman was still active i.e. we didn't use him
                TryDeactivateQuest(QUEST_ChaosChampion);
            }
            else
            {
                state.data.half = 0;
                SetQuestState(QUEST_ChaosChampion, &state);
            }
        }
    }

    // Check for random starter
    {
        // Only care about the very first check
        if(VarGet(VAR_ROGUE_CURRENT_ROOM_IDX) == 0)
        {
            bool8 isRandomanDisabled = FlagGet(FLAG_ROGUE_RANDOM_TRADE_DISABLED);

            if(!enteringPotentialEncounter && !isRandomanDisabled)
            {
                TryDeactivateQuest(QUEST_Nuzlocke);
                TryDeactivateQuest(QUEST_IronMono2);
                TryDeactivateQuest(QUEST_Hardcore4);
            }
        }
    }
}

static void CheckCurseQuests(void)
{
    u16 i;

    // Check for all curse items
    for(i = FIRST_ITEM_CURSE; i != LAST_ITEM_CURSE + 1; ++i)
    {
        if(!CheckBagHasItem(i, 1))
        {
            TryDeactivateQuest(QUEST_CursedBody);
            break;
        }
    }

    if(!CheckBagHasItem(ITEM_PARTY_CURSE, 5))
    {
        TryDeactivateQuest(QUEST_IronMono1);
        TryDeactivateQuest(QUEST_IronMono2);
    }

    if(!CheckBagHasItem(ITEM_WILD_ENCOUNTER_CURSE, 10))
    {
        TryDeactivateQuest(QUEST_Nuzlocke);
    }

    if(!CheckBagHasItem(ITEM_SHOP_PRICE_CURSE, 99))
    {
        TryDeactivateQuest(QUEST_IronMono2);
    }

    if(!CheckBagHasItem(ITEM_BATTLE_ITEM_CURSE, 1))
    {
        TryDeactivateQuest(QUEST_Nuzlocke);
        TryDeactivateQuest(QUEST_IronMono2);
        TryDeactivateQuest(QUEST_Hardcore);
        TryDeactivateQuest(QUEST_Hardcore2);
        TryDeactivateQuest(QUEST_Hardcore3);
        TryDeactivateQuest(QUEST_Hardcore4);
    }

    if(!CheckBagHasItem(ITEM_SPECIES_CLAUSE_CURSE, 1))
    {
        TryDeactivateQuest(QUEST_Nuzlocke);
        TryDeactivateQuest(QUEST_Hardcore3);
        TryDeactivateQuest(QUEST_Hardcore4);
    }
}

void QuestNotify_BeginAdventure(void)
{
    FlagClear(FLAG_ROGUE_QUESTS_ASK_FOR_RETIRE);
    FlagClear(FLAG_ROGUE_QUESTS_NEVER_ASK_FOR_RETIRE);

    sPreviousRouteType = 0;

    // Cannot activate quests on Gauntlet mode
    if(FlagGet(FLAG_ROGUE_GAUNTLET_MODE))
    {
        ForEachUnlockedQuest(ActivateGauntletAdventureQuests);
    }
    else
    {
        ForEachUnlockedQuest(ActivateAdventureQuests);
    }

    // Handle skip difficulty
    if(Rogue_GetCurrentDifficulty() > 0)
    {
        u16 i;

        TryDeactivateQuest(QUEST_GymChallenge);
        TryDeactivateQuest(QUEST_GymMaster);
        TryDeactivateQuest(QUEST_NoFainting2);
        TryDeactivateQuest(QUEST_NoFainting3);
        TryDeactivateQuest(QUEST_Hardcore);
        TryDeactivateQuest(QUEST_Hardcore2);
        TryDeactivateQuest(QUEST_Hardcore3);
        TryDeactivateQuest(QUEST_Hardcore4);
        TryDeactivateQuest(QUEST_CursedBody);
        TryDeactivateQuest(QUEST_Nuzlocke);
        TryDeactivateQuest(QUEST_IronMono1);
        TryDeactivateQuest(QUEST_IronMono2);
        TryDeactivateQuest(QUEST_LegendOnly);
        TryDeactivateQuest(QUEST_ShinyOnly);

        TryDeactivateQuest(QUEST_KantoMode);
        TryDeactivateQuest(QUEST_JohtoMode);
        TryDeactivateQuest(QUEST_HoennMode);
        TryDeactivateQuest(QUEST_GlitchMode);
#ifdef ROGUE_EXPANSION
        TryDeactivateQuest(QUEST_SinnohMode);
        TryDeactivateQuest(QUEST_UnovaMode);
        TryDeactivateQuest(QUEST_KalosMode);
        TryDeactivateQuest(QUEST_AlolaMode);
        TryDeactivateQuest(QUEST_GalarMode);
#endif

        for(i = TYPE_NORMAL; i < NUMBER_OF_MON_TYPES; ++i)
            TryDeactivateQuest(TypeToMonoQuest[i]);
    }

    if(Rogue_GetCurrentDifficulty() > 4)
    {
        TryDeactivateQuest(QUEST_NoFainting1);
    }

    if(Rogue_GetCurrentDifficulty() > 8)
    {
        // Can't technically happen atm
        TryDeactivateQuest(QUEST_EliteMaster);
    }

    if(Rogue_GetConfigRange(CONFIG_RANGE_TRAINER) < DIFFICULTY_LEVEL_HARD)
    {
        TryDeactivateQuest(QUEST_Hardcore2);
        TryDeactivateQuest(QUEST_Hardcore3);
        TryDeactivateQuest(QUEST_Hardcore4);
    }

    if(!Rogue_GetConfigToggle(CONFIG_TOGGLE_BAG_WIPE))
    {
        TryDeactivateQuest(QUEST_Nuzlocke);
        TryDeactivateQuest(QUEST_IronMono2);
        TryDeactivateQuest(QUEST_Hardcore4);
    }

    if(!FlagGet(FLAG_ROGUE_HARD_ITEMS) || Rogue_GetConfigToggle(CONFIG_TOGGLE_EV_GAIN) || Rogue_GetConfigToggle(CONFIG_TOGGLE_OVER_LVL))
    {
        TryDeactivateQuest(QUEST_Hardcore4);
    }

    if(Rogue_GetConfigRange(CONFIG_RANGE_BATTLE_FORMAT) != BATTLE_FORMAT_DOUBLES)
    {
        TryDeactivateQuest(QUEST_OrreMode);
    }

    if(!FlagGet(FLAG_ROGUE_GAUNTLET_MODE))
    {
        TryDeactivateQuest(QUEST_GauntletMode);
    }

    {
        bool8 rainbowMode = Rogue_GetModeRules()->trainerOrder == TRAINER_ORDER_RAINBOW;
        u16 dexLimit = VarGet(VAR_ROGUE_REGION_DEX_LIMIT);
        u16 genLimit = VarGet(VAR_ROGUE_ENABLED_GEN_LIMIT);

        // TODO FIXUP
        //bool8 kantoBosses = FALSE; //FlagGet(FLAG_ROGUE_KANTO_BOSSES);
        //bool8 johtoBosses = FALSE; //FlagGet(FLAG_ROGUE_JOHTO_BOSSES);
        //bool8 hoennBosses = FALSE; //FlagGet(FLAG_ROGUE_HOENN_BOSSES);

        bool8 justKantoBosses = FALSE; //;kantoBosses && !johtoBosses && !hoennBosses;
        bool8 justJohtoBosses = FALSE; //!kantoBosses && johtoBosses && !hoennBosses;
        bool8 glitchBosses = FALSE; //!kantoBosses && !johtoBosses && !hoennBosses;

        // Equiv to dex limit
        if(dexLimit == 0 && genLimit == 1)
            dexLimit = 1;

        if(rainbowMode || dexLimit != 1 || !justKantoBosses)
            TryDeactivateQuest(QUEST_KantoMode);

        if(rainbowMode || dexLimit != 2 || !justJohtoBosses)
            TryDeactivateQuest(QUEST_JohtoMode);

        if(!rainbowMode || dexLimit != 3)
            TryDeactivateQuest(QUEST_HoennMode);

#ifdef ROGUE_EXPANSION
        if(rainbowMode || dexLimit != 0 || genLimit != 8 || !glitchBosses)
#else
        if(rainbowMode || dexLimit != 0 || genLimit != 3 || !glitchBosses)
#endif
            TryDeactivateQuest(QUEST_GlitchMode);

#ifdef ROGUE_EXPANSION
        if(!rainbowMode || dexLimit != 4)
            TryDeactivateQuest(QUEST_SinnohMode);

        if(!rainbowMode || dexLimit != 5)
            TryDeactivateQuest(QUEST_UnovaMode);

        if(!rainbowMode || dexLimit != 6)
            TryDeactivateQuest(QUEST_KalosMode);

        if(!rainbowMode || dexLimit != 7)
            TryDeactivateQuest(QUEST_AlolaMode);

        if(!rainbowMode || dexLimit != 8)
            TryDeactivateQuest(QUEST_GalarMode);
#endif
    }

    UpdateChaosChampion(TRUE);
    CheckCurseQuests();
}

static void OnStartBattle(void)
{
    UpdateMonoQuests();
    
    if(IsQuestActive(QUEST_Hardcore3) || IsQuestActive(QUEST_Hardcore4) || IsQuestActive(QUEST_LegendOnly))
    {
        u16 i;

        for(i = 0; i < PARTY_SIZE; ++i)
        {
            u16 species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES);
            if(species != SPECIES_NONE)
            {
                if(RoguePokedex_IsSpeciesLegendary(species))
                {
                    TryDeactivateQuest(QUEST_Hardcore3);
                    TryDeactivateQuest(QUEST_Hardcore4);
                }
                else
                {
                    TryDeactivateQuest(QUEST_LegendOnly);
                }
            }
        }
    }

    if(IsQuestActive(QUEST_ShinyOnly))
    {
        u16 i;

        for(i = 0; i < PARTY_SIZE; ++i)
        {
            u16 species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES);
            if(species != SPECIES_NONE)
            {
                if(!IsMonShiny(&gPlayerParty[i]))
                {
                    TryDeactivateQuest(QUEST_ShinyOnly);
                }
            }
        }
    }
}

static void OnEndBattle(void)
{
    struct OLDRogueQuestState state;

    if(IsQuestActive(QUEST_Collector1))
    {
        u16 caughtCount = GetNationalPokedexCount(FLAG_GET_CAUGHT);
        if(caughtCount >= 15)
            TryMarkQuestAsComplete(QUEST_Collector1);
    }

    if(IsQuestActive(QUEST_Collector2))
    {
        u16 caughtCount = GetNationalPokedexCount(FLAG_GET_CAUGHT);
        if(caughtCount >= 100)
            TryMarkQuestAsComplete(QUEST_Collector2);
    }

    if(IsQuestActive(QUEST_NoFainting1) && GetQuestState(QUEST_NoFainting1, &state))
    {
        if(Rogue_IsPartnerMonInTeam() == FALSE)
        {
            state.isValid = FALSE;
            SetQuestState(QUEST_NoFainting1, &state);
        }
    }
}

void QuestNotify_EndAdventure(void)
{
    TryMarkQuestAsComplete(QUEST_FirstAdventure);

    ForEachUnlockedQuest(ActivateHubQuests);
}

void QuestNotify_OnWildBattleEnd(void)
{
    if(gBattleOutcome == B_OUTCOME_CAUGHT)
    {
        if(IsQuestActive(QUEST_DenExplorer) && gRogueAdvPath.currentRoomType == ADVPATH_ROOM_WILD_DEN)
            TryMarkQuestAsComplete(QUEST_DenExplorer);
    }
    //

    OnEndBattle();
}

static void UpdateMonoQuests(void)
{
    u16 type;
    u16 questId;
    u8 i;
    bool8 hasType;

    for(type = TYPE_NORMAL; type < NUMBER_OF_MON_TYPES; ++type)
    {
        questId = TypeToMonoQuest[type];

        if(IsQuestActive(questId))
        {
            for(i = 0; i < gPlayerPartyCount; ++i)
            {
                u16 species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES);
                hasType = RoguePokedex_GetSpeciesType(species, 0) == type || RoguePokedex_GetSpeciesType(species, 1) == type;

                if(species != SPECIES_NONE && !hasType)
                {
                    TryDeactivateQuest(questId);
                    break;
                }
            }
        }
    }
}

static void CompleteMonoQuests(void)
{
    u16 type;
    u16 questId;

    for(type = TYPE_NORMAL; type < NUMBER_OF_MON_TYPES; ++type)
    {
        questId = TypeToMonoQuest[type];

        if(IsQuestActive(questId))
            TryMarkQuestAsComplete(questId);
    }
}

void QuestNotify_OnTrainerBattleEnd(bool8 isBossTrainer)
{
    u8 i;

    if(isBossTrainer)
    {
        u16 relativeDifficulty = Rogue_GetCurrentDifficulty() - VarGet(VAR_ROGUE_SKIP_TO_DIFFICULTY);

        switch(Rogue_GetCurrentDifficulty())
        {
            case 1:
                TryMarkQuestAsComplete(QUEST_Gym1);
                break;
            case 2:
                TryMarkQuestAsComplete(QUEST_Gym2);
                break;
            case 3:
                TryMarkQuestAsComplete(QUEST_Gym3);
                break;
            case 4:
                TryMarkQuestAsComplete(QUEST_Gym4);
                break;
            case 5:
                TryMarkQuestAsComplete(QUEST_Gym5);
                break;
            case 6:
                TryMarkQuestAsComplete(QUEST_Gym6);
                break;
            case 7:
                TryMarkQuestAsComplete(QUEST_Gym7);
                break;
            case 8: // Just beat last Gym
                TryMarkQuestAsComplete(QUEST_Gym8);
                TryMarkQuestAsComplete(QUEST_NoFainting2);
                break;

            case 12: // Just beat last E4
                if(IsQuestActive(QUEST_CollectorLegend))
                {
                    for(i = 0; i < PARTY_SIZE; ++i)
                    {
                        u16 species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES);
                        if(RoguePokedex_IsSpeciesLegendary(species))
                        {
                            TryMarkQuestAsComplete(QUEST_CollectorLegend);
                            break;
                        }
                    }
                }
                break;

            case 14: // Just beat final champ
                TryMarkQuestAsComplete(QUEST_Champion);
                TryMarkQuestAsComplete(QUEST_NoFainting3);
                TryMarkQuestAsComplete(QUEST_ChaosChampion);
                TryMarkQuestAsComplete(QUEST_Hardcore);
                TryMarkQuestAsComplete(QUEST_Hardcore2);
                TryMarkQuestAsComplete(QUEST_Hardcore3);
                TryMarkQuestAsComplete(QUEST_Hardcore4);
                TryMarkQuestAsComplete(QUEST_CursedBody);
                TryMarkQuestAsComplete(QUEST_Nuzlocke);
                TryMarkQuestAsComplete(QUEST_IronMono1);
                TryMarkQuestAsComplete(QUEST_IronMono2);
                TryMarkQuestAsComplete(QUEST_LegendOnly);
                TryMarkQuestAsComplete(QUEST_ShinyOnly);
                TryMarkQuestAsComplete(QUEST_GauntletMode);

                TryMarkQuestAsComplete(QUEST_KantoMode);
                TryMarkQuestAsComplete(QUEST_JohtoMode);
                TryMarkQuestAsComplete(QUEST_HoennMode);
                TryMarkQuestAsComplete(QUEST_GlitchMode);
#ifdef ROGUE_EXPANSION
                TryMarkQuestAsComplete(QUEST_SinnohMode);
                TryMarkQuestAsComplete(QUEST_UnovaMode);
                TryMarkQuestAsComplete(QUEST_KalosMode);
                TryMarkQuestAsComplete(QUEST_AlolaMode);
                TryMarkQuestAsComplete(QUEST_GalarMode);
#endif

                CompleteMonoQuests();
                break;
        }

        if(Rogue_GetCurrentDifficulty() >= 4)
            TryMarkQuestAsComplete(QUEST_GymChallenge);

        if(Rogue_GetCurrentDifficulty() >= 8)
            TryMarkQuestAsComplete(QUEST_GymMaster);

        if(Rogue_GetCurrentDifficulty() >= 12)
            TryMarkQuestAsComplete(QUEST_EliteMaster);

        if(relativeDifficulty == 4)
        {
            if(IsQuestActive(QUEST_NoFainting1))
                TryMarkQuestAsComplete(QUEST_NoFainting1);
        }
    }
    
    OnEndBattle();
}

void QuestNotify_OnMonFainted()
{
    TryDeactivateQuest(QUEST_NoFainting2);
    TryDeactivateQuest(QUEST_NoFainting3);

    if(IsQuestActive(QUEST_WobFate))
    {
        bool8 isTrainerBattle = (gBattleTypeFlags & BATTLE_TYPE_TRAINER) != 0;
        if(!isTrainerBattle)
        {
            u16 species = GetMonData(&gEnemyParty[gBattlerPartyIndexes[gBattlerAttacker ^ BIT_SIDE]], MON_DATA_SPECIES);

            if(species == SPECIES_WOBBUFFET)
                TryMarkQuestAsComplete(QUEST_WobFate);
        }
    }
}

void QuestNotify_OnExitHubTransition(void)
{
    // Could've traded by event so update mono quests
    UpdateMonoQuests();
}

void QuestNotify_OnWarp(struct WarpData* warp)
{
    if(Rogue_IsRunActive())
    {
        struct OLDRogueQuestState state;

        // Warped into
        switch(gRogueAdvPath.currentRoomType)
        {
            case ADVPATH_ROOM_ROUTE:
                if(gRogueAdvPath.currentRoomParams.perType.route.difficulty == 2)
                {
                    if(IsQuestActive(QUEST_Bike1) && GetQuestState(QUEST_Bike1, &state))
                    {
                        state.data.byte[0] = gSaveBlock2Ptr->playTimeHours;
                        state.data.byte[1] = gSaveBlock2Ptr->playTimeMinutes;
                        SetQuestState(QUEST_Bike1, &state);
                    }

                    if(Rogue_GetCurrentDifficulty() >= 8)
                    {
                        if(IsQuestActive(QUEST_Bike2) && GetQuestState(QUEST_Bike2, &state))
                        {
                            state.data.byte[0] = gSaveBlock2Ptr->playTimeHours;
                            state.data.byte[1] = gSaveBlock2Ptr->playTimeMinutes;
                            SetQuestState(QUEST_Bike2, &state);
                        }
                    }
                }
                break;

            case ADVPATH_ROOM_RESTSTOP:
                UpdateChaosChampion(TRUE);
                break;

            case ADVPATH_ROOM_BOSS:
                // About to face final champ
                if(Rogue_GetCurrentDifficulty() == 13)
                {
                    if(IsQuestActive(QUEST_OrreMode) 
                    && PartyContainsBaseSpecies(gPlayerParty, gPlayerPartyCount, SPECIES_ESPEON)
                    && PartyContainsBaseSpecies(gPlayerParty, gPlayerPartyCount, SPECIES_UMBREON)
                    )
                        TryMarkQuestAsComplete(QUEST_OrreMode);

#ifdef ROGUE_EXPANSION
                    if(IsQuestActive(QUEST_ShayminItem) && PartyContainsBaseSpecies(gPlayerParty, gPlayerPartyCount, SPECIES_SHAYMIN))
                        TryMarkQuestAsComplete(QUEST_ShayminItem);

                    if(IsQuestActive(QUEST_HoopaItem) && PartyContainsBaseSpecies(gPlayerParty, gPlayerPartyCount, SPECIES_HOOPA))
                        TryMarkQuestAsComplete(QUEST_HoopaItem);

                    if(IsQuestActive(QUEST_NatureItem))
                    {
                        if(PartyContainsBaseSpecies(gPlayerParty, gPlayerPartyCount, SPECIES_TORNADUS)
                        || PartyContainsBaseSpecies(gPlayerParty, gPlayerPartyCount, SPECIES_THUNDURUS)
                        || PartyContainsBaseSpecies(gPlayerParty, gPlayerPartyCount, SPECIES_LANDORUS)
                        )
                            TryMarkQuestAsComplete(QUEST_NatureItem);
                    }
    
                    if(IsQuestActive(QUEST_DeoxysItem) && PartyContainsBaseSpecies(gPlayerParty, gPlayerPartyCount, SPECIES_DEOXYS))
                        TryMarkQuestAsComplete(QUEST_DeoxysItem);
#endif
                }

                break;
        }

        // Warped out of
        switch(sPreviousRouteType)
        {
            case ADVPATH_ROOM_ROUTE:
                if(gRogueAdvPath.currentRoomParams.perType.route.difficulty == 2)
                {
                    if(IsQuestActive(QUEST_Bike1) && GetQuestState(QUEST_Bike1, &state))
                    {
                        u16 startTime = ((u16)state.data.byte[0]) * 60 + ((u16)state.data.byte[1]);
                        u16 exitTime = ((u16)gSaveBlock2Ptr->playTimeHours) * 60 + ((u16)gSaveBlock2Ptr->playTimeMinutes);

                        if((exitTime - startTime) < 120)
                            TryMarkQuestAsComplete(QUEST_Bike1);
                    }

                    if(Rogue_GetCurrentDifficulty() >= 8)
                    {
                        if(IsQuestActive(QUEST_Bike2) && GetQuestState(QUEST_Bike2, &state))
                        {
                            u16 startTime = ((u16)state.data.byte[0]) * 60 + ((u16)state.data.byte[1]);
                            u16 exitTime = ((u16)gSaveBlock2Ptr->playTimeHours) * 60 + ((u16)gSaveBlock2Ptr->playTimeMinutes);

                            if((exitTime - startTime) < 60)
                                TryMarkQuestAsComplete(QUEST_Bike2);
                        }
                    }
                }
                break;

            case ADVPATH_ROOM_RESTSTOP:
                if(IsQuestActive(QUEST_BigSaver) && GetMoney(&gSaveBlock1Ptr->money) >= 50000)
                    TryMarkQuestAsComplete(QUEST_BigSaver);
                break;
        }

        if(gRogueAdvPath.currentRoomType != ADVPATH_ROOM_RESTSTOP)
        {
            UpdateChaosChampion(FALSE);
        }

        sPreviousRouteType = gRogueAdvPath.currentRoomType;
    }
}

void QuestNotify_OnAddMoney(u32 amount)
{

}

void QuestNotify_OnRemoveMoney(u32 amount)
{
    if(Rogue_IsRunActive())
    {
        struct OLDRogueQuestState state;

        if(gRogueAdvPath.currentRoomType == ADVPATH_ROOM_RESTSTOP)
        {
            if(IsQuestActive(QUEST_ShoppingSpree) && GetQuestState(QUEST_ShoppingSpree, &state))
            {
                state.data.half += amount;
                SetQuestState(QUEST_ShoppingSpree, &state);

                if(state.data.half >= 20000)
                    TryMarkQuestAsComplete(QUEST_ShoppingSpree);
            }
        }
    }
}

void QuestNotify_OnAddBagItem(u16 itemId, u16 count)
{
    if(IsQuestActive(QUEST_BerryCollector))
    {
        if(itemId >= FIRST_BERRY_INDEX && itemId <= LAST_BERRY_INDEX)
        {
            u16 i;
            u16 uniqueBerryCount = 0;

            for(i = FIRST_BERRY_INDEX; i <= LAST_BERRY_INDEX; ++i)
            {
                if(CheckBagHasItem(i,1))
                    ++uniqueBerryCount;
            }

            if(uniqueBerryCount >= 10)
                TryMarkQuestAsComplete(QUEST_BerryCollector);
        }

    }
}

void QuestNotify_OnRemoveBagItem(u16 itemId, u16 count)
{

}

void QuestNotify_OnUseBattleItem(u16 itemId)
{
    //bool8 isPokeball = itemId >= FIRST_BALL && itemId <= LAST_BALL;
    //if(!isPokeball)
    //{
    //    if(IsQuestActive(QUEST_Hardcore))
    //        TryDeactivateQuest(QUEST_Hardcore);
//
    //    if(IsQuestActive(QUEST_Hardcore2))
    //        TryDeactivateQuest(QUEST_Hardcore2);
//
    //    if(IsQuestActive(QUEST_Hardcore3))
    //        TryDeactivateQuest(QUEST_Hardcore3);
//
    //    if(IsQuestActive(QUEST_Hardcore4))
    //        TryDeactivateQuest(QUEST_Hardcore4);
    //}
}

void QuestNotify_OnMegaEvolve(u16 species)
{
#ifdef ROGUE_EXPANSION
    if(Rogue_GetCurrentDifficulty() >= 13)
    {
        if(IsQuestActive(QUEST_MegaEvo))
            TryMarkQuestAsComplete(QUEST_MegaEvo);
    }
#endif
}

void QuestNotify_OnZMoveUsed(u16 move)
{
#ifdef ROGUE_EXPANSION
    if(Rogue_GetCurrentDifficulty() >= 13)
    {
        if(IsQuestActive(QUEST_ZMove))
            TryMarkQuestAsComplete(QUEST_ZMove);
    }
#endif
}

void QuestNotify_StatIncrement(u8 statIndex)
{
    switch (statIndex)
    {
    case GAME_STAT_TOTAL_BATTLES:
        OnStartBattle();
        break;
    } 
}