// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// C face of the script host: the game (C/C++ both) calls these at boot
// and at the hook points; everything else lives in script_host.h.

#ifndef SH_ENGINE_SCRIPT_SH_SCRIPT_H
#define SH_ENGINE_SCRIPT_SH_SCRIPT_H

#ifdef __cplusplus
extern "C" {
#endif

/* The game functions behind sh.* (API v3, script_host.h's GameApi). Bind before
 * ShScript_Boot; a NULL entry makes the matching function error in the script. */
/* One argument of the generic sh.call bridge (kind 0 nil, 1 integer, 2 string,
 * 3 boolean). */
typedef struct ShScriptArg
{
    int         kind;
    long long   i;
    const char* s;
} ShScriptArg;

/* render: one sh.render.frame_hold{...} request. The values are the
 * script API's own, not a GPU's: each renderer maps them itself.
 *   source: 0 previous (the frame shown last), 1 held (the held picture: see
 *           render_held_capture), 2 current (the frame being drawn, as drawn so far)
 *   tint:   r, g, b, 0..255; 128 = unchanged
 *   blend:  -1 none (opaque), 0 half (back/2 + front/2), 1 add, 2 sub (back - front),
 *           3 quarter (back + front/4)
 *   layer:  0 world (right after the world, before the picture and the HUD), 1 overlay (over
 *           the world, under the HUD)
 *   offset: dx, dy in screen pixels */
typedef struct ShRenderFrameHold
{
    int source;
    int tint[3];
    int blend;
    int layer;
    int offset[2];
} ShRenderFrameHold;

/* render: sh.render.world_redirect{layers = {...}}: this frame the
 * world is drawn into the held picture instead of the screen, and the held picture is then
 * drawn onto the screen as `count` layers, the first at the bottom. Values as frame_hold. */
typedef struct ShRenderLayer
{
    int offset[2];
    int tint[3];
    int blend;
} ShRenderLayer;

typedef struct ShRenderLayers
{
    int count;
    const ShRenderLayer* items;
} ShRenderLayers;

/* render: sh.render.tile{rect=, colour=, blend=, layer=}: one
 * untextured rectangle of one colour.
 *   x, y, w, h: screen pixels from the screen centre
 *   colour:     r, g, b, 0..255
 *   blend:      as frame_hold
 *   layer:      0 world, 1 overlay (as frame_hold), 2 picture (as ShRenderSprites), 3 screen
 *               (over the overlay layer, under the HUD)
 *   tpage:      >= 0: raw texture page word of the page packet (page, colour mode, blend
 *               bits), as ShRenderSpriteRect.tpage; blend then only marks the tile
 *               semi-transparent. -1: the page under the tile's left edge with blend's bits
 *   dither:     non-zero: the page packet turns dithering on */
typedef struct ShRenderTile
{
    int x, y, w, h;
    int colour[3];
    int blend;
    int layer;
    int tpage;
    int dither;
} ShRenderTile;

/* render: sh.render.sprite{...}: textured rectangles copied 1:1 from
 * a named texture, all with the same texture, layer, tint and blend.
 *   texture: 0 background (the picture sh.event_bg_texture_cmd loads and draws)
 *   layer:   0 world, 1 overlay (as frame_hold), 2 picture (over the full-screen background
 *            picture, under the HUD)
 *   tint:    r, g, b, 0..255; 128 = unchanged
 *   blend:   -1 none (opaque), 0 half, 1 add, 2 sub, 3 quarter (as frame_hold)
 *   u, v:    the call's shared texel position; it picks the texture page when the list is
 *            empty
 *   items:   count rectangles; x, y in screen pixels from the screen centre, u, v in texels of
 *            the texture (0, 0 = its top-left corner). Per item: tint[0] < 0 keeps the call's
 *            tint, blend -2 keeps the call's blend; tpage >= 0 is a raw draw mode for this
 *            sprite (u, v are then texels of that page, clut a raw CLUT word) and every such
 *            sprite gets its own page packet, as hand-built overlay code does */
typedef struct ShRenderSpriteRect
{
    int x, y, w, h;
    int u, v;
    int tint[3];
    int blend;
    int clut;
    int tpage;
} ShRenderSpriteRect;

typedef struct ShRenderSprites
{
    int texture;
    int layer;
    int tint[3];
    int blend;
    int u, v;
    int count;
    const ShRenderSpriteRect* items;
} ShRenderSprites;

/* render: sh.render.texture_load{file=, texture=, buffer=}: queues a
 * TIM read into a named texture through a named staging buffer (the queue uploads it).
 *   file:    game file number
 *   texture: 0 background (as ShRenderSprites), 1 paper_map (the paper map page picture)
 *   buffer:  0 file (the buffer the background loader uses), 1 image (the image staging
 *            buffer some scenes read pictures through), 2 paper_map (the buffer the paper
 *            map pictures are read through) */
typedef struct ShRenderTextureLoad
{
    int file;
    int texture;
    int buffer;
} ShRenderTextureLoad;

/* render: sh.render.vram_save / vram_restore{rect=, buffer=}: a VRAM
 * rectangle is copied into a named RAM buffer / back from it, waiting for drawing to finish.
 *   x, y, w, h: VRAM pixels (16-bit units)
 *   buffer:     0 screen_backup, 1 screen_backup_alt (two RAM areas the scenes that cover the
 *               screen with a picture keep a piece of VRAM in) */
typedef struct ShRenderVramCopy
{
    int x, y, w, h;
    int buffer;
} ShRenderVramCopy;

/* render: sh.render.world_quads{...}: textured quads at world
 * positions, projected through the view in force in an effect-slot draw hook (the 2D effects
 * view: origin at the player's whole-metre position) and sorted into the world's depth table.
 *   points:      point_count x, y, z triples, world units (4096 = 1 m)
 *   page_x/y:    VRAM position of the texture page (x a multiple of 64, y 0 or 256)
 *   colours:     0 16-colour palette, 1 256-colour palette, 2 direct 15-bit
 *   palette_x/y: VRAM position of the palette (x a multiple of 16)
 *   blend:       as frame_hold
 *   depth_bias:  added to a quad's depth before it picks its table entry (depth / 8)
 *   clip_x/y:    a tested corner further than this from the screen centre skips the quad
 * Per quad:
 *   corners:     indices into points, in the order 0 1 2 3 (two triangles 0-1-2, 1-2-3)
 *   uv:          u, v texels of the page per corner
 *   tint:        r, g, b, 0..255
 *   depth_test:  -1 the average depth of the corners decides whether the quad is in front of
 *                the eye and inside the table; 0..3 that corner's depth does
 *   screen_test: -1 every corner must be inside clip; 0..3 only that corner is tested
 *   precise:     bit k set: corner k keeps its sub-pixel projection (renderers that use one)
 *   sort_points: the four points whose depths the renderer orders the quad's pixels by
 * The table entry is (average corner depth + depth_bias) / 8. */
typedef struct ShRenderWorldQuad
{
    int corners[4];
    int uv[8];
    int tint[3];
    int depth_test;
    int screen_test;
    int precise;
    int sort_points[4];
} ShRenderWorldQuad;

typedef struct ShRenderWorldQuads
{
    int point_count;
    const int* points;
    int quad_count;
    const ShRenderWorldQuad* quads;
    int page_x, page_y;
    int colours;
    int palette_x, palette_y;
    int blend;
    int depth_bias;
    int clip_x, clip_y;
} ShRenderWorldQuads;

typedef struct ShScriptGameApi
{
    /* v2 */
    int         (*flag_get)(int id);       /* 0/1 */
    void        (*flag_set)(int id, int value);
    void        (*give_item)(int itemId, int count);
    void        (*player_pos)(int out[3]); /* Q12 world position */
    const char* (*map_name)(void);         /* "map1_s02"; static storage */
    /* v3 */
    void        (*flag_set_alt)(int id);         /* Savegame_EventFlagSetAlt */
    int         (*item_count)(int itemId);       /* 0 when not held */
    void        (*take_item)(int itemId, int count);
    void        (*player_set_pos)(const int xyz[3]);
    void        (*player_freeze)(int on);        /* 1 freeze, 0 unfreeze */
    void        (*player_unfreeze_set_idle)(int setIdle); /* exact Player_ControlUnfreeze(setIdle) */
    int         (*msg_draw)(int msgIdx);         /* e_MapMsgState: 0 showing, -1 done, 1..3 selection */
    void        (*sfx_play)(int sfxId);          /* e_SfxId */
    void        (*bgm_change)(int trackIdx);
    void        (*fade_start)(int fadeIn, int isWhite);
    int         (*fade_done)(void);              /* 0/1 */
    void        (*map_event_set)(int sysState, int param, int mapIdx, int completeFlag);
    /* v3.1: ported map handlers (per-tick Event_* steppers + dispatch context) */
    int         (*event_point)(void);            /* pointOfInterestIdx of the active event, -1 without */
    void        (*event_item_take)(int itemId, int count, int flagIdx, int msgIdx);
    void        (*event_common_item_take)(int pickupType, int flagIdx);
    void        (*state_set)(int sysState);      /* SysWork_StateSetNext; 0 = Gameplay */
    /* v3.2: message steppers and the paper-map pickup */
    void        (*event_msg)(int hasSelection, int msgIdx, int step0, int step1, int step2,
                             int incSubStep);
    void        (*event_msg_with_sfx)(int msgIdx, int sfxId);   /* sfx at the event's map point */
    void        (*event_msg_with_bg)(int texFileIdx, int fadeQ12_0, int fadeQ12_1, int msgIdx);
    void        (*event_paper_map_take)(int paperMapFlagIdx, int flagIdx, int msgIdx);
    /* v3.3 */
    int         (*input_action)(const char* action, int mode); /* mode 0 held 1 clicked 2 released 3 pulsed */
    int         (*input_axis)(int stick, int axis);            /* -128..127 */
    void        (*camera_pos)(int warp, int posQ12[3], int argsQ12[7]);
    void        (*camera_look)(int warp, int lookQ12[3], int argsQ12[7]);
    void        (*event_msg_with_texture)(int texFileIdx, int fadeQ12_0, int fadeQ12_1, int msgIdx0, int msgIdx1);
    void        (*flashlight)(int on);
    void        (*background_gamma)(int gammaQ8);
    int         (*event_wait_timer)(int durationQ12, int unk); /* 1 while waiting */
    /* v3.4: multi-step handler state + steppers */
    int         (*event_step_get)(int idx);        /* event step counter idx */
    void        (*event_step_set)(int idx, int value);
    void        (*event_step_increment)(int idx);
    void        (*event_fade_cmd)(int cmd, int fadeOut, int fadeType, int timestepQ12, int incSubStep);
    void        (*event_selection)(int hasSelection, int msgIdx, int step0, int step1, int step2,
                                   int stepSecondary);
    void        (*sfx_stop)(int sfxId);
    void        (*event_waypoint_set)(int isAbs, int charaSlotIdx, int waypointIdx, int angleQ12, int xQ12,
                                      int zQ12);
    void        (*event_waypoint_player)(int animId, int waypointCount, int incSubStep);
    /* v3.5: cutscenes */
    int         (*chara_load)(int modelIdx, int charaId, int forceFree);  /* 0/1 */
    int         (*chara_process_loads)(void);                             /* 0/1 */
    void        (*event_chara_anim)(int cmd, int charaSlotIdx, int animState, int incSubStep);
    int         (*fs_read_dms)(int fileIdx);
    int         (*fs_queue_done)(void);
    void        (*dms_ready)(void);
    void        (*cutscene_border)(int state);
    void        (*cutscene_flag)(int on);
    int         (*cutscene_timer_get)(void);
    void        (*cutscene_timer_set)(int valueQ12);
    /* v3.6: the deferred tail's engine surface */
    void        (*overlay_screen_set)(int stateStep, int fileIdx, int reset);   /* overlay screen state setter */
    void        (*overlay_screen_show)(int fileIdx, int fadeQ12_0, int fadeQ12_1); /* fade the overlay screen in */
    void        (*overlay_screen_hide)(int fadeQ12_0, int fadeQ12_1);           /* fade the overlay screen out */
    void        (*sfx_3d)(int sfxId, int volQ8);      /* positional sfx at the event map point */
    void        (*sfx_3d_at)(int sfxId, int xQ12, int yQ12, int zQ12, int volQ8); /* ... at an explicit Q12 position */
    void        (*pad_effect)(int idx, int level);    /* queue a controller rumble effect */
    void        (*selection_silent)(int on);         /* preset the silent yes selection */
    void        (*bg_img_transition)(int which, int alphaQ12); /* Screen_BackgroundImgTransition (plate cutscenes) */
    int         (*state_timer_get)(void);            /* cross-fade alpha, Q12 */
    void        (*state_timer_set)(int valueQ12);
    void        (*event_step_reset)(void);           /* SysWork_StateStepReset */
    void        (*camera_auto_return)(int warp);     /* vcReturnPreAutoCamWork */
    void        (*player_fall_tick)(int direction); /* vy += Q12_MULT_PRECISE(dt, ±0.3): +1 fall, -1 climb */
    int         (*delta_time_get)(void);           /* g_DeltaTime (Q12 seconds) */
    void        (*world_object_add)(const char* name); /* WorldGfx_ObjectAdd on the map's g_WorldObject<N> */
    void        (*event_bg_texture_cmd)(int cmd, int texFileIdx, int incSubStep); /* Event_BgTextureCmd (bg TIM puzzles) */
    void        (*event_inv_item_cmd)(int cmd, int itemId, int itemCount, int incSubStep); /* Event_InvItemCmd */
    int         (*pickup_animate)(int itemId);      /* Gfx_PickupItemAnimate: true when the item faces the camera */
    void        (*chara_anim_play_to_end)(int charaSlot, int animState); /* Event_CharaAnimPlayToEnd (slot 0 = player) */
    int         (*chara_spawn)(int charaId, int spawnFlags, int posX, int posZ, int rotY, int stateStep); /* Chara_Spawn; returns slot, -1 = full */
    void        (*chara_spawn_flags_set)(int charaId, int spawnIdx, int spawnFlags); /* Chara_SpawnFlagsSet */
    void        (*chara_slot_transform_get)(int charaSlot, int outPosRotQ12[4]); /* npcs[slot-1] position + rotY (Q12) */
    void        (*chara_slot_transform_set)(int charaSlot, const int xyzQ12[3], int rotYQ12);
    int         (*chara_slot_chara_id_get)(int charaSlot); /* model.charaId, -1 = empty/out of range */
    void        (*chara_slot_control_set)(int charaSlot, int controlState, int stateStep); /* model.controlState/stateStep */
    int         (*event_msg_with_audio)(int mapMsgIdx, int voiceCmd); /* Map_MessageWithAudio; returns MapMsgState */
    int         (*event_activation_type_get)(void); /* g_MapEventData->activationType, -1 outside an event */
    int         (*math_sin)(int angleQ12);        /* sh::world::fixed_sin (the game's Q12 value) */
    int         (*math_cos)(int angleQ12);        /* sh::world::fixed_cos */
    int         (*event_tween_linear)(int targetQ12, int durationQ12, int timerIdx); /* returns current Q12 */
    void        (*event_tween_reset)(int timerIdx);
    void        (*delta_time_set)(int valueQ12); /* g_DeltaTime */
    void        (*chara_slot_anim_flags_set)(int charaSlot, int flags);   /* slot 0 = player */
    void        (*chara_slot_anim_flags_clear)(int charaSlot, int flags);
    void        (*water_effect_enable)(void);
    void        (*water_effect_disable)(void);
    void        (*event_sys_state_branch_on_flag)(int flagIdx, int stepTrue, int stepFalse, int setSubStep);
    void        (*sfx_flags_play)(int sfxId, int xQ12, int yQ12, int zQ12, int volQ8, int sfxFlags); /* Sfx_WithFlagsPlay */
    void        (*sfx_flags_pitch_play)(int sfxId, int xQ12, int yQ12, int zQ12, int volQ8, int sfxFlags, int pitch);
    void        (*sfx_pitch_play)(int sfxId, int xQ12, int yQ12, int zQ12, int volQ8, int pitch);
    void        (*sfx_play_at_pos)(int sfxId, int xQ12, int yQ12, int zQ12, int volQ8, int pitch); /* Sfx_WithPitchPlay */
    void        (*sfx_falloff_pitch_play)(int sfxId, int xQ12, int yQ12, int zQ12, int vol, int falloffQ12, int pitch);
    void        (*held_item_show)(void);   /* WorldGfx_HeldItemShow */
    void        (*held_item_hide)(void);   /* WorldGfx_HeldItemHide */
    void        (*game_warm_boot)(void);   /* Game_WarmBoot */
    int         (*world_object_pos_get)(const char* name, int outPosQ12[3]); /* g_WorldObject<N> position; 1 = resolved */
    int         (*world_object_pos_set)(const char* name, const int xyzQ12[3]);
    int         (*world_object_rot_y_set)(const char* name, int rotYQ12); /* s_WorldObjectPose variants only */
    void        (*held_item_attach)(int charaId, int modelBone); /* WorldGfx_HeldItemAttach (MODEL_BONE-packed bone) */
    int         (*held_item_set)(int itemId);  /* WorldGfx_PlayerHeldItemSet */
    int         (*held_item_prev)(void);       /* WorldGfx_PlayerPrevHeldItem */
    void        (*player_weapon_attack_set)(int weaponAttack); /* packed, see WEAPON_ATTACK */
    int         (*player_weapon_attack_get)(void);
    int         (*math_atan2)(int yQ12, int xQ12); /* ratan2, bit-exact Q12 angle */
    void        (*event_bg_texture_fade_in)(int texFileIdx, int fadeTimestep0Q12, int fadeTimestep1Q12);
    void        (*event_bg_texture_fade_out)(int fadeTimestepFromBlackQ12, int fadeTimestepToBlackQ12);
    void        (*sd_call)(int cmd);                          /* SD_Call */
    void        (*sfx_attributes_update)(int sfxId, int balance, int vol, int pitch);
    int         (*screen_fade_finished)(void);                /* ScreenFade_IsFinished */
    int         (*world_object_rot_y_get)(const char* name);   /* q3_12 rotY */
    void        (*player_move_speed_clear)(void);            /* Player_MoveSpeedClear */
    int         (*rng_int)(int low, int high);                 /* Rng_GenerateInt */
    int         (*rng_uint)(int low, int high);                /* Rng_GenerateUInt */
    int         (*screen_fade_timestep_get)(void); /* g_ScreenFadeTimestep (Q12) */
    void        (*screen_fade_timestep_set)(int valueQ12);
    void        (*player_rotate)(int angleQ12);       /* player heading */
    int         (*player_rot_y_get)(void);           /* player heading (Q12) */
    int         (*event_wait_player_stop)(void);       /* 1 while waiting */
    void        (*event_step_increment_delayed)(int delayQ12, int reset);
    void        (*event_step_increment_after_fade)(int stateStep, int cond, int fadeType,
                                                   int timestepQ12, int reset);
    void        (*screen_fade_status_set)(int status);
    void        (*game_state_update)(void);           /* GameState_UnkUpdate */
    void        (*delta_time_reset)(void);            /* g_DeltaTime = 0 */
    void        (*screen_fade_reset_timestep)(void);
    /* v3.7 */
    int         (*ambient_sfx)(void);                 /* Sd_AmbientSfxInit -> 0/1 */
    int         (*player_vel_y_get)(void);            /* Q12 */
    void        (*player_vel_y_set)(int vyQ12);
    /* Character data (gamedata/chara), see script_host.h. */
    int         (*chara_id_find)(const char* name);
    int         (*chara_sfx)(int charaId, const char* slot, int fallback);
    double      (*chara_param)(int charaId, const char* name, double fallback, int* found);
    int         (*chara_param_q12)(int charaId, const char* name, int fallbackQ12);
    int         (*chara_anim)(int charaId, const char* name, int fallback);
    int         (*chara_state_number)(int charaId, const char* name);  /* cutscene state ("pickup_crouch"), -1 if none */
    /* Item names, see script_host.h. */
    int         (*item_number)(const char* name);
    int         (*common_pickup_number)(const char* name);
    /* names-rest: event flags by name (gamedata/flags/), see script_host.h */
    int         (*flag_number)(const char* name);
    int         (*file_number)(const char* name);  /* "TIM/LOCKER1.TIM" -> FILE_*, -1 if none */
    int         (*paper_map_number)(const char* name);  /* "FogSchool1F" -> e_PaperMapIdx, -1 if none */
    int         (*sd_command_number)(const char* name); /* "stop_xa_audio" -> SD bus command, -1 if none */
    /* Keyed text (gamedata/text/<lang>/). */
    const char* (*text_get)(const char* key);   /* readable text, current language then English; NULL if absent */
    int         (*msg_index)(const char* key);  /* message slot of the key in the current map, -1 if none */
    /* The generic bridge (sh.call + named ovl_/chara_/sys_/vec_/dms_ binds);
     * returns the count of values in out, -1 = error text in err. */
    int         (*evb_call)(const char* name, int argc, const ShScriptArg* argv, long long* out,
                            int outCap, char* err, int errCap);
    /* The flat functions of the maps 0-3 handlers. Slots: 0 = player, 1.. = npcs[slot-1];
     * bufIdx = FS_BUFFER_<n>; map_var type: 0 s8, 1 u8, 2 s16, 3 u16, 4 s32. */
    int         (*fs_read)(int fileIdx, int bufIdx);        /* Fs_QueueStartRead */
    void        (*fs_wait)(void);                           /* Fs_QueueWaitForEmpty */
    int         (*fs_seek)(int fileIdx);                    /* Fs_QueueStartSeek */
    void        (*dms_fix)(int bufIdx);                     /* Dms_HeaderFixOffsets */
    void        (*dms_chara_apply)(int charaSlot, const char* name, int timeQ12, int bufIdx);
    void        (*dms_camera_apply)(int timeQ12, int bufIdx);
    void        (*vc_cam_target)(const int xyzQ12[3], int warp);   /* vcUserCamTarget(pos, NULL, warp) */
    void        (*vc_watch_target)(const int xyzQ12[3], int warp); /* vcUserWatchTarget(pos, NULL, warp) */
    void        (*vc_view_position)(int outQ12[3]);         /* vwGetViewPosition */
    void        (*light_intensity_set)(int valueQ12);
    void        (*light_bones_clear)(void);                 /* lightBoneCoord = lensFlareBoneCoord = NULL */
    void        (*light_pos_set)(const int xyzQ12[3]);
    void        (*light_rot_set)(const int xyzQ12[3]);
    void        (*flashlight_attrs_fix)(void);              /* Game_FlashlightAttributesFix */
    int         (*math_vec2_mag_q6)(int xQ12, int zQ12);    /* Math_Vector2MagCalcSafeQ6 */
    void        (*chara_model_clear)(int charaSlot);        /* Chara_ModelCharaIdClear(chara, 0, 0) */
    void        (*chara_props_reset)(int charaSlot);        /* reset a character slot properties */
    void        (*player_weapon_stash)(void);               /* stash the player weapon */
    void        (*player_weapon_restore)(void);             /* restore the stashed player weapon */
    void        (*map_effects_init)(int idx0, int idx1);    /* Gfx_MapInitMapEffectsUpdate */
    int         (*sys_flags_get)(void);
    void        (*sys_flags_set)(int value);
    int         (*bgm_status_flags_get)(void);
    void        (*bgm_status_flags_set)(int value);
    int         (*sys_timer_get)(void);                     /* system timer */
    void        (*sys_timer_set)(int valueQ12);
    int         (*event_msg_audio)(int alias, int mapMsgIdx, const char* idxSym, int audioIdx, const char* cmdsSym); /* returns audioIdx */
    void        (*event_msg_dimmed_bg)(int texFileIdx, int fade0, int fade1, int msgIdx0, int msgIdx1);
    void        (*chara_anim_unlock_play_to_end)(int charaSlot);
    void        (*paper_map_state)(int state, int paperMapIdx);  /* paper map state */
    void        (*paper_map_cmd)(int cmd, int paperMapIdx);      /* Event_PaperMapCmd */
    void        (*map_screen_vertex_draw)(int a0, int a1, int a2);
    int         (*map_screen_marking_draw)(const int args[7]);
    int         (*map_screen_player_mark_draw)(int a0, int a1, int a2, int a3);
    void        (*map_box_outline_draw)(const int args[9]);
    int         (*sd_play_sfx)(int sfxId, int balance, int vol);
    int         (*vc_stereo_balance)(const int xyzQ12[3]);
    int         (*light_intensity_get)(void);
    int         (*chara_get)(int charaSlot, int field, int* ok);   /* field: see PcEventsA_CharaField */
    int         (*chara_set)(int charaSlot, int field, int value);
    int         (*chara_anim_start_keyframe_get)(int charaSlot);    /* Chara_AnimStartKeyframeIdxGet (overlay) */
    void        (*math_point_from_angle)(const int xyzQ12[3], int angleQ12, int arg2, int arg3); /* Math_PointFromAngleCalc */
    void        (*chara_bones_init)(int idx);                       /* Chara_BonesInit */
    void        (*camera_default_speeds)(int out[4]);               /* user camera max speed xz/y, default look-at max speed x/y */
    int         (*fs_entry_loaded)(int queueIdx);                   /* Fs_QueueIsEntryLoaded */
    void        (*xa_preload)(int xaIdx);                           /* Sd_XaPreLoadAudioPreTaskAdd */
    int         (*map_effects_update)(int idx0, int idx1, int primType, const char* primDataSym, int arg4, int arg5);
    int         (*savegame_get)(int field);                         /* 0 mapIdx, 1 paperMapIdx, 2 itemToggleFlags */
    void        (*savegame_set)(int field, int value);
    void        (*cursor_draw)(const int args[12]);                 /* Gfx_CursorDraw */
    int         (*mouse_cursor_puzzle_override)(const char* xSym, const char* ySym); /* sh::input::MousePointer::puzzle_override */
    void        (*game_timer_update)(void);                         /* Game_TimerUpdate */
    int         (*item_remove)(int itemId, int count);              /* Player_ItemRemove */
    /* sh.<group>.<name>(...) — same contract as evb_call;
     * named_list = every "<group>.<name>" (NULL-terminated); raw_api_enabled = the
     * script_raw_api config key (1 = the evb_call bridge is exposed to scripts too). */
    int         (*named_call)(const char* name, int argc, const ShScriptArg* argv, long long* out,
                              int outCap, char* err, int errCap);
    const char* const* (*named_list)(void);
    int         (*raw_api_enabled)(void);
    /* render: sh.render.frame_hold; 0 drawn, -1 refused (err says why). */
    int         (*render_frame_hold)(const ShRenderFrameHold* spec, char* err, int errCap);
    /* render: sh.render.sprite; 0 drawn, -1 refused (err says why). */
    int         (*render_sprites)(const ShRenderSprites* spec, char* err, int errCap);
    /* render: sh.render.texture_load; 0 queued, -1 refused (err says why). */
    int         (*render_texture_load)(const ShRenderTextureLoad* spec, char* err, int errCap);
    /* render: sh.render.held_clear (the held picture becomes black) and sh.render.held_capture
     * (the frame drawn so far is copied into the held picture at the end of the world layer);
     * 0 done, -1 refused (err says why). */
    int         (*render_held_clear)(char* err, int errCap);
    int         (*render_held_capture)(char* err, int errCap);
    /* render: sh.render.world_redirect; 0 drawn, -1 refused (err says why). */
    int         (*render_world_redirect)(const ShRenderLayers* spec, char* err, int errCap);
    /* render: sh.render.tile; 0 drawn, -1 refused (err says why). */
    int         (*render_tile)(const ShRenderTile* spec, char* err, int errCap);
    /* render: sh.render.fade_in_level: how far the screen is faded in, 4096 = fully visible
     * (the level the last fade update left). */
    int         (*render_fade_in_level)(void);
    /* render (screen): sh.render.vram_save / vram_restore; 0 done, -1 refused. */
    int         (*render_vram_save)(const ShRenderVramCopy* spec, char* err, int errCap);
    int         (*render_vram_restore)(const ShRenderVramCopy* spec, char* err, int errCap);
    /* render (screen): sh.render.display_init{width=, interlaced=}: the display and draw
     * buffers are set up again (interlaced = 480 lines for a full-screen picture). */
    int         (*render_display_init)(int width, int interlaced, char* err, int errCap);
    /* render (screen): sh.render.display_swap(): the shown and drawn buffers swap now. */
    void        (*render_display_swap)(void);
    /* render (screen): sh.render.vblank_interval(n): a frame lasts at least n vertical blanks. */
    void        (*render_vblank_interval)(int vblanks);
    /* render (screen): sh.render.picture_screen(on): PC presenter mode for a scene that
     * covers the whole screen with a flat picture (4:3 framing, black side bars). */
    void        (*render_picture_screen)(int on);
    /* render (screen): sh.render.background{texture=}: the named picture fills the screen
     * this frame (texture as ShRenderTextureLoad); 0 drawn, -1 refused. */
    int         (*render_background)(int texture, char* err, int errCap);
    /* paper maps: sh.paper_map_grant(name): the page counts as found (the map screen shows it). */
    void        (*paper_map_grant)(int paperMapIdx);
    /* characters: sh.chara_spawn_entry_set(entry, on): entry of the map's character spawn
     * table is in play (on) and the map spawner leaves it alone, or free again. */
    void        (*chara_spawn_entry_set)(int entry, int on);
    /* render: sh.render.world_quads; the number of quads drawn, -1 refused (err says why). */
    int         (*render_world_quads)(const ShRenderWorldQuads* spec, char* err, int errCap);
} ShScriptGameApi;

/* The map whose events.lua runs ("map6_s02"; "" before the first map). */
const char* ShScript_MapName(void);

/* The map's ported handlers. Returns 1 when a Lua handler of that name (resolved
 * through maps/<map>/handlers.json) exists and was called this tick — the caller
 * then skips the C++ handler. Returns 0 when there is none (the normal case). */
int ShScript_CallMapHandler(int param);
/* The readable name of the active map's handler `param` (handlers.json), "" when none. */
const char* ShScript_MapHandlerName(int param);

void ShScript_SetGameApi(const ShScriptGameApi* api);

/* Registration of the game API: every part of the program that
 * implements sh.* functions registers a binder; ShScript_InstallGameApi runs them in ascending
 * `order` (equal orders in registration order) over one zeroed ShScriptGameApi and installs the
 * result. A binder may wrap an entry an earlier one set (named_call chains), so the order is the
 * contract. Register before ShScript_InstallGameApi (static registration objects do). */
typedef void (*ShScriptGameBinder)(ShScriptGameApi* api);
void ShScript_RegisterGameBinder(int order, ShScriptGameBinder binder);
void ShScript_InstallGameApi(void);

#ifdef __cplusplus
/* One line per binder file: static ShScriptGameBinderEntry s_binder(order, BindFunction); */
struct ShScriptGameBinderEntry
{
    ShScriptGameBinderEntry(int order, ShScriptGameBinder binder) { ShScript_RegisterGameBinder(order, binder); }
};
#endif

/* Character behaviour in Lua.
 * A character type with chara/<Name>/behavior.lua (base data or a mod; chara/common.lua
 * runs first in the same state) is updated by that file's update(slot) instead of the
 * type's C++ update function. One Lua state per type, dropped on every map load (the
 * type's module state starts over, like the overlay's own globals).
 *   ShScript_CharaBehaviorHas: 1 when the type has a behaviour script.
 *   ShScript_CharaBehaviorUpdate: 1 update(slot) ran, 0 no script, -1 the script errored
 *     (logged; the tick is lost for that character). */
int ShScript_CharaBehaviorHas(int charaId, const char* name);
int ShScript_CharaBehaviorUpdate(int charaId, const char* name, int slot);
/* The generic fallback behaviour, chara/fallback_ai.lua (+ chara/<name>/fallback_ai.lua, the type's tuning),
 * in a Lua state per type apart from its own behaviour: what a spawn runs when its own AI cannot work on the
 * map. Same results as ShScript_CharaBehaviorUpdate. */
int ShScript_CharaFallbackUpdate(int charaId, const char* name, int slot);
/* 1 when chara/fallback_ai.lua exists. */
int ShScript_CharaFallbackExists(void);
/* 1 when chara/<name>/behavior.lua exists in the base data or a mod (no Lua state is made). */
int ShScript_CharaBehaviorExists(const char* name);
/* Calls fn(a, b) in the type's running behaviour state; 1 with *out set, 0 otherwise. */
int ShScript_CharaBehaviorCall(int charaId, const char* fn, int a, int b, long long* out);
/* The type whose behaviour script is running now (the one the three calls above run, the state's key: Unknown23
 * while the Incubator's slot runs its fight), -1 outside them. The native helpers of one type's behaviour ask it
 * which type's data and map effects they serve. */
int ShScript_CharaBehaviorRunning(void);

/* Loads and runs gamedata/scripts/game.lua (or the highest-priority mod's copy) and calls
 * its on_boot(). Missing script: fine, all hooks are no-ops. A script error is logged and
 * leaves the hooks inert. Safe to call again: the second call is a no-op. */
void ShScript_Boot(const char* gamedataDir, const char* modsDir);

/* Mod settings (sh.config): reads `path` (mods_config.cfg next to config.cfg) and saves every
 * change there. Call before ShScript_Boot, whose menu scripts declare and read the settings. */
void ShScript_SetModConfigPath(const char* path);

/* Hooks of the running script; no-ops without one. */
void ShScript_OnNewGame(void);
void ShScript_OnMapLoad(const char* mapName); /* "map1_s02"; NULL treated as empty */
void ShScript_OnUpdate(void);                 /* every frame; returns at once without a hook */

/* `mods reload`: the menu scripts (gamedata/menus, the mods' menus/) and the game script with every
 * mod's scripts/main.lua run again, in new states, at the start of the next ShScript_OnUpdate (the
 * request may come from inside a script). on_boot runs again; mod.storage and the coroutines start
 * empty; the map's scripts stay until the next map load. */
void ShScript_RequestReload(void);

/* The modding tools (Lua editor): runs the current map's events.lua again (the base file
 * and the mods' layers, read from disk now) in a new state with its on_boot, without loading the map again.
 * The map's Lua globals and coroutines start over; the map's engine state (map vars, flags) stays. */
void ShScript_ReloadMapScript(void);
/* Runs a Lua snippet in the game script's state (target 0) or the map script's (1); `out` gets the printed text
 * and the results or the error (truncated to `cap`). 1 on success. */
int ShScript_RunSnippet(int target, const char* code, char* out, int cap);
/* Runs a mod's test file (docs/modding/scripting.md, "Tests") in the game script's state (0) or the map
 * script's (1) with the tools' `harness`; `out` gets the report (Host::run_test_file). 1 on success. */
int ShScript_RunTestFile(int target, const char* harness, const char* code, const char* chunk_name, char* out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* SH_ENGINE_SCRIPT_SH_SCRIPT_H */
