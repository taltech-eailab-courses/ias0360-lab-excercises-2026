
/* hw_config.c
Copyright 2021 Carl John Kugler III

Licensed under the Apache License, Version 2.0 (the License); you may not use
this file except in compliance with the License. You may obtain a copy of the
License at

   http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an AS IS BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.
*/

/*

This file should be tailored to match the hardware design.


There should be one element of the spi[] array for each RP2040 hardware SPI used.

There should be one element of the spi_ifs[] array for each SPI interface object.
* Each element of spi_ifs[] must point to an spi_t instance with member "spi".

There should be one element of the sdio_ifs[] array for each SDIO interface object.

There should be one element of the sd_cards[] array for each SD card slot.
* Each element of sd_cards[] must point to its interface with spi_if_p or sdio_if_p.
*/

/* Hardware configuration for Pico SD Card Development Board
See https://oshwlab.com/carlk3/rp2040-sd-card-dev

See https://docs.google.com/spreadsheets/d/1BrzLWTyifongf_VQCc2IpJqXWtsrjmG7KnIbSBy-CPU/edit?usp=sharing,
tab "Dev Brd", for pin assignments assumed in this configuration file.
*/

#include <assert.h>
//
#include "hw_config.h"

/* The lab hardware (see lab_1_1/lcd_sd_card_example and this assignment's lcd/ project) has the SD card on
 * SDIO: CMD GP18, D0 GP19 (CLK GP17, D1..D3 GP20..22). Note that SDIO CLK collides with the touch IRQ pin
 * GP17. Build with -DAW_SD_USE_SPI=1 to use the SD slot on the shared SPI1 (CS GP22) instead; on this
 * hardware that configuration made f_mount fail with FR_NOT_READY. */
#ifndef AW_SD_USE_SPI
#define AW_SD_USE_SPI 0
#endif
#ifndef AW_SD_SPI_BAUD
#define AW_SD_SPI_BAUD (5 * 1000 * 1000)
#endif

// Hardware Configuration of SPI "objects"
// Note: multiple SD cards can be driven by one SPI if they use different slave
// selects (or "chip selects").
// Using SPI1 to avoid conflicts with CYW43439 WiFi chip (which uses SPI0 internally on Pico W)
static spi_t spis[] = {
    {   // SPI1 for SD card (avoiding CYW43439 conflict)
        .hw_inst = spi1,            // Use SPI1 instead of SPI0
        .sck_gpio = 10,             // SPI1 SCK  (GP10)
        .mosi_gpio = 11,            // SPI1 MOSI (GP11) - TX
        .miso_gpio = 12,            // SPI1 MISO (GP12) - RX
        .set_drive_strength = true,
        .mosi_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .sck_gpio_drive_strength  = GPIO_DRIVE_STRENGTH_12MA,
        .no_miso_gpio_pull_up = true,
        .baud_rate = AW_SD_SPI_BAUD  // used after the 400 kHz card initialisation
    }
};

/* SPI Interfaces */
static sd_spi_if_t spi_ifs[] = {
    {
        .spi = &spis[0],
        .ss_gpio = 22           // CS (Chip Select) pin for SD card (matches DEV_Config.h SD_CS_PIN)
    }
};

/* SDIO Interfaces */
/*
Pins CLK_gpio, D1_gpio, D2_gpio, and D3_gpio are at offsets from pin D0_gpio.
The offsets are determined by sd_driver\SDIO\rp2040_sdio.pio.
    CLK_gpio = (D0_gpio + SDIO_CLK_PIN_D0_OFFSET) % 32;
    As of this writing, SDIO_CLK_PIN_D0_OFFSET is 30,
        which is -2 in mod32 arithmetic, so:
    CLK_gpio = D0_gpio -2.
    D1_gpio = D0_gpio + 1;
    D2_gpio = D0_gpio + 2;
    D3_gpio = D0_gpio + 3;
*/
static sd_sdio_if_t sdio_ifs[] = {
    {
        .CMD_gpio = 18,
        .D0_gpio = 19,
        .CLK_gpio_drive_strength = GPIO_DRIVE_STRENGTH_12MA,
        .CMD_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .D0_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .D1_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .D2_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .D3_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .SDIO_PIO = pio0,           // Use PIO0 (CYW43 uses PIO internally, but PIO0 is safer for SDIO)
        .DMA_IRQ_num = DMA_IRQ_1,   // Use DMA_IRQ_1 to avoid conflict with WiFi (which may use DMA_IRQ_0)
        .baud_rate = 125 * 1000 * 1000 / 7
    }
};

/* Hardware Configuration of the SD Card "objects"
    These correspond to SD card sockets
*/
static sd_card_t sd_cards[] = {
#if AW_SD_USE_SPI
    {   // sd_cards[0]: SD slot on the LCD board, SPI1 shared with LCD + touch
        .type = SD_IF_SPI,
        .spi_if_p = &spi_ifs[0],
        .use_card_detect = false
    }
#else
    {   // sd_cards[0]: Socket sd0 using SDIO interface
        .type = SD_IF_SDIO,
        .spi_if_p = &sdio_ifs[0],
        .use_card_detect = false,
        .card_detect_gpio = 9,  
        .card_detected_true = 0, // What the GPIO read returns when a card is
                                 // present.
        .card_detect_use_pull = true,
        .card_detect_pull_hi = true          
    }
#endif
};
/* ********************************************************************** */

/* Bus mutex of the shared SPI1. The SD driver holds it for every complete SD transaction
 * (chip select, command, data, busy wait); the LCD/touch code takes the very same mutex around
 * its transactions (see src/aw_spi_bus.c). It must be initialised before core 1 starts. */
mutex_t *aw_sd_spi_mutex(void) { return &spis[0].mutex; }

size_t sd_get_num() { return count_of(sd_cards); }

sd_card_t *sd_get_by_num(size_t num) {
    assert(num < sd_get_num());
    if (num < sd_get_num()) {
        return &sd_cards[num];
    } else {
        return NULL;
    }
}

/* [] END OF FILE */
