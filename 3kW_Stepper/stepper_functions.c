#include <stdio.h>
#include <stdarg.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "stepper.h"





// Perform initialisation if LED pin
int pico_led_init(void) 
{
#if defined(PICO_DEFAULT_LED_PIN)
    // A device like Pico that uses a GPIO for the LED will define PICO_DEFAULT_LED_PIN
    // so we can use normal GPIO functionality to turn the led on and off
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    return PICO_OK;
#endif
}

// Turn the led on or off
void pico_set_led(bool led_on) 
{
    // Just set the GPIO on or off
    gpio_put(PICO_DEFAULT_LED_PIN, led_on);
}

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
    // Visual debug: blink LED to show function entry
    pico_set_led(true);
    sleep_ms(100);
    pico_set_led(false);
    
    printf("DEBUG: stepper_move ENTERED with speed=%u, distance=%.2f, direction=%u\n", speed, travel_distance, direction);
    
    uint32_t step_count = (uint32_t)(travel_distance / Step_resolution + 0.5f);
    printf("DEBUG: calculated step_count=%u\n", step_count);
    
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
    printf("DEBUG: PWM slice_num=%u for GPIO %d\n", slice_num, Stepper_PULSE);
    pwm_set_clkdiv(slice_num, 1.0f);
    pwm_set_gpio_level(Stepper_PULSE, calculate_pwm_wrap(pulse_frequency) / 2);

    // Ensure PWM is disabled at start
    pwm_set_enabled(slice_num, false);
    printf("DEBUG: PWM initialized and disabled\n");

    stop_pulse = false;
    Step_actual = 0;

    printf("DEBUG: Starting move - steps=%u, freq=%.1f Hz, accel_steps=%u, total_time=%.1f ms\n",
           step_count, pulse_frequency, accel_steps, total_move_ms);
    
    // Visual debug: blink LED twice to show PWM setup
    pico_set_led(true);
    sleep_ms(50);
    pico_set_led(false);
    sleep_ms(50);
    pico_set_led(true);
    sleep_ms(50);
    pico_set_led(false);

    // Safety timeout: max 30 seconds for any move
    uint32_t start_time = time_us_32();
    uint32_t timeout_us = 30000000; // 30 seconds

    for (uint32_t step = 0; step < step_count; ++step) {
        // Check for timeout
        if ((time_us_32() - start_time) > timeout_us) {
            pwm_set_enabled(slice_num, false);
            printf("DEBUG: Move timed out after 30 seconds at step %u\n", step);
            break;
        }

        if (stop_pulse) {
            pwm_set_enabled(slice_num, false);
            printf("DEBUG: Pulse generation stopped by limit switch at step %u\n", step);
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

        // Debug output every 500 steps to reduce spam
            if (step % 500 == 0) {
            float percent = (float)(step + 1) / step_count * 100.0f;
            printf("DEBUG: Step %u/%u completed (%.1f%%)\n", step + 1, step_count, percent);
            stepper_broadcast("POSITION: %.3f mm (step %u/%u, %.1f%%)\n", Accumulated_Distance, step + 1, step_count, percent);
        }
    }

    // Ensure PWM is completely disabled
    pwm_set_enabled(slice_num, false);
    printf("DEBUG: PWM disabled, move complete\n");
    stepper_broadcast("FINAL POSITION: %.3f mm (Steps: %ld)\n", Accumulated_Distance, Accumulated_Steps);

    // Visual debug: blink LED three times to show completion
    for(int i = 0; i < 3; i++) {
        pico_set_led(true);
        sleep_ms(100);
        pico_set_led(false);
        sleep_ms(100);
    }

    printf("Stepper pulse sequence complete (accumulated steps: %u)\t (accumulated distance: %.3f mm)\n", Accumulated_Steps, Accumulated_Distance);
}

void stepper_calibrate(void) {
    stepper_broadcast("CALIBRATION: Starting homing sequence...\n");

    // First run toward the near stop until the switch activates
    if (!gpio_get(Near_Stop)) {
        stop_pulse = false;
        stepper_move(1, 10000.0f, Direction_Close);
    }

    if (!gpio_get(Near_Stop)) {
        stepper_broadcast("CALIBRATION: Near-stop not reached, aborting calibration.\n");
        return;
    }

    Accumulated_Steps = 0;
    Accumulated_Distance = 0.0f;
    stop_pulse = false;
    stepper_broadcast("CALIBRATION: Near-stop reached, resetting position counters.\n");

    // Then run toward the far stop until the opposite switch activates
    if (!gpio_get(Far_Stop)) {
        stop_pulse = false;
        stepper_move(1, 10000.0f, Direction_Open);
    }

    if (!gpio_get(Far_Stop)) {
        stepper_broadcast("CALIBRATION: Far-stop not reached, aborting calibration.\n");
        return;
    }

    Calibration_Steps = Accumulated_Steps;
    Calibration_Distance = Accumulated_Distance;
    stepper_broadcast("CALIBRATION: Completed. Total steps = %u, total distance = %.3f mm\n", Calibration_Steps, Calibration_Distance);
}

static void stepper_broadcast(const char *fmt, ...) {
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (len <= 0) {
        return;
    }

    if (len >= (int)sizeof(buffer)) {
        len = sizeof(buffer) - 1;
        buffer[len] = '\0';
    }

    printf("%s", buffer);
    for (int i = 0; i < len; i++) {
        char ch = buffer[i];
        if (ch == '\n') {
            uart_putc(UART_ID, '\r');
            uart_putc(UART_ID, '\n');
        } else {
            uart_putc(UART_ID, ch);
        }
    }
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