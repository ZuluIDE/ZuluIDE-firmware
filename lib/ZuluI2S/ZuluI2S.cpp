/*
    I2SIn and I2SOut for Raspberry Pi Pico
    Implements one or more I2S interfaces using DMA

    Copyright (c) 2022 Earle F. Philhower, III <earlephilhower@yahoo.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include <Arduino.h>
#include "ZuluI2S.h"
#include "zulu_pio_i2s.pio.h"
#include <pico/stdlib.h>


I2S::I2S(PinMode direction) {
    _running = false;
    _div_int = 1;
    _div_frac = 0;
    _bps = 16;
    _writtenHalf = false;
    _isOutput = direction == OUTPUT;
    _pinBCLK = 26;
    _pinDOUT = 28;
    _pinMCLK = 25;
    _MCLKenabled = false;
#ifdef PIN_I2S_BCLK
    _pinBCLK = PIN_I2S_BCLK;
#endif

#ifdef PIN_I2S_DOUT
    if (_isOutput) {
        _pinDOUT = PIN_I2S_DOUT;
    }
#endif

#ifdef PIN_I2S_DIN
    if (!_isOutput) {
        _pinDOUT = PIN_I2S_DIN;
    }
#endif
    _silenceSample = 0;
    _isLSBJ = false;
    _isTDM = false;
    _tdmChannels = 8;
    _swapClocks = false;
    _multMCLK = 256;
}

I2S::~I2S() {
    end();
}

bool I2S::setBCLK(pin_size_t pin) {
    if (_running || (pin > 28)) {
        return false;
    }
    _pinBCLK = pin;
    return true;
}


bool I2S::setMCLK(pin_size_t pin) {
    if (_running || (pin > 28)) {
        return false;
    }
    _pinMCLK = pin;
    return true;
}
bool I2S::setDATA(pin_size_t pin) {
    if (_running || (pin > 29)) {
        return false;
    }
    _pinDOUT = pin;
    return true;
}

bool I2S::setBitsPerSample(int bps) {
    if (_running || ((bps != 8) && (bps != 16) && (bps != 24) && (bps != 32))) {
        return false;
    }
    _bps = bps;
    return true;
}

bool I2S::setDivider(uint16_t div_int, uint8_t div_frac) {
    _div_int = div_int;
    _div_frac = div_frac;
    pio_sm_set_clkdiv_int_frac(_pio, _sm, _div_int, _div_frac);
    return true;
}

uint I2S::getPioDreq() {
    return pio_get_dreq(_pio, _sm, _isOutput);
}

volatile void *I2S::getPioFIFOAddr()
{
    return (volatile void *)&_pio->txf[_sm];
}

bool I2S::setSysClk(int samplerate) { // optimise sys_clk for desired samplerate
    if (samplerate % 11025 == 0) {
        set_sys_clock_khz(I2SSYSCLK_44_1, false); // 147.6 unsuccessful - no I2S no USB
        return true;
    }
    if (samplerate % 8000 == 0) {
        set_sys_clock_khz(I2SSYSCLK_8, false);
        return true;
    }
    return false;
}

bool I2S::setMCLKmult(int mult) {
    if (_running) {
        return false;
    }
    if ((mult % 64) == 0) {
        _MCLKenabled = true;
        _multMCLK = mult;
        return true;
    }
    return false;
}

bool I2S::setLSBJFormat() {
    if (_running || !_isOutput) {
        return false;
    }
    _isLSBJ = true;
    return true;
}

bool I2S::setTDMFormat() {
    if (_running || !_isOutput) {
        return false;
    }
    _isTDM = true;
    return true;
}

bool I2S::setTDMChannels(int channels) {
    if (_running || !_isOutput) {
        return false;
    }
    _tdmChannels = channels;
    return true;
}

bool I2S::swapClocks() {
    if (_running || !_isOutput) {
        return false;
    }
    _swapClocks = true;
    return true;
}

// void I2S::onTransmit(void(*fn)(void)) {
//     if (_isOutput) {
//         _cb = fn;
//         if (_running) {
//             _arb->setCallback(_cb);
//         }
//     }
// }

// void I2S::onReceive(void(*fn)(void)) {
//     if (!_isOutput) {
//         _cb = fn;
//         if (_running) {
//             _arb->setCallback(_cb);
//         }
//     }
// }


bool I2S::begin() {
    _running = true;
    _hasPeeked = false;
    _isHolding = 0;
    int off = 0;
    _i2s = new PIOProgram(&pio_i2s_out_program);
    if (!_i2s->prepare(&_pio, &_sm, &off)) {
        _running = false;
        delete _i2s;
        _i2s = nullptr;
        return false;
    }
    pio_i2s_out_program_init(_pio, _sm, off, _pinDOUT, _pinBCLK, _bps, _swapClocks);
    setDivider(_div_int, _div_frac);

    pio_sm_set_enabled(_pio, _sm, true);
    _pio->txf[_sm] = 0xAAAAAAAA;
    return true;
}

void I2S::end() {
    if (_running) {
        pio_sm_set_enabled(_pio, _sm, false);
        _running = false;
        // delete _arb;
        // _arb = nullptr;
        delete _i2s;
        _i2s = nullptr;
    }
}
