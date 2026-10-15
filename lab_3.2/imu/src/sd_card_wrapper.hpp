#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "ff.h"
#include "sd_card.h"
#include "f_util.h"
#include "hw_config.h"

#ifdef __cplusplus
}
#endif

#define PATH_MAX_LEN 256

typedef struct {
    uint32_t files;
    uint32_t dirs;
    uint64_t total_bytes;
} list_stats_t;

class SD_CardWrapper {
public:
    // Legacy constructor: mounts immediately, formats a card without file system and halts the
    // program (infinite loop) when anything fails. Kept for the existing demo targets.
    SD_CardWrapper();
    // Non-fatal variant: constructs without touching the hardware. Call mount() afterwards; it
    // reports failures instead of halting and never formats the card.
    struct NoHalt {};
    explicit SD_CardWrapper(NoHalt) : sd_(nullptr), drive_(nullptr), mounted_(false) {}
    ~SD_CardWrapper();

    bool mount(FRESULT* fr_out = nullptr);
    void unmount();  // forget the volume (e.g. after I/O errors); mount() can be called again
    // Creates a directory (no error if it exists). writeFileAtomic() writes "<rel>.tmp", f_sync()s, closes,
    // verifies the size and renames it over <rel>; every FatFs result is checked. `fr_out` gets the failing FRESULT.
    bool makeDir(const std::string& relPath, FRESULT* fr_out = nullptr);
    bool writeFileAtomic(const std::string& relPath, const void* data, size_t len, FRESULT* fr_out = nullptr);
    // Absolute FatFs path ("sd0:/rel") for use with the FatFs API directly.
    bool toAbsolute(char* out, size_t out_sz, const char* rel) { return joinPath(out, out_sz, rel); }

    bool isMounted() const { return mounted_; }

    size_t getFileSize(const std::string& relPath);
    bool createFile(const std::string& relPath);
    bool writeFile(const std::string& relPath, const void* data, size_t len);
    bool appendToFile(const std::string& relPath, const void* data, size_t len);
    bool readFile(const std::string& relPath, std::vector<uint8_t>& out);
    bool readChunkedFile(const std::string& relPath, std::vector<uint8_t>& out_chunks, size_t chunk_size, size_t offset);
    bool deleteFile(const std::string& relPath);
    bool clearFile(const std::string& relPath);
    bool listFiles();

    // Streaming write: the file stays open while data is appended (FatFs buffers one sector internally),
    // so the caller never has to hold the data in RAM. closeStream() flushes and closes.
    bool openStream(const std::string& relPath);
    bool writeStream(const void* data, size_t len);
    bool closeStream();
    bool isStreamOpen() const { return stream_open_; }
    // FatFs result of the last makeDir/openStream/writeStream/closeStream call (FR_NOT_READY if not mounted).
    FRESULT lastResult() const { return last_result_; }

private:
    bool joinPath(char* out, size_t out_sz, const char* rel);
    FRESULT writeToFile(FIL *file, const void *data, UINT len, UINT *bytes_written);
    FRESULT checkAndListFiles(void);
    FRESULT listDirRecursive(const char *path, list_stats_t *stats);
    void printFresult(FRESULT fr, const char* op);

    FATFS fs_;
    FIL stream_;
    bool stream_open_ = false;
    FRESULT last_result_ = FR_OK;
    sd_card_t* sd_;
    const char* drive_;
    bool mounted_;
};