// button.cpp //////////////////////////////////////////////////////////////////

#include "button.h"

// Constructor /////////////////////////////////////////////////////////////////

Button::Button(uint8_t pin, uint32_t debounceMs, uint32_t shortPressMaxMs, uint32_t longPressMinMs,
               PinLogic logic, ButtonCallback shortCallback, ButtonCallback longCallback)
    : button_pin(pin),
      debounce_ms(debounceMs),
      short_press_max_ms(shortPressMaxMs),
      long_press_min_ms(longPressMinMs),
      pin_logic(logic),
      short_callback(shortCallback),
      long_callback(longCallback) {
}

// Public Methods //////////////////////////////////////////////////////////////

void Button::begin() {
    // Configure pin based on logic type
    if (pin_logic == ACTIVE_LOW) {
        pinMode(button_pin, INPUT_PULLUP);
    } else {
        pinMode(button_pin, INPUT);
    }
    
    // Initialize state
    current_state = readPin();
    last_state = current_state;
    press_start_time = 0;
    long_press_fired = false;
    press_edge = false;
    release_edge = false;
}

void Button::update() {
    // Read current pin state
    last_state = current_state;
    current_state = readPin();
    
    // Update edge detection flags
    updateEdgeFlags();
    
    const uint32_t now = millis();
    
    if (current_state) {
        // Button is currently pressed
        if (press_edge) {
            // New press started
            press_start_time = now;
            long_press_fired = false;
        }
        
        const uint32_t hold_ms = now - press_start_time;
        
        // Long press: fire immediately when threshold is reached
        if (!long_press_fired && hold_ms >= long_press_min_ms) {
            if (long_callback) {
                long_callback();
            }
            long_press_fired = true;
        }
    } else {
        // Button was just released
        if (release_edge && press_start_time != 0) {
            const uint32_t hold_ms = now - press_start_time;
            // Short press: released within window (debounce < hold <= short max) and long wasn't fired
            if (!long_press_fired &&
                hold_ms >= debounce_ms &&
                hold_ms <= short_press_max_ms &&
                short_callback) {
                short_callback();
            }
            press_start_time = 0;
        }
    }
}

bool Button::isPressed() const {
    return current_state;
}

bool Button::wasPressed() const {
    return press_edge;
}

bool Button::wasReleased() const {
    return release_edge;
}

uint32_t Button::getPressTime() const {
    if (current_state && press_start_time > 0) {
        return millis() - press_start_time;
    }
    return 0;
}

// Private Methods /////////////////////////////////////////////////////////////

bool Button::readPin() const {
    const bool pin_state = digitalRead(button_pin);
    
    if (pin_logic == ACTIVE_LOW) {
        return !pin_state; // Invert for active low
    } else {
        return pin_state;  // Direct for active high
    }
}

void Button::updateEdgeFlags() {
    press_edge = (current_state && !last_state);   // Rising edge (pressed)
    release_edge = (!current_state && last_state); // Falling edge (released)
}

// End of file /////////////////////////////////////////////////////////////////
