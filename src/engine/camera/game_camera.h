#pragma once

// The game's camera (`camera` of the session): the one CameraRig the game runs on — its zone
// list and the whole persistent camera state — plus the follow-camera motion defaults that
// scripts retune at run time. This object is the storage: the frame update, the camera API, the
// replay dump, the debug views and the scripts all read and write it, nothing keeps a copy.

#include "engine/camera/camera_rig.h"

namespace sh::camera {

struct GameCamera {
    CameraRig rig;
    // Look-at motion of the automatic camera and of a script look-at given no parameters
    // (camera.json camera.default_watch).
    WatchMoveParams default_watch;
    // Camera motion of the automatic camera and of a script camera target given no parameters
    // (camera.json camera.user_camera).
    CamMoveParams user_camera;

    CameraState& state() { return rig.state(); }
    const CameraState& state() const { return rig.state(); }
};

// The motion defaults start as the built-in numbers of CameraConfig; the game sets those of the
// loaded camera.json on the first use of the camera API.
GameCamera& game_camera();

}  // namespace sh::camera
