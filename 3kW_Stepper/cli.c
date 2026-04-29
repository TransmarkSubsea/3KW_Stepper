#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "cli.h"
#include "stepper.h"

// Function prototypes
void stepper_move(uint16_t speed, float travel_distance, bool direction);
int pico_led_init(void);
void pico_set_led(bool led_on);

// External variables from main
extern volatile uint32_t Step_actual;
extern volatile int32_t Accumulated_Steps;
extern volatile float Accumulated_Distance;
extern const float Step_resolution;
extern volatile bool stop_pulse;

// CLI state variables
static char rx_buffer[CLI_RX_BUFFER_SIZE];
static char command_line[CLI_RX_BUFFER_SIZE];
static uint32_t rx_index = 0;
static volatile bool command_ready = false;
static volatile bool moving = false;

// Forward declarations
static void cli_uart_irq_handler(void);
static void cli_print_error(cli_error_t error);
static void cli_trim_string(char *str);
static void cli_broadcast(const char *fmt, ...);
static void cli_enqueue_command(void);

static void cli_enqueue_command(void) {
    if (command_ready) {
        return;
    }
    strncpy(command_line, rx_buffer, CLI_RX_BUFFER_SIZE);
    command_line[CLI_RX_BUFFER_SIZE - 1] = '\0';
    command_ready = true;
}

/**
 * Initialize the CLI system
 */
void cli_init(void) {
    rx_index = 0;
    moving = false;
    command_ready = false;
    
    // Set up UART interrupt for RX data
    irq_set_exclusive_handler(UART1_IRQ, cli_uart_irq_handler);
    irq_set_enabled(UART1_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);  // Enable RX interrupt, disable TX interrupt
    
    printf("CLI initialized. Type 'help' for command syntax.\n");
}

/**
 * UART interrupt handler
 */
static void cli_uart_irq_handler(void) {
    while (uart_is_readable(UART_ID)) {
        uint8_t ch = uart_getc(UART_ID);
        
        // Handle backspace
        if (ch == '\b' || ch == 0x7F) {
            if (rx_index > 0) {
                rx_index--;
                uart_putc(UART_ID, '\b');
                uart_putc(UART_ID, ' ');
                uart_putc(UART_ID, '\b');
            }
            continue;
        }
        
            // Handle newline/carriage return
        if (ch == '\n' || ch == '\r') {
            if (rx_index > 0 && !command_ready) {
                rx_buffer[rx_index] = '\0';
                cli_enqueue_command();
                rx_index = 0;
            }
            uart_putc(UART_ID, '\r');
            uart_putc(UART_ID, '\n');
            continue;
        }
        
        // Echo and store printable characters
        if (isprint(ch) && rx_index < CLI_RX_BUFFER_SIZE - 1) {
            rx_buffer[rx_index++] = ch;
            uart_putc(UART_ID, ch);
        }
    }
}

/**
 * Process the command buffer
 */
void cli_process(void) {
    if (!command_ready) {
        return;
    }

    bool uart_irq_enabled = irq_is_enabled(UART1_IRQ);
    irq_set_enabled(UART1_IRQ, false);
    strncpy(rx_buffer, command_line, CLI_RX_BUFFER_SIZE);
    rx_buffer[CLI_RX_BUFFER_SIZE - 1] = '\0';
    command_ready = false;
    irq_set_enabled(UART1_IRQ, uart_irq_enabled);

    cli_trim_string(rx_buffer);
    
    char command[CLI_RX_BUFFER_SIZE];
    bool query_mode = false;

    if (rx_buffer[0] == '?') {
        query_mode = true;
        size_t len = strlen(rx_buffer);
        if (len > 1) {
            size_t copy_len = len - 1;
            if (copy_len >= CLI_RX_BUFFER_SIZE) {
                copy_len = CLI_RX_BUFFER_SIZE - 1;
            }
            memcpy(command, rx_buffer + 1, copy_len);
            command[copy_len] = '\0';
        } else {
            command[0] = '\0';
        }
        cli_trim_string(command);
    } else {
        strncpy(command, rx_buffer, CLI_RX_BUFFER_SIZE - 1);
        command[CLI_RX_BUFFER_SIZE - 1] = '\0';
        cli_trim_string(command);
    }

    // Handle help command
    if (strcmp(command, "help") == 0 || strcmp(command, "HELP") == 0 || strcmp(command, "?") == 0 || strcmp(command, "?HELP") == 0) {
        cli_broadcast("=== Stepper Motor CLI ===\n");
        cli_broadcast("Command syntax:\n");
        cli_broadcast("  MOVE ABS <speed> <value>\n");
        cli_broadcast("  MOVE REL <speed> <dir> <value>\n");
        cli_broadcast("  speed:  1-11 (1=slow, 11=fast)\n");
        cli_broadcast("  dir:    0=open/forward, 1=close/reverse (REL only)\n");
        cli_broadcast("  value:  distance in mm for REL, absolute position in mm for ABS\n");
        cli_broadcast("Example:\n");
        cli_broadcast("  MOVE REL 5 0 100   - Move open at speed 5 for 100mm\n");
        cli_broadcast("  MOVE ABS 3 250     - Move to absolute position 250mm at speed 3\n");
        cli_broadcast("Other commands:\n");
        cli_broadcast("  STAT    - Show current status\n");
        cli_broadcast("  STOP    - Emergency stop\n");
        cli_broadcast("> ");
        return;
    }

    // Query commands prefix
    if (query_mode) {
        if (strcmp(command, "MOVE") == 0 || strcmp(command, "move") == 0) {
            cli_broadcast("POSITION: %.3f mm (Steps: %ld)\n", cli_get_position(), Accumulated_Steps);
            cli_broadcast("> ");
            return;
        }
        if (strcmp(command, "STAT") == 0 || strcmp(command, "stat") == 0) {
            cli_print_status();
            cli_broadcast("> ");
            return;
        }
        cli_print_error(CLI_ERR_UNKNOWN_COMMAND);
        cli_broadcast("> ");
        return;
    }

    // Handle status command
    if (strcmp(command, "status") == 0 || strcmp(command, "STAT") == 0) {
        cli_print_status();
        cli_broadcast("> ");
        return;
    }
    
    // Handle stop command
    if (strcmp(command, "stop") == 0) {
        stop_pulse = true;
        cli_broadcast("Emergency stop requested.\n");
        cli_broadcast("> ");
        return;
    }
    
    // Parse and execute MOVE command
    if (strncmp(command, "MOVE", 4) == 0 || strncmp(command, "move", 4) == 0) {
        cli_command_t cmd;
        cli_error_t err = cli_parse_command(rx_buffer, &cmd);
        
        if (err != CLI_ERR_OK) {
            cli_print_error(err);
            printf("> ");
            return;
        }
        
        err = cli_execute_command(&cmd);
        if (err != CLI_ERR_OK) {
            cli_print_error(err);
        }
        printf("> ");
        return;
    }
    
    printf("Unknown command: %s\n", rx_buffer);
    printf("Type 'help' for available commands.\n");
    printf("> ");
}

/**
 * Parse a command string
 */
cli_error_t cli_parse_command(const char *input, cli_command_t *cmd) {
    if (!input || !cmd) {
        return CLI_ERR_INVALID_FORMAT;
    }
    
    char buffer[CLI_RX_BUFFER_SIZE];
    strncpy(buffer, input, CLI_RX_BUFFER_SIZE - 1);
    buffer[CLI_RX_BUFFER_SIZE - 1] = '\0';
    
    // Parse: MOVE ABS <speed> <value> or MOVE REL <speed> <dir> <value>
    char *token = strtok(buffer, " ");
    if (!token || (strcmp(token, "MOVE") != 0 && strcmp(token, "move") != 0)) {
        return CLI_ERR_INVALID_FORMAT;
    }

    // Parse mode first
    token = strtok(NULL, " ");
    if (!token) return CLI_ERR_INVALID_FORMAT;
    if (strcmp(token, "ABS") == 0 || strcmp(token, "abs") == 0) {
        cmd->is_absolute = true;
    } else if (strcmp(token, "REL") == 0 || strcmp(token, "rel") == 0) {
        cmd->is_absolute = false;
    } else {
        return CLI_ERR_INVALID_MODE;
    }

    // Parse speed
    token = strtok(NULL, " ");
    if (!token) return CLI_ERR_INVALID_FORMAT;
    int speed = atoi(token);
    if (speed < CLI_MIN_SPEED || speed > CLI_MAX_SPEED) {
        return CLI_ERR_INVALID_SPEED;
    }
    cmd->speed = (uint8_t)speed;

    if (cmd->is_absolute) {
        // Absolute: no direction token, next value is target position
        token = strtok(NULL, " ");
        if (!token) return CLI_ERR_INVALID_FORMAT;
        float value = atof(token);
        if (value < CLI_MIN_POS_MM || value > CLI_MAX_POS_MM) {
            return CLI_ERR_OUT_OF_RANGE;
        }
        cmd->value_mm = value;
        cmd->direction = CLI_DIR_OPEN; // placeholder, actual direction computed at execution
    } else {
        // Relative: parse direction and distance
        token = strtok(NULL, " ");
        if (!token) return CLI_ERR_INVALID_FORMAT;
        int dir = atoi(token);
        if (dir != 0 && dir != 1) {
            return CLI_ERR_INVALID_DIRECTION;
        }
        cmd->direction = (uint8_t)dir;

        token = strtok(NULL, " ");
        if (!token) return CLI_ERR_INVALID_FORMAT;
        float value = atof(token);
        if (value < 0.0f || value > CLI_MAX_TRAVEL_MM) {
            return CLI_ERR_OUT_OF_RANGE;
        }
        cmd->value_mm = value;
    }

    return CLI_ERR_OK;
}

/**
 * Execute a validated command
 */
cli_error_t cli_execute_command(const cli_command_t *cmd) {
    if (moving) {
        return CLI_ERR_MOVEMENT_IN_PROGRESS;
    }

    float current_pos = cli_get_position();
    float target_distance = cmd->value_mm;
    float final_pos = current_pos;
    uint8_t direction = cmd->direction;

    if (cmd->is_absolute) {
        final_pos = cmd->value_mm;
        if (final_pos == current_pos) {
            printf("Already at absolute position %.2f mm\n", current_pos);
            return CLI_ERR_OK;
        }

        if (final_pos > current_pos) {
            direction = CLI_DIR_OPEN;
            target_distance = final_pos - current_pos;
        } else {
            direction = CLI_DIR_CLOSE;
            target_distance = current_pos - final_pos;
        }
    } else {
        if (direction == CLI_DIR_OPEN) {
            final_pos = current_pos + target_distance;
        } else {
            final_pos = current_pos - target_distance;
        }
    }

    if (final_pos < CLI_MIN_POS_MM || final_pos > CLI_MAX_POS_MM) {
        return CLI_ERR_OUT_OF_RANGE;
    }

    // Print command acknowledgement
    printf("CMD ACK: Speed=%u, Direction=%s, Distance=%.2f mm, Mode=%s\n",
           cmd->speed,
           (direction == CLI_DIR_OPEN) ? "OPEN" : "CLOSE",
           target_distance,
           cmd->is_absolute ? "ABS" : "REL");

    printf("DEBUG: About to call stepper_move with speed=%u, distance=%.2f, direction=%u\n",
           cmd->speed, target_distance, direction);

    // Visual debug: blink LED once before calling stepper_move
    pico_set_led(true);
    sleep_ms(200);
    pico_set_led(false);

    printf("START: Position %.2f mm -> %.2f mm\n", current_pos, final_pos);

    moving = true;
    printf("DEBUG: Calling stepper_move...\n");
    stepper_move(cmd->speed, target_distance, direction);
    printf("DEBUG: stepper_move returned\n");
    moving = false;

    float end_pos = cli_get_position();
    printf("DONE: Final position %.2f mm (Steps: %ld)\n", end_pos, Accumulated_Steps);

    return CLI_ERR_OK;
}

/**
 * Get current position
 */
float cli_get_position(void) {
    return Accumulated_Distance;
}

/**
 * Check if moving
 */
bool cli_is_moving(void) {
    return moving;
}

/**
 * Print system status
 */
void cli_print_status(void) {
    cli_broadcast("=== System Status ===\n");
    cli_broadcast("Current Position: %.3f mm\n", cli_get_position());
    cli_broadcast("Accumulated Steps: %ld\n", Accumulated_Steps);
    cli_broadcast("Moving: %s\n", moving ? "YES" : "NO");
    cli_broadcast("\n--- Limits ---\n");
    cli_broadcast("Min Position: %.1f mm\n", CLI_MIN_POS_MM);
    cli_broadcast("Max Position: %.1f mm\n", CLI_MAX_POS_MM);
    cli_broadcast("Speed Range: %d-%d\n", CLI_MIN_SPEED, CLI_MAX_SPEED);
    cli_broadcast("Max Travel: %.1f mm\n", CLI_MAX_TRAVEL_MM);
}

/**
 * Print error message
 */
static void cli_print_error(cli_error_t error) {
    printf("ERROR: ");
    switch (error) {
        case CLI_ERR_OK:
            printf("No error.\n");
            break;
        case CLI_ERR_INVALID_FORMAT:
            printf("Invalid command format.\n");
            break;
        case CLI_ERR_INVALID_SPEED:
            printf("Invalid speed (must be %d-%d).\n", CLI_MIN_SPEED, CLI_MAX_SPEED);
            break;
        case CLI_ERR_INVALID_DIRECTION:
            printf("Invalid direction (must be 0 or 1).\n");
            break;
        case CLI_ERR_OUT_OF_RANGE:
            printf("Value out of range (position: %.0f-%.0f mm).\n", CLI_MIN_POS_MM, CLI_MAX_POS_MM);
            break;
        case CLI_ERR_INVALID_MODE:
            printf("Invalid mode (must be REL or ABS).\n");
            break;
        case CLI_ERR_MOVEMENT_IN_PROGRESS:
            printf("Movement already in progress.\n");
            break;
        case CLI_ERR_UNKNOWN_COMMAND:
            printf("Unknown command.\n");
            break;
        default:
            printf("Unknown error code %d.\n", error);
    }
}

static void cli_broadcast(const char *fmt, ...) {
    char buffer[CLI_RX_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (len > 0) {
        if (len > (int)sizeof(buffer) - 1) {
            len = sizeof(buffer) - 1;
            buffer[len] = '\0';
        }
        // Send to USB stdio
        printf("%s", buffer);
        // Send to UART as well, converting newlines to CRLF
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
}

/**
 * Trim whitespace from string
 */
static void cli_trim_string(char *str) {
    if (!str) return;
    
    // Trim leading whitespace
    char *start = str;
    while (*start && isspace(*start)) start++;
    
    // Trim trailing whitespace
    char *end = start + strlen(start) - 1;
    while (end >= start && isspace(*end)) end--;
    
    // Move trimmed string to beginning
    memmove(str, start, (end - start + 1));
    str[end - start + 1] = '\0';
}
