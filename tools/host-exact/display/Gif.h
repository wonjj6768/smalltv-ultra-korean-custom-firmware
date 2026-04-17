#pragma once

#include "Arduino.h"

class Gif {
   public:
    auto begin() -> bool { return true; }
    auto playOne(const String&) -> bool { playing_ = true; return true; }
    auto update() -> void { playing_ = false; }
    auto stop() -> void { playing_ = false; }
    auto isPlaying() const -> bool { return playing_; }
    auto setLoopEnabled(bool) -> void {}

   private:
    bool playing_ = false;
};
