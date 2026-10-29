/**
 * ZuluIDE™ - Copyright (c) 2026 Rabbit Hole Computing™
 *
 * ZuluIDE™ firmware is licensed under the GPL version 3 or any later version.
 *
 * https://www.gnu.org/licenses/gpl-3.0.html
 * ----
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the ZuluIDE Hardware Support Library Exception
 * (GPL-3.0_HSL_Exception.md), as published by Rabbit Hole Computing™.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/
#pragma once

#include <rotary_control.h>
#include <zuluide/observer_transfer.h>
#include <zuluide/status/system_status.h>
#include <display/display_ssd1306.h>
#include <zuluide/pipe/image_response_pipe.h>
#include <zuluide/pipe/image_request_pipe.h>
#include <zuluide/control/select_controller_src_type.h>
#include <zuluide/i2c/i2c_server.h>

extern zuluide::control::RotaryControl g_rotary_input;
extern zuluide::ObserverTransfer<zuluide::status::SystemStatus> *uiStatusController;
extern zuluide::DisplaySSD1306 display;
extern zuluide::pipe::ImageResponsePipe<zuluide::control::select_controller_source_t>* g_controllerImageResponsePipe;
extern zuluide::pipe::ImageResponsePipe<zuluide::i2c::i2c_server_source_t> g_I2CServerImageResponsePipe;
extern zuluide::pipe::ImageRequestPipe<zuluide::i2c::i2c_server_source_t> g_I2CServerImageRequestPipe;
extern zuluide::i2c::I2CServer g_I2cServer;

/**
   Attempts to determine whether the hardware UI or the web service is attached to the device.
 */
uint8_t platform_check_for_controller();

/**
   Throttle the UI while IDE data is moving. Returns true when the scrolling
   display refresh should run now. See [UI] busy_display_refresh_ms in zuluide.ini.
   Any input change restores the full refresh rate for 3 seconds.
 */
bool platform_ui_display_refresh_due();

/**
   Poll the buttons and rotary dial, throttled to [UI] busy_input_poll_ms while
   IDE data is moving. Any input change restores full rate polling for 3 seconds.
   Also delivers the events queued by platform_ui_sample_input().
 */
void platform_ui_poll_input();

/**
   Sample the buttons and rotary dial from inside IDE transfers, only during the
   3 second window after an input change. Events are queued, not delivered, so
   the UI state does not change in the middle of a command. Must only be called
   from the core that owns the I2C bus.
 */
void platform_ui_sample_input();

/**
   True during the 3 second window after a button or rotary dial change.
 */
bool platform_ui_input_active();

/**
   Sets the status controller connection used to process status events on the UI core.
 */
void platform_set_status_controller(zuluide::ObserverTransfer<zuluide::status::SystemStatus> *statusController);

/**
   Sets the display controller, the component tracking the state of the user interface.
 */
void platform_set_display_controller(zuluide::Observable<zuluide::control::DisplayState>& displayController);

/**
   Sets the controller that is used by the UI to change the system state.
 */
void platform_set_device_control(zuluide::status::DeviceControlSafe* deviceControl);

/**
 * Runs the connection routine for device control
 */
void platform_wifi_controller_connect();
/**
   Sets the filename request pipe that is used by controllers to request filenames from a different core safely.
 */
void platform_set_controller_image_response_pipe(zuluide::pipe::ImageResponsePipe<zuluide::control::select_controller_source_t> *imageResponsePipe);

 /**
   Sets the input receiver, which handles receiving input from the hardware UI and performs updates to the UI as appropriate.
 */
void platform_set_input_interface(zuluide::control::InputReceiver* inputReceiver);

/**
   Closes the I2C interface. Call during pre-reboot cleanup to release the bus cleanly.
 */
void platform_close_i2c();
/**
   Upgrade ZuluControl-firmware over I2C
 */
void platform_i2c_upgrade_zulucontrol_fw(const char* filename);