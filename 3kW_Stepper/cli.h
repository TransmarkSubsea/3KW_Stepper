#ifndef CLI_H
#define CLI_H

#include <stdint.h>
#include <stdbool.h>

// ========== CONFIGURABLE LIMITS ==========
#define CLI_MAX_POS_MM 500.0f          // Maximum absolute position in mm
#define CLI_MIN_POS_MM 0.0f            // Minimum absolute position in mm
#define CLI_MAX_SPEED 11               // Maximum speed (1-11)
#define CLI_MIN_SPEED 1                // Minimum speed (1-11)
#define CLI_MAX_TRAVEL_MM 500.0f       // Maximum incremental travel distance in mm
#define CLI_RX_BUFFER_SIZE 64          // Serial RX buffer size

// ========== DIRECTION DEFINES ==========
#define CLI_DIR_OPEN 0
#define CLI_DIR_CLOSE 1

// ========== ERROR CODES ==========
typedef enum {
    CLI_ERR_OK = 0,
    CLI_ERR_INVALID_FORMAT = 1,
    CLI_ERR_INVALID_SPEED = 2,
    CLI_ERR_INVALID_DIRECTION = 3,
    CLI_ERR_OUT_OF_RANGE = 4,
    CLI_ERR_INVALID_MODE = 5,
    CLI_ERR_MOVEMENT_IN_PROGRESS = 6,
    CLI_ERR_UNKNOWN_COMMAND = 7
} cli_error_t;

// ========== COMMAND STRUCTURE ==========
typedef struct {
    uint8_t speed;           // 1-11
    uint8_t direction;       // 0 (open) or 1 (close)
    float value_mm;          // distance or position
    bool is_absolute;        // true = absolute position, false = relative distance
} cli_command_t;

// ========== FUNCTION PROTOTYPES ==========

void stepper_move(uint16_t speed, float travel_distance, bool direction);
int pico_led_init(void);
void pico_set_led(bool led_on);

/**
 * Initialize the CLI system (sets up serial interrupt handler)
 */
void cli_init(void);

/**
 * Process serial RX data (call periodically or from interrupt)
 */
void cli_process(void);

/**
 * Parse a command string into a command structure
 * @param input: null-terminated command string
 * @param cmd: pointer to command structure to fill
 * @return: error code (CLI_ERR_OK if successful)
 */
cli_error_t cli_parse_command(const char *input, cli_command_t *cmd);

/**
 * Execute a validated command
 * @param cmd: pointer to command structure
 * @return: error code
 */
cli_error_t cli_execute_command(const cli_command_t *cmd);

/**
 * Get the current position in mm
 */
float cli_get_position(void);

/**
 * Check if movement is currently in progress
 */
bool cli_is_moving(void);

/**
 * Print current system status
 */
void cli_print_status(void);

#endif // CLI_H
