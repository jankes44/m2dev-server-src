#include "stdafx.h"
#include "packet_idle_hunting.h"
#include "char.h"
#include "questmanager.h"
#include "desc.h"
#include "idle_hunting_manager.h"

void CInputMain::IdleHunting(LPCHARACTER ch, const char* c_pData)
{
    if (!ch || !ch->GetDesc())
        return;

    TPacketCGIdleHunting* p = (TPacketCGIdleHunting*)c_pData;
    
    switch (p->subheader)
    {
        case IDLE_HUNTING_SUBHEADER_CG_GET_GROUPS:
        {
            // Call quest function to get available groups
            lua_State* L = quest::CQuestManager::instance().GetLuaState();
            quest::PC* pPC = quest::CQuestManager::instance().GetPC(ch->GetPlayerID());
            
            if (!pPC || !L)
                return;
            
            // Push quest state
            pPC->SetCurrentCharacterPtr(ch);
            
            // Call pc.get_available_idle_hunting_groups()
            lua_getglobal(L, "pc");
            if (lua_istable(L, -1))
            {
                lua_getfield(L, -1, "get_available_idle_hunting_groups");
                if (lua_isfunction(L, -1))
                {
                    // Call the function
                    if (lua_pcall(L, 0, 1, 0) == 0)
                    {
                        // Result is a table of groups
                        if (lua_istable(L, -1))
                        {
                            // Count groups
                            int count = lua_objlen(L, -1);
                            
                            // Prepare packet
                            TPacketGCIdleHuntingGroupsList header;
                            header.header = HEADER_GC_IDLE_HUNTING;
                            header.subheader = IDLE_HUNTING_SUBHEADER_GC_GROUPS_LIST;
                            header.count = count;
                            
                            // Send header
                            ch->GetDesc()->Packet(&header, sizeof(header));
                            
                            // Send each group
                            for (int i = 1; i <= count; i++)
                            {
                                lua_rawgeti(L, -1, i);
                                if (lua_istable(L, -1))
                                {
                                    TPacketGCIdleHuntingGroup group;
                                    memset(&group, 0, sizeof(group));
                                    
                                    // Extract group data
                                    lua_getfield(L, -1, "id");
                                    group.group_id = (DWORD)lua_tonumber(L, -1);
                                    lua_pop(L, 1);
                                    
                                    lua_getfield(L, -1, "name");
                                    strncpy(group.name, lua_tostring(L, -1), sizeof(group.name) - 1);
                                    lua_pop(L, 1);
                                    
                                    lua_getfield(L, -1, "display_name");
                                    strncpy(group.display_name, lua_tostring(L, -1), sizeof(group.display_name) - 1);
                                    lua_pop(L, 1);
                                    
                                    lua_getfield(L, -1, "min_level");
                                    group.min_level = (BYTE)lua_tonumber(L, -1);
                                    lua_pop(L, 1);
                                    
                                    lua_getfield(L, -1, "premium_only");
                                    group.premium_only = (BYTE)lua_toboolean(L, -1);
                                    lua_pop(L, 1);
                                    
                                    // Get multipliers from manager
                                    const IdleHuntingGroup* fullGroup = CIdleHuntingManager::instance().GetGroup(group.group_id);
                                    if (fullGroup)
                                    {
                                        group.exp_multiplier = fullGroup->exp_multiplier;
                                        group.yang_multiplier = fullGroup->yang_multiplier;
                                    }
                                    
                                    // Send group data
                                    ch->GetDesc()->Packet(&group, sizeof(group));
                                }
                                lua_pop(L, 1); // pop group table
                            }
                        }
                        lua_pop(L, 1); // pop result
                    }
                }
                lua_pop(L, 1); // pop function
            }
            lua_pop(L, 1); // pop pc table
            
            pPC->SetCurrentCharacterPtr(NULL);
            break;
        }
        
        case IDLE_HUNTING_SUBHEADER_CG_START:
        {
            DWORD group_id = p->group_id;
            
            // Call quest function pc.start_idle_hunting(group_id)
            lua_State* L = quest::CQuestManager::instance().GetLuaState();
            quest::PC* pPC = quest::CQuestManager::instance().GetPC(ch->GetPlayerID());
            
            if (!pPC || !L)
                return;
            
            pPC->SetCurrentCharacterPtr(ch);
            
            lua_getglobal(L, "pc");
            if (lua_istable(L, -1))
            {
                lua_getfield(L, -1, "start_idle_hunting");
                if (lua_isfunction(L, -1))
                {
                    lua_pushnumber(L, group_id);
                    lua_pcall(L, 1, 0, 0);
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            
            pPC->SetCurrentCharacterPtr(NULL);
            
            // Send result
            TPacketGCIdleHuntingResult result;
            result.header = HEADER_GC_IDLE_HUNTING;
            result.subheader = IDLE_HUNTING_SUBHEADER_GC_RESULT;
            result.success = 1;
            snprintf(result.message, sizeof(result.message), "Idle hunting started");
            ch->GetDesc()->Packet(&result, sizeof(result));
            break;
        }
        
        case IDLE_HUNTING_SUBHEADER_CG_STOP:
        {
            // Call pc.stop_idle_hunting()
            lua_State* L = quest::CQuestManager::instance().GetLuaState();
            quest::PC* pPC = quest::CQuestManager::instance().GetPC(ch->GetPlayerID());
            
            if (!pPC || !L)
                return;
            
            pPC->SetCurrentCharacterPtr(ch);
            
            lua_getglobal(L, "pc");
            if (lua_istable(L, -1))
            {
                lua_getfield(L, -1, "stop_idle_hunting");
                if (lua_isfunction(L, -1))
                {
                    lua_pcall(L, 0, 0, 0);
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            
            pPC->SetCurrentCharacterPtr(NULL);
            break;
        }
        
        case IDLE_HUNTING_SUBHEADER_CG_CLAIM_REWARDS:
        {
            // Call pc.claim_idle_hunting_rewards()
            lua_State* L = quest::CQuestManager::instance().GetLuaState();
            quest::PC* pPC = quest::CQuestManager::instance().GetPC(ch->GetPlayerID());
            
            if (!pPC || !L)
                return;
            
            pPC->SetCurrentCharacterPtr(ch);
            
            lua_getglobal(L, "pc");
            if (lua_istable(L, -1))
            {
                lua_getfield(L, -1, "claim_idle_hunting_rewards");
                if (lua_isfunction(L, -1))
                {
                    lua_pcall(L, 0, 0, 0);
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            
            pPC->SetCurrentCharacterPtr(NULL);
            break;
        }
        
        case IDLE_HUNTING_SUBHEADER_CG_GET_STATUS:
        {
            TPacketGCIdleHuntingStatus status;
            status.header = HEADER_GC_IDLE_HUNTING;
            status.subheader = IDLE_HUNTING_SUBHEADER_GC_STATUS;
            status.is_active = ch->IsIdleHuntingActive() ? 1 : 0;
            status.group_id = ch->GetIdleHuntingGroupId();
            status.time_left = ch->GetIdleHuntingTimeLeft();
            status.max_time = ch->GetIdleHuntingMaxTime();
            status.start_time = ch->GetIdleHuntingStartTime();
            
            ch->GetDesc()->Packet(&status, sizeof(status));
            break;
        }
    }
}
