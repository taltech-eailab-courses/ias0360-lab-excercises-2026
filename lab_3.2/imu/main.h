#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

// Project-specific constants for main.cpp currently live as local #defines
// in main.cpp itself (NUMBER_OF_CSV_FILES, REQUEST_ID). Add shared
// declarations/constants here as the project grows.


#define REQUEST_ID "YOUR_STUDENT_ID"
#define NUM_CHARACTER 5
#define STATIONARY_US 200000       // stationary prefix (0.2 s in the notebook)
#define PREPARE_SECONDS 2          // hold-still countdown before every character
#define OUTPUT_RATE_HZ 100         // the stored / uploaded trajectory is decimated to about this rate
#define CAPTURE_QUEUE_DEPTH 8

// Cutoff of the acceleration low-pass (50 Hz in the notebook); override with -DAW_LPF_CUTOFF_HZ=<hz>.
#ifndef AW_LPF_CUTOFF_HZ
#define AW_LPF_CUTOFF_HZ 50
#endif

// Core 0 fills raw_samples, then hands the character over to core 1 through capture_queue; core 0 does not touch
// the buffers again before core 1 has finished the character (chars_finished).
enum { MSG_CHAR_CAPTURED = 1, MSG_SESSION_END };
typedef struct {
    uint8_t type;
    uint8_t char_index;
    uint8_t char_count;
    uint32_t session_id;
    uint32_t sample_count;       // stationary prefix + gesture
    uint32_t prefix_count;
    float sample_rate_hz;        // measured rate of the gesture samples
} CaptureMsg;


// Which SD step failed (1 mount, 2 mkdir, 3 open, 4 write, 5 close, 6 estimate); reported with the FatFs result.
enum { STEP_MOUNT = 1, STEP_MKDIR, STEP_OPEN, STEP_WRITE, STEP_CLOSE, STEP_ESTIMATE };

#endif // MAIN_H
