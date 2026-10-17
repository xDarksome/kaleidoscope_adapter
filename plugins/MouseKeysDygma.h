/* Kaleidoscope-MouseKeys - Mouse keys for Kaleidoscope.
 * Copyright (C) 2017-2018  Keyboard.io, Inc.
 * Copyright (C) 2025 Dygma Lab S.L. www.dygma.com
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "kbd_core.h"

#include "kaleidoscope/Runtime.h"
#include "kaleidoscope/plugin/MouseKeys/MouseKeyDefs.h"
#include "kaleidoscope/plugin/MouseKeys/MouseWarpModes.h"
#include "kaleidoscope/plugin/MouseKeys/MouseWrapper.h"

// Alternative mouse speed keys: movement keys with this bit set in their
// keycode (e.g. 0x5081 for up, instead of 0x5001) move the pointer using the
// alternative speed, acceleration and speed limit below, instead of the
// primary `mouse.*` settings.
#define KEY_MOUSE_ALTERNATIVE  0b10000000

// The alternative mouse settings, which can be overridden at build time.
#ifndef MOUSEKEYS_ALTERNATIVE_SPEED
#define MOUSEKEYS_ALTERNATIVE_SPEED          1
#endif
#ifndef MOUSEKEYS_ALTERNATIVE_ACCEL_SPEED
#define MOUSEKEYS_ALTERNATIVE_ACCEL_SPEED    1
#endif
#ifndef MOUSEKEYS_ALTERNATIVE_SPEED_LIMIT
#define MOUSEKEYS_ALTERNATIVE_SPEED_LIMIT    127
#endif

// Speed multipliers applied while Shift is held, to the primary and to the
// alternative mouse speed keys respectively (e.g. 0.5 or 2.0). They scale both
// the movement and the speed limit, and can be overridden at build time.
#ifndef MOUSEKEYS_SHIFT_MULTIPLIER
#define MOUSEKEYS_SHIFT_MULTIPLIER                1.0
#endif
#ifndef MOUSEKEYS_ALTERNATIVE_SHIFT_MULTIPLIER
#define MOUSEKEYS_ALTERNATIVE_SHIFT_MULTIPLIER    1.0
#endif

namespace kaleidoscope {
namespace plugin {
class MouseKeys_ : public kaleidoscope::Plugin {
 public:

  typedef struct PACK
  {
      uint8_t speed;
      uint16_t speedDelay;
      uint8_t accelSpeed;
      uint16_t accelDelay;
      uint8_t wheelSpeed;
      uint16_t wheelDelay;
      uint8_t speedLimit;
  } MouseKeys_config_t;

  MouseKeys_(void) {}

  static void setWarpGridSize(uint8_t grid_size);
  static void setSpeedLimit(uint8_t speed_limit);

  // Kaleidoscope Focus library functions
  EventHandlerResult onFocusEvent(const char *command);
  // On Setup handler function
  EventHandlerResult onSetup();

  EventHandlerResult beforeReportingState();
  EventHandlerResult afterEachCycle();
  EventHandlerResult onKeyswitchEvent(Key &mappedKey, KeyAddr key_addr, uint8_t keyState);

 private:
  static const MouseKeys_config_t * p_MouseKeys_config;

  static uint8_t mouseMoveIntent;
  static kbdtimer_t move_start_timer;
  static kbdtimer_t accel_start_timer;
  static kbdtimer_t wheel_start_timer;

  static void scrollWheel(uint8_t keyCode);
  static void move(int8_t x, int8_t y, uint8_t speedLimit, uint16_t multiplier);

  static void mem_speed_save( uint8_t speed );
  static void mem_speed_delay_save( uint16_t speedDelay );
  static void mem_accel_speed_save( uint8_t accelSpeed );
  static void mem_accel_delay_save( uint16_t accelDelay );
  static void mem_wheel_speed_save( uint8_t wheelSpeed );
  static void mem_wheel_delay_save( uint16_t wheelDelay );
  static void mem_speed_limit_save( uint8_t speedLimit );
};
}
}

extern kaleidoscope::plugin::MouseKeys_ MouseKeys;
