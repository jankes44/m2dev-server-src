#ifndef __PACKET_IDLE_HUNTING_H__
#define __PACKET_IDLE_HUNTING_H__

enum
{
    IDLE_HUNTING_SUBHEADER_CG_GET_GROUPS = 1,      // Request available groups
    IDLE_HUNTING_SUBHEADER_CG_START = 2,           // Start hunting with group_id
    IDLE_HUNTING_SUBHEADER_CG_STOP = 3,            // Stop hunting
    IDLE_HUNTING_SUBHEADER_CG_CLAIM_REWARDS = 4,   // Claim rewards
    IDLE_HUNTING_SUBHEADER_CG_GET_STATUS = 5,      // Get current status
};

enum
{
    IDLE_HUNTING_SUBHEADER_GC_GROUPS_LIST = 1,     // Send groups to client
    IDLE_HUNTING_SUBHEADER_GC_STATUS = 2,          // Send status to client
    IDLE_HUNTING_SUBHEADER_GC_RESULT = 3,          // Action result (success/fail)
};

// Client -> Server
typedef struct SPacketCGIdleHunting
{
    BYTE header;
    BYTE subheader;
    DWORD group_id;  // For START command
} TPacketCGIdleHunting;

// Server -> Client: Group info
typedef struct SPacketGCIdleHuntingGroup
{
    DWORD group_id;
    char name[64];
    char display_name[128];
    BYTE min_level;
    BYTE premium_only;
    float exp_multiplier;
    float yang_multiplier;
} TPacketGCIdleHuntingGroup;

// Server -> Client: Groups list
typedef struct SPacketGCIdleHuntingGroupsList
{
    BYTE header;
    BYTE subheader;
    BYTE count;  // Number of groups following
    // TPacketGCIdleHuntingGroup groups[count];
} TPacketGCIdleHuntingGroupsList;

// Server -> Client: Current status
typedef struct SPacketGCIdleHuntingStatus
{
    BYTE header;
    BYTE subheader;
    BYTE is_active;
    DWORD group_id;
    DWORD time_left;
    DWORD max_time;
    DWORD start_time;
} TPacketGCIdleHuntingStatus;

// Server -> Client: Action result
typedef struct SPacketGCIdleHuntingResult
{
    BYTE header;
    BYTE subheader;
    BYTE success;
    char message[256];
} TPacketGCIdleHuntingResult;

#endif
