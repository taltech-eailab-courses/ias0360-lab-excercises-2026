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
    SD_CardWrapper();
    ~SD_CardWrapper();

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

private:
    bool joinPath(char* out, size_t out_sz, const char* rel);
    FRESULT writeToFile(FIL *file, const void *data, UINT len, UINT *bytes_written);
    FRESULT checkAndListFiles(void);
    FRESULT listDirRecursive(const char *path, list_stats_t *stats);
    void printFresult(FRESULT fr, const char* op);

    FATFS fs_;
    sd_card_t* sd_;
    const char* drive_;
    bool mounted_;
};