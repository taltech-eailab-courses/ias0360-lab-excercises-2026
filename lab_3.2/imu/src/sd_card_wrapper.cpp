#include <stdio.h>
#include <string.h>
#include <cstring>
#include <vector>
#include <cstdio>

#include "sd_card_wrapper.hpp"

#define HALT_WITH_MESSAGE(fmt, ...) do { \
    printf("Fatal: " fmt "\n", ##__VA_ARGS__); \
    while (1) { \
        tight_loop_contents(); \
    } \
} while (0)

static bool is_dot_or_dotdot(const char *name) {
    return (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')));
}

SD_CardWrapper::SD_CardWrapper(){
    
    if (!sd_init_driver()) {
        HALT_WITH_MESSAGE("sd_init_driver() failed.");
    }

    this->sd_ = sd_get_by_num(0);
    if (!this->sd_) {
        HALT_WITH_MESSAGE("No SD config found (sd_get_by_num(0) == NULL)");
    }

    this->drive_ = sd_get_drive_prefix(this->sd_);
    if (!this->drive_) {
        HALT_WITH_MESSAGE("sd_get_drive_prefix() returned NULL");
    }

    FRESULT fr = f_mount(&this->fs_, this->drive_, 1);
    printf("f_mount -> %s (%d)\n", FRESULT_str(fr), fr);

    if (fr == FR_NO_FILESYSTEM) {
        BYTE work[4096]; // >= FF_MAX_SS
        MKFS_PARM opt = { FM_FAT | FM_SFD, 0, 0, 0, 0 };
        fr = f_mkfs(this->drive_, &opt, work, sizeof(work));
        printf("f_mkfs -> %s (%d)\n", FRESULT_str(fr), fr);
        if (fr == FR_OK) {
            fr = f_mount(&this->fs_, this->drive_, 1);
            printf("f_mount(after mkfs) -> %s (%d)\n", FRESULT_str(fr), fr);
        }
    }

    if (fr != FR_OK) {
        HALT_WITH_MESSAGE("SD card mount failed: %s (%d)", FRESULT_str(fr), fr);
    }

    printf("SD card mounted at %s\n", this->drive_);
    this->mounted_ = true;
}

bool SD_CardWrapper::mount(FRESULT* fr_out) {
    FRESULT fr = FR_NOT_READY;
    if (mounted_) {
        if (fr_out) *fr_out = FR_OK;
        return true;
    }
    if (!sd_init_driver()) {
        printf("[SD_CardWrapper] sd_init_driver() failed\n");
        if (fr_out) *fr_out = fr;
        return false;
    }
    sd_ = sd_get_by_num(0);
    drive_ = sd_ ? sd_get_drive_prefix(sd_) : nullptr;
    if (!sd_ || !drive_) {
        if (fr_out) *fr_out = FR_INVALID_DRIVE;
        return false;
    }
    fr = f_mount(&fs_, drive_, 1);
    printFresult(fr, "f_mount");
    if (fr_out) *fr_out = fr;
    mounted_ = (fr == FR_OK);
    return mounted_;
}

bool SD_CardWrapper::makeDir(const std::string& relPath, FRESULT* fr_out) {
    char abs_path[PATH_MAX_LEN];
    FRESULT fr = FR_NOT_READY;
    if (mounted_ && joinPath(abs_path, sizeof abs_path, relPath.c_str())) {
        fr = f_mkdir(abs_path);
        if (fr == FR_EXIST) fr = FR_OK;
    }
    printFresult(fr, "makeDir");
    last_result_ = fr;
    if (fr_out) *fr_out = fr;
    return fr == FR_OK;
}

bool SD_CardWrapper::writeFileAtomic(const std::string& relPath, const void* data, size_t len, FRESULT* fr_out) {
    char final_path[PATH_MAX_LEN], tmp_path[PATH_MAX_LEN + 8];
    FRESULT fr = FR_NOT_READY;
    auto done = [&](FRESULT r) {
        if (fr_out) *fr_out = r;
        return r == FR_OK;
    };
    if (!mounted_ || !joinPath(final_path, sizeof final_path, relPath.c_str())) return done(fr);
    snprintf(tmp_path, sizeof tmp_path, "%s.tmp", final_path);

    FIL file;
    fr = f_open(&file, tmp_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return done(fr);
    UINT written = 0;
    fr = f_write(&file, data, static_cast<UINT>(len), &written);
    if (fr == FR_OK && written != len) fr = FR_DENIED;  // short write: volume full
    if (fr == FR_OK) fr = f_sync(&file);
    const FRESULT fc = f_close(&file);
    if (fr == FR_OK) fr = fc;
    FILINFO info;
    if (fr == FR_OK) {
        fr = f_stat(tmp_path, &info);
        if (fr == FR_OK && info.fsize != len) fr = FR_INT_ERR;
    }
    if (fr == FR_OK) {
        f_unlink(final_path);  // FR_NO_FILE is fine
        fr = f_rename(tmp_path, final_path);
    }
    if (fr != FR_OK) f_unlink(tmp_path);
    return done(fr);
}

void SD_CardWrapper::unmount() {
    if (mounted_ && drive_) f_unmount(drive_);
    mounted_ = false;
    // Force a full card initialisation on the next mount (card may have been swapped).
    if (sd_) sd_->state.m_Status |= STA_NOINIT;
}

SD_CardWrapper::~SD_CardWrapper() {
    if (this->mounted_) {
        f_unmount(this->drive_);
        this->mounted_ = false;
    }
    this->sd_ = nullptr;
    this->drive_ = nullptr;
}

bool SD_CardWrapper::joinPath(char* out, size_t out_sz, const char* rel) {
    if (!this->drive_) return false;
    if (rel && rel[0] == '/') rel++;
    if (this->drive_[strlen(this->drive_) - 1] == '/')
        snprintf(out, out_sz, "%s%s", this->drive_, rel ? rel : "");
    else
        snprintf(out, out_sz, "%s/%s", this->drive_, rel ? rel : "");
    return true;
}

void SD_CardWrapper::printFresult(FRESULT fr, const char* op) {
    if (fr != FR_OK)
        printf("[SD_CardWrapper] %s failed: %s (%d)\n", op, FRESULT_str(fr), fr);
}

FRESULT SD_CardWrapper::writeToFile(FIL *file, const void *data, UINT len, UINT *bytes_written) {
    *bytes_written = 0;
    FRESULT fr = f_write(file, data, len, bytes_written);
    if (fr == FR_OK) {
        fr = f_sync(file); // ensure data hits the card
    }
    return fr;
}

size_t SD_CardWrapper::getFileSize(const std::string& relPath) {
    if (!this->mounted_) return 0;

    char abs_path[256];
    joinPath(abs_path, sizeof(abs_path), relPath.c_str());

    FIL file;
    FRESULT fr = f_open(&file, abs_path, FA_READ);
    if (fr != FR_OK) {
        printFresult(fr, "f_open");
        return 0;
    }

    FSIZE_t size = f_size(&file);
    f_close(&file);
    return static_cast<size_t>(size);
}

FRESULT SD_CardWrapper::listDirRecursive(const char *path, list_stats_t *stats) {
    DIR dir;
    FILINFO fno;
    FRESULT fr = f_opendir(&dir, path);
    if (fr != FR_OK) {
        printf("f_opendir('%s') -> %s (%d)\n", path, FRESULT_str(fr), fr);
        return fr;
    }

    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK) {
            printf("f_readdir('%s') -> %s (%d)\n", path, FRESULT_str(fr), fr);
            break;
        }
        if (fno.fname[0] == '\0') break; // end of directory

        if (is_dot_or_dotdot(fno.fname)) continue;

        if (fno.fattrib & AM_DIR) {
            stats->dirs++;
            char subpath[PATH_MAX_LEN];
            snprintf(subpath, sizeof(subpath), "%s/%s", path, fno.fname);
            printf("[DIR]  %s\n", subpath);
            fr = listDirRecursive(subpath, stats);
            if (fr != FR_OK) break;
        } else {
            stats->files++;
            stats->total_bytes += (uint64_t)fno.fsize;
            printf("[FILE] %s/%s  (%lu bytes)\n", path, fno.fname, (unsigned long)fno.fsize);
        }
    }

    FRESULT frc = f_closedir(&dir);
    if (fr == FR_OK && frc != FR_OK) fr = frc;
    return fr;
}

FRESULT SD_CardWrapper::checkAndListFiles(void) {
    
    char root[PATH_MAX_LEN];
    joinPath(root, sizeof(root), ""); // ensures a trailing slash when we add children

    list_stats_t stats = {0};
    printf("\n--- SD Card File Listing for '%s' ---\n", this->drive_);
    FRESULT fr = listDirRecursive(this->drive_, &stats);
    if (fr != FR_OK && fr != FR_NO_PATH) {
        printf("Directory listing aborted due to error.\n");
        return fr;
    }

    if (stats.files == 0 && stats.dirs == 0) {
        printf("No files or directories found on the SD card.\n");
    } else if (stats.files == 0) {
        printf("No files found (but %u director%s present).\n", stats.dirs, (stats.dirs == 1 ? "y" : "ies"));
    } else {
        printf("\nSummary: %u file%s in %u director%s, total %llu bytes.\n",
               stats.files, (stats.files == 1 ? "" : "s"),
               stats.dirs, (stats.dirs == 1 ? "y" : "ies"),
               (unsigned long long)stats.total_bytes);
    }
    return FR_OK;
}

bool SD_CardWrapper::createFile(const std::string& relPath) {
    if (!this->mounted_) return false;

    char abs_path[256];
    joinPath(abs_path, sizeof(abs_path), relPath.c_str());

    FIL file;
    FRESULT fr = f_open(&file, abs_path, FA_CREATE_ALWAYS | FA_CREATE_NEW);
    if (fr != FR_OK && fr != FR_EXIST) {
        printFresult(fr, "create_file");
        return false;
    }

    f_close(&file);
    printf("[SD_CardWrapper] Created file %s\n", abs_path);
    return true;
}

bool SD_CardWrapper::writeFile(const std::string& relPath, const void* data, size_t len) {
    if (!this->mounted_) return false;

    char abs_path[256];
    joinPath(abs_path, sizeof(abs_path), relPath.c_str());

    FIL file;
    FRESULT fr = f_open(&file, abs_path, FA_WRITE | FA_OPEN_ALWAYS);
    if (fr != FR_OK) {
        printFresult(fr, "f_open(append)");
        return false;
    }

    UINT written = 0;
    fr = f_write(&file, data, len, &written);
    if (fr != FR_OK) {
        printFresult(fr, "write_to_file");
        f_close(&file);
        return false;
    }

    f_close(&file);
    printf("[SD_CardWrapper] Wrote %u bytes to %s\n", written, abs_path);
    return true;
}

bool SD_CardWrapper::openStream(const std::string& relPath) {
    char abs_path[PATH_MAX_LEN];
    last_result_ = FR_NOT_READY;
    if (!mounted_ || stream_open_ || !joinPath(abs_path, sizeof abs_path, relPath.c_str())) return false;

    last_result_ = f_open(&stream_, abs_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (last_result_ != FR_OK) {
        printFresult(last_result_, "openStream");
        return false;
    }
    stream_open_ = true;
    return true;
}

bool SD_CardWrapper::writeStream(const void* data, size_t len) {
    if (!stream_open_) return false;

    UINT written = 0;
    last_result_ = f_write(&stream_, data, static_cast<UINT>(len), &written);
    if (last_result_ == FR_OK && written != len) last_result_ = FR_DENIED;  // short write: volume full
    if (last_result_ != FR_OK) {
        printFresult(last_result_, "writeStream");
        return false;
    }
    return true;
}

bool SD_CardWrapper::closeStream() {
    if (!stream_open_) return false;

    stream_open_ = false;
    last_result_ = f_close(&stream_);  // flushes the last partial sector
    printFresult(last_result_, "closeStream");
    return last_result_ == FR_OK;
}

bool SD_CardWrapper::appendToFile(const std::string& relPath, const void* data, size_t len) {
    if (!this->mounted_) return false;

    char abs_path[256];
    joinPath(abs_path, sizeof(abs_path), relPath.c_str());

    FIL file;
    FRESULT fr = f_open(&file, abs_path, FA_WRITE | FA_OPEN_ALWAYS);
    if (fr != FR_OK) {
        printFresult(fr, "f_open(append)");
        return false;
    }

    // Move to the end of the file
    fr = f_lseek(&file, f_size(&file));
    if (fr != FR_OK) {
        printFresult(fr, "f_lseek");
        f_close(&file);
        return false;
    }

    printf("Writing data (hex): ");
    for (size_t i = 0; i < len; ++i) {
        printf("%02X ", static_cast<const uint8_t*>(data)[i]);
    }

    printf("\n");
    UINT written = 0;
    fr = f_write(&file, data, len, &written);
    if (fr != FR_OK) {
        printFresult(fr, "write_to_file");
        f_close(&file);
        return false;
    }

    f_close(&file);
    printf("[SD_CardWrapper] Appended %u bytes to %s\n", written, abs_path);
    return true;
}


bool SD_CardWrapper::clearFile(const std::string& relPath) {
    if (!this->mounted_) return false;

    char abs_path[256];
    joinPath(abs_path, sizeof(abs_path), relPath.c_str());

    FIL file;
    FRESULT fr = f_open(&file, abs_path, FA_WRITE | FA_OPEN_ALWAYS);
    if (fr != FR_OK) {
        printFresult(fr, "f_open(append)");
        return false;
    }

    // Truncate the file to zero length
    fr = f_truncate(&file);
    if (fr != FR_OK) {
        printFresult(fr, "f_truncate");
        f_close(&file);
        return false;
    }

    f_close(&file);
    printf("[SD_CardWrapper] Cleared file %s\n",  abs_path);
    return true;
}

bool SD_CardWrapper::readFile(const std::string& relPath, std::vector<uint8_t>& out) {
    if (!this->mounted_) return false;

    char abs_path[256];
    joinPath(abs_path, sizeof(abs_path), relPath.c_str());

    FIL file;
    FRESULT fr = f_open(&file, abs_path, FA_READ);
    if (fr != FR_OK) {
        printFresult(fr, "f_open");
        return false;
    }

    FSIZE_t size = f_size(&file);
    out.resize(size);
    UINT br = 0;
    fr = f_read(&file, out.data(), size, &br);
    f_close(&file);

    if (fr != FR_OK) {
        printFresult(fr, "f_read");
        return false;
    }

    printf("[SD_CardWrapper] Read %u bytes from %s\n", br, abs_path);
    return true;
}

bool SD_CardWrapper::readChunkedFile(const std::string& relPath, std::vector<uint8_t>& out_chunks, size_t chunk_size, size_t offset) {
    if (!this->mounted_) return false;

    char abs_path[256];
    joinPath(abs_path, sizeof(abs_path), relPath.c_str());

    FIL file;
    FRESULT fr = f_open(&file, abs_path, FA_READ);
    if (fr != FR_OK) {
        printFresult(fr, "f_open(append)");
        return false;
    }

    size_t file_size = static_cast<size_t>(f_size(&file));
    if (offset >= file_size) {
        printFresult(FR_INVALID_PARAMETER, "Offset beyond file size");
        f_close(&file);
        return false;
    }

    // Resize output to hold one chunk
    out_chunks.resize(chunk_size);

    // Move to the end of the file
    fr = f_lseek(&file, static_cast<FSIZE_t>(offset));
    if (fr != FR_OK) {
        printFresult(fr, "f_lseek");
        f_close(&file);
        return false;
    }

    UINT br = 0;
    fr = f_read(&file, out_chunks.data(), chunk_size, &br);
    f_close(&file);

    if (fr != FR_OK) {
        printFresult(fr, "f_read");
        return false;
    }

    printf("[SD_CardWrapper] Read %u bytes from %s\n", br, abs_path);
    return true;
}

bool SD_CardWrapper::deleteFile(const std::string& relPath) {
    if (!this->mounted_) return false;

    char abs_path[256];
    joinPath(abs_path, sizeof(abs_path), relPath.c_str());

    FRESULT fr = f_unlink(abs_path);
    if (fr != FR_OK) {
        printFresult(fr, "f_unlink");
        return false;
    }

    printf("[SD_CardWrapper] Deleted %s\n", abs_path);
    return true;
}

bool SD_CardWrapper::listFiles() {
    if (!mounted_) return false;
    FRESULT fr = checkAndListFiles();
    if (fr != FR_OK) {
        printFresult(fr, "checkAndListFiles");
        return false;
    }
    return true;
}