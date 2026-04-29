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

// Function to move stepper motor
void stepper_move(uint16_t speed, float travel_distance, bool direction) {
    uint32_t step_count = (uint32_t)(travel_distance / Step_resolution + 0.5f);
    float pulse_frequency = 0;

    if (speed > 11) 
    {
        speed = 11; // Limit speed to max of 11 to prevent exceeding max pulse frequency
    } else if (speed < 1) {
        speed = 1; // Limit speed to min of 1 to prevent zero frequency
    }
    pulse_frequency = (speed + 1) * 1000; // Fixed PWM frequency in Hz, max is 14000

    gpio_put(Stepper_DIR, direction); // Set direction

    //printf("Starting stepper pulse sequence using PWM (%u pulses at %.1f Hz)\n", step_count, pulse_frequency);

    uint slice_num = pwm_gpio_to_slice_num(Stepper_PULSE);
    pwm_set_clkdiv(slice_num, 1.0f);

    uint32_t wrap = (uint32_t)((float)clock_get_hz(clk_sys) / pulse_frequency - 1.0f + 0.5f);
    if (wrap > 65535u) wrap = 65535u;
    if (wrap < 1u) wrap = 1u;
    pwm_set_wrap(slice_num, (uint16_t)wrap);
    pwm_set_gpio_level(Stepper_PULSE, (uint16_t)(wrap / 2));

    stop_pulse = false;
    Step_actual = 0;
    for (uint32_t step = 0; step < step_count; ++step) 
    {
        if (stop_pulse) 
        {
            pwm_set_enabled(slice_num, false);
            printf("Pulse generation stopped by limit switch\n");
            break;
        }

        pwm_set_counter(slice_num, 0);
        pwm_set_enabled(slice_num, true);

        uint32_t period_us = (uint32_t)((1.0f / pulse_frequency) * 1000000.0f + 0.5f);
        sleep_us(period_us);

        pwm_set_enabled(slice_num, false);
        Step_actual++;
        if (direction == Direction_Open) 
        {
            Accumulated_Steps++;
        } else {
            Accumulated_Steps--;
        }
    }

    printf("Stepper pulse sequence complete (accumulated steps: %u)\n", Accumulated_Steps);
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
            }       
        }
    }
}