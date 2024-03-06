/**
 * ZuluIDE™ - Copyright (c) 2024 Rabbit Hole Computing™
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#pragma once

namespace zuluide::control {
  /***
      Interface defines how a hardware interface provides user input to the UI.
      The hardware library uses this to tell the default MVC controller within ZuluIDE about hardward changes. However, it could be reused by a replacement/alternative UI if desired.
   */
  class InputReceiver {
  public:
    /***
        Notifies that the rotary encoder has moved by the given offset. Sign indicates the direction of rotation.
     */
    virtual void RotaryUpdate(int offset) = 0;

    /***
        Indidcates that the button included with the rotary encoder has been pressed.
     */
    virtual void RotaryButtonPressed() = 0;

    /***
        Indidcates that the primary button has been pressed.
     */
    virtual void PrimaryButtonPressed() = 0;

    /***
        Indidcates that the secondary button has been pressed.
     */
    virtual void SecondaryButtonPressed() = 0;
  };

  /***
      Used to initialize the hardward interface.
   */
  class InputInterface {
  public:
    /***
        Provides the hardware interface with the receiver that should be notified when events are occuring.
     */
    virtual void SetReciever(InputReceiver* reciever) = 0;

    /***
        Tells the hardware interface to start sending events to the receiver.
     */
    virtual void StartSendingEvents() = 0;

    /***
        Tells the hardware interface to stop sending events to the receiver.
     */
    virtual void StopSendingEvents() = 0;
  };
}
