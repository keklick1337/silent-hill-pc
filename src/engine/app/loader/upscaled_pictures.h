#pragma once

// The 2D pictures of texture_upscale (docs/RENDER.md section 12): a TIM the game reads into video memory
// (title, menus, inventory and examine pictures, paper maps, save and loading screens, effect sheets) is drawn from
// its imported PNG, as a replacement picture (render/textures/replacement_store.h) over the TIM's place, and from
// its upscaled picture as soon as the cache has one. A replacement is stretched over the original's cells, so the
// game's texel coordinates stay as they are at any size.

#include <set>
#include <string>

#include "render/textures/replacement_store.h"

namespace sh::app {

// After a TIM `disc_path` ("TIM/MP_0TOWN.TIM") was written to video memory at `key` and no mod replaced it: puts its
// picture there. False, and nothing put, with texture_upscale off or for a picture that is not upscaled (fonts,
// masks, a picture whose palette mixes semi-transparent and solid colours: a replacement has no STP bit, so such a
// picture would blend differently). `stp_uniform` says the palette row (or the direct colours) does not mix them.
bool put_upscaled_picture(const std::string& disc_path, const sh::textures::PictureKey& key, bool stp_uniform);

// Better pictures arrived for these keys (upscale_cache.h texture_key): the ones still in video memory are put again.
void refresh_upscaled_pictures(const std::set<std::string>& keys);

}  // namespace sh::app
