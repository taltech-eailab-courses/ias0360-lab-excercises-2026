#include "icm20948.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"     // only used by main_4
#include "pico/util/queue.h"    // only used by main_4

int main(void)
{
    stdio_init_all();

    IMU_EN_SENSOR_TYPE enMotionSensorType;
    IMU_ST_ANGLES_DATA stAngles;
    IMU_ST_SENSOR_DATA stGyroRawData;
    IMU_ST_SENSOR_DATA stAccelRawData;
    IMU_ST_SENSOR_DATA stMagnRawData;

    imuInit(&enMotionSensorType);
    if (IMU_EN_SENSOR_TYPE_ICM20948 == enMotionSensorType) {
        printf("Motion sensor is ICM-20948\n");
    } else {
        printf("Motion sensor NULL\n");
    }

    uint64_t t_prev = time_us_64();

    while (1) {
        imuDataGet(&stAngles, &stGyroRawData, &stAccelRawData, &stMagnRawData);

        uint64_t t_now = time_us_64();
        uint64_t dt_us = (t_now - t_prev);
        t_prev = t_now;
        float hz = (dt_us > 0) ? (1000000.0f / (float)dt_us) : 0.0f;

        printf("\r\n/-------------------------------------------------------------/\r\n");
        printf("Roll: %.2f   Pitch: %.2f   Yaw: %.2f   |   Sample Rate: %.1f Hz\r\n",
               stAngles.fRoll, stAngles.fPitch, stAngles.fYaw, hz);

        //sleep_ms(100);
    }

    return 0;
}

int main_2(void)
{
    stdio_init_all();

    IMU_EN_SENSOR_TYPE type;
    IMU_ST_SENSOR_DATA g, a;
    imuInit(&type);

    if (IMU_EN_SENSOR_TYPE_ICM20948 == type) {
        printf("Motion sensor is ICM-20948\n");
    } else {
        printf("Motion sensor NULL\n");
    }

    uint64_t t_prev = time_us_64();

    while (1) {
        int16_t gx, gy, gz, ax, ay, az;

        icm20948AccelRead(&ax, &ay, &az);
        icm20948GyroRead (&gx, &gy, &gz);

        uint64_t t_now = time_us_64();
        uint64_t dt_us = (t_now - t_prev);
        t_prev = t_now;
        float hz = (dt_us > 0) ? (1000000.0f / (float)dt_us) : 0.0f;

        printf("ACC [mg-ish raw]: X=%d Y=%d Z=%d   |   GYRO [LSB]: X=%d Y=%d Z=%d   |   %.1f Hz\r\n",
               ax, ay, az, gx, gy, gz, hz);

        // sleep_ms(5); // small delay to avoid spamming
    }

    return 0;
}

int main_3(void)
{
    stdio_init_all();

    IMU_EN_SENSOR_TYPE type;
    imuInit(&type);

    if (IMU_EN_SENSOR_TYPE_ICM20948 == type) {
        printf("Motion sensor is ICM-20948 (FAST read path)\n");
    } else {
        printf("Motion sensor NULL\n");
    }

    uint64_t t_prev = time_us_64();

    while (1) {
        int16_t gx, gy, gz, ax, ay, az;

        // Option A: call the fast functions directly
        icm20948AccelFastRead(&ax, &ay, &az);
        icm20948GyroFastRead (&gx, &gy, &gz);

        // // Option B: if you prefer the wrapper:
        // IMU_ST_SENSOR_DATA g, a;
        // imuDataOnlyGet(&g, &a);
        // gx=g.s16X; gy=g.s16Y; gz=g.s16Z;
        // ax=a.s16X; ay=a.s16Y; az=a.s16Z;

        uint64_t t_now = time_us_64();
        uint64_t dt_us = (t_now - t_prev);
        t_prev = t_now;
        float hz = (dt_us > 0) ? (1000000.0f / (float)dt_us) : 0.0f;

        printf("FAST ACC: X=%d Y=%d Z=%d   |   FAST GYRO: X=%d Y=%d Z=%d   |   %.1f Hz\r\n",
               ax, ay, az, gx, gy, gz, hz);
        // No sleep => let it run as fast as possible
    }

    return 0;
}

typedef struct {
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
    uint32_t t_us;   // timestamp (lower 32 bits is fine for short runs)
} Sample;

static queue_t sample_q;

static void core1_reader(void)
{
    IMU_EN_SENSOR_TYPE type;

    // Important: init I2C/IMU on this core as well if your drivers require per-core context.
    // Usually one init on core0 is fine if both cores share the same hardware state,
    // but to be safe we at least check sensor here.
    // If needed, comment out the next line.
    // imuInit(&type);

    uint32_t t_prev = (uint32_t)time_us_64();

    while (1) {
        IMU_ST_SENSOR_DATA stGyroRawData, stAccelRawData;
        // int16_t gx, gy, gz, ax, ay, az;
        // icm20948AccelFastRead(&ax, &ay, &az);
        // icm20948GyroFastRead (&gx, &gy, &gz);

        imuDataAccGyrGet(&stGyroRawData, &stAccelRawData);

        uint32_t t_now = (uint32_t)time_us_64();
        Sample s = { stAccelRawData.s16X, stAccelRawData.s16Y, stAccelRawData.s16Z,
                      stGyroRawData.s16X, stGyroRawData.s16Y, stGyroRawData.s16Z,
                      t_now };
        queue_add_blocking(&sample_q, &s);

        // (Optional) pace the producer slightly if needed
        // sleep_us(500); // ~2 kHz -> uncomment to throttle
    }
}

int main_4(void)
{
    stdio_init_all();

    IMU_EN_SENSOR_TYPE type;
    imuInit(&type);

    if (IMU_EN_SENSOR_TYPE_ICM20948 == type) {
        printf("Motion sensor is ICM-20948 (multicore)\n");
    } else {
        printf("Motion sensor NULL\n");
    }

    // Queue can hold up to N samples; adjust for your bandwidth
    queue_init(&sample_q, sizeof(Sample), 64);

    // Launch core1 reader
    multicore_launch_core1(core1_reader);

    uint32_t t_prev = (uint32_t)time_us_64();

    while (1) {
        Sample s;
        queue_remove_blocking(&sample_q, &s);

        uint32_t t_now = (uint32_t)time_us_64();
        uint32_t dt_us = (t_now - t_prev);
        t_prev = t_now;
        float hz = (dt_us > 0) ? (1000000.0f / (float)dt_us) : 0.0f;

        printf("ACC: X=%d Y=%d Z=%d | GYRO: X=%d Y=%d Z=%d | RX Rate: %.1f Hz\r\n",
               s.ax, s.ay, s.az, s.gx, s.gy, s.gz, hz);
        // No sleep: printing rate is now bounded mostly by USB/serial throughput.
    }

    return 0;
}
