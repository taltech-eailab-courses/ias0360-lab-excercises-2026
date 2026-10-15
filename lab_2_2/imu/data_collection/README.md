# Dataset Collection Application for ICM20948 IMU Sensor

This application collects accelerometer data from an ICM20948 IMU sensor connected to a Raspberry Pi Pico and saves it to an SD card in CSV format.

## Configuration Parameters

### 1. Sample Record Time

**Location**: Line 19 in `lab_0_1.cpp`
```cpp
const uint32_t MAX_DATA_COLLECTION_TIME_US = 10 * 1000 * 1000; // 10 seconds
```

**How to change**: Modify the value to set the duration for each recording session in microseconds.
- Current setting: 10 seconds (10 * 1000 * 1000 microseconds)
- Example: For 5 seconds, change to: `5 * 1000 * 1000`
- Example: For 30 seconds, change to: `30 * 1000 * 1000`

### 2. Number of Sample Collections

**Location**: Line 20 in `lab_0_1.cpp`
```cpp
uint32_t RECORD_TIMES = 10;
```

**How to change**: Modify this value to set how many recording sessions to perform.
- Current setting: 10 recording sessions
- Example: For 5 sessions, change to: `5`
- Example: For 100 sessions, change to: `100`

### 3. Wait Time Between Sample Collections

**Location**: Currently **NOT IMPLEMENTED** in the code. Sessions run back-to-back without delay.

**How to add**: To add a delay between recording sessions, modify the main loop (around line 177) by adding a sleep after each session:

```cpp
RECORD = false;     

// Add delay between sessions (example: 5 seconds)
sleep_ms(5000);  // 5000 milliseconds = 5 seconds

// End of session visual indicator
(show_color_rgb(1,1,1), sleep_ms(250),...);
```

### 4. Sampling Rate

**Current Implementation**: 
- **Maximum possible rate** - no artificial delays between samples
- The actual sampling rate depends on:
  - ICM20948 sensor's internal sampling rate
  - I2C communication speed
  - Processing overhead

**How to control sampling rate**: 
- **Location**: Line 207 in `lab_0_1.cpp` (currently commented out)
```cpp
// optional: sleep_us(1000); // 1 kHz source rate
```

**To set specific sampling rates**:
- **1 kHz**: Uncomment the line above: `sleep_us(1000);`
- **500 Hz**: Use `sleep_us(2000);`
- **100 Hz**: Use `sleep_us(10000);`
- **50 Hz**: Use `sleep_us(20000);`

## Data Storage

### File Location and Naming

**Storage**: Data is saved to the SD card mounted in the system.

**File naming format**: 
```
<PREFIX>_<TIMESTAMP>.txt
```

**File prefix configuration**:
- **Location**: Line 26 in `lab_0_1.cpp`
```cpp
char FILE_NAME_PREFIX[32] = "train";
```
- **How to change**: Modify the prefix string (e.g., change `"train"` to `"test"` or `"data"`)

**Example filenames**:
- `train_123456789.txt`
- `test_987654321.txt`

### File Format

**CSV format** with header:
```csv
ax,ay,az
-1024,2048,16383
-1000,2100,16200
...
```

**Data description**:
- `ax`: X-axis accelerometer raw value (int16_t, -32768 to 32767)
- `ay`: Y-axis accelerometer raw value (int16_t, -32768 to 32767)  
- `az`: Z-axis accelerometer raw value (int16_t, -32768 to 32767)

## Visual Indicators (WS2812 LED)

### LED Colors and Meanings

| Color | Status | Description |
|-------|--------|-------------|
| **Red** | Startup | System initializing |
| **Green** | Ready/Recording | System ready or actively recording |
| **Blue** | **Overflow Warning** | Sample queue overflow detected |
| **White/Blue Flash** | Session Complete | End of recording session indicator |
| **Blue Flash (Continuous)** | All Complete | All recording sessions finished |

### Overflow Notification

**What it means**: The sample queue buffer is full and data samples are being dropped.

**When it occurs**: 
- Sampling rate is too high for the system to process
- SD card write operations are too slow
- Queue buffer size is insufficient

**Location in code**: Line 205 in `lab_0_1.cpp`
```cpp
if (!queue_try_add(&sample_q, &s)) {
    show_color_rgb(0, 0, 255); // overflow indicator - BLUE LED
}
```

**How to fix overflow**:
1. Reduce sampling rate by adding delays (`sleep_us()`)
2. Increase queue buffer size by modifying `QUEUE_DEPTH` (line 32)
3. Use a faster SD card (Class 10 or higher)

## Buffer Configuration

**Queue buffer size**: 
- **Location**: Line 32 in `lab_0_1.cpp`
```cpp
#define QUEUE_DEPTH  (8192)      // 8192 * 8B = 64 KB ring -> ~8s at 1 kHz
```
- **Current capacity**: 8192 samples (64 KB buffer)
- **Duration**: ~8 seconds at 1 kHz sampling rate
- **How to change**: Increase value for larger buffer (e.g., `16384` for 16K samples)

## Hardware Requirements

- Raspberry Pi Pico
- ICM20948 IMU sensor (I2C connection)
- SD card and compatible SD card reader/module
- WS2812 LED connected to GPIO pin 4
- Proper I2C connections for the IMU sensor

## Build and Flash

Use the provided VS Code tasks:
1. **Compile**: Use "Compile Project" task
2. **Flash**: Use "Flash" or "Run Project" task

## Troubleshooting

1. **Blue LED constantly on**: Reduce sampling rate or increase buffer size
2. **No files created**: Check SD card connection and formatting
3. **Files empty**: Check IMU sensor I2C connections
4. **System hangs**: Check all hardware connections and power supply