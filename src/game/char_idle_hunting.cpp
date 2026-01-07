#include "stdafx.h"
#include "char.h"
#include "db.h"
#include "config.h"
#include "mob_manager.h"
#include "item_manager.h"
#include "item.h"
#include "desc.h"
#include "log.h"
#include "questmanager.h"

// Configuration
static const DWORD MAX_DAILY_SECONDS = 28800; // 8 hours
static const int BASE_KILLS_PER_HOUR = 100;
static const float EXP_RATE_MODIFIER = 1.0f; // 100% of normal exp
static const float YANG_RATE_MODIFIER = 0.5f; // 50% of normal yang

void CHARACTER::LoadIdleHunting()
{
    if (!IsPC())
        return;

    std::unique_ptr<SQLMsg> pMsg(DBManager::instance().DirectQuery(
        "SELECT mob_vnum, start_time, last_claim_time, total_time_today, last_reset_date, is_active, max_daily_seconds "
        "FROM idle_hunting WHERE pid = %u", 
        GetPlayerID()));
    
    if (pMsg->Get()->uiNumRows > 0)
    {
        MYSQL_ROW row = mysql_fetch_row(pMsg->Get()->pSQLResult);
        
        m_idleHunting.mobVnum = strtoul(row[0], nullptr, 10);
        m_idleHunting.startTime = strtoul(row[1], nullptr, 10);
        m_idleHunting.lastClaimTime = strtoul(row[2], nullptr, 10);
        m_idleHunting.totalTimeToday = strtoul(row[3], nullptr, 10);
        m_idleHunting.isActive = atoi(row[5]);
        m_idleHunting.maxDailySeconds = row[6] ? strtoul(row[6], nullptr, 10) : MAX_DAILY_SECONDS;
        
        if (row[4])
            strncpy(m_idleHunting.lastResetDate, row[4], sizeof(m_idleHunting.lastResetDate) - 1);
        
        m_idleHunting.isActive = atoi(row[5]);
        
        // If hunt was active (is_active=1), player logged back in, set to "ready to claim" state
        if (m_idleHunting.isActive == 1)
        {
            const CMob* mobData = CMobManager::instance().Get(m_idleHunting.mobVnum);
            if (!mobData)
            {
                sys_err("Idle Hunting: Invalid mob vnum %u for player %u", m_idleHunting.mobVnum, GetPlayerID());
                m_idleHunting.mobVnum = 0;
                m_idleHunting.isActive = 0;
                SaveIdleHunting();
                return;
            }
            
            // Mark as ready to claim (state 2)
            m_idleHunting.isActive = 2;
            SaveIdleHunting();
            
            sys_log(0, "IDLE_HUNT: Player %u logged in with active hunt, marked as ready to claim", GetPlayerID());
            ChatPacket(CHAT_TYPE_INFO, "Your idle hunt is complete! Talk to the NPC to claim rewards.");
        }
        else if (m_idleHunting.isActive == 2)
        {
            // Player already has unclaimed rewards from a previous login
            sys_log(0, "IDLE_HUNT: Player %u logged in with unclaimed rewards", GetPlayerID());
            ChatPacket(CHAT_TYPE_INFO, "You have unclaimed idle hunt rewards! Talk to the NPC to claim them.");
        }
    }
}

void CHARACTER::StartIdleHunting(DWORD mobVnum)
{
    sys_log(0, "IDLE_HUNT: StartIdleHunting called for player %u, mob %u", GetPlayerID(), mobVnum);
    
    if (!IsPC())
    {
        sys_log(0, "IDLE_HUNT: Not a PC, returning");
        return;
    }

    // Validate mob exists
    const CMob* mobData = CMobManager::instance().Get(mobVnum);
    if (!mobData)
    {
        sys_log(0, "IDLE_HUNT: Mob %u not found in CMobManager!", mobVnum);
        ChatPacket(CHAT_TYPE_INFO, "Invalid monster selection!");
        sys_err("Idle Hunting: Player %u tried to hunt invalid mob %u", GetPlayerID(), mobVnum);
        return;
    }
    
    sys_log(0, "IDLE_HUNT: Mob validated: %s (level %d)", mobData->m_table.szLocaleName, mobData->m_table.bLevel);

    // Check if hunt is pending, active, or has unclaimed rewards
    if (m_idleHunting.mobVnum > 0)
    {
        sys_log(0, "IDLE_HUNT: Hunt already configured (mob=%u, is_active=%d), returning", m_idleHunting.mobVnum, m_idleHunting.isActive);
        if (m_idleHunting.isActive == 0)
            ChatPacket(CHAT_TYPE_INFO, "You already configured a hunt. Log out to start, or cancel it first!");
        else if (m_idleHunting.isActive == 1)
            ChatPacket(CHAT_TYPE_INFO, "Idle hunting is already in progress!");
        else if (m_idleHunting.isActive == 2)
            ChatPacket(CHAT_TYPE_INFO, "You have unclaimed rewards! Claim them first before starting a new hunt.");
        return;
    }

    // Check daily limit before starting
    time_t now = time(0);
    struct tm* timeinfo = localtime(&now);
    if (!timeinfo)
    {
        sys_err("localtime failed for player %u", GetPlayerID());
        ChatPacket(CHAT_TYPE_INFO, "System error - please try again.");
        return;
    }
    
    char currentDate[11];
    strftime(currentDate, sizeof(currentDate), "%Y-%m-%d", timeinfo);
    
    // Reset counter if new day
    if (strcmp(currentDate, m_idleHunting.lastResetDate) != 0)
    {
        m_idleHunting.totalTimeToday = 0;
    }
    
    if (m_idleHunting.totalTimeToday >= m_idleHunting.maxDailySeconds)
    {
        int maxHours = m_idleHunting.maxDailySeconds / 3600;
        ChatPacket(CHAT_TYPE_INFO, "You've reached the %d-hour daily limit!", maxHours);
        return;
    }

    sys_log(0, "IDLE_HUNT: Configuring idle hunt for player %u, mob %u (pending logout)", GetPlayerID(), mobVnum);
    
    // Set to pending state - hunt won't start until player logs out
    m_idleHunting.mobVnum = mobVnum;
    m_idleHunting.startTime = 0; // Will be set on logout
    m_idleHunting.lastClaimTime = 0;
    m_idleHunting.isActive = 0; // Pending state - not active yet
    strncpy(m_idleHunting.lastResetDate, currentDate, sizeof(m_idleHunting.lastResetDate) - 1);
    m_idleHunting.lastResetDate[sizeof(m_idleHunting.lastResetDate) - 1] = '\0';
    
    sys_log(0, "IDLE_HUNT: Saving pending hunt configuration for player %u", GetPlayerID());
    SaveIdleHunting();
    sys_log(0, "IDLE_HUNT: SaveIdleHunting completed for player %u", GetPlayerID());
    
    ChatPacket(CHAT_TYPE_INFO, "Idle hunt configured. Log out to start hunting %s!", mobData->m_table.szLocaleName);
    
    char szHint[128];
    snprintf(szHint, sizeof(szHint), "mob_vnum %u level %d (pending)", mobVnum, mobData->m_table.bLevel);
    LogManager::instance().CharLog(this, 0, "IDLE_HUNT_CONFIG", szHint);
}

void CHARACTER::StopIdleHunting()
{
    if (m_idleHunting.mobVnum == 0)
        return;

    char szHint[128];
    snprintf(szHint, sizeof(szHint), "mob_vnum %u time_used %u state %d", m_idleHunting.mobVnum, m_idleHunting.totalTimeToday, m_idleHunting.isActive);
    LogManager::instance().CharLog(this, 0, "IDLE_HUNT_STOP", szHint);
    
    m_idleHunting.mobVnum = 0;
    m_idleHunting.isActive = 0;
    m_idleHunting.startTime = 0;
    m_idleHunting.lastClaimTime = 0;
    ChatPacket(CHAT_TYPE_INFO, "Idle hunting has been cancelled.");
    
    SaveIdleHunting();
}

void CHARACTER::CalculateIdleRewards()
{
    // Only calculate rewards if in "ready to claim" state (2)
    if (m_idleHunting.isActive != 2)
    {
        ChatPacket(CHAT_TYPE_INFO, "No idle hunt rewards to claim!");
        return;
    }

    DWORD currentTime = static_cast<DWORD>(time(0));
    
    // Validate time hasn't gone backwards
    if (currentTime < m_idleHunting.startTime)
    {
        sys_err("Idle Hunting: Time went backwards for player %u", GetPlayerID());
        StopIdleHunting();
        return;
    }
    
    DWORD elapsedSeconds = currentTime - m_idleHunting.startTime;
    
    // Max 24 hours offline (sanity check)
    if (elapsedSeconds > 86400)
    {
        sys_err("Idle Hunting: Suspicious elapsed time %u for player %u", elapsedSeconds, GetPlayerID());
        elapsedSeconds = 86400;
    }
    
    // Check daily limit
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char currentDate[11];
    strftime(currentDate, sizeof(currentDate), "%Y-%m-%d", timeinfo);
    
    // Reset counter if new day
    if (strcmp(currentDate, m_idleHunting.lastResetDate) != 0)
    {
        m_idleHunting.totalTimeToday = 0;
        strncpy(m_idleHunting.lastResetDate, currentDate, sizeof(m_idleHunting.lastResetDate) - 1);
    }
    
    // Calculate remaining time
    DWORD remainingTime = m_idleHunting.maxDailySeconds - m_idleHunting.totalTimeToday;
    
    if (remainingTime <= 0)
    {
        int maxHours = m_idleHunting.maxDailySeconds / 3600;
        ChatPacket(CHAT_TYPE_INFO, "You've reached the %d-hour daily limit for idle hunting!", maxHours);
        StopIdleHunting();
        return;
    }
    
    // Cap elapsed time to remaining daily limit
    if (elapsedSeconds > remainingTime)
    {
        elapsedSeconds = remainingTime;
        int maxHours = m_idleHunting.maxDailySeconds / 3600;
        ChatPacket(CHAT_TYPE_INFO, "Daily %d-hour limit reached! Collecting partial rewards.", maxHours);
    }
    
    // Update daily time counter
    m_idleHunting.totalTimeToday += elapsedSeconds;
    
    // Get mob data
    const CMob* mobData = CMobManager::instance().Get(m_idleHunting.mobVnum);
    if (!mobData)
    {
        sys_err("Idle Hunting: Invalid mob %u for player %u", m_idleHunting.mobVnum, GetPlayerID());
        StopIdleHunting();
        return;
    }
    
    // Calculate efficiency based on level difference
    int levelDiff = GetLevel() - mobData->m_table.bLevel;
    float efficiencyRate = 1.0f;
    
    if (levelDiff > 10)
        efficiencyRate = 0.1f; // 90% penalty if 10+ levels above
    else if (levelDiff > 5)
        efficiencyRate = 0.5f; // 50% penalty if 5-10 levels above
    else if (levelDiff < -10)
        efficiencyRate = 0.2f; // 80% penalty if 10+ levels below
    else if (levelDiff < -5)
        efficiencyRate = 0.4f; // 60% penalty if 5-10 levels below
    
    // Calculate kills
    float actualKillsPerHour = BASE_KILLS_PER_HOUR * efficiencyRate;
    int totalKills = static_cast<int>((elapsedSeconds / 3600.0f) * actualKillsPerHour);
    
    // Sanity check on kills
    if (totalKills < 0)
        totalKills = 0;
    if (totalKills > 10000) // Max 10k kills per session
    {
        sys_err("Idle Hunting: Suspicious kill count %d for player %u", totalKills, GetPlayerID());
        totalKills = 10000;
    }
    
    // Calculate EXP
    QWORD expPerKill = mobData->m_table.dwExp;
    QWORD totalExp = static_cast<QWORD>(totalKills * expPerKill * EXP_RATE_MODIFIER);
    
    // Cap total exp
    if (totalExp > 2000000000) // 2 billion cap
    {
        sys_err("Idle Hunting: Suspicious exp %llu for player %u", totalExp, GetPlayerID());
        totalExp = 2000000000;
    }
    
    // Apply exp
    if (totalExp > 0)
    {
        PointChange(POINT_EXP, static_cast<int>(totalExp));
    }
    
    // Generate items and yang
    GenerateIdleHuntingDrops(m_idleHunting.mobVnum, totalKills);
    
    // Log claim
    const CMob* claimMobData = CMobManager::instance().Get(m_idleHunting.mobVnum);
    const char* mobName = claimMobData ? claimMobData->m_table.szLocaleName : "Unknown";
    char szHint[256];
    snprintf(szHint, sizeof(szHint), "mob_vnum %u (%s) kills %d exp %lu time %us", m_idleHunting.mobVnum, mobName, totalKills, totalExp, elapsedSeconds);
    LogManager::instance().CharLog(this, 0, "IDLE_HUNT_CLAIM", szHint);
    
    // Format time for display
    int hours = elapsedSeconds / 3600;
    int minutes = (elapsedSeconds % 3600) / 60;
    int remainingHours = (m_idleHunting.maxDailySeconds - m_idleHunting.totalTimeToday) / 3600;
    int remainingMinutes = ((m_idleHunting.maxDailySeconds - m_idleHunting.totalTimeToday) % 3600) / 60;
    
    ChatPacket(CHAT_TYPE_INFO, "=== Idle Hunting Results ===");
    ChatPacket(CHAT_TYPE_INFO, "Time hunted: %dh %dm", hours, minutes);
    ChatPacket(CHAT_TYPE_INFO, "Killed: %d mobs", totalKills);
    ChatPacket(CHAT_TYPE_INFO, "Gained: %llu experience", totalExp);
    ChatPacket(CHAT_TYPE_INFO, "Daily time remaining: %dh %dm", remainingHours, remainingMinutes);
    
    // Update last claim time
    m_idleHunting.lastClaimTime = currentTime;
    
    // Automatically stop idle hunting
    StopIdleHunting();
}

void CHARACTER::GenerateIdleHuntingDrops(DWORD mobVnum, int killCount)
{
    if (killCount <= 0)
        return;

    const CMob* dropMobData = CMobManager::instance().Get(mobVnum);
    if (!dropMobData)
        return;
    
    // Gold drops (reduced rate for idle)
    int goldMin = dropMobData->m_table.dwGoldMin;
    int goldMax = dropMobData->m_table.dwGoldMax;
    DWORD totalGold = 0;
    
    for (int i = 0; i < killCount; i++)
    {
        if (number(1, 100) <= 30) // 30% gold drop chance when idle
        {
            totalGold += number(goldMin, goldMax);
        }
    }
    
    // Apply yang rate modifier
    totalGold = static_cast<DWORD>(totalGold * YANG_RATE_MODIFIER);
    
    // Cap yang
    if (totalGold > 2000000000) // 2 billion cap
    {
        sys_err("Idle Hunting: Suspicious yang %u for player %u", totalGold, GetPlayerID());
        totalGold = 2000000000;
    }
    
    if (totalGold > 0)
    {
        PointChange(POINT_GOLD, totalGold);
        ChatPacket(CHAT_TYPE_INFO, "Earned %u yang from idle hunting!", totalGold);
        
        char szYangHint[128];
        snprintf(szYangHint, sizeof(szYangHint), "yang_earned %u kills %d", totalGold, killCount);
        LogManager::instance().CharLog(this, 0, "IDLE_HUNT_YANG", szYangHint);
    }
    
    // Use actual mob drop tables from mob_drop_item.txt
    int itemsDropped = 0;
    std::map<DWORD, int> collectedItems; // Track items to batch create
    
    // Process "kill" type drops (mob_drop_item.txt kill drops)
    CMobItemGroup* pMobItemGroup = ITEM_MANAGER::instance().GetMobItemGroup(mobVnum);
    if (pMobItemGroup && !pMobItemGroup->IsEmpty())
    {
        int killPerDrop = pMobItemGroup->GetKillPerDrop();
        if (killPerDrop > 0)
        {
            // Calculate drops based on kill count with reduced rate for idle
            for (int i = 0; i < killCount; i++)
            {
                // 50% of normal drop rate for idle hunting (multiply by 2 instead of 10)
                if (number(1, killPerDrop * 2) == 1)
                {
                    const CMobItemGroup::SMobItemGroupInfo& info = pMobItemGroup->GetOne();
                    if (info.dwItemVnum > 0)
                    {
                        collectedItems[info.dwItemVnum] += info.iCount;
                    }
                }
            }
        }
    }
    
    // Process "drop" type drops (mob_drop_item.txt drop items)
    CDropItemGroup* pDropItemGroup = ITEM_MANAGER::instance().GetDropItemGroup(mobVnum);
    if (pDropItemGroup)
    {
        const std::vector<CDropItemGroup::SDropItemGroupInfo>& vec = pDropItemGroup->GetVector();
        for (const auto& dropInfo : vec)
        {
            // Calculate how many times this item should drop
            for (int i = 0; i < killCount; i++)
            {
                // Use 50% of normal drop rate for idle hunting (divide by 2 instead of 20)
                int adjustedPct = dropInfo.dwPct / 2;
                if (adjustedPct > 0 && number(1, 10000) <= adjustedPct)
                {
                    collectedItems[dropInfo.dwVnum] += dropInfo.iCount;
                }
            }
        }
    }
    
    // Create and give all collected items
    for (const auto& pair : collectedItems)
    {
        DWORD itemVnum = pair.first;
        int totalCount = pair.second;
        
        // Cap individual item stacks
        if (totalCount > 200)
            totalCount = 200;
        
        // Give items in stacks
        while (totalCount > 0 && itemsDropped < 100) // Max 100 item stacks
        {
            int giveCount = MIN(totalCount, 200); // Max stack size
            LPITEM item = AutoGiveItem(itemVnum, giveCount);
            if (item)
            {
                itemsDropped++;
                totalCount -= giveCount;
            }
            else
                break; // Inventory full
        }
    }
    
    if (itemsDropped > 0)
    {
        ChatPacket(CHAT_TYPE_INFO, "Received items from idle hunting!");
        char szHint[64];
        snprintf(szHint, sizeof(szHint), "%d", itemsDropped);
        LogManager::instance().CharLog(this, 0, "IDLE_HUNT_ITEMS", szHint);
    }
}

void CHARACTER::SaveIdleHunting()
{
    sys_log(0, "IDLE_HUNT_SAVE: Function called, IsPC=%d", IsPC() ? 1 : 0);
    
    if (!IsPC())
    {
        sys_log(0, "IDLE_HUNT_SAVE: Early return - not a PC");
        return;
    }
    
    sys_log(0, "IDLE_HUNT_SAVE: IsPC check passed, building query");

    char szQuery[512];
    snprintf(szQuery, sizeof(szQuery),
        "REPLACE INTO player.idle_hunting "
        "(pid, mob_vnum, start_time, last_claim_time, total_time_today, last_reset_date, is_active, max_daily_seconds) "
        "VALUES(%u, %u, %u, %u, %u, '%s', %d, %u)", 
        GetPlayerID(), 
        m_idleHunting.mobVnum, 
        m_idleHunting.startTime, 
        m_idleHunting.lastClaimTime,
        m_idleHunting.totalTimeToday,
        m_idleHunting.lastResetDate,
        m_idleHunting.isActive,
        m_idleHunting.maxDailySeconds
    );
    
    sys_log(0, "IDLE_HUNT_SAVE player=%u query=%s", GetPlayerID(), szQuery);
    DBManager::instance().Query(szQuery);
}