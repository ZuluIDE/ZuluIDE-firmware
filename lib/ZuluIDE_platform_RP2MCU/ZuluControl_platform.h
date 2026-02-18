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
   Sets the filename request pipe that is used by controllers to request filenames from a different core safely.
 */
void platform_set_controller_image_response_pipe(zuluide::pipe::ImageResponsePipe<zuluide::control::select_controller_source_t> *imageResponsePipe);

 /**
   Sets the input receiver, which handles receiving input from the hardware UI and performs updates to the UI as appropriate.
 */
void platform_set_input_interface(zuluide::control::InputReceiver* inputReceiver);

