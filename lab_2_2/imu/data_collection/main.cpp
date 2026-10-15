#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"

#include "icm20948.h"
#include "ws2812.pio.h"
#include "ff.h"         
#include "sd_card.h"     
#include "f_util.h"     
#include "hw_config.h" 

const uint WS2812_PIN = 4;
static FATFS fs;

// Configuration
// Data collection parameters. Edit as needed.
const uint32_t MAX_DATA_COLLECTION_TIME_US = 3 * 1000 * 1000; // 3 seconds
uint32_t RECORD_TIMES = 25;


// Control flags
volatile bool RECORD = false;        
volatile uint32_t SESSION = 0;     
volatile bool STOP_ALL = false;     


char FILE_NAME_PREFIX[32] = "train";

typedef struct __attribute__((packed)) 
{
    int16_t ax, ay, az;          // accel
    uint8_t  _pad[2];            // pad to 8 bytes total
} imu_sample_t;

#define QUEUE_DEPTH  (8192)      // 8192 * 8B = 64 KB ring -> ~8s at 1 kHz
static queue_t sample_q;


static void die(FRESULT fr, const char *op) 
{
    printf("%s failed: %s (%d)\n", op, FRESULT_str(fr), fr);
    while (1) tight_loop_contents();
}

static void halt_forever(const char *msg) 
{
    if (msg) printf("%s\n", msg);
    while (1) tight_loop_contents();
}

static inline void put_pixel(uint32_t pixel_grb) 
{
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

static inline uint32_t rgb_to_grb_u32(uint8_t r, uint8_t g, uint8_t b) 
{
    return ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
}

static inline void show_color_rgb(uint8_t r, uint8_t g, uint8_t b) 
{
    uint32_t grb = rgb_to_grb_u32(r, g, b);
    put_pixel(grb);
}

void init_pio_for_ws2812()
{
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);

    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, true);
}

static const char *sd_mount_or_format(FATFS *pfs, int card_num) 
{
    if (!sd_init_driver()) {
        printf("sd_init_driver() failed\n");
        halt_forever(NULL);
    }
    sd_card_t *sd = sd_get_by_num(card_num);
    if (!sd) {
        printf("No SD config found (sd_get_by_num(%d) == NULL)\n", card_num);
        halt_forever(NULL);
    }
    const char *drive = sd_get_drive_prefix(sd);

    FRESULT fr = f_mount(pfs, drive, 1);
    printf("f_mount -> %s (%d)\n", FRESULT_str(fr), fr);

    if (fr == FR_NO_FILESYSTEM) {
        BYTE work[4096];                 
        MKFS_PARM opt = { FM_FAT | FM_SFD, 0, 0, 0, 0 };
        fr = f_mkfs(drive, &opt, work, sizeof work);
        printf("f_mkfs -> %s (%d)\n", FRESULT_str(fr), fr);
        if (fr != FR_OK) die(fr, "f_mkfs");

        fr = f_mount(pfs, drive, 1);
        printf("f_mount(after mkfs) -> %s (%d)\n", FRESULT_str(fr), fr);
    }
    if (fr != FR_OK) die(fr, "f_mount");

    return drive;
}

static void open_log_file(FIL *f, const char *drive, const char *prefix, bool write_header) {
    char path[96];
    uint64_t t = time_us_64();

    snprintf(path, sizeof path, "%s/%s_%llu.txt", drive, prefix, (unsigned long long)t);

    FRESULT fr = f_open(f, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) die(fr, "f_open");
    printf("logging to: %s\n", path);

    if (write_header) {
        const char *hdr = "ax,ay,az\n";
        UINT bw = 0;
        fr = f_write(f, hdr, (UINT)strlen(hdr), &bw);
        if (fr != FR_OK || bw != strlen(hdr)) die(fr, "f_write(header)");
        f_sync(f);
    }
}


static void core1_entry(void) 
{
    const char *drive = sd_mount_or_format(&fs, 0);

    uint32_t last_seen_session = 0;

    for (;;) {
        while (SESSION == last_seen_session && !STOP_ALL) { tight_loop_contents(); }
        if (STOP_ALL) break;

        FIL f;
        open_log_file(&f, drive, FILE_NAME_PREFIX, true);

        UINT bw; FRESULT fr; uint32_t lines_since_sync = 0;
        imu_sample_t s;
        for (;;) 
        {
            queue_remove_blocking(&sample_q, &s);
            
            char line[96];
            int n = snprintf(line, sizeof line, "%d,%d,%d\n",
                                s.ax, s.ay, s.az);
            bw = 0;
            fr = f_write(&f, line, (UINT)n, &bw);
            if (fr != FR_OK || bw != (UINT)n) die(fr, "f_write(sample)");
            if (++lines_since_sync >= 128) { f_sync(&f); lines_since_sync = 0; }
            
            if (!RECORD && queue_is_empty(&sample_q)) break; // session done
            tight_loop_contents();
        }

        f_sync(&f);
        f_close(&f);

        last_seen_session = SESSION;
    }

    f_unmount(drive);
    while (1) tight_loop_contents();
}

int main() 
{
    stdio_init_all();
    sleep_ms(2000);

    init_pio_for_ws2812();
    show_color_rgb(255, 0, 0);

    queue_init(&sample_q, sizeof(imu_sample_t), QUEUE_DEPTH);
    multicore_launch_core1(core1_entry);
    printf("Device setup complete on core1\n");

    IMU_EN_SENSOR_TYPE enMotionSensorType;
    IMU_ST_SENSOR_DATA stGyroRawData, stAccelRawData;
    imuInit(&enMotionSensorType);
    printf("Device setup complete on core0\n");
    show_color_rgb(0, 255, 0);

    for (uint32_t i = 0; i < RECORD_TIMES; i++) 
    {     
        SESSION++;       
        RECORD = true;   

        show_color_rgb(0, 255, 0);
        
        uint32_t start_time = time_us_64();
        while (time_us_64() < start_time + MAX_DATA_COLLECTION_TIME_US) 
        {
            imuDataOnlyGet(&stGyroRawData, &stAccelRawData);

            imu_sample_t s = {
                .ax = stAccelRawData.s16X, .ay = stAccelRawData.s16Y, .az = stAccelRawData.s16Z,
            };

            if (!queue_try_add(&sample_q, &s)) {
                show_color_rgb(0, 0, 255); // overflow indicator
            }
            // optional: sleep_us(1000); // 1 kHz source rate
        }

        RECORD = false;     

        // End of session
        (show_color_rgb(1,1,1), sleep_ms(500),show_color_rgb(0,0,255), sleep_ms(500),show_color_rgb(1,1,1), sleep_ms(500),show_color_rgb(0,0,255), sleep_ms(500)); 
    }

    STOP_ALL = true;      

    for(;;){ show_color_rgb(0,0,255); sleep_ms(250); show_color_rgb(1,1,1); sleep_ms(250); }
    return 0;
}