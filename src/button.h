// button.h ////////////////////////////////////////////////////////////////////

#pragma once
#include <Arduino.h>

// Function pointer type for button action
typedef void (*ButtonCallback)();

// Button class with debouncing and short/long press detection
class Button {
public:
    enum PinLogic {
        ACTIVE_LOW = 0,   // Button pressed = LOW (with pullup)
        ACTIVE_HIGH = 1   // Button pressed = HIGH
    };
    
    // debounceMs: minimum hold (ms) to count as a valid press
    // shortPressMaxMs: short press = release between debounce and this (ms)
    // longPressMinMs: long press = hold this long (ms); callback fires immediately
    // shortCallback: called on release when press was in short window (may be nullptr)
    // longCallback: called as soon as longPressMinMs is reached (may be nullptr)
    Button(uint8_t pin, uint32_t debounceMs, uint32_t shortPressMaxMs, uint32_t longPressMinMs,
           PinLogic logic, ButtonCallback shortCallback, ButtonCallback longCallback);
    
    void begin();
    void update();
    
    // Status queries
    bool isPressed() const;
    bool wasPressed() const;    // Returns true once per press
    bool wasReleased() const;   // Returns true once per release
    uint32_t getPressTime() const; // How long current press has lasted (ms)

private:
    const uint8_t button_pin;
    const uint32_t debounce_ms;
    const uint32_t short_press_max_ms;
    const uint32_t long_press_min_ms;
    const PinLogic pin_logic;
    const ButtonCallback short_callback;
    const ButtonCallback long_callback;
    
    // Button state tracking
    bool current_state{false};
    bool last_state{false};
    uint32_t press_start_time{0};
    bool long_press_fired{false};
    
    // Edge detection flags
    bool press_edge{false};
    bool release_edge{false};
    
    // Helper methods
    bool readPin() const;
    void updateEdgeFlags();
};

// End of file /////////////////////////////////////////////////////////////////
