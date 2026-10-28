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

#include <Arduino.h>

#include "MouseKeysDygma.h"
#include "kbdfal_ll_memory.h"

#include "kaleidoscope/Runtime.h"
#include "Kaleidoscope-FocusSerial.h"
#include "kaleidoscope/keyswitch_state.h"

#define DEFAULT_SPEED                   1
#define DEFAULT_SPEED_DELAY             1
#define DEFAULT_ACCEL_SPEED             1
#define DEFAULT_ACCEL_DELAY             64
#define DEFAULT_WHEEL_SPEED             1
#define DEFAULT_WHEEL_DELAY             50
#define DEFAULT_SPEED_LIMIT             127

// Speed multipliers as 8.8 fixed point (256 = 1.0).
static constexpr uint16_t SHIFT_MULTIPLIER =
    uint16_t(MOUSEKEYS_SHIFT_MULTIPLIER * 256 + 0.5);
static constexpr uint16_t ALTERNATIVE_SHIFT_MULTIPLIER =
    uint16_t(MOUSEKEYS_ALTERNATIVE_SHIFT_MULTIPLIER * 256 + 0.5);
static constexpr uint16_t NO_MULTIPLIER = 256;

namespace kaleidoscope {
namespace plugin {

const MouseKeys_::MouseKeys_config_t * MouseKeys_::p_MouseKeys_config = nullptr;

uint8_t MouseKeys_::mouseMoveIntent;
uint8_t MouseKeys_::mouseWheelIntent;

kbdtimer_t MouseKeys_::move_start_timer;
kbdtimer_t MouseKeys_::accel_start_timer;
kbdtimer_t MouseKeys_::wheel_start_timer;

void MouseKeys_::setWarpGridSize(uint8_t grid_size) {
  MouseWrapper.warp_grid_size = grid_size;
}

void MouseKeys_::setSpeedLimit(uint8_t speed_limit) {
  MouseWrapper.speedLimit = speed_limit;
}

// The speed multiplier (8.8 fixed point) to apply to the primary or the
// alternative mouse/wheel keys: the set's Shift multiplier while Shift is held.
uint16_t MouseKeys_::shiftMultiplier(bool alternative) {
  bool shift = kaleidoscope::Runtime.hid().keyboard().isModifierKeyActive(Key_LeftShift) ||
               kaleidoscope::Runtime.hid().keyboard().isModifierKeyActive(Key_RightShift);
  if (!shift)
    return NO_MULTIPLIER;
  return alternative ? ALTERNATIVE_SHIFT_MULTIPLIER : SHIFT_MULTIPLIER;
}

// Scroll by the wheel speed of the primary or alternative wheel keys, scaled
// by the Shift multiplier. Fractional amounts (in 1/256ths of a wheel step)
// add up over time, so e.g. a multiplier of 0.5 on a speed of 1 scrolls one
// step every other time.
static int32_t wheelRemainder = 0;

void MouseKeys_::scrollWheel(uint8_t keyCode) {
  if (!kbdtimer_check(&wheel_start_timer))
    return;

  bool alternative = keyCode & KEY_MOUSE_ALTERNATIVE;
  uint8_t wheelSpeed = alternative ? MOUSEKEYS_ALTERNATIVE_WHEEL_SPEED : p_MouseKeys_config->wheelSpeed;
  uint16_t wheelDelay = alternative ? MOUSEKEYS_ALTERNATIVE_WHEEL_DELAY : p_MouseKeys_config->wheelDelay;

  kbdtimer_set_ms( &wheel_start_timer, wheelDelay );

  int32_t amount = (int32_t)wheelSpeed * shiftMultiplier(alternative) + wheelRemainder;
  int32_t steps = amount / 256;
  if (steps > 127) steps = 127;
  wheelRemainder = amount - steps * 256;
  if (steps == 0)
    return;

  if (keyCode & KEY_MOUSE_UP)
    kaleidoscope::Runtime.hid().mouse().move(0, 0, steps);
  else if (keyCode & KEY_MOUSE_DOWN)
    kaleidoscope::Runtime.hid().mouse().move(0, 0, -steps);
  else if (keyCode & KEY_MOUSE_LEFT)
    kaleidoscope::Runtime.hid().mouse().move(0, 0, 0, -steps);
  else if (keyCode & KEY_MOUSE_RIGHT)
    kaleidoscope::Runtime.hid().mouse().move(0, 0, 0, steps);
}

// To approximate a sine wave, this uses two parabolas. Acceleration begins
// slowly, grows rapidly in the middle, and slows again near the top.
// (Same curve as MouseWrapper_::acceleration().)
static uint8_t acceleration(uint8_t cycles) {
  if (cycles < 128) {
    uint16_t c2 = cycles * cycles;
    return 1 + (c2 >> 7);
  } else {
    uint16_t remaining_cycles = 256 - cycles;
    uint16_t c2 = remaining_cycles * remaining_cycles;
    return 255 - (c2 >> 7);
  }
}

// Get the diagonalized version of a value, i.e. value * sqrt(2) / 2. If the
// value ends up being zero, return the original value instead.
static int16_t diagonalize(int16_t value) {
  int16_t diagonalValue = value * 99 / 140;
  return (diagonalValue == 0 ? value : diagonalValue);
}

static int32_t clampMove(int32_t value, int32_t limit) {
  if (value > limit) return limit;
  if (value < -limit) return -limit;
  return value;
}

// Move the pointer, like MouseWrapper_::move(), but with a speed multiplier
// (8.8 fixed point) applied to both the movement and the speed limit. The
// movement is computed in 1/256ths of a sub-pixel, so that fractional speeds
// (e.g. a multiplier of 0.5 on a speed of 1) still add up over time. Unlike
// MouseWrapper_::move(), the leftover is added after the speed limit is
// applied, so that movement capped below one pixel per step still adds up
// instead of getting stuck (which used to freeze diagonal movement with low
// speed limits).
void MouseKeys_::move(int8_t x, int8_t y, uint8_t speedLimit, uint16_t multiplier) {
  static int32_t remainderX = 0;
  static int32_t remainderY = 0;
  const int32_t unitsPerPixel = (int32_t)MouseWrapper.subpixelsPerPixel * 256;

  // Keep the result within what a single HID report can carry (127 pixels).
  int32_t limit = (int32_t)speedLimit * multiplier;
  if (limit > 127 * unitsPerPixel) limit = 127 * unitsPerPixel;

  if (x != 0 && y != 0) {
    // Diagonal movements get a diagonalized speed and speed limit, so that
    // the pointer moves at the same speed in all directions.
    limit = limit * 99 / 140;
    x = diagonalize(x);
    y = diagonalize(y);
  }

  int32_t step = (int32_t)acceleration(MouseWrapper.accelStep) * multiplier;
  int32_t moveX = 0;
  int32_t moveY = 0;

  if (x != 0) {
    moveX = clampMove(x * step, limit) + remainderX;
  }
  if (y != 0) {
    moveY = clampMove(y * step, limit) + remainderY;
  }

  MouseWrapper.end_warping();
  // Move by whole pixels, and save the leftover for later.
  int8_t pixelsX = moveX / unitsPerPixel;
  int8_t pixelsY = moveY / unitsPerPixel;
  kaleidoscope::Runtime.hid().mouse().move(pixelsX, pixelsY, 0);
  remainderX = moveX - pixelsX * unitsPerPixel;
  remainderY = moveY - pixelsY * unitsPerPixel;
}

EventHandlerResult MouseKeys_::afterEachCycle() {
  kaleidoscope::Runtime.hid().mouse().sendReport();
  kaleidoscope::Runtime.hid().mouse().releaseAllButtons();
  mouseMoveIntent = 0;
  mouseWheelIntent = 0;

  return EventHandlerResult::OK;
}

EventHandlerResult MouseKeys_::beforeReportingState() {
  // Scrolling is done here rather than when the wheel key is scanned, so that
  // a held Shift is already in the report (see shiftMultiplier()).
  if (mouseWheelIntent != 0) {
    scrollWheel(mouseWheelIntent);
  }

  if (mouseMoveIntent == 0) {
    MouseWrapper.accelStep = 0;
    return EventHandlerResult::OK;
  }

  if ( !kbdtimer_check(&move_start_timer) )
    return EventHandlerResult::OK;

  kbdtimer_set_ms( &move_start_timer, p_MouseKeys_config->speedDelay );

  int8_t moveX = 0, moveY = 0;

  // If any alternative mouse speed key is held, the whole movement uses the
  // alternative settings.
  bool alternative = mouseMoveIntent & KEY_MOUSE_ALTERNATIVE;
  uint8_t speed = alternative ? MOUSEKEYS_ALTERNATIVE_SPEED : p_MouseKeys_config->speed;
  uint8_t accelSpeed = alternative ? MOUSEKEYS_ALTERNATIVE_ACCEL_SPEED : p_MouseKeys_config->accelSpeed;
  uint8_t speedLimit = alternative ? MOUSEKEYS_ALTERNATIVE_SPEED_LIMIT : p_MouseKeys_config->speedLimit;

  // While Shift is held, the movement is scaled by that set's multiplier.
  uint16_t multiplier = shiftMultiplier(alternative);

  if ( kbdtimer_check( &accel_start_timer ) ) {
    if (MouseWrapper.accelStep < 255 - accelSpeed) {
      MouseWrapper.accelStep += accelSpeed;
    }
    kbdtimer_set_ms( &accel_start_timer, p_MouseKeys_config->accelDelay );
  }

  if (mouseMoveIntent & KEY_MOUSE_UP)
    moveY -= speed;
  if (mouseMoveIntent & KEY_MOUSE_DOWN)
    moveY += speed;

  if (mouseMoveIntent & KEY_MOUSE_LEFT)
    moveX -= speed;
  if (mouseMoveIntent & KEY_MOUSE_RIGHT)
    moveX += speed;

  move(moveX, moveY, speedLimit, multiplier);

  return EventHandlerResult::OK;
}

EventHandlerResult MouseKeys_::onKeyswitchEvent(Key &mappedKey, KeyAddr key_addr, uint8_t keyState) {
  if (mappedKey.getFlags() != (SYNTHETIC | IS_MOUSE_KEY))
    return EventHandlerResult::OK;

  if (mappedKey.getKeyCode() & KEY_MOUSE_BUTTON && !(mappedKey.getKeyCode() & KEY_MOUSE_WARP)) {
    uint8_t button = mappedKey.getKeyCode() & ~KEY_MOUSE_BUTTON;

    if (keyIsPressed(keyState)) {
      // Reset warp state on initial mouse button key-down only so we can use
      // warp keys to drag-and-drop:
      if (keyToggledOn(keyState)) {
        MouseWrapper.reset_warping();
      }

      kaleidoscope::Runtime.hid().mouse().pressButtons(button);
    } else if (keyToggledOff(keyState)) {
      kaleidoscope::Runtime.hid().mouse().releaseButtons(button);
      MouseWrapper.end_warping();
    }
  } else if (!(mappedKey.getKeyCode() & KEY_MOUSE_WARP)) {
    if (keyToggledOn(keyState)) {
      kbdtimer_set_ms( &move_start_timer, p_MouseKeys_config->speedDelay );
      kbdtimer_set_ms( &accel_start_timer, p_MouseKeys_config->accelDelay );
      /* After the Toggle On, there is no delay for the wheel. Let's set it to 0
       * and ask for another iteration. */
      wheel_start_timer = 0;
      wheelRemainder = 0;
      kbdpwr_sleep_postpone();
    }
    if (keyIsPressed(keyState)) {
      if (mappedKey.getKeyCode() & KEY_MOUSE_WHEEL) {
        // Injected wheel keys (e.g. from macros, which send their own
        // reports) scroll right away; physical ones in beforeReportingState().
        if (keyState & INJECTED) {
          scrollWheel(mappedKey.getKeyCode());
        } else {
          mouseWheelIntent = mappedKey.getKeyCode();
        }
      } else {
        mouseMoveIntent |= mappedKey.getKeyCode();
      }
    } else if (keyToggledOff(keyState)) {
      /* If a mouse key toggles off, we want to explicitly stop moving (or
       * scrolling) in that direction. We want to do this to support use-cases
       * where we send multiple reports per cycle (such as macros), and can't
       * rely on the main loop clearing the report for us. We do not want to
       * clear the whole report either, because we want any other mouse keys
       * to still have their desired effect. Therefore, we selectively stop
       * movement or scrolling. */
      mouseMoveIntent &= ~mappedKey.getKeyCode();
      bool x = false, y = false, vWheel = false, hWheel = false;

      if (mappedKey.getKeyCode() & KEY_MOUSE_UP ||
          mappedKey.getKeyCode() & KEY_MOUSE_DOWN) {
        if (mappedKey.getKeyCode() & KEY_MOUSE_WHEEL) {
          vWheel = true;
        } else {
          y = true;
        }
      } else if (mappedKey.getKeyCode() & KEY_MOUSE_LEFT ||
                 mappedKey.getKeyCode() & KEY_MOUSE_RIGHT) {
        if (mappedKey.getKeyCode() & KEY_MOUSE_WHEEL) {
          hWheel = true;
        } else {
          x = true;
        }
      }

      kaleidoscope::Runtime.hid().mouse().stop(x, y, vWheel, hWheel);
    }
  } else if (keyToggledOn(keyState)) {
    if (mappedKey.getKeyCode() & KEY_MOUSE_WARP && mappedKey.getFlags() & IS_MOUSE_KEY) {
      MouseWrapper.warp(((mappedKey.getKeyCode() & KEY_MOUSE_WARP_END) ? WARP_END : 0x00) |
                        ((mappedKey.getKeyCode() & KEY_MOUSE_UP) ? WARP_UP : 0x00) |
                        ((mappedKey.getKeyCode() & KEY_MOUSE_DOWN) ? WARP_DOWN : 0x00) |
                        ((mappedKey.getKeyCode() & KEY_MOUSE_LEFT) ? WARP_LEFT : 0x00) |
                        ((mappedKey.getKeyCode() & KEY_MOUSE_RIGHT) ? WARP_RIGHT : 0x00));
    }
  }

  return EventHandlerResult::EVENT_CONSUMED;
}

EventHandlerResult MouseKeys_::onFocusEvent(const char *command)
{
  if (::Focus.handleHelp(command, PSTR("mouse.speed\nmouse.speedDelay\nmouse.accelSpeed\nmouse.accelDelay\nmouse.wheelSpeed\nmouse.wheelDelay\nmouse.speedLimit")))
    return EventHandlerResult::OK;

  if (strncmp_P(command, PSTR("mouse."), 6) != 0)
    return EventHandlerResult::OK;

  if (strcmp_P(command + 6, PSTR("speed")) == 0)
  {
    if (::Focus.isEOL())
    {
      ::Focus.send(p_MouseKeys_config->speed);
    }
    else
    {
      uint8_t auxspeed;
      ::Focus.read(auxspeed);

      mem_speed_save( auxspeed );
    }
  }

  if (strcmp_P(command + 6, PSTR("speedDelay")) == 0)
  {
    if (::Focus.isEOL())
    {
      ::Focus.send(p_MouseKeys_config->speedDelay);
    }
    else
    {
      uint16_t auxspeedDelay = 0;
      uint8_t a{0};
      uint8_t b{0};
      ::Focus.read(a);
      while (!::Focus.isEOL())
      {
        ::Focus.read(b);
      }
      auxspeedDelay = ((b << 8) | a);

      mem_speed_delay_save( auxspeedDelay );
    }
  }

  if (strcmp_P(command + 6, PSTR("accelSpeed")) == 0)
  {
    if (::Focus.isEOL())
    {
      ::Focus.send(p_MouseKeys_config->accelSpeed);
    }
    else
    {
      uint8_t auxaccelSpeed;
      ::Focus.read(auxaccelSpeed);

      mem_accel_speed_save( auxaccelSpeed );
    }
  }

  if (strcmp_P(command + 6, PSTR("accelDelay")) == 0)
  {
    if (::Focus.isEOL())
    {
      ::Focus.send(p_MouseKeys_config->accelDelay);
    }
    else
    {
      uint16_t auxaccelDelay = 0;
      uint8_t a{0};
      uint8_t b{0};
      ::Focus.read(a);
      while (!::Focus.isEOL())
      {
        ::Focus.read(b);
      }
      auxaccelDelay = ((b << 8) | a);

      mem_accel_delay_save( auxaccelDelay );
    }
  }

  if (strcmp_P(command + 6, PSTR("wheelSpeed")) == 0)
  {
    if (::Focus.isEOL())
    {
      ::Focus.send(p_MouseKeys_config->wheelSpeed);
    }
    else
    {
      uint8_t auxwheelSpeed;
      ::Focus.read(auxwheelSpeed);

      mem_wheel_speed_save( auxwheelSpeed );
    }
  }

  if (strcmp_P(command + 6, PSTR("wheelDelay")) == 0)
  {
    if (::Focus.isEOL())
    {
      ::Focus.send(p_MouseKeys_config->wheelDelay);
    }
    else
    {
      uint16_t auxwheelDelay = 0;
      uint8_t a{0};
      uint8_t b{0};
      ::Focus.read(a);
      while (!::Focus.isEOL())
      {
        ::Focus.read(b);
      }
      auxwheelDelay = ((b << 8) | a);

      mem_wheel_delay_save( auxwheelDelay );
    }
  }

  if (strcmp_P(command + 6, PSTR("speedLimit")) == 0)
  {
    if (::Focus.isEOL())
    {
      ::Focus.send(p_MouseKeys_config->speedLimit);
    }
    else
    {
      uint8_t auxspeedLimit;
      ::Focus.read(auxspeedLimit);
      setSpeedLimit(auxspeedLimit);

      mem_speed_limit_save( auxspeedLimit );
    }
  }

  return EventHandlerResult::EVENT_CONSUMED;
}

EventHandlerResult MouseKeys_::onSetup(void) {
  result_t result = RESULT_ERR;

  kaleidoscope::Runtime.hid().mouse().setup();
  kaleidoscope::Runtime.hid().absoluteMouse().setup();

  result = kbdfal_ll_memory_item_request( KBDMEM_ITEM_TYPE_MOUSEKEYS, (const void **)&p_MouseKeys_config );
  ASSERT_DYGMA( result == RESULT_OK, "kbdfal_ll_memory_item_request failed" );

  if( p_MouseKeys_config->speed >= 255 )
  {
    mem_speed_save( DEFAULT_SPEED );
  }

  if( p_MouseKeys_config->speedDelay >= 65535 )
  {
    mem_speed_delay_save( DEFAULT_SPEED_DELAY );
  }

  if( p_MouseKeys_config->accelSpeed >= 255 )
  {
    mem_accel_speed_save( DEFAULT_ACCEL_SPEED );
  }

  if( p_MouseKeys_config->accelDelay >= 65535 )
  {
    mem_accel_delay_save( DEFAULT_ACCEL_DELAY );
  }

  if( p_MouseKeys_config->wheelSpeed >= 255 )
  {
    mem_wheel_speed_save( DEFAULT_WHEEL_SPEED );
  }

  if( p_MouseKeys_config->wheelDelay >= 65535 )
  {
    mem_wheel_delay_save( DEFAULT_WHEEL_DELAY );
  }

  if( p_MouseKeys_config->speedLimit >= 255 )
  {
    mem_speed_limit_save( DEFAULT_SPEED_LIMIT );
  }

  setSpeedLimit( p_MouseKeys_config->speedLimit );

  return EventHandlerResult::OK;

  UNUSED( result );
}

void MouseKeys_::mem_speed_save( uint8_t speed )
{
    result_t result = RESULT_ERR;

    result = kbdfal_ll_memory_data_save( &p_MouseKeys_config->speed, &speed, sizeof(p_MouseKeys_config->speed) );
    ASSERT_DYGMA( result == RESULT_OK, "kbdfal_ll_memory_save failed" );

    UNUSED( result );
}

void MouseKeys_::mem_speed_delay_save( uint16_t speedDelay )
{
    result_t result = RESULT_ERR;

    result = kbdfal_ll_memory_data_save( &p_MouseKeys_config->speedDelay, &speedDelay, sizeof(p_MouseKeys_config->speedDelay) );
    ASSERT_DYGMA( result == RESULT_OK, "kbdfal_ll_memory_save failed" );

    UNUSED( result );
}

void MouseKeys_::mem_accel_speed_save( uint8_t accelSpeed )
{
    result_t result = RESULT_ERR;

    result = kbdfal_ll_memory_data_save( &p_MouseKeys_config->accelSpeed, &accelSpeed, sizeof(p_MouseKeys_config->accelSpeed) );
    ASSERT_DYGMA( result == RESULT_OK, "kbdfal_ll_memory_save failed" );

    UNUSED( result );
}

void MouseKeys_::mem_accel_delay_save( uint16_t accelDelay )
{
    result_t result = RESULT_ERR;

    result = kbdfal_ll_memory_data_save( &p_MouseKeys_config->accelDelay, &accelDelay, sizeof(p_MouseKeys_config->accelDelay) );
    ASSERT_DYGMA( result == RESULT_OK, "kbdfal_ll_memory_save failed" );

    UNUSED( result );
}

void MouseKeys_::mem_wheel_speed_save( uint8_t wheelSpeed )
{
    result_t result = RESULT_ERR;

    result = kbdfal_ll_memory_data_save( &p_MouseKeys_config->wheelSpeed, &wheelSpeed, sizeof(p_MouseKeys_config->wheelSpeed) );
    ASSERT_DYGMA( result == RESULT_OK, "kbdfal_ll_memory_save failed" );

    UNUSED( result );
}

void MouseKeys_::mem_wheel_delay_save( uint16_t wheelDelay )
{
    result_t result = RESULT_ERR;

    result = kbdfal_ll_memory_data_save( &p_MouseKeys_config->wheelDelay, &wheelDelay, sizeof(p_MouseKeys_config->wheelDelay) );
    ASSERT_DYGMA( result == RESULT_OK, "kbdfal_ll_memory_save failed" );

    UNUSED( result );
}

void MouseKeys_::mem_speed_limit_save( uint8_t speedLimit )
{
    result_t result = RESULT_ERR;

    result = kbdfal_ll_memory_data_save( &p_MouseKeys_config->speedLimit, &speedLimit, sizeof(p_MouseKeys_config->speedLimit) );
    ASSERT_DYGMA( result == RESULT_OK, "kbdfal_ll_memory_save failed" );

    UNUSED( result );
}

}
}

kaleidoscope::plugin::MouseKeys_ MouseKeys;
