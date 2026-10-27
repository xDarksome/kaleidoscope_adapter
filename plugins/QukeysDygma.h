/* -*- mode: c++ -*-
 * Kaleidoscope-Qukeys -- Assign two keycodes to a single key
 * Copyright (C) 2017-2019  Michael Richters
 * Copyright (C) 2025 Dygma Lab S.L. www.dygma.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "kbd_core.h"

#include "kaleidoscope/Runtime.h"
#include <Kaleidoscope-Ranges.h>
#include "kaleidoscope/plugin/Qukeys/KeyAddrEventQueue.h"

// DualUse Key definitions for Qukeys in the keymap
#define MT(mod, key) Key(                                               \
    kaleidoscope::ranges::DUM_FIRST +                                   \
    (((Key_ ## mod).getKeyCode() - Key_LeftControl.getKeyCode()) << 8) + \
    (Key_ ## key).getKeyCode() |                                        \
    ((Key_ ## mod).getKeyCode() & 0x100) \
)
#define SFT_T(key) MT(LeftShift, key)
#define CTL_T(key) MT(LeftControl, key)
#define ALT_T(key) MT(LeftAlt, key)
#define GUI_T(key) MT(LeftGui, key)

// Define `QUKEYS_SPECULATIVE_HOLD` to enable speculative hold for Control and
// Shift by default (see `Qukeys::setSpeculativeHoldMods()`).
#ifdef QUKEYS_SPECULATIVE_HOLD
#define _QUKEYS_SPECULATIVE_HOLD_MODS_DEFAULT                          \
  (kaleidoscope::plugin::Qukeys::mod_left_control |                    \
   kaleidoscope::plugin::Qukeys::mod_left_shift |                      \
   kaleidoscope::plugin::Qukeys::mod_right_control |                   \
   kaleidoscope::plugin::Qukeys::mod_right_shift)
#else
#define _QUKEYS_SPECULATIVE_HOLD_MODS_DEFAULT 0
#endif

// `QUKEYS_HOLD_ON_OTHER_KEY_PRESS` sets which qukeys use hold-on-other-key-press
// by default (see `Qukeys::setHoldOnOtherKeyPress()`): a bitmask of
// `Qukeys::hold_on_other_key_press_layer` (1) and
// `Qukeys::hold_on_other_key_press_mod` (2). Off by default.
#ifndef QUKEYS_HOLD_ON_OTHER_KEY_PRESS
#define QUKEYS_HOLD_ON_OTHER_KEY_PRESS 0
#endif

// `QUKEYS_QUICK_TAP_TERM` sets the default quick tap term in milliseconds (see
// `Qukeys::setQuickTapTerm()`). 0, the default, turns it off.
#ifndef QUKEYS_QUICK_TAP_TERM
#define QUKEYS_QUICK_TAP_TERM 0
#endif

#define LT(layer, key) Key(kaleidoscope::ranges::DUL_FIRST + (layer << 8) + (Key_ ## key).getKeyCode())

/*#define _DEPRECATED_MESSAGE_QUKEY_ROW_COL_CONSTRUCTOR                           \
  "The `Qukey(layer, row, col, alternate_key)` constructor using separate\n"    \
  "`row` & `col` parameters has been deprecated. Please replace this\n"         \
  "constructor with the new `KeyAddr` version:\n"                               \
  "    `Qukey(layer, KeyAddr(row, col), alternate_key)`"                        \
  "The deprecated function will be removed after 2020-12-31"

#define _DEPRECATED_MESSAGE_QUKEYS_TIMEOUT                                      \
  "The Qukeys.setTimeout() function has been renamed to setHoldTimeout()\n"     \
  "in order to distinguish it from the other timeouts more clearly."            \
  "The deprecated function will be removed after 2020-12-31"

#define _DEPRECATED_MESSAGE_QUKEYS_RELEASEDELAY                                 \
  "The Qukeys.setReleaseDelay() is now obsolete. The rollover grace period\n"   \
  "for qukey release has been replaced with an improved version based on\n"     \
  "the percentage of overlap between the qukey and the subsequent\n"            \
  "key. Please use the setOverlapThreshold() function instead."                 \
  "The deprecated function will be removed after 2020-12-31" */

namespace kaleidoscope {
namespace plugin {

// Data structure for an individual qukey
struct Qukey {
  // The layer this qukey is mapped on.
  int8_t layer;
  // The keyswitch address of the qukey.
  KeyAddr addr;
  // The alternake Key value this qukey should use (when held).
  Key alternate_key;

  // This is the constructor that should be used when creating a Qukey object in
  // the PROGMEM array that will be used by Qukeys (i.e. in the `QUKEYS()`
  // macro).
  constexpr
  Qukey(int8_t layer, KeyAddr k, Key alternate_key)
    : layer(layer), addr(k), alternate_key(alternate_key) {}
  // This constructor is here so that we can create an empty Qukey object in RAM
  // into which we can copy the values from a PROGMEM Qukey object.
  Qukey() = default;

//  // Old-style Qukey constructor for backwards compatibility.
//  DEPRECATED(QUKEY_ROW_COL_CONSTRUCTOR)
//  constexpr
//  Qukey(int8_t layer, uint8_t row, uint8_t col, Key alternate_key)
//    : layer(layer), addr(KeyAddr(row, col)), alternate_key(alternate_key) {}
};


class Qukeys : public kaleidoscope::Plugin {

 public:
    typedef struct PACK
    {
        // The number of milliseconds until a qukey held on its own will take on its
        // alternate state (or primary state, in the case of a SpaceCadet-type qukey).
        uint16_t hold_timeout;

        // This variable stores the percentage number between 0 and 99 that determines
        // how forgiving the plugin is of rollover from a qukey to a modified key.
        uint8_t overlap_threshold;

        // The minimum number of milliseconds a qukey must be held before it is
        // allowed to take on its alternate key value (to limit unintended modifiers
        // for very fast typists).
        uint8_t minimum_hold_time;

        // The minimum interval in milliseconds between the previous keypress and the
        // press of a qukey required to make the qukey eligible to take on its
        // alternate value.
        uint8_t minimum_prior_interval;

    } Qukeys_config_t;

 public:
  // Methods for turning the plugin on and off.
  void activate() {
    active_ = true;
  }
  void deactivate() {
    active_ = false;
  }
  void toggle() {
    active_ = !active_;
  }

//  // Set the timeout (in milliseconds) for a held qukey. If a qukey is held at
//  // least this long, it will take on its alternate value (unless its primary
//  // value is a modifier -- i.e. a SpaceCadet type key).
//  void setHoldTimeout(uint16_t hold_timeout) {
//    hold_timeout_ = hold_timeout;
//  }
//
//  // Set the percentage of the duration of a subsequent key's press that must
//  // overlap with the qukey preceding it above which the qukey will take on its
//  // alternate key value. In other words, if the user presses qukey `Q`, then
//  // key `K`, and the overlap of the two keys (`O`) is less than this
//  // percentage, the qukey will take on its primary value, but if it's greater
//  // (i.e. `K` spends a very small fraction of its time pressed after the `Q` is
//  // released), the qukey will take on its alternate value.
//  void setOverlapThreshold(uint8_t percentage) {
//    // Only percentages less than 100% are meaningful. 0% means to turn off the
//    // rollover grace period and rely solely on release order to determine the
//    // qukey's state.
//    if (percentage < 100) {
//      overlap_threshold_ = percentage;
//    } else {
//      overlap_threshold_ = 0;
//    }
//  }
//
//  // Set the minimum length of time a qukey must be held before it can resolve
//  // to its alternate key value. If a qukey is pressed and released in less than
//  // this number of milliseconds, it will always produce its primary key value.
//  void setMinimumHoldTime(uint8_t min_hold_time) {
//    minimum_hold_time_ = min_hold_time;
//  }
//
//  // Set the minimum interval between the previous keypress and the qukey press
//  // to make the qukey eligible to become its alternate keycode.
//  void setMinimumPriorInterval(uint8_t min_interval) {
//    minimum_prior_interval_ = min_interval;
//  }

  // HID modifier bits, for use with `setSpeculativeHoldMods()`.
  static constexpr uint8_t mod_left_control{1 << 0};
  static constexpr uint8_t mod_left_shift{1 << 1};
  static constexpr uint8_t mod_left_alt{1 << 2};
  static constexpr uint8_t mod_left_gui{1 << 3};
  static constexpr uint8_t mod_right_control{1 << 4};
  static constexpr uint8_t mod_right_shift{1 << 5};
  static constexpr uint8_t mod_right_alt{1 << 6};
  static constexpr uint8_t mod_right_gui{1 << 7};

  // Set the modifiers eligible for speculative hold, as a bitmask of the
  // `mod_*` constants above (0 turns the feature off). Like QMK's
  // `SPECULATIVE_HOLD`, while a qukey whose alternate (hold) value is a
  // modifier is still undecided, the modifier is sent to the host right away,
  // and withdrawn again if the qukey turns out to be a tap. This makes mod-taps
  // usable for e.g. Ctrl+click or Shift+scroll with a mouse. A qukey is only
  // eligible if all of its modifiers are in the mask. Alt and GUI are excluded
  // from the default mask because tapping them on their own can have side
  // effects on some systems (e.g. opening a menu).
  void setSpeculativeHoldMods(uint8_t mods) {
    speculative_hold_mods_ = mods;
  }

  // Qukey types for `setHoldOnOtherKeyPress()`.
  static constexpr uint8_t hold_on_other_key_press_layer{1 << 0};
  static constexpr uint8_t hold_on_other_key_press_mod{1 << 1};

  // Set which qukeys use hold-on-other-key-press, as a bitmask of the
  // `hold_on_other_key_press_*` constants above (0 turns it off). Like QMK's
  // `HOLD_ON_OTHER_KEY_PRESS`, such a qukey takes on its alternate (hold)
  // value as soon as another key is pressed while it is held, instead of
  // waiting for that key to be released or for the hold timeout. The minimum
  // hold time doesn't apply; the minimum prior interval still does.
  void setHoldOnOtherKeyPress(uint8_t types) {
    hold_on_other_key_press_ = types;
  }

  // Set the quick tap term in milliseconds (0 turns it off). Like QMK's
  // `QUICK_TAP_TERM`, if a qukey that was just tapped is pressed again within
  // this time after its release, with no other key pressed in between, it
  // takes on its primary (tap) value right away, so holding it makes the tap
  // key repeat instead of taking on its alternate (hold) value.
  void setQuickTapTerm(uint16_t quick_tap_term) {
    quick_tap_term_ = quick_tap_term;
  }

  // Function for defining the array of qukeys data (in PROGMEM). It's a
  // template function that takes as its sole argument an array reference of
  // size `_qukeys_count`, so there's no need to use `sizeof` to calculate the
  // correct size, and pass it as a separate parameter.
  template <uint8_t _qukeys_count>
  void configureQukeys(Qukey const(&qukeys)[_qukeys_count]) {
    qukeys_ = qukeys;
    qukeys_count_ = _qukeys_count;
  }

//  // Obsolete configuration functions.
//  DEPRECATED(QUKEYS_TIMEOUT)
//  void setTimeout(uint16_t time_limit) {
//    setHoldTimeout(time_limit);
//  }
//  DEPRECATED(QUKEYS_RELEASEDELAY)
//  void setReleaseDelay(uint8_t release_delay) {
//    if (release_delay == 0) {
//      overlap_threshold_ = 0;
//    } else {
//      overlap_threshold_ = 60;
//    }
//  }

  // A wildcard value for a qukey that exists on every layer.
  static constexpr int8_t layer_wildcard{-1};

  // Kaleidoscope hook functions.
  EventHandlerResult onKeyswitchEvent(Key &mapped_key,
                                      KeyAddr key_addr,
                                      uint8_t key_state);
  EventHandlerResult beforeReportingState();

  // Kaleidoscope Focus library functions
  EventHandlerResult onFocusEvent(const char *command);

  // Reserve storage function for EEPROM_SETTINGS and load stored values if any
  EventHandlerResult onSetup();


 private:
  // An array of Qukey objects in PROGMEM.
  Qukey const * qukeys_{nullptr};
  uint8_t qukeys_count_{0};

  // The maximum number of events in the queue at a time.
  static constexpr uint8_t queue_capacity_{8};

  // The event queue stores a series of press and release events.
  KeyAddrEventQueue<queue_capacity_> event_queue_;

  // This determines whether the plugin is on or off.
  bool active_{true};

  const Qukeys_config_t * p_Qukeys_config;

//  // This variable stores the percentage number between 0 and 99 that determines
//  // how forgiving the plugin is of rollover from a qukey to a modified key.
//  uint8_t overlap_threshold_{80};

//  // The number of milliseconds until a qukey held on its own will take on its
//  // alternate state (or primary state, in the case of a SpaceCadet-type qukey).
//  uint16_t hold_timeout_{250};

//  // The minimum number of milliseconds a qukey must be held before it is
//  // allowed to take on its alternate key value (to limit unintended modifiers
//  // for very fast typists).
//  uint8_t minimum_hold_time_{50};
//
//  // The minimum interval in milliseconds between the previous keypress and the
//  // press of a qukey required to make the qukey eligible to take on its
//  // alternate value.
//  uint8_t minimum_prior_interval_{75};

  // Timestamp of the keypress event immediately prior to the queue head event.
  // The initial value is 256 to ensure that it won't trigger an error if a
  // qukey is pressed before `minimum_prior_interval_` milliseconds after the
  // keyboard powers on, and that value can only be as high as 255.
  uint16_t prior_keypress_timestamp_{256};

  // This is a guard against re-processing events when qukeys flushes them from
  // its event queue. We can't just use an "injected" key state flag, because
  // that would cause other plugins to also ignore the event.
  bool flushing_queue_{false};

  // Quick tap term in milliseconds (0 is off).
  uint16_t quick_tap_term_{QUKEYS_QUICK_TAP_TERM};

  // The qukey that was last tapped, and when its release was flushed, for
  // quick tap. `quick_tap_pending_addr_` is the qukey whose tap press was
  // flushed, until its release is.
  KeyAddr quick_tap_addr_{};
  KeyAddr quick_tap_pending_addr_{};
  uint16_t quick_tap_release_time_{0};

  // Bitmask of the qukey types using hold-on-other-key-press (0 is off).
  uint8_t hold_on_other_key_press_{QUKEYS_HOLD_ON_OTHER_KEY_PRESS};

  // Bitmask of the modifiers eligible for speculative hold (0 is off).
  uint8_t speculative_hold_mods_{_QUKEYS_SPECULATIVE_HOLD_MODS_DEFAULT};

  // The address and the alternate Key value of the qukey whose modifier is
  // currently being applied speculatively, if any.
  KeyAddr speculative_hold_addr_{};
  Key speculative_hold_key_{Key_Transparent};

  // A cache of the current qukey's primary and alternate key values, so we
  // don't have to keep looking them up from PROGMEM.
  struct {
    Key primary_key{Key_Transparent};
    Key alternate_key{Key_Transparent};
  } queue_head_;

  // Internal helper methods.
  bool processQueue();
  void flushEvent(Key event_key);
  bool isQukey(KeyAddr k);
  bool isDualUseKey(Key key);
  bool releaseDelayed(uint16_t overlap_start, uint16_t overlap_end) const;
  bool isKeyAddrInQueueBeforeIndex(KeyAddr k, uint8_t index) const;
  void startSpeculativeHold();
  void updateSpeculativeHold();
  static uint8_t getModifiers(Key key);

  void mem_hold_timeout_save( uint16_t hold_timeout );
  void mem_overlap_threshold_save( uint8_t overlap_threshold );
  void mem_minimum_hold_time_save( uint8_t minimum_hold_time );
  void mem_minimum_prior_interval_save( uint8_t minimum_prior_interval );
};

// This function returns true for any key that we expect to be used chorded with
// a "modified" key. This includes actual keyboard modifiers, but also layer
// shift keys. Used for determining if a qukey is a SpaceCadet-type key.
bool isModifierKey(Key key);

} // namespace plugin {
} // namespace kaleidoscope {

extern kaleidoscope::plugin::Qukeys qukeys;

// Macro for use in sketch file to simplify definition of the qukeys array and
// guarantee that the count is set correctly. This is considerably less
// important than it used to be, with the `configureQukeys()` function taking
// care of guaranteeing the correct count setting.
#define QUKEYS(qukey_defs...) {                                         \
    static kaleidoscope::plugin::Qukey const qk_table[] PROGMEM = {     \
      qukey_defs                                                        \
    };                                                                  \
    Qukeys.configureQukeys(qk_table);                                   \
}
