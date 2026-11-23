#include "engine/loop/game_random.h"

namespace sh::loop {

GameRandom& game_random() {
    static GameRandom rng;
    return rng;
}

}  // namespace sh::loop
