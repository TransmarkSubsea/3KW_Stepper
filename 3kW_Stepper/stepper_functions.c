#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "stepper.h"

// Extern declarations for global variables
extern volatile bool stop_pulse;
extern volatile uint32_t last_interrupt_time;
extern const uint32_t DEBOUNCE_MS;
extern const float Step_resolution;
extern volatile uint32_t Step_actual;
extern volatile int32_t Accumulated_Steps;
extern volatile float Accumulated_Distance;

// Helper to convert a target PWM frequency into a valid wrap value
static uint16_t calculate_pwm_wrap(float frequency) {
    if (frequency < 1.0f) {
        frequency = 1.0f;
    }
    uint32_t wrap = (uint32_t)((float)clock_get_hz(clk_sys) / frequency - 1.0f + 0.5f);
    if (wrap > 65535u) wrap = 65535u;
    if (wrap < 1u) wrap = 1u;
    return (uint16_t)wrap;
}

// Function to move stepper motor
void stepper_move(uint16_t speed, float travel_distance, bool direction) {
    uint32_t step_count = (uint32_t)(travel_distance / Step_resolution + 0.5f);
    float pulse_frequency = 0;

    if (speed > 11) {
        speed = 11; // Limit speed to max of 11 to prevent exceeding max pulse frequency
    } else if (speed < 1) {
        speed = 1; // Limit speed to min of 1 to prevent zero frequency
    }
    pulse_frequency = (speed + 1) * 1000; // Fixed PWM frequency in Hz, max is 12000

    gpio_put(Stepper_DIR, direction); // Set direction

    // Determine accel/decel step counts so each phase is no longer than 50ms
    float total_move_ms = (step_count / pulse_frequency) * 1000.0f;
    float ramp_ms = 50.0f;
    if (total_move_ms < 2.0f * ramp_ms) {
        ramp_ms = total_move_ms / 2.0f;
    }

    uint32_t accel_steps = (uint32_t)(0.5f * pulse_frequency * ramp_ms / 1000.0f + 0.5f);
    if (accel_steps < 1u) {
        accel_steps = 1u;
    }
    if (accel_steps > step_count / 2) {
        accel_steps = step_count / 2;
    }
    uint32_t decel_steps = accel_steps;

    uint slice_num = pwm_gpio_to_slice_num(Stepper_PULSE);
    pwm_set_clkdiv(slice_num, 1.0f);
    pwm_set_gpio_level(Stepper_PULSE, calculate_pwm_wrap(pulse_frequency) / 2);

    stop_pulse = false;
    Step_actual = 0;
    for (uint32_t step = 0; step < step_count; ++step) {
        if (stop_pulse) {
            pwm_set_enabled(slice_num, false);
            printf("Pulse generation stopped by limit switch\n");
            break;
        }

        float target_frequency = pulse_frequency;
        if (step < accel_steps) {
            target_frequency = pulse_frequency * (float)(step + 1) / (float)accel_steps;
        } else if (step >= step_count - decel_steps) {
            uint32_t decel_index = step - (step_count - decel_steps);
            target_frequency = pulse_frequency * (float)(decel_steps - decel_index) / (float)decel_steps;
            if (target_frequency < 1.0f) {
                target_frequency = 1.0f;
            }
        }

        uint16_t wrap = calculate_pwm_wrap(target_frequency);
        pwm_set_wrap(slice_num, wrap);
        pwm_set_gpio_level(Stepper_PULSE, wrap / 2);

        pwm_set_counter(slice_num, 0);
        pwm_set_enabled(slice_num, true);

        uint32_t period_us = (uint32_t)((1.0f / target_frequency) * 1000000.0f + 0.5f);
        sleep_us(period_us);

        pwm_set_enabled(slice_num, false);
        Step_actual++;
        if (direction == Direction_Open) {
            Accumulated_Steps++;
        } else {
            Accumulated_Steps--;
        }
        Accumulated_Distance = (float)(Accumulated_Steps * Step_resolution);
    }

    printf("Stepper pulse sequence complete (accumulated steps: %u)\t (accumulated distance: %.3f mm)\n", Accumulated_Steps, Accumulated_Distance);
}

// Interrupt handler for limit switches with debouncing
void limit_switch_isr(uint gpio, uint32_t events) 
{
    uint32_t current_time = time_us_32() / 1000; // Convert to ms
    
    // Debounce check
    if ((current_time - last_interrupt_time) < DEBOUNCE_MS) 
    {
        return;
    }
    
    last_interrupt_time = current_time;
    
    if ((gpio == Near_Stop || gpio == Far_Stop) && (events & GPIO_IRQ_LEVEL_HIGH)) 
    {
        if (gpio_get(Near_Stop) || gpio_get(Far_Stop)) 
        {
            stop_pulse = true;
            printf("Limit switch activated: %s\n", gpio == Near_Stop ? "Near_Stop" : "Far_Stop");
            if (gpio == Near_Stop) 
            {
                Accumulated_Steps = 0; // Update accumulated steps when near stop is hit
                Accumulated_Distance = 0; // Update accumulated distance when near stop is hit
            }       
        }
    }
}