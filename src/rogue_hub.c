#include "global.h"
#include "constants/layouts.h"
#include "constants/metatile_labels.h"
#include "constants/script_menu.h"

#include "event_data.h"
#include "fieldmap.h"
#include "field_player_avatar.h"
#include "menu.h"
#include "overworld.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "random.h"
#include "strings.h"
#include "string_util.h"

#include "rogue_controller.h"
#include "rogue_hub.h"
#include "rogue_followmon.h"
#include "rogue_multiplayer.h"
#include "rogue_quest.h"

#define TREE_TYPE_DENSE     0
#define TREE_TYPE_SPARSE    1

#define HOME_AREA_DISPLAY_MONS 4

struct RegionCoords
{
    u16 xStart;
    u16 yStart;
    u16 xEnd;
    u16 yEnd;
};

struct MapInfo
{
    u16 group;
    u16 num;
};

struct TileFixup
{
    u8 path : 1;
    u8 pond : 1;
    u8 mountain : 1;
};

static struct RegionCoords const sHomeRegionCoords[HOME_REGION_COUNT] = 
{
    [HOME_REGION_HOUSE] =  { 15, 14, 19, 19 },
    [HOME_REGION_PLACEABLE_REGION] =  { 4, 4, 31, 31 },
};

static struct MapInfo const sHomeAreaStyles[HOME_AREA_STYLE_COUNT] = 
{
    [HOME_AREA_STYLE_OVERGROWN] = {}, // default
    [HOME_AREA_STYLE_FLOWERS] = { MAP_GROUP(ROGUE_TEMPLATE_HOME_FLOWERS), MAP_NUM(ROGUE_TEMPLATE_HOME_FLOWERS) },
    [HOME_AREA_STYLE_PLAIN] = { MAP_GROUP(ROGUE_TEMPLATE_HOME_GRASS), MAP_NUM(ROGUE_TEMPLATE_HOME_GRASS) },
};

static struct RegionCoords const sHomeDecorEnvRegions[HOME_DECOR_ENV_COUNT] = 
{
    [HOME_DECOR_ENV_POND_2x2] = { 0,5, 1,6 },
    [HOME_DECOR_ENV_POND_3x3] = { 0,5, 2,7 },
    [HOME_DECOR_ENV_POND_4x4] = { 0,5, 3,8 },
    [HOME_DECOR_ENV_POND_5x5] = { 0,5, 4,9 },

    [HOME_DECOR_ENV_MOUNTAIN_2x2] = { 5,5, 6,6 },
    [HOME_DECOR_ENV_MOUNTAIN_3x3] = { 5,5, 7,7 },
    [HOME_DECOR_ENV_MOUNTAIN_4x4] = { 5,5, 8,8 },
    [HOME_DECOR_ENV_MOUNTAIN_5x5] = { 5,5, 9,9 },

    [HOME_DECOR_ENV_GRASS_PATH_2x2] = { 10,5, 11,6 },
    [HOME_DECOR_ENV_GRASS_PATH_3x3] = { 10,5, 12,7 },
    [HOME_DECOR_ENV_GRASS_PATH_4x4] = { 10,5, 13,8 },
    [HOME_DECOR_ENV_GRASS_PATH_5x5] = { 10,5, 14,9 },
};

static void MetatileSet_Tile(u16 xStart, u16 yStart, u16 tile);
static void MetatileFill_Tile(u16 xStart, u16 yStart, u16 xEnd, u16 yEnd, u16 tile);
static void MetatileFill_TreesOverlapping(u16 xStart, u16 yStart, u16 xEnd, u16 yEnd, u8 treeType);
static void MetatileFill_TreeStumps(u16 xStart, u16 yStart, u16 xEnd, u8 treeType);
static void MetatileFill_TreeCaps(u16 xStart, u16 yStart, u16 xEnd);

static void MetatileFill_CommonWarpExitVertical(u16 xStart, u16 yStart);
static void MetatileFill_CommonWarpExitHorizontal(u16 xStart, u16 yStart);
static void MetatileFill_CommonPathRemoval(u16 xStart, u16 yStart, u16 xEnd, u16 yEnd);
static void MetatileFill_BlitMapRegion(u16 mapGroup, u16 mapNum, u16 destX1, u16 destY1, u16 destX2, u16 destY2, u16 srcX1, u16 srcY1);

static void RogueHub_UpdateLabsAreaMetatiles();
static void RogueHub_UpdateAdventureEntranceAreaMetatiles();
static void RogueHub_UpdateHomeAreaMetatiles();
static void RogueHub_PlaceHomeEnvironmentDecorations();
static void RogueHub_UpdateHomeInteriorMetatiles();
static void RogueHub_UpdateFarmingAreaMetatiles();
static void RogueHub_UpdateSafariAreaMetatiles();
static void RogueHub_UpdateRideTrainingAreaMetatiles();
static void RogueHub_UpdateMartsAreaMetatiles();
static void RogueHub_UpdateTownSquareAreaMetatiles();
static void RogueHub_UpdateChallengeFrontierAreaMetatiles();
static void RogueHub_UpdateDayCareAreaMetatiles();

static void BuildAtRandomConnectionFrom(u8 fromArea, u8 buildArea);

static void FixupTileCommon(struct TileFixup* settings);

void RogueHub_Enter()
{
    //AGB_ASSERT(gRogueSaveBlock->hubMap == NULL);
    //gRogueSaveBlock->hubMap = AllocZeroed(sizeof(struct RogueHubMap));
}

void RogueHub_Exit()
{
    //AGB_ASSERT(gRogueSaveBlock->hubMap != NULL);
    //free(gRogueSaveBlock->hubMap);
    //gRogueSaveBlock->hubMap = NULL;
}

static struct RogueHubMap* GetActiveHubMap()
{
    if(RogueMP_IsActive() && !RogueMP_IsHost())
    {
        AGB_ASSERT(gRogueMultiplayer != NULL);
        return &gRogueMultiplayer->gameState.hub.hubMap;
    }

    return &gRogueSaveBlock->hubMap;
}

void RogueHub_ClearProgress()
{
    memset(&gRogueSaveBlock->hubMap, 0, sizeof(gRogueSaveBlock->hubMap));

    // Build default area at 0,0
    RogueHub_BuildArea(HUB_AREA_LABS, 0, 0);

    // Place required areas randomly (Order matters)
    BuildAtRandomConnectionFrom(HUB_AREA_LABS, HUB_AREA_ADVENTURE_ENTRANCE);
    BuildAtRandomConnectionFrom(HUB_AREA_ADVENTURE_ENTRANCE, HUB_AREA_SAFARI_ZONE);
    BuildAtRandomConnectionFrom(HUB_AREA_LABS, HUB_AREA_TOWN_SQUARE);
}

bool8 RogueHub_HasUpgrade(u16 upgradeId)
{
    u16 idx = upgradeId / 8;
    u16 bit = upgradeId % 8;

    u8 bitMask = 1 << bit;

    AGB_ASSERT(idx < ARRAY_COUNT(GetActiveHubMap()->upgradeFlags));
    return (GetActiveHubMap()->upgradeFlags[idx] & bitMask) != 0;
}

void RogueHub_SetUpgrade(u16 upgradeId, bool8 state)
{
    u16 idx = upgradeId / 8;
    u16 bit = upgradeId % 8;

    u8 bitMask = 1 << bit;
    
    AGB_ASSERT(idx < ARRAY_COUNT(GetActiveHubMap()->upgradeFlags));
    if(state)
    {
        GetActiveHubMap()->upgradeFlags[idx] |= bitMask;
    }
    else
    {
        GetActiveHubMap()->upgradeFlags[idx] &= ~bitMask;
    }
}

bool8 RogueHub_HasUpgradeRequirements(u16 upgradeId)
{
    u8 i;
    u8 check;

    if(!RogueHub_HasAreaBuilt(gRogueHubUpgrades[upgradeId].targetArea))
        return FALSE;

    for(i = 0; i < HUB_UPGRADE_MAX_REQUIREMENTS; ++i)
    {
        check = gRogueHubUpgrades[upgradeId].requiredUpgrades[i];
        if(check == HUB_UPGRADE_NONE)
            break;

        if(!RogueHub_HasUpgrade(check))
            return FALSE;
    }

    return TRUE;
}

bool8 RogueHub_HasAreaBuilt(u8 area)
{
    u16 idx = area / 8;
    u16 bit = area % 8;

    u8 bitMask = 1 << bit;

    AGB_ASSERT(idx < ARRAY_COUNT(GetActiveHubMap()->areaBuiltFlags));
    return (GetActiveHubMap()->areaBuiltFlags[idx] & bitMask) != 0;
}

void RogueHub_BuildArea(u8 area, s8 x, s8 y)
{
    u16 idx = area / 8;
    u16 bit = area % 8;

    u8 bitMask = 1 << bit;
    
    AGB_ASSERT(idx < ARRAY_COUNT(GetActiveHubMap()->areaBuiltFlags));
    
    GetActiveHubMap()->areaBuiltFlags[idx] |= bitMask;
    GetActiveHubMap()->areaCoords[area].x = x;
    GetActiveHubMap()->areaCoords[area].y = y;
}

static void IncrementCoordsByDirection(struct Coords8* coords, u8 dir)
{
    switch (dir)
    {
    case HUB_AREA_CONN_SOUTH:
        --coords->y;
        break;
    case HUB_AREA_CONN_NORTH:
        ++coords->y;
        break;
    case HUB_AREA_CONN_WEST:
        --coords->x;
        break;
    case HUB_AREA_CONN_EAST:
        ++coords->x;
        break;
    }
}

void RogueHub_BuildAreaInConnDir(u8 area, u8 connDir)
{
    u8 currentArea = RogueHub_GetAreaFromCurrentMap();

    if(currentArea != HUB_AREA_NONE && !RogueHub_HasAreaBuilt(area) && connDir < HUB_AREA_CONN_COUNT)
    {
        struct Coords8 pos;
        pos.x = GetActiveHubMap()->areaCoords[currentArea].x;
        pos.y = GetActiveHubMap()->areaCoords[currentArea].y;
        IncrementCoordsByDirection(&pos, connDir);

        RogueHub_BuildArea(area, pos.x, pos.y);
    }
}

bool8 RogueHub_HasAreaBuildRequirements(u8 area)
{
    u8 i;
    u8 check;

    // Cannot build until post game
    if(area == HUB_AREA_CHALLENGE_FRONTIER)
    {
        if(!FlagGet(FLAG_ROGUE_MET_POKABBIE))
            return FALSE;
    }

    for(i = 0; i < HUB_UPGRADE_MAX_REQUIREMENTS; ++i)
    {
        check = gRogueHubAreas[area].requiredUpgrades[i];
        if(check == HUB_UPGRADE_NONE)
            break;

        if(!RogueHub_HasUpgrade(check))
            return FALSE;
    }

    return TRUE;
}

u8 RogueHub_FindAreaAtCoord(s8 x, s8 y)
{
    u8 i;

    for(i = 0; i < HUB_AREA_COUNT; ++i)
    {
        if(RogueHub_HasAreaBuilt(i))
        {
            if(GetActiveHubMap()->areaCoords[i].x == x && GetActiveHubMap()->areaCoords[i].y == y)
                return i;
        }
    }

    return HUB_AREA_NONE;
}

u8 RogueHub_FindAreaInDir(u8 area, u8 connDir)
{
    if(RogueHub_HasAreaBuilt(area))
    {
        struct Coords8 pos;
        pos.x = GetActiveHubMap()->areaCoords[area].x;
        pos.y = GetActiveHubMap()->areaCoords[area].y;
        IncrementCoordsByDirection(&pos, connDir);

        return RogueHub_FindAreaAtCoord(pos.x, pos.y);
    }

    return HUB_AREA_NONE;
}

static bool8 CanAreaConnect(u8 area, u8 dir)
{
    // If both warps aren't set to 0, this is a valid warp
    return gRogueHubAreas[area].connectionWarps[dir][0] != 0 || gRogueHubAreas[area].connectionWarps[dir][1] != 0;
}

static u8 InvertConnDirection(u8 dir)
{
    switch (dir)
    {
    case HUB_AREA_CONN_SOUTH:
        return HUB_AREA_CONN_NORTH;
    case HUB_AREA_CONN_NORTH:
        return HUB_AREA_CONN_SOUTH;
    case HUB_AREA_CONN_WEST:
        return HUB_AREA_CONN_EAST;
    case HUB_AREA_CONN_EAST:
        return HUB_AREA_CONN_WEST;
    }

    return dir;
}

bool8 RogueHub_AreaHasFreeConnection(u8 area, u8 dir)
{
    if(CanAreaConnect(area, dir))
    {
        struct Coords8 pos;
        pos.x = GetActiveHubMap()->areaCoords[area].x;
        pos.y = GetActiveHubMap()->areaCoords[area].y;
        IncrementCoordsByDirection(&pos, dir);

        return RogueHub_FindAreaAtCoord(pos.x, pos.y) == HUB_AREA_NONE;
    }

    return FALSE;
}

bool8 RogueHub_CanBuildConnectionBetween(u8 fromArea, u8 toArea, u8 dir)
{
    u8 invDir = InvertConnDirection(dir);
    return RogueHub_AreaHasFreeConnection(fromArea, dir) && CanAreaConnect(toArea, invDir);
}

u8 RogueHub_GetAreaAtConnection(u8 area, u8 dir)
{
    if(CanAreaConnect(area, dir))
    {
        struct Coords8 pos;
        pos.x = GetActiveHubMap()->areaCoords[area].x;
        pos.y = GetActiveHubMap()->areaCoords[area].y;
        IncrementCoordsByDirection(&pos, dir);

        return RogueHub_FindAreaAtCoord(pos.x, pos.y);
    }

    return HUB_AREA_NONE;
}

static u8 GetAreaForLayout(u16 layout)
{
    u8 area;

    switch (layout)
    {
    case LAYOUT_ROGUE_AREA_SAFARI_ZONE_TUTORIAL:
        layout = LAYOUT_ROGUE_AREA_SAFARI_ZONE;
        break;
    }

    for(area = HUB_AREA_FIRST; area < HUB_AREA_COUNT; ++area)
    {
        if(gRogueHubAreas[area].primaryMapLayout == layout)
            return area;
    }

    return HUB_AREA_NONE;
}

u8 RogueHub_GetAreaFromCurrentMap()
{
    return GetAreaForLayout(gMapHeader.mapLayoutId);
}

u16 RogueHub_GetWeatherState()
{
    return GetActiveHubMap()->weatherState;
}

void RogueHub_OnNewDayStarted()
{
    // Ignore this if client
    if(RogueMP_IsActive() && !RogueMP_IsHost())
        return;

    gRogueSaveBlock->hubMap.weatherState = Random();
}

void RogueHub_ModifyMapWarpEvent(struct MapHeader *mapHeader, u8 warpId, struct WarpEvent *warp)
{
    u8 area = GetAreaForLayout(mapHeader->mapLayoutId);

    if(area != HUB_AREA_NONE)
    {
        u8 dir;
        u8 warpArea = HUB_AREA_NONE;
        u8 enterDir = HUB_AREA_CONN_SOUTH;

        for(dir = HUB_AREA_CONN_SOUTH; dir <= HUB_AREA_CONN_EAST; ++dir)
        {
            if(CanAreaConnect(area, dir))
            {
                if(gRogueHubAreas[area].connectionWarps[dir][0] == warpId || gRogueHubAreas[area].connectionWarps[dir][1] == warpId)
                    break;
            }
        }

        // We're trying to warp out into a valid direction
        if(dir <= HUB_AREA_CONN_EAST)
        {
            warpArea = RogueHub_GetAreaAtConnection(area, dir);
            enterDir = InvertConnDirection(dir);
        }

        // Warping into a valid area, so setup a valid warp
        if(warpArea != HUB_AREA_NONE)
        {
            warp->mapGroup = gRogueHubAreas[warpArea].primaryMapGroup;
            warp->mapNum = gRogueHubAreas[warpArea].primaryMapNum;
            warp->warpId = gRogueHubAreas[warpArea].connectionWarps[enterDir][0];

            if(gRogueHubAreas[warpArea].primaryMapLayout == LAYOUT_ROGUE_AREA_SAFARI_ZONE && VarGet(VAR_ROGUE_INTRO_STATE) == ROGUE_INTRO_STATE_CATCH_MON)
            {
                warp->mapGroup = MAP_GROUP(ROGUE_AREA_SAFARI_ZONE_TUTORIAL);
                warp->mapNum = MAP_NUM(ROGUE_AREA_SAFARI_ZONE_TUTORIAL);
            }
        }
        else
        {
            // TODO - We should do safe warp back to main area if we get here
        }
    }
}

bool8 RogueHub_AcceptMapConnection(struct MapHeader *mapHeader, const struct MapConnection *connection)
{
    // Convert from CONNECTION_ to HUB_AREA_CONN_
    u8 area = GetAreaForLayout(mapHeader->mapLayoutId);

    if(area != HUB_AREA_NONE)
    {
        u8 connDir = connection->direction - 1;
        
        // Hide any connections, unless we actually have the connection built
        return RogueHub_GetAreaAtConnection(area, connDir) != HUB_AREA_NONE;
    }

    return TRUE;
}

void RogueHub_ApplyMapMetatiles()
{
    bool8 applyCommonFixup = TRUE;

    switch (gMapHeader.mapLayoutId)
    {
    case LAYOUT_ROGUE_AREA_LABS:
        RogueHub_UpdateLabsAreaMetatiles();
        break;
    case LAYOUT_ROGUE_AREA_ADVENTURE_ENTRANCE:
        RogueHub_UpdateAdventureEntranceAreaMetatiles();
        break;

    case LAYOUT_ROGUE_AREA_HOME:
        applyCommonFixup = FALSE;
        RogueHub_UpdateHomeAreaMetatiles();
        break;
    case LAYOUT_ROGUE_INTERIOR_HOME:
        applyCommonFixup = FALSE;
        RogueHub_UpdateHomeInteriorMetatiles();
        break;

    case LAYOUT_ROGUE_AREA_FARMING_FIELD:
        RogueHub_UpdateFarmingAreaMetatiles();
        break;

    case LAYOUT_ROGUE_AREA_SAFARI_ZONE:
    case LAYOUT_ROGUE_AREA_SAFARI_ZONE_TUTORIAL:
        RogueHub_UpdateSafariAreaMetatiles();
        break;

    case LAYOUT_ROGUE_AREA_RIDE_TRAINING:
        RogueHub_UpdateRideTrainingAreaMetatiles();
        break;

    case LAYOUT_ROGUE_AREA_MARTS:
        RogueHub_UpdateMartsAreaMetatiles();
        break;

    case LAYOUT_ROGUE_AREA_TOWN_SQUARE:
        RogueHub_UpdateTownSquareAreaMetatiles();
        break;

    case LAYOUT_ROGUE_AREA_CHALLENGE_FRONTIER:
        RogueHub_UpdateChallengeFrontierAreaMetatiles();
        break;

    case LAYOUT_ROGUE_AREA_DAY_CARE:
        RogueHub_UpdateDayCareAreaMetatiles();
        break;
    
    default:
        break;
    }

    if(applyCommonFixup)
    {
        struct TileFixup fixup;
        fixup.path = TRUE;
        fixup.pond = FALSE;
        fixup.mountain = FALSE;
        FixupTileCommon(&fixup);
    }
}

static void UpdateStatueLevel()
{
    if(Rogue_Use200PercEffects())
        gRogueSaveBlock->hubMap.statueLevel = 3;
    else if(Rogue_Use100PercEffects())
        gRogueSaveBlock->hubMap.statueLevel = 1;
    else if(RogueQuest_HasCollectedRewards(QUEST_ID_CHAMPION))
        gRogueSaveBlock->hubMap.statueLevel = 1;
    else
        gRogueSaveBlock->hubMap.statueLevel = 0;
}

void RogueHub_UpdateWarpStates()
{
    UpdateStatueLevel();
}

u8 RogueHub_GetStatueLevel()
{
    UpdateStatueLevel();
    return GetActiveHubMap()->statueLevel;
}

static void RogueHub_UpdateLabsAreaMetatiles()
{
    // Remove connectionss
    if(RogueHub_GetAreaAtConnection(HUB_AREA_LABS, HUB_AREA_CONN_NORTH) == HUB_AREA_NONE)
    {
        MetatileFill_TreesOverlapping(12, 0, 15, 0, TREE_TYPE_DENSE);
        MetatileFill_TreeStumps(12, 1, 15, TREE_TYPE_DENSE);

        MetatileFill_CommonPathRemoval(12, 2, 15, 7);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_LABS, HUB_AREA_CONN_EAST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(26, 7);

        MetatileFill_CommonPathRemoval(22, 8, 25, 10);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_LABS, HUB_AREA_CONN_SOUTH) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitVertical(12, 12);
        MetatileFill_TreeCaps(12, 13, 15);

        MetatileFill_CommonPathRemoval(12, 11, 15, 11);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_LABS, HUB_AREA_CONN_WEST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(0, 7);

        MetatileFill_CommonPathRemoval(2, 8, 5, 10);
    }
}

static void RogueHub_UpdateAdventureEntranceAreaMetatiles()
{
    // Remove connectionss
    if(RogueHub_GetAreaAtConnection(HUB_AREA_ADVENTURE_ENTRANCE, HUB_AREA_CONN_EAST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(18, 9);

        MetatileFill_CommonPathRemoval(12, 10, 17, 12);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_ADVENTURE_ENTRANCE, HUB_AREA_CONN_SOUTH) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitVertical(8, 14);
        MetatileFill_TreeCaps(8, 15, 11);

        MetatileFill_CommonPathRemoval(8, 13, 11, 13);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_ADVENTURE_ENTRANCE, HUB_AREA_CONN_WEST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(0, 9);

        MetatileFill_CommonPathRemoval(2, 10, 7, 12);
    }
}

static void BlitPlayerHomeRegion(u16 region, u16 style)
{
    AGB_ASSERT(region < HOME_REGION_COUNT);
    AGB_ASSERT(style < HOME_AREA_STYLE_COUNT);

    if(style != HOME_AREA_STYLE_OVERGROWN) // HOME_AREA_STYLE_OVERGROWN is default
    {
        MetatileFill_BlitMapRegion(
            sHomeAreaStyles[style].group, sHomeAreaStyles[style].num,
            sHomeRegionCoords[region].xStart, sHomeRegionCoords[region].yStart, sHomeRegionCoords[region].xEnd, sHomeRegionCoords[region].yEnd,
            sHomeRegionCoords[region].xStart, sHomeRegionCoords[region].yStart
        );
    }
}

static void BlitPlayerHouse(u16 style, bool8 isUpgraded)
{
    u16 const width = (sHomeRegionCoords[HOME_REGION_HOUSE].xEnd - sHomeRegionCoords[HOME_REGION_HOUSE].xStart + 1);
    AGB_ASSERT(style < HOME_BUILDING_STYLE_COUNT);

    MetatileFill_BlitMapRegion(
        MAP_GROUP(ROGUE_TEMPLATE_HOMES), MAP_NUM(ROGUE_TEMPLATE_HOMES),
        sHomeRegionCoords[HOME_REGION_HOUSE].xStart, 
        sHomeRegionCoords[HOME_REGION_HOUSE].yStart, 
        sHomeRegionCoords[HOME_REGION_HOUSE].xEnd, 
        sHomeRegionCoords[HOME_REGION_HOUSE].yEnd - 1, // bottom tile is just to stop things being placed too close
        width * (style * 2 + (isUpgraded ? 0 : 1)),0
    );
}

static void BlitPlayerHouseEnvDecor(s32 x, s32 y, u16 decor)
{
    u8 xStart = sHomeDecorEnvRegions[decor].xStart;
    u8 yStart = sHomeDecorEnvRegions[decor].yStart;
    u8 xEnd = sHomeDecorEnvRegions[decor].xEnd;
    u8 yEnd = sHomeDecorEnvRegions[decor].yEnd;

    AGB_ASSERT(decor < HOME_DECOR_ENV_COUNT);

    // Clip anything which is outside of the placeable region
    if(x < sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].xStart)
    {
        u8 delta = sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].xStart - x;
        x = sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].xStart;
        xStart += delta;
    }

    if(y < sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].yStart)
    {
        u8 delta = sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].yStart - y;
        y = sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].yStart;
        yStart += delta;
    }

    if(x + (xEnd - xStart) > sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].xEnd)
    {
        u8 delta = x + (xEnd - xStart) - sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].xEnd;
        xEnd -= delta;
    }

    if(y + (yEnd - yStart) > sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].yEnd)
    {
        u8 delta = y + (yEnd - yStart) - sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].yEnd;
        yEnd -= delta;
    }

    MetatileFill_BlitMapRegion(
        MAP_GROUP(ROGUE_TEMPLATE_HOMES), MAP_NUM(ROGUE_TEMPLATE_HOMES),
        x, y, 
        x + (xEnd - xStart), y + (yEnd - yStart),
        xStart, yStart
    );
}

static void RogueHub_UpdateHomeAreaMetatiles()
{
    // Blit map styles first
    u8 i;
    struct RogueHubMap* hubMap = GetActiveHubMap();

    //for(i = 0; i < HOME_REGION_COUNT; ++i)
    //{
    //    if(i == HOME_REGION_HOUSE)
    //        BlitPlayerHouse(hubMap->homeRegionStyles[i], RogueHub_HasUpgrade(HUB_UPGRADE_HOME_UPPER_FLOOR));
    //    else
    //        BlitPlayerHomeRegion(i, hubMap->homeRegionStyles[i]);
    //}

    RogueHub_PlaceHomeEnvironmentDecorations();

    // Remove connections
    if(RogueHub_GetAreaAtConnection(HUB_AREA_HOME, HUB_AREA_CONN_NORTH) == HUB_AREA_NONE)
    {
        MetatileFill_TreesOverlapping(15, 0, 20, 0, TREE_TYPE_DENSE);
        MetatileFill_TreeStumps(15, 1, 20, TREE_TYPE_DENSE);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_HOME, HUB_AREA_CONN_EAST) == HUB_AREA_NONE)
    {
        MetatileFill_TreesOverlapping(34, 15, 35, 20, TREE_TYPE_DENSE);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_HOME, HUB_AREA_CONN_SOUTH) == HUB_AREA_NONE)
    {
        MetatileFill_TreesOverlapping(15, 34, 20, 35, TREE_TYPE_DENSE);
        MetatileFill_TreeCaps(16, 33, 19);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_HOME, HUB_AREA_CONN_WEST) == HUB_AREA_NONE)
    {
        MetatileFill_TreesOverlapping(0, 15, 1, 20, TREE_TYPE_DENSE);
    }
}

static void RogueHub_PlaceHomeEnvironmentDecorations()
{
    u8 i;
    struct RogueHubMap* hubMap = GetActiveHubMap();

    // Reset to default state
    MetatileFill_BlitMapRegion(
        MAP_GROUP(ROGUE_AREA_HOME), MAP_NUM(ROGUE_AREA_HOME),
        sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].xStart, sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].yStart,
        sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].xEnd, sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].yEnd,
        sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].xStart, sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].yStart
    );

    for(i = 0; i < HOME_DECOR_OUTSIDE_ENV_COUNT; ++i)
    {
        struct RogueHubDecoration* decor = &hubMap->homeDecorations[HOME_DECOR_OUTSIDE_ENV_OFFSET + i];
        if(decor->active)
        {
            BlitPlayerHouseEnvDecor(decor->x, decor->y, decor->decorId);
        }
    }

    //BlitPlayerHouseEnvDecor(7, 6, HOME_DECOR_ENV_POND_2x2);
    //BlitPlayerHouseEnvDecor(21, 6, HOME_DECOR_ENV_GRASS_PATH_5x5);
    //BlitPlayerHouseEnvDecor(7, 19, HOME_DECOR_ENV_GRASS_PATH_3x3);

    // Replace this now, but need to tell fixup to ignore it
    BlitPlayerHouse(hubMap->homeRegionStyles[HOME_REGION_HOUSE], RogueHub_HasUpgrade(HUB_UPGRADE_HOME_UPPER_FLOOR));

    // Fixup connecting tiles
    {
        struct TileFixup fixup;
        fixup.path = TRUE;
        fixup.pond = TRUE;
        fixup.mountain = TRUE;
        FixupTileCommon(&fixup);
    }
}

static void RogueHub_UpdateHomeInteriorMetatiles()
{
    if(!RogueHub_HasUpgrade(HUB_UPGRADE_HOME_UPPER_FLOOR))
    {
        // Replace with back wall
        MetatileSet_Tile(7, 0, 0x254 | MAPGRID_COLLISION_MASK);
        MetatileSet_Tile(7, 1, 0x25C | MAPGRID_COLLISION_MASK);
        MetatileSet_Tile(8, 0, 0x21E | MAPGRID_COLLISION_MASK);
        MetatileSet_Tile(8, 1, 0x226 | MAPGRID_COLLISION_MASK);
        MetatileSet_Tile(9, 0, 0x254 | MAPGRID_COLLISION_MASK);
        MetatileSet_Tile(9, 1, 0x25C | MAPGRID_COLLISION_MASK);
    }
}

static void RogueHub_UpdateFarmingAreaMetatiles()
{
    // Remove connectionss
    if(RogueHub_GetAreaAtConnection(HUB_AREA_BERRY_FIELD, HUB_AREA_CONN_NORTH) == HUB_AREA_NONE)
    {
        MetatileFill_TreesOverlapping(1, 0, 6, 2, TREE_TYPE_DENSE);
        MetatileFill_TreeStumps(1, 3, 6, TREE_TYPE_DENSE);

        MetatileFill_CommonPathRemoval(2, 4, 5, 5);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_BERRY_FIELD, HUB_AREA_CONN_EAST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(38, 5);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_BERRY_FIELD, HUB_AREA_CONN_SOUTH) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitVertical(32, 10);
        MetatileFill_TreeCaps(32, 11, 35);

        MetatileFill_CommonPathRemoval(32, 9, 35, 9);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_BERRY_FIELD, HUB_AREA_CONN_WEST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(0, 5);
    }


    // Fill right field
    //if(!RogueHub_HasUpgrade(HUB_UPGRADE_BERRY_FIELD_EXTRA_FIELD))
    //{
    //    MetatileFill_TreesOverlapping(12, 1, 19, 6, TREE_TYPE_DENSE);
    //    MetatileFill_TreeStumps(13, 7, 19, TREE_TYPE_DENSE);
    //    MetatileFill_TreeStumps(12, 7, 12, TREE_TYPE_SPARSE);
    //    
    //    MetatileFill_Tile(12, 8, 19, 8, METATILE_GeneralHub_Grass);
    //}
}

static void RogueHub_UpdateSafariAreaMetatiles()
{
    // Remove connectionss
    if(RogueHub_GetAreaAtConnection(HUB_AREA_SAFARI_ZONE, HUB_AREA_CONN_EAST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(36, 13);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_SAFARI_ZONE, HUB_AREA_CONN_SOUTH) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitVertical(16, 31);
        MetatileFill_TreeCaps(16, 31, 19);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_SAFARI_ZONE, HUB_AREA_CONN_WEST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(0, 13);
    }

    // Open cave
    if(RogueHub_HasUpgrade(HUB_UPGRADE_SAFARI_ZONE_LEGENDS_CAVE))
    {
        MetatileSet_Tile(18, 4, METATILE_Fallarbor_BrownCaveEntrance_Top);
        MetatileSet_Tile(18, 5, METATILE_Fallarbor_BrownCaveEntrance_Bottom);
    }
}

static void RogueHub_UpdateRideTrainingAreaMetatiles()
{
    // Remove connectionss
    if(RogueHub_GetAreaAtConnection(HUB_AREA_RIDE_TRAINING, HUB_AREA_CONN_NORTH) == HUB_AREA_NONE)
    {
        MetatileFill_TreesOverlapping(20, 0, 23, 8, TREE_TYPE_DENSE);
        MetatileFill_TreeStumps(20, 9, 23, TREE_TYPE_DENSE);

        MetatileFill_CommonPathRemoval(20, 10, 23, 17);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_RIDE_TRAINING, HUB_AREA_CONN_EAST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(32, 17);

        MetatileFill_CommonPathRemoval(24, 18, 31, 21);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_RIDE_TRAINING, HUB_AREA_CONN_SOUTH) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitVertical(20, 34);
        MetatileFill_TreeCaps(20, 35, 23);

        MetatileFill_CommonPathRemoval(20, 22, 23, 33);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_RIDE_TRAINING, HUB_AREA_CONN_WEST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(0, 17);

        MetatileFill_CommonPathRemoval(2, 18, 19, 21);
    }
}

static void RogueHub_UpdateMartsAreaMetatiles()
{
    // Remove connectionss
    if(RogueHub_GetAreaAtConnection(HUB_AREA_MARTS, HUB_AREA_CONN_NORTH) == HUB_AREA_NONE)
    {
        MetatileFill_TreesOverlapping(16, 0, 19, 0, TREE_TYPE_DENSE);
        MetatileFill_TreeStumps(16, 1, 19, TREE_TYPE_DENSE);

        MetatileFill_CommonPathRemoval(16, 2, 19, 13);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_MARTS, HUB_AREA_CONN_EAST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(32, 13);

        MetatileFill_CommonPathRemoval(20, 14, 31, 17);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_MARTS, HUB_AREA_CONN_SOUTH) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitVertical(16, 20);
        MetatileFill_TreeCaps(16, 21, 21);

        MetatileFill_CommonPathRemoval(16, 18, 19, 19);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_MARTS, HUB_AREA_CONN_WEST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(0, 13);

        MetatileFill_CommonPathRemoval(2, 14, 15, 17);
    }
}

static void RogueHub_UpdateTownSquareAreaMetatiles()
{
    // Remove connectionss
    if(RogueHub_GetAreaAtConnection(HUB_AREA_TOWN_SQUARE, HUB_AREA_CONN_NORTH) == HUB_AREA_NONE)
    {
        MetatileFill_TreesOverlapping(13, 0, 18, 0, TREE_TYPE_DENSE);
        MetatileFill_TreeStumps(13, 1, 18, TREE_TYPE_DENSE);

        MetatileFill_CommonPathRemoval(14, 2, 17, 8);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_TOWN_SQUARE, HUB_AREA_CONN_EAST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(26, 11);

        MetatileFill_CommonPathRemoval(18, 12, 25, 14);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_TOWN_SQUARE, HUB_AREA_CONN_SOUTH) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitVertical(14, 18);
        MetatileFill_TreeCaps(14, 19, 17);

        MetatileFill_CommonPathRemoval(14, 15, 17, 17);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_TOWN_SQUARE, HUB_AREA_CONN_WEST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(0, 7);

        MetatileFill_CommonPathRemoval(2, 9, 13, 11);
    }
}

static void RogueHub_UpdateChallengeFrontierAreaMetatiles()
{
    // Remove connectionss
    if(RogueHub_GetAreaAtConnection(HUB_AREA_CHALLENGE_FRONTIER, HUB_AREA_CONN_NORTH) == HUB_AREA_NONE)
    {
        MetatileFill_TreesOverlapping(25, 0, 30, 12, TREE_TYPE_DENSE);
        MetatileFill_TreeStumps(25, 13, 30, TREE_TYPE_DENSE);

        MetatileFill_CommonPathRemoval(26, 14, 29, 14);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_CHALLENGE_FRONTIER, HUB_AREA_CONN_EAST) == HUB_AREA_NONE)
    {
        MetatileFill_TreesOverlapping(32, 13, 33, 18, TREE_TYPE_DENSE);

        MetatileFill_CommonPathRemoval(30, 15, 31, 17);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_CHALLENGE_FRONTIER, HUB_AREA_CONN_SOUTH) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitVertical(12, 26);
        MetatileFill_TreeCaps(12, 27, 15);

        MetatileFill_CommonPathRemoval(12, 25, 15, 25);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_CHALLENGE_FRONTIER, HUB_AREA_CONN_WEST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(8, 21);
        MetatileFill_TreesOverlapping(0, 21, 7, 25, TREE_TYPE_DENSE);

        MetatileFill_CommonPathRemoval(10, 22, 11, 24);
    }
    
}

static void RogueHub_UpdateDayCareAreaMetatiles()
{
    // Remove connectionss
    if(RogueHub_GetAreaAtConnection(HUB_AREA_DAY_CARE, HUB_AREA_CONN_NORTH) == HUB_AREA_NONE)
    {
        MetatileFill_TreesOverlapping(17, 0, 22, 0, TREE_TYPE_DENSE);
        MetatileFill_TreeStumps(17, 1, 22, TREE_TYPE_DENSE);

        MetatileFill_CommonPathRemoval(18, 2, 21, 9);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_DAY_CARE, HUB_AREA_CONN_EAST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(36, 9);

        MetatileFill_CommonPathRemoval(22, 10, 35, 12);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_DAY_CARE, HUB_AREA_CONN_SOUTH) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitVertical(18, 24);
        MetatileFill_TreeCaps(18, 25, 21);

        MetatileFill_CommonPathRemoval(18, 15, 21, 23);
    }

    if(RogueHub_GetAreaAtConnection(HUB_AREA_DAY_CARE, HUB_AREA_CONN_WEST) == HUB_AREA_NONE)
    {
        MetatileFill_CommonWarpExitHorizontal(0, 11);

        MetatileFill_CommonPathRemoval(2, 12, 17, 14);
    }
}

// Metatile util functions
//

static void MetatileSet_Tile(u16 x, u16 y, u16 tile)
{
    MapGridSetMetatileIdAt(x + MAP_OFFSET, y + MAP_OFFSET, tile);
}

static void UNUSED MetatileFill_Tile(u16 xStart, u16 yStart, u16 xEnd, u16 yEnd, u16 tile)
{
    u16 x, y;

    for(x = xStart; x <= xEnd; ++x)
    {
        for(y = yStart; y <= yEnd; ++y)
        {
            MapGridSetMetatileIdAt(x + MAP_OFFSET, y + MAP_OFFSET, tile);
        }
    }
}

static void MetatileFill_TreesOverlapping(u16 xStart, u16 yStart, u16 xEnd, u16 yEnd, u8 treeType)
{
    u16 x, y, tile;

    for(x = xStart; x <= xEnd; ++x)
    {
        for(y = yStart; y <= yEnd; ++y)
        {
            if(treeType == TREE_TYPE_DENSE)
            {
                if((y % 2) == 0)
                {
                    tile = (x % 2) == 0 ? METATILE_GeneralHub_Tree_TopLeft_Dense : METATILE_GeneralHub_Tree_TopRight_Dense;
                }
                else
                {
                    tile = (x % 2) == 0 ? METATILE_GeneralHub_Tree_BottomLeft_Dense_Overlapped : METATILE_GeneralHub_Tree_BottomRight_Dense_Overlapped;
                }
            }
            else
            {
                if((y % 2) == 0)
                {
                    tile = (x % 2) == 0 ? METATILE_GeneralHub_Tree_TopLeft_Sparse : METATILE_GeneralHub_Tree_TopRight_Sparse;
                }
                else
                {
                    tile = (x % 2) == 0 ? METATILE_GeneralHub_Tree_BottomLeft_Sparse_Overlapped : METATILE_GeneralHub_Tree_BottomRight_Sparse_Overlapped;
                }
            }

            MapGridSetMetatileIdAt(x + MAP_OFFSET, y + MAP_OFFSET, tile | MAPGRID_COLLISION_MASK);
        }
    }
}

static void MetatileFill_TreeStumps(u16 xStart, u16 yStart, u16 xEnd, u8 treeType)
{
    u16 x, tile;

    for(x = xStart; x <= xEnd; ++x)
    {
        if(treeType == TREE_TYPE_DENSE)
            tile = (x % 2) == 0 ? METATILE_GeneralHub_Tree_BottomLeft_Dense : METATILE_GeneralHub_Tree_BottomRight_Dense;
        else
            tile = (x % 2) == 0 ? METATILE_GeneralHub_Tree_BottomLeft_Sparse : METATILE_GeneralHub_Tree_BottomRight_Sparse;

        MapGridSetMetatileIdAt(x + MAP_OFFSET, yStart + MAP_OFFSET, tile | MAPGRID_COLLISION_MASK);
    }
}

static void MetatileFill_TreeCaps(u16 xStart, u16 yStart, u16 xEnd)
{
    u16 x, tile;

    for(x = xStart; x <= xEnd; ++x)
    {
        tile = (x % 2) == 0 ? METATILE_GeneralHub_Tree_TopLeft_CapGrass : METATILE_GeneralHub_Tree_TopRight_CapGrass;
        MapGridSetMetatileIdAt(x + MAP_OFFSET, yStart + MAP_OFFSET, tile);
    }
}

static void MetatileFill_CommonWarpExit(u16 xStart, u16 yStart, u16 xEnd, u16 yEnd)
{
    u16 x, y;
    u32 tile;

    for(x = xStart; x <= xEnd; ++x)
    {
        for(y = yStart; y <= yEnd; ++y)
        {
            tile = MapGridGetMetatileIdAt(x + MAP_OFFSET, y + MAP_OFFSET);

            switch (tile)
            {
            // Tree tops
            case 0x00E:
            case 0x00F:
            case 0x03E:
            case 0x03F:
            case 0x047:
                MapGridSetMetatileIdAt(x + MAP_OFFSET, y + MAP_OFFSET, METATILE_GeneralHub_Grass);
                break;

            // Tree bottoms
            case 0x016:
            case 0x017:
            case 0x03D:
                MapGridSetMetatileIdAt(x + MAP_OFFSET, y + MAP_OFFSET, METATILE_GeneralHub_Grass);
                break;

            // Tree overlap tree left
            case 0x1F4:
                MapGridSetMetatileIdAt(x + MAP_OFFSET, y + MAP_OFFSET, 0x1CE);
                break;
            // Tree overlap tree right
            case 0x1F5:
                MapGridSetMetatileIdAt(x + MAP_OFFSET, y + MAP_OFFSET, 0x1CF);
                break;

            // Grass warps/shadows
            case METATILE_GeneralHub_GrassWarpNorth:
            case METATILE_GeneralHub_GrassWarpEast:
            case METATILE_GeneralHub_GrassWarpSouth:
            case METATILE_GeneralHub_GrassWarpWest:
            case METATILE_GeneralHub_GrassWarpShadowNorth:
            case METATILE_GeneralHub_GrassWarpShadowEast:
            case METATILE_GeneralHub_GrassWarpShadowSouth:
            case METATILE_GeneralHub_GrassWarpShadowWest:
                MapGridSetMetatileIdAt(x + MAP_OFFSET, y + MAP_OFFSET, METATILE_GeneralHub_Grass);
                break;
            
            default:
                break;
            }
        }
    }
}

static void MetatileFill_CommonWarpExitVertical(u16 xStart, u16 yStart)
{
    MetatileFill_CommonWarpExit(xStart, yStart, xStart + 3, yStart + 1);
}

static void MetatileFill_CommonWarpExitHorizontal(u16 xStart, u16 yStart)
{
    MetatileFill_CommonWarpExit(xStart, yStart, xStart + 1, yStart + 4);
}

static void MetatileFill_CommonPathRemoval(u16 xStart, u16 yStart, u16 xEnd, u16 yEnd)
{
    MetatileFill_Tile(xStart, yStart, xEnd, yEnd, METATILE_General_Grass);
}

static void MetatileFill_BlitMapRegion(u16 mapGroup, u16 mapNum, u16 destX1, u16 destY1, u16 destX2, u16 destY2, u16 srcX1, u16 srcY1)
{
    int i, dx, dy, sx, sy;
    struct MapHeader const * mapHeader = Overworld_GetMapHeaderByGroupAndId(mapGroup, mapNum);

    for(dy = destY1, sy = srcY1; dy <= destY2; ++dy, ++sy)
    {
        for(dx = destX1, sx = srcX1; dx <= destX2; ++dx, ++sx)
        {
            i = sx + mapHeader->mapLayout->width * sy;
            AGB_ASSERT(i < mapHeader->mapLayout->width * mapHeader->mapLayout->height);

            MapGridSetMetatileEntryAt(dx + MAP_OFFSET, dy + MAP_OFFSET, mapHeader->mapLayout->map[i]);
        }
    }
}

void RogueHub_SetRandomFollowMonsFromPC()
{
    // Try get from current box first
    u8 tryCount;
    u16 species;
    u32 checkMask;
    u32 tryMask = 0;
    u8 foundCount = 0;

    // Start with the final box as thats where most players keep their "cool" mons
    u8 boxId = TOTAL_BOXES_COUNT - 1;

    for(tryCount = 0; tryCount < 64; ++tryCount)
    {
        u8 pos = Random() % IN_BOX_COUNT;
        checkMask = (1 << pos);

        // If we've gotten this far, just try to take from the current box
        if(tryCount == 32 && boxId != StorageGetCurrentBox())
        {
            tryMask = 0;
            boxId = StorageGetCurrentBox();
        }

        // If we haven't already tried this pos
        if((tryMask & checkMask) == 0)
        {
            tryMask |= checkMask;
            species = GetBoxMonDataAt(boxId, pos, MON_DATA_SPECIES);

            if(species != SPECIES_NONE)
            {
                FollowMon_SetGraphics(foundCount, species, GetBoxMonDataAt(boxId, pos, MON_DATA_IS_SHINY));
                if(++foundCount >= HOME_AREA_DISPLAY_MONS)
                    break;
            }
        }
    }

    // Fill in the rest of the slots with nothing
    for(; foundCount < HOME_AREA_DISPLAY_MONS; ++foundCount)
    {
        FollowMon_SetGraphics(foundCount, SPECIES_NONE, FALSE);
    }
}

static void BuildAtRandomConnectionFrom(u8 fromArea, u8 buildArea)
{
    while (TRUE)
    {
        u8 dir = Random() % HUB_AREA_CONN_COUNT;
        u8 invDir = InvertConnDirection(dir);

        if(RogueHub_AreaHasFreeConnection(fromArea, dir) && CanAreaConnect(buildArea, invDir))
        {
            struct Coords8 pos;
            pos.x = GetActiveHubMap()->areaCoords[fromArea].x;
            pos.y = GetActiveHubMap()->areaCoords[fromArea].y;
            IncrementCoordsByDirection(&pos, dir);

            // We cannot place the lab next to the safari as it will bork up the tutorial sequence
            if(buildArea == HUB_AREA_TOWN_SQUARE)
            {
                if(
                    RogueHub_FindAreaAtCoord(pos.x + 1, pos.y) == HUB_AREA_SAFARI_ZONE ||
                    RogueHub_FindAreaAtCoord(pos.x - 1, pos.y) == HUB_AREA_SAFARI_ZONE ||
                    RogueHub_FindAreaAtCoord(pos.x, pos.y + 1) == HUB_AREA_SAFARI_ZONE ||
                    RogueHub_FindAreaAtCoord(pos.x, pos.y - 1) == HUB_AREA_SAFARI_ZONE
                )
                    continue;
            }

            RogueHub_BuildArea(buildArea, pos.x, pos.y);
            break;
        }
    }
}

bool8 RogueHub_IsPlayerBaseLayout(u16 layoutId)
{
    return layoutId == LAYOUT_ROGUE_AREA_HOME || layoutId == LAYOUT_ROGUE_INTERIOR_HOME || layoutId == LAYOUT_ROGUE_INTERIOR_HOME_UPPER;
}

void RogueHub_ModifyPlayerBaseObjectEvents(u16 layoutId, bool8 loadingFromSave, struct ObjectEventTemplate *objectEvents, u8* objectEventCount, u8 objectEventCapacity)
{

}

extern u8 const Rogue_Area_Home_InteractWithWorkbench[];
extern u8 const Rogue_Area_Home_DecorateTile[];
extern u8 const Rogue_Area_Home_ChooseDecoration[];

bool8 IsCoordInHomeRegion(s32 x, s32 y, u8 region)
{
    return (
        x >= sHomeRegionCoords[region].xStart &&
        x <= sHomeRegionCoords[region].xEnd &&
        y >= sHomeRegionCoords[region].yStart &&
        y <= sHomeRegionCoords[region].yEnd
    );
}

const u8* RogueHub_GetDecoratingScriptFor(u16 layoutId, struct MapPosition *position, u16 metatileBehavior, u8 direction, u8 const* existingScript)
{
    // Always buffer coordinates so scripts known where we're interacting
    gSpecialVar_0x800A = position->x - MAP_OFFSET;
    gSpecialVar_0x800B = position->y - MAP_OFFSET;

    if(existingScript == Rogue_Area_Home_InteractWithWorkbench)
    {
        return existingScript;
    }

    //if(layoutId == LAYOUT_ROGUE_AREA_HOME)
    //{
    //    u8 i;
//
    //    for(i = 0; i < HOME_REGION_COUNT; ++i)
    //    {
    //        if(
    //            position->x - MAP_OFFSET >= sHomeRegionCoords[i].xStart &&
    //            position->x - MAP_OFFSET <= sHomeRegionCoords[i].xEnd &&
    //            position->y - MAP_OFFSET >= sHomeRegionCoords[i].yStart &&
    //            position->y - MAP_OFFSET <= sHomeRegionCoords[i].yEnd
    //        )
    //            break;
    //    }
//
    //    gSpecialVar_0x8004 = i;
    //}
    //else
    //{
    //    gSpecialVar_0x8004 = HOME_REGION_COUNT;
    //}

    return Rogue_Area_Home_ChooseDecoration;

    gSpecialVar_0x800A = position->x - MAP_OFFSET;
    gSpecialVar_0x800B = position->y - MAP_OFFSET;

    return Rogue_Area_Home_DecorateTile;
}

static void UpdatePlaceCoords(u8* placeX, u8* placeY, u8 decorId)
{
    u8 faceDir = GetPlayerFacingDirection();
    u8 width = 1 + (sHomeDecorEnvRegions[decorId].xEnd - sHomeDecorEnvRegions[decorId].xStart);
    u8 height = 1 + (sHomeDecorEnvRegions[decorId].yEnd - sHomeDecorEnvRegions[decorId].yStart);

    // Coords are auto place from top left corner, so compensate for that and attempt to place in middle
    switch (faceDir)
    {
    case DIR_NORTH:
        *placeX -= width / 2;
        *placeY -= height - 1;
        break;
    
    case DIR_EAST:
        *placeY -= height / 2;
        break;

    case DIR_SOUTH:
        *placeX -= width / 2;
        break;

    case DIR_WEST:
        *placeX -= width - 1;
        *placeY -= height / 2;
        break;
    }
}

u8 RogueHub_PlaceHomeDecor(u8 decorId)
{
    // TODO - Detect what area and what type player is
    u8 i;
    struct RogueHubMap* hubMap = &gRogueSaveBlock->hubMap;
    u8 placeX = gSpecialVar_0x800A;
    u8 placeY = gSpecialVar_0x800B;

    AGB_ASSERT(decorId < HOME_DECOR_ENV_COUNT);

    if(IsCoordInHomeRegion(placeX, placeY, HOME_REGION_PLACEABLE_REGION) && !IsCoordInHomeRegion(placeX, placeY, HOME_REGION_HOUSE))
    {
        UpdatePlaceCoords(&placeX, &placeY, decorId);

        for(i = 0; i < HOME_DECOR_OUTSIDE_ENV_COUNT; ++i)
        {
            struct RogueHubDecoration* decor = &hubMap->homeDecorations[HOME_DECOR_OUTSIDE_ENV_OFFSET + i];
            if(!decor->active)
            {
                decor->x = placeX;
                decor->y = placeY;
                decor->decorId = decorId;
                decor->active = TRUE;

                RogueHub_PlaceHomeEnvironmentDecorations();
                return HOME_DECOR_OUTSIDE_ENV_OFFSET + i;
            }
        }
    
        return HOME_DECOR_CODE_NO_ROOM;
    }

    return HOME_DECOR_CODE_NOT_HERE;
}

void RogueHub_RemoveHomeDecor(u8 index)
{
    AGB_ASSERT(index < HOME_DECOR_TOTAL_COUNT);
    gRogueSaveBlock->hubMap.homeDecorations[index].active = FALSE;
    RogueHub_PlaceHomeEnvironmentDecorations();
}

static u32 GetCurrentAreaMetatileAt(s32 x, s32 y)
{
    u32 metaTile = MapGridGetMetatileIdAt(x + MAP_OFFSET, y + MAP_OFFSET);
    
    // Player home cannot auto join to player house
    if(gMapHeader.mapLayoutId == LAYOUT_ROGUE_AREA_HOME)
    {
        if(IsCoordInHomeRegion(x, y, HOME_REGION_HOUSE))
        {
            // Override these tiles to avoid fixup odd stuff
            metaTile = 0;
        }
    }

    return metaTile;
}

static bool8 IsCompatibleMetatile(u32 classTile,  u32 checkTile)
{
    if(classTile == METATILE_GeneralHub_Pond_Centre)
    {
        return (
            checkTile == METATILE_GeneralHub_Pond_Centre ||
            checkTile == METATILE_GeneralHub_Pond_Conn_EastWest_South ||
            checkTile == METATILE_GeneralHub_Pond_Conn_NorthEast ||
            checkTile == METATILE_GeneralHub_Pond_Conn_NorthSouth_East ||
            checkTile == METATILE_GeneralHub_Pond_Conn_NorthSouth_West ||
            checkTile == METATILE_GeneralHub_Pond_Conn_NorthWest ||
            checkTile == METATILE_GeneralHub_Pond_Conn_SouthEast ||
            checkTile == METATILE_GeneralHub_Pond_Conn_SouthWest
        );
    }
    else if(classTile == METATILE_GeneralHub_GrassPath_Centre)
    {
        return (
            checkTile == METATILE_GeneralHub_GrassPath_Centre ||
            checkTile == METATILE_GeneralHub_GrassPath_Conn_EastWest_North ||
            checkTile == METATILE_GeneralHub_GrassPath_Conn_EastWest_South ||
            checkTile == METATILE_GeneralHub_GrassPath_Conn_NorthEast ||
            checkTile == METATILE_GeneralHub_GrassPath_Conn_NorthSouth_East ||
            checkTile == METATILE_GeneralHub_GrassPath_Conn_NorthSouth_West ||
            checkTile == METATILE_GeneralHub_GrassPath_Conn_NorthWest ||
            checkTile == METATILE_GeneralHub_GrassPath_Conn_SouthEast ||
            checkTile == METATILE_GeneralHub_GrassPath_Conn_SouthWest
        );
    }
    else if(classTile == METATILE_GeneralHub_Mountain_Centre)
    {
        return (
            checkTile == METATILE_GeneralHub_Mountain_Centre ||
            checkTile == METATILE_GeneralHub_Mountain_Conn_EastWest_North ||
            checkTile == METATILE_GeneralHub_Mountain_Conn_EastWest_South ||
            checkTile == METATILE_GeneralHub_Mountain_Conn_NorthEast ||
            checkTile == METATILE_GeneralHub_Mountain_Conn_NorthSouth_East ||
            checkTile == METATILE_GeneralHub_Mountain_Conn_NorthSouth_West ||
            checkTile == METATILE_GeneralHub_Mountain_Conn_NorthWest ||
            checkTile == METATILE_GeneralHub_Mountain_Conn_SouthEast ||
            checkTile == METATILE_GeneralHub_Mountain_Conn_SouthEast_Inside ||
            checkTile == METATILE_GeneralHub_Mountain_Conn_SouthWest ||
            checkTile == METATILE_GeneralHub_Mountain_Conn_SouthWest_Inside ||
            checkTile == METATILE_GeneralHub_MountainRaised_Conn_EastWest_South
        );
    }

    return classTile == checkTile;
}

static bool8 IsCompatibleMetatileAt(s32 x, s32 y, u32 classTile)
{
    return IsCompatibleMetatile(classTile, GetCurrentAreaMetatileAt(x, y));
}

// Pond
//

static void FixupTile_Pond_Horizontal(s32 x, s32 y)
{
    bool8 west = IsCompatibleMetatileAt(x - 1, y + 0, METATILE_GeneralHub_Pond_Centre);
    bool8 east = IsCompatibleMetatileAt(x + 1, y + 0, METATILE_GeneralHub_Pond_Centre);

    if(!west && east)
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Pond_Conn_NorthSouth_East);
    }
    else if(west && !east)
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Pond_Conn_NorthSouth_West);
    }
}

static void FixupTile_Pond_Vertical(s32 x, s32 y)
{
    bool8 north = IsCompatibleMetatileAt(x + 0, y - 1, METATILE_GeneralHub_Pond_Centre);
    bool8 south = IsCompatibleMetatileAt(x + 0, y + 1, METATILE_GeneralHub_Pond_Centre);

    if(!north && south)
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Pond_Conn_EastWest_South);
    }
    else if(!north && !south)
    {
        // Also placed if nothing above or below
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Pond_Conn_EastWest_South);
    }
}

static void FixupTile_Pond_Fixup(s32 x, s32 y, u32 centreTile)
{
    u32 northTile = GetCurrentAreaMetatileAt(x + 0, y - 1);
    u32 eastTile =  GetCurrentAreaMetatileAt(x + 1, y + 0);
    u32 southTile = GetCurrentAreaMetatileAt(x + 0, y + 1);
    u32 westTile =  GetCurrentAreaMetatileAt(x - 1, y + 0);

    if(centreTile == METATILE_GeneralHub_Pond_Centre)
    {
        // Inside Corners
        if(
            IsCompatibleMetatile(METATILE_GeneralHub_Pond_Centre, northTile) &&
            IsCompatibleMetatile(METATILE_GeneralHub_Pond_Centre, eastTile) &&
            IsCompatibleMetatile(METATILE_GeneralHub_Pond_Centre, westTile)
        )
        {
            if(!IsCompatibleMetatileAt(x - 1, y - 1, METATILE_GeneralHub_Pond_Centre))
                MetatileSet_Tile(x, y, METATILE_GeneralHub_Pond_Conn_NorthWest);
            else if(!IsCompatibleMetatileAt(x + 1, y - 1, METATILE_GeneralHub_Pond_Centre))
                MetatileSet_Tile(x, y, METATILE_GeneralHub_Pond_Conn_NorthEast);
        }
    }
    // Outside Corners
    else if(!IsCompatibleMetatile(METATILE_GeneralHub_Pond_Centre, northTile) && !IsCompatibleMetatile(METATILE_GeneralHub_Pond_Centre, westTile))
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Pond_Conn_SouthEast);
    }
    else if(!IsCompatibleMetatile(METATILE_GeneralHub_Pond_Centre, northTile) && !IsCompatibleMetatile(METATILE_GeneralHub_Pond_Centre, eastTile))
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Pond_Conn_SouthWest);
    }

    // Fix up grass
    if(southTile == METATILE_GeneralHub_Grass)
        MetatileSet_Tile(x, y + 1, METATILE_GeneralHub_Grass_NorthDrop);
}

// Grass Path
//

static void FixupTile_GrassPath_Horizontal(s32 x, s32 y)
{
    bool8 west = IsCompatibleMetatileAt(x - 1, y + 0, METATILE_GeneralHub_GrassPath_Centre);
    bool8 east = IsCompatibleMetatileAt(x + 1, y + 0, METATILE_GeneralHub_GrassPath_Centre);

    if(!west && east)
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_GrassPath_Conn_NorthSouth_East);
    }
    else if(west && !east)
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_GrassPath_Conn_NorthSouth_West);
    }
}

static void FixupTile_GrassPath_Vertical(s32 x, s32 y)
{
    bool8 north = IsCompatibleMetatileAt(x + 0, y - 1, METATILE_GeneralHub_GrassPath_Centre);
    bool8 south = IsCompatibleMetatileAt(x + 0, y + 1, METATILE_GeneralHub_GrassPath_Centre);

    if(!north && south)
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_GrassPath_Conn_EastWest_South);
    }
    else if(north && !south)
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_GrassPath_Conn_EastWest_North);
    }
}

static void FixupTile_GrassPath_Fixup(s32 x, s32 y, u32 centreTile)
{
    u32 northTile = GetCurrentAreaMetatileAt(x + 0, y - 1);
    u32 eastTile =  GetCurrentAreaMetatileAt(x + 1, y + 0);
    u32 southTile = GetCurrentAreaMetatileAt(x + 0, y + 1);
    u32 westTile =  GetCurrentAreaMetatileAt(x - 1, y + 0);

    // Outside Corners (North)
    if(!IsCompatibleMetatile(METATILE_GeneralHub_GrassPath_Centre, northTile) && !IsCompatibleMetatile(METATILE_GeneralHub_GrassPath_Centre, westTile))
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_GrassPath_Conn_SouthEast);
    }
    else if(!IsCompatibleMetatile(METATILE_GeneralHub_GrassPath_Centre, northTile) && !IsCompatibleMetatile(METATILE_GeneralHub_GrassPath_Centre, eastTile))
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_GrassPath_Conn_SouthWest);
    }
    // Outside Corners (South)
    if(!IsCompatibleMetatile(METATILE_GeneralHub_GrassPath_Centre, southTile) && !IsCompatibleMetatile(METATILE_GeneralHub_GrassPath_Centre, westTile))
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_GrassPath_Conn_NorthEast);
    }
    else if(!IsCompatibleMetatile(METATILE_GeneralHub_GrassPath_Centre, southTile) && !IsCompatibleMetatile(METATILE_GeneralHub_GrassPath_Centre, eastTile))
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_GrassPath_Conn_NorthWest);
    }
}

// Mountain Path
//

static void FixupTile_Mountain_Horizontal(s32 x, s32 y)
{
    bool8 west = IsCompatibleMetatileAt(x - 1, y + 0, METATILE_GeneralHub_Mountain_Centre);
    bool8 east = IsCompatibleMetatileAt(x + 1, y + 0, METATILE_GeneralHub_Mountain_Centre);

    if(!west && east)
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Mountain_Conn_NorthSouth_East | MAPGRID_COLLISION_MASK);
    }
    else if(west && !east)
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Mountain_Conn_NorthSouth_West | MAPGRID_COLLISION_MASK);
    }
}

static void FixupTile_Mountain_Vertical(s32 x, s32 y)
{
    bool8 north = IsCompatibleMetatileAt(x + 0, y - 1, METATILE_GeneralHub_Mountain_Centre);
    bool8 south = IsCompatibleMetatileAt(x + 0, y + 1, METATILE_GeneralHub_Mountain_Centre);

    if(!north && south)
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Mountain_Conn_EastWest_South | MAPGRID_COLLISION_MASK);
    }
    else if(north && !south)
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Mountain_Conn_EastWest_North | MAPGRID_COLLISION_MASK);
    }
}

static void FixupTile_Mountain_Fixup(s32 x, s32 y, u32 centreTile)
{
    u32 northTile = GetCurrentAreaMetatileAt(x + 0, y - 1);
    u32 eastTile =  GetCurrentAreaMetatileAt(x + 1, y + 0);
    u32 southTile = GetCurrentAreaMetatileAt(x + 0, y + 1);
    u32 westTile =  GetCurrentAreaMetatileAt(x - 1, y + 0);

    // Outside Corners (North)
    if(!IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, northTile) && !IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, westTile))
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Mountain_Conn_SouthEast | MAPGRID_COLLISION_MASK);
    }
    else if(!IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, northTile) && !IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, eastTile))
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Mountain_Conn_SouthWest | MAPGRID_COLLISION_MASK);
    }
    // Outside Corners (South)
    if(!IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, southTile) && !IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, westTile))
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Mountain_Conn_NorthEast | MAPGRID_COLLISION_MASK);
    }
    else if(!IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, southTile) && !IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, eastTile))
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Mountain_Conn_NorthWest | MAPGRID_COLLISION_MASK);
    }
    // Inside Corners (Noth)
    else if(centreTile == METATILE_GeneralHub_Mountain_Centre && IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, westTile) && IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, northTile) && !IsCompatibleMetatileAt(x - 1, y - 1, METATILE_GeneralHub_Mountain_Centre))
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_MountainRaised_Conn_EastWest_South);
    }
    else if(centreTile == METATILE_GeneralHub_Mountain_Centre && IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, eastTile) && IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, northTile) && !IsCompatibleMetatileAt(x + 1, y - 1, METATILE_GeneralHub_Mountain_Centre))
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_MountainRaised_Conn_EastWest_South);
    }
    // Inside Corners (South)
    else if(centreTile == METATILE_GeneralHub_Mountain_Centre && IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, westTile) && IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, southTile) && !IsCompatibleMetatileAt(x - 1, y + 1, METATILE_GeneralHub_Mountain_Centre))
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Mountain_Conn_SouthWest_Inside | MAPGRID_COLLISION_MASK);
    }
    else if(centreTile == METATILE_GeneralHub_Mountain_Centre && IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, eastTile) && IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, southTile) && !IsCompatibleMetatileAt(x + 1, y + 1, METATILE_GeneralHub_Mountain_Centre))
    {
        MetatileSet_Tile(x, y, METATILE_GeneralHub_Mountain_Conn_SouthEast_Inside | MAPGRID_COLLISION_MASK);
    }
}

static void FixupTileCommon(struct TileFixup* settings)
{
    s32 fromX, fromY, toX, toY;
    u8 x, y;
    u16 metatileId;
    bool8 ignoreHouseTiles = FALSE;

    switch (gMapHeader.mapLayoutId)
    {
    case LAYOUT_ROGUE_AREA_HOME:
        ignoreHouseTiles = TRUE;
        fromX = sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].xStart;
        fromY = sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].yStart;
        toX = sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].xEnd;
        toY = sHomeRegionCoords[HOME_REGION_PLACEABLE_REGION].yEnd;
        break;

    default:
        {
            struct MapHeader const * mapHeader = Overworld_GetMapHeaderByGroupAndId(gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum);
            fromX = 2;
            fromY = 2;
            toX = mapHeader->mapLayout->width - 2;
            toY = mapHeader->mapLayout->height - 1;
        }
        break;
    }

    // 3 pass system, horizontal, vertical + fixup

    // Horizontal
    for(y = fromY; y <= toY; ++y)
    {
        for(x = fromX; x <= toX; ++x)
        {
            // Don't adjust home tiles 
            if(ignoreHouseTiles && IsCoordInHomeRegion(x, y, HOME_REGION_HOUSE))
                continue;

            metatileId = MapGridGetMetatileIdAt(x + MAP_OFFSET, y + MAP_OFFSET);

            if(settings->pond && metatileId == METATILE_GeneralHub_Pond_Centre)
                FixupTile_Pond_Horizontal(x, y);

            else if(settings->path && metatileId == METATILE_GeneralHub_GrassPath_Centre)
                FixupTile_GrassPath_Horizontal(x, y);

            else if(settings->mountain && metatileId == METATILE_GeneralHub_Mountain_Centre)
                FixupTile_Mountain_Horizontal(x, y);
        }
    }

    // Vertical + Fixup
    for(x = fromX; x <= toX; ++x)
    {
        for(y = fromY; y <= toY; ++y)
        {
            // Don't adjust home tiles 
            if(ignoreHouseTiles && IsCoordInHomeRegion(x, y, HOME_REGION_HOUSE))
                continue;

            metatileId = MapGridGetMetatileIdAt(x + MAP_OFFSET, y + MAP_OFFSET);

            if(settings->pond)
            {
                if(metatileId == METATILE_GeneralHub_Pond_Centre)
                    FixupTile_Pond_Vertical(x, y);

                if(IsCompatibleMetatile(METATILE_GeneralHub_Pond_Centre, metatileId))
                    FixupTile_Pond_Fixup(x, y, metatileId);
            }

            if(settings->path)
            {
                if(metatileId == METATILE_GeneralHub_GrassPath_Centre)
                    FixupTile_GrassPath_Vertical(x, y);

                if(IsCompatibleMetatile(METATILE_GeneralHub_GrassPath_Centre, metatileId))
                    FixupTile_GrassPath_Fixup(x, y, metatileId);
            }

            if(settings->mountain)
            {
                if(metatileId == METATILE_GeneralHub_Mountain_Centre)
                    FixupTile_Mountain_Vertical(x, y);

                if(IsCompatibleMetatile(METATILE_GeneralHub_Mountain_Centre, metatileId))
                    FixupTile_Mountain_Fixup(x, y, metatileId);
            }
        }
    }
}