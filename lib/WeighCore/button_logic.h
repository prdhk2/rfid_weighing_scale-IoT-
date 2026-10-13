#pragma once

#include <cstdint>

// Debounced push button with press, release and long-press edges. Pure C++,
// unit-tested natively in test/test_button_logic/.
//
// The original firmware read the buttons as levels: holding "Masuk" fired a
// new upload every time the screen came back, and a button held through a
// screen change acted on the next screen too. Here every action is an edge,
// and suppressUntilRelease() makes the current press count for nothing - the
// app calls it on every screen change, so a press only ever acts on the
// screen it started on.
class DebouncedButton {
 public:
  struct Events {
    bool pressed = false;    // stable press began (not suppressed)
    bool released = false;   // stable release (of a press that was not suppressed)
    bool longPress = false;  // held for longPressMs, fired once per press
  };

  DebouncedButton(uint32_t debounceMs, uint32_t longPressMs)
      : debounceMs_(debounceMs), longPressMs_(longPressMs) {}

  // rawPressed: the electrical level already converted to "pressed". Call
  // regularly (every loop pass); timing is millis()-based and wrap-safe.
  Events update(bool rawPressed, uint32_t now) {
    Events events;
    if (!initialised_) {
      // A button already down at power-up (or shorted) must not fire.
      initialised_ = true;
      raw_ = stable_ = rawPressed;
      suppressed_ = rawPressed;
      rawSince_ = now;
      return events;
    }

    if (rawPressed != raw_) {
      raw_ = rawPressed;
      rawSince_ = now;
    }
    if (raw_ != stable_ && now - rawSince_ >= debounceMs_) {
      stable_ = raw_;
      if (stable_) {
        pressedAt_ = now;
        longFired_ = false;
        events.pressed = !suppressed_;
      } else {
        events.released = !suppressed_;
        suppressed_ = false;
      }
    }
    if (stable_ && !suppressed_ && !longFired_ && now - pressedAt_ >= longPressMs_) {
      longFired_ = true;
      events.longPress = true;
    }
    return events;
  }

  bool isDown() const { return stable_; }

  // Ignores the press in progress (if any) until the button is released.
  void suppressUntilRelease() {
    if (stable_ || raw_) suppressed_ = true;
  }

 private:
  uint32_t debounceMs_;
  uint32_t longPressMs_;
  bool initialised_ = false;
  bool raw_ = false;
  bool stable_ = false;
  bool suppressed_ = false;
  bool longFired_ = false;
  uint32_t rawSince_ = 0;
  uint32_t pressedAt_ = 0;
};
