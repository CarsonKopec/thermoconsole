/*
 * ThermoConsole Pico Controller
 * I2C Slave mode - sends button state when polled by Pi Zero
 * 
 * I2C Configuration:
 *   Address: 0x42
 *   Bus: I2C1 (GPIO 14 = SDA, GPIO 15 = SCL)
 *   Speed: 400kHz (Fast Mode)
 * 
 * Protocol:
 *   Master reads 2 bytes containing button state:
 *   Byte 0: Buttons 0-7 (up, down, left, right, A, B, X, Y)
 *   Byte 1: Buttons 8-9 (start, select) in bits 0-1
 * 
 * Button GPIO mapping (directly to GND, active LOW):
 *   GPIO 2:  Up
 *   GPIO 3:  Down
 *   GPIO 4:  Left
 *   GPIO 5:  Right
 *   GPIO 6:  A
 *   GPIO 7:  B
 *   GPIO 8:  X
 *   GPIO 9:  Y
 *   GPIO 10: Start
 *   GPIO 11: Select
 * 
 * I2C Wiring to Pi Zero:
 *   Pico GPIO 14 (SDA) -> Pi GPIO 2 (SDA1)
 *   Pico GPIO 15 (SCL) -> Pi GPIO 3 (SCL1)
 *   Pico GND           -> Pi GND
 *   (Pico can be powered from Pi 3.3V or VSYS from 5V)
 * 
 * Build with Pico SDK:
 *   mkdir build && cd build
 *   cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk
 *   make
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Configuration
 * ───────────────────────────────────────────────────────────────────────────── */

#define I2C_PORT        i2c1
#define I2C_SDA_PIN     14
#define I2C_SCL_PIN     15
#define I2C_ADDRESS     0x42
#define I2C_BAUDRATE    400000  /* 400kHz Fast Mode */

#define NUM_BUTTONS     10
#define DEBOUNCE_MS     5

/* Button indices (must match runtime THERMO_BTN_* constants) */
#define BTN_UP      0
#define BTN_DOWN    1
#define BTN_LEFT    2
#define BTN_RIGHT   3
#define BTN_A       4
#define BTN_B       5
#define BTN_X       6
#define BTN_Y       7
#define BTN_START   8
#define BTN_SELECT  9

/* GPIO pin assignments - directly mapped to button indices */
static const uint8_t BUTTON_PINS[NUM_BUTTONS] = {
    2,   /* Up     - GPIO 2 */
    3,   /* Down   - GPIO 3 */
    4,   /* Left   - GPIO 4 */
    5,   /* Right  - GPIO 5 */
    6,   /* A      - GPIO 6 */
    7,   /* B      - GPIO 7 */
    8,   /* X      - GPIO 8 */
    9,   /* Y      - GPIO 9 */
    10,  /* Start  - GPIO 10 */
    11,  /* Select - GPIO 11 */
};

/* LED pin for status */
#define LED_PIN PICO_DEFAULT_LED_PIN

/* ─────────────────────────────────────────────────────────────────────────────
 * State
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct {
    bool state;             /* Debounced state */
    uint32_t last_change;   /* Timestamp of last raw change */
} ButtonState;

static ButtonState buttons[NUM_BUTTONS];
static volatile uint16_t button_state = 0;  /* Packed button bits */

/* I2C data buffer */
static uint8_t i2c_tx_buffer[2];
static volatile int i2c_tx_index = 0;

/* ─────────────────────────────────────────────────────────────────────────────
 * Button Reading
 * ───────────────────────────────────────────────────────────────────────────── */

static void update_buttons(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint16_t new_state = 0;
    
    for (int i = 0; i < NUM_BUTTONS; i++) {
        /* Buttons are active LOW (pulled up, connected to GND when pressed) */
        bool pressed = !gpio_get(BUTTON_PINS[i]);
        
        /* Debounce: only change state if stable for DEBOUNCE_MS */
        if (pressed != buttons[i].state) {
            if (now - buttons[i].last_change >= DEBOUNCE_MS) {
                buttons[i].state = pressed;
            }
        } else {
            buttons[i].last_change = now;
        }
        
        if (buttons[i].state) {
            new_state |= (1 << i);
        }
    }
    
    button_state = new_state;
    
    /* Update TX buffer (atomic-ish update) */
    i2c_tx_buffer[0] = (uint8_t)(new_state & 0xFF);
    i2c_tx_buffer[1] = (uint8_t)((new_state >> 8) & 0x03);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * I2C Slave Handler (Interrupt-driven)
 * ───────────────────────────────────────────────────────────────────────────── */

static void i2c_slave_irq_handler(void) {
    i2c_hw_t *hw = i2c_get_hw(I2C_PORT);
    uint32_t status = hw->intr_stat;
    
    /* Read request from master */
    if (status & I2C_IC_INTR_STAT_R_RD_REQ_BITS) {
        /* Send next byte */
        if (i2c_tx_index < 2) {
            hw->data_cmd = i2c_tx_buffer[i2c_tx_index++];
        } else {
            /* Send 0 if we've sent all data */
            hw->data_cmd = 0;
        }
        /* Clear the interrupt */
        (void)hw->clr_rd_req;
    }
    
    /* RX buffer full (master wrote to us) */
    if (status & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {
        /* Read and discard - we don't expect writes */
        (void)hw->data_cmd;
    }
    
    /* Stop or restart detected - reset TX index */
    if (status & (I2C_IC_INTR_STAT_R_STOP_DET_BITS | I2C_IC_INTR_STAT_R_RESTART_DET_BITS)) {
        i2c_tx_index = 0;
        (void)hw->clr_stop_det;
        (void)hw->clr_restart_det;
    }
    
    /* Start detected */
    if (status & I2C_IC_INTR_STAT_R_START_DET_BITS) {
        i2c_tx_index = 0;
        (void)hw->clr_start_det;
    }
}

static void setup_i2c_slave(void) {
    /* Initialize I2C pins */
    gpio_init(I2C_SDA_PIN);
    gpio_init(I2C_SCL_PIN);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    
    /* Get hardware registers */
    i2c_hw_t *hw = i2c_get_hw(I2C_PORT);
    
    /* Disable I2C to configure */
    hw->enable = 0;
    while (hw->enable_status & 1) { tight_loop_contents(); }
    
    /* Configure as slave */
    hw->con = 
        I2C_IC_CON_SPEED_VALUE_FAST << I2C_IC_CON_SPEED_LSB |
        I2C_IC_CON_IC_RESTART_EN_BITS |
        I2C_IC_CON_TX_EMPTY_CTRL_BITS;
    
    /* Set slave address */
    hw->sar = I2C_ADDRESS;
    
    /* Set TX/RX FIFO thresholds */
    hw->tx_tl = 0;
    hw->rx_tl = 0;
    
    /* Clear all interrupts */
    (void)hw->clr_intr;
    
    /* Enable relevant interrupts */
    hw->intr_mask = 
        I2C_IC_INTR_MASK_M_RD_REQ_BITS |      /* Master read request */
        I2C_IC_INTR_MASK_M_RX_FULL_BITS |     /* RX FIFO full */
        I2C_IC_INTR_MASK_M_STOP_DET_BITS |    /* Stop condition */
        I2C_IC_INTR_MASK_M_START_DET_BITS |   /* Start condition */
        I2C_IC_INTR_MASK_M_RESTART_DET_BITS;  /* Restart condition */
    
    /* Re-enable I2C */
    hw->enable = 1;
    while (!(hw->enable_status & 1)) { tight_loop_contents(); }
    
    /* Set up IRQ handler */
    int irq_num = (I2C_PORT == i2c0) ? I2C0_IRQ : I2C1_IRQ;
    irq_set_exclusive_handler(irq_num, i2c_slave_irq_handler);
    irq_set_enabled(irq_num, true);
    
    printf("[OK] I2C slave configured at 0x%02X\n", I2C_ADDRESS);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Initialization
 * ───────────────────────────────────────────────────────────────────────────── */

static void init_buttons(void) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        uint8_t pin = BUTTON_PINS[i];
        
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);  /* Internal pull-up, button grounds the pin */
        
        buttons[i].state = false;
        buttons[i].last_change = 0;
    }
}

static void init_led(void) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Main Loop
 * ───────────────────────────────────────────────────────────────────────────── */

int main(void) {
    /* Initialize stdio for debug output (USB) */
    stdio_init_all();
    
    /* Initialize hardware */
    init_led();
    init_buttons();
    
    /* Blink LED to indicate startup */
    for (int i = 0; i < 3; i++) {
        gpio_put(LED_PIN, 1);
        sleep_ms(100);
        gpio_put(LED_PIN, 0);
        sleep_ms(100);
    }
    
    printf("\n");
    printf("================================\n");
    printf("ThermoConsole Controller v2.0\n");
    printf("Mode: I2C Slave\n");
    printf("================================\n");
    printf("I2C Address: 0x%02X\n", I2C_ADDRESS);
    printf("I2C SDA: GPIO %d\n", I2C_SDA_PIN);
    printf("I2C SCL: GPIO %d\n", I2C_SCL_PIN);
    printf("Buttons: GPIO 2-11\n");
    printf("\n");
    
    /* Setup I2C slave */
    setup_i2c_slave();
    
    printf("Ready!\n");
    
    /* LED on to indicate ready */
    gpio_put(LED_PIN, 1);
    
    /* Main loop - continuously update button state */
    uint32_t last_debug = 0;
    while (true) {
        update_buttons();
        
        /* Debug output every second */
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_debug >= 1000) {
            last_debug = now;
            if (button_state != 0) {
                printf("Buttons: 0x%04X\n", button_state);
            }
        }
        
        /* Brief sleep to prevent busy-waiting */
        sleep_us(500);  /* 2kHz update rate */
    }
    
    return 0;
}
