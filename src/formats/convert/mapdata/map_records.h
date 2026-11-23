#ifndef SH_FORMATS_CONVERT_MAPDATA_MAP_RECORDS_H
#define SH_FORMATS_CONVERT_MAPDATA_MAP_RECORDS_H

/* The data of a map overlay (VIN/MAPn_Snn.BIN) as plain C records: every bit field of the disc structures is a field
 * of its own. Shared by the converter (sh_convert_mapdata), the engine reader (sh_maps) and the game code that copies
 * the records into the overlay's structures. Layouts on the disc and the game
 * structures they stand for. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    SH_MAP_SPAWN_GROUPS     = 2,   /* disc header charaSpawnInfos[2][16] */
    SH_MAP_SPAWN_SLOTS      = 16,
    SH_MAP_CHARA_GROUP_IDS  = 4,   /* disc header charaGroupIds, CHARA_GROUP_COUNT */
    SH_MAP_CAMERA_PATHS     = 100, /* CAMERA_PATH_COUNT_MAX */
    SH_MAP_COLLISION_COUNT  = 200, /* COLLISION_TRIGGER_COUNT_MAX */
    SH_MAP_POINTS_MAX       = 256, /* s_EventData::mapPointIdx is a u8 */
    SH_MAP_EVENTS_MAX       = 256
};

/* Scalar fields of the disc's map overlay header (the original's s_MapOverlayHdr). */
typedef struct ShMapHeaderInfo
{
    int8_t  unused8;         /* 0x08 unused_8 */
    uint8_t pad9[3];         /* 0x09 padding; zero on the disc */
    int8_t  bgmIdx;          /* 0x14 */
    uint8_t ambientAudioIdx; /* 0x15 */
    int8_t  field16;         /* 0x16 ambient tint and draw distance */
    int8_t  field17;         /* 0x17 weather */
} ShMapHeaderInfo;

/* s_SpawnInfo, 12 bytes on the disc. */
typedef struct ShMapSpawn
{
    int32_t positionX;         /* Q19.12 */
    int8_t  charaId;           /* e_CharaId; 0 (Chara_None): the group's character */
    uint8_t rotationY;         /* Q0.8 turn: 256 = 360 degrees */
    int8_t  flags;             /* copied to s_Model::stateStep */
    int8_t  gameDifficultyMin; /* 4-bit signed: e_GameDifficulty */
    uint8_t pad7;              /* bits 4-7 of byte 7 */
    int32_t positionZ;         /* Q19.12 */
} ShMapSpawn;

typedef struct ShMapSpawns
{
    int8_t     charaGroupIds[SH_MAP_CHARA_GROUP_IDS];
    ShMapSpawn slots[SH_MAP_SPAWN_GROUPS][SH_MAP_SPAWN_SLOTS];
} ShMapSpawns;

/* s_MapPoint2d, 12 bytes. */
typedef struct ShMapPoint
{
    int32_t positionX;       /* Q19.12 */
    uint8_t paperMapIdx;     /* 5 bits, e_PaperMapIdx */
    uint8_t weatherOnEnter;  /* 4 bits; 0: keep, n: room transition sets weather n-1 */
    uint8_t loadingScreenId; /* 3 bits, e_LoadingScreenId */
    uint8_t unused4_12;      /* 4 bits */
    uint8_t triggerParam0;   /* 8 bits, usually a Q0.8 angle */
    uint8_t triggerParam1;   /* 8 bits */
    int32_t positionZ;       /* Q19.12 */
} ShMapPoint;

/* s_EventData, 12 bytes. */
typedef struct ShMapEvent
{
    int16_t requiredFlag;    /* e_EventFlag */
    int16_t completeFlag;    /* e_EventFlag */
    int8_t  triggerType;     /* 4-bit signed, e_EventTriggerType */
    uint8_t activationType;  /* 4 bits */
    uint8_t mapPointIdx;     /* index into the map points */
    uint8_t requiredItemId;  /* e_InvItemId */
    uint8_t pad7;
    uint8_t sysState;        /* 5 bits, e_SysState */
    uint8_t eventParam;      /* 8 bits */
    uint8_t transitionFlags; /* 6 bits */
    uint8_t sfxPairIdx;      /* 5 bits */
    uint8_t setCameraRail;   /* 1 bit */
    uint8_t mapIdx;          /* 6 bits */
    uint8_t bit31;           /* 1 bit */
} ShMapEvent;

/* VC_ROAD_DATA, 24 bytes. Areas are Q11.4 (1/16 m), heights Q27.4 in 8 bits. */
typedef struct ShMapCamera
{
    int16_t switchArea[4];  /* lim_sw: min_hx, max_hx, min_hz, max_hz */
    int16_t roadArea[4];    /* lim_rd */
    uint8_t flags;          /* 8 bits, VC_ROAD_FLAGS */
    uint8_t areaSizeType;   /* 2 bits */
    uint8_t roadType;       /* 3 bits, rd_type */
    uint8_t moveYType;      /* 3 bits, mv_y_type */
    int8_t  roadMaxY;       /* lim_rd_max_hy */
    int8_t  roadMinY;       /* lim_rd_min_hy */
    int8_t  watchOffsetY;   /* ofs_watch_hy */
    uint8_t lensFlare;      /* 4 bits */
    int8_t  cameraMoveType; /* 4-bit signed, cam_mv_type */
    int8_t  fixAngleX;      /* Q0.7 */
    int8_t  fixAngleY;
} ShMapCamera;

/* s_CollisionTrigger, 4 bytes. */
typedef struct ShMapCollision
{
    uint8_t isEndOfArray; /* 1 bit */
    int16_t positionX;    /* 10-bit signed, metres */
    int16_t positionZ;    /* 10-bit signed, metres */
    uint8_t sizeX;        /* 4 bits, metres */
    uint8_t sizeZ;        /* 4 bits, metres */
    uint8_t height;       /* 3 bits, half metres */
} ShMapCollision;

#ifdef __cplusplus
}
#endif

#endif
