#include <stdio.h>
#include <vector>
#include <cstring> 
#include <sstream>

#include "pico/sync.h"
#include "pico/stdlib.h"
#include "pico/platform.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/timer.h"

#include "main.h"
#include "request.hpp"
#include "sd_card_wrapper.hpp"

static inline void debug_unpause_timer(void) {
    timer_hw->dbgpause = 0; 
}

volatile OCR_Packet g_ocr_packet;
mutex_t mutex;  // Declare a mutex


int init_wifi_connection(void) {

	stdio_init_all();
    debug_unpause_timer();  

    sleep_ms(5000);

    if (cyw43_arch_init()) {
        printf("Wi-Fi chip init failed\n");
        return -1;
    }
    printf("Wi-Fi chip initialized successfully\n");

    cyw43_arch_enable_sta_mode();

    // Connect to the WiFI network - loop until connected
    printf("Start connecting wifi.\n");
    while(cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000) != 0) {
        printf("Attempting to connect...\n");
    }
    // Print a success message once connected
    printf("WiFi connected successfully! \n");

    sleep_ms(3000);

    return 0;
}

// Send a POST request with mnist float32 data
int post_request_with_mnist_data(void) {

    const char* post_url = "/api/v1/mnist";
    HttpRequest request(post_url);

    std::string json;
    json.append("{\"x\": \"");
    json.append(MNIST_TEST_DATA);
    json.append("\"}");
    request.set_body_raw(json.c_str(), json.size());

    ResponseDataStr response = request.post();
    if (response.status_ok) {
        printf("MNIST inference: %s \n", response.y);
        // Expected output: "8"
    } else {
        printf("MNIST prediction failed\n");
    }

    while (1) tight_loop_contents();
    return 0;
}


// Send a POST request with chunked body
int post_request_with_chunked_body(void) {

    const char* post_url = "/api/v1/test_large_request";
    HttpRequest request(post_url);

    // Send data in chunks
    size_t offset = 0;
    int packet_index = 1;
    size_t total_packets_count = (DATA_SIZE + REQUEST_CHUNK_SIZE - 1) / REQUEST_CHUNK_SIZE;

    // Assume that you know the total size of data to send
    while (offset < DATA_SIZE) {
        size_t remaining = DATA_SIZE - offset;
        size_t chunk_elems = (remaining > REQUEST_CHUNK_SIZE) ? REQUEST_CHUNK_SIZE : remaining;

        // Example of body construction for each chunk
        // This can be done by multicore, reading from SD-card, etc.
        std::ostringstream chunk_stream;
        for (size_t i = 0; i < chunk_elems; i++) {
            chunk_stream << static_cast<unsigned>(TEST_OCR_REQUEST[offset + i]);
            if (offset + i < DATA_SIZE - 1) {
                chunk_stream << ",";
            }
        }
        std::string json_str;
        json_str.append("{\"x\": \"");
        json_str.append(chunk_stream.str());
        json_str.append("\", \"request_id\": \"");
        json_str.append(REQUEST_ID);
        json_str.append("\", \"packet_index\": ");
        json_str.append(std::to_string(packet_index));
        json_str.append(", \"total_packets\": ");
        json_str.append(std::to_string(total_packets_count));
        json_str.append("}");

        request.set_body_raw(json_str.c_str(), json_str.size());

        ResponseDataStr response = request.post();
        if (!response.status_ok) {
            printf("Chunk send failed at element offset %zu.\n", offset);
            return ERR_VAL;
        }
        
        if (packet_index == total_packets_count - 1) {
            // After sending all the packets, we can process the response
            printf("Sent last packet %d/%d\n", packet_index + 1, total_packets_count);
            // Expecting {"data":"Large request received."}
            printf("Response: %s\n", response.y ? response.y : "No response");
        } else {
            printf("Sent packet %d/%d\n", packet_index + 1, total_packets_count);
        }

        offset += chunk_elems;
        packet_index++;
        sleep_ms(100);  // optional flush delay

        request.reinitialize();

        if (response.y) {
            free(response.y); // free the response string if it was allocated
            response.y = NULL;
        }
    }

    printf("Request complete\n");
    while (1) tight_loop_contents();
    return 0;
}


// ------ SD Card write function ------ //
void write_data_to_sd_card(void) {
    // Save large data to SD card for later use

    SD_CardWrapper sd_card;
    if (!sd_card.isMounted()) {
        printf("SD card not mounted, cannot write data\n");
        while (1) tight_loop_contents();
    }

    // filename to store large request data
    size_t offset = 0;
    size_t total_packets_count = (DATA_SIZE + REQUEST_CHUNK_SIZE - 1) / REQUEST_CHUNK_SIZE;

    // create a file in SD card
    if (!sd_card.createFile(SD_CARD_FILENAME)) {
        printf("Failed to create file on SD card\n");
        while (1) tight_loop_contents();
    }

    sleep_ms(100);  // optional flush delay

    // erase the content if any
    if (!sd_card.clearFile(SD_CARD_FILENAME)) {
        printf("Failed to clear existing file content on SD card\n");
        while (1) tight_loop_contents();
    }

    // write data in chunks
    bool success = true;
    while (offset < DATA_SIZE) {
        size_t remaining = DATA_SIZE - offset;
        size_t chunk_elems = (remaining > REQUEST_CHUNK_SIZE) ? REQUEST_CHUNK_SIZE : remaining;

        if (!sd_card.appendToFile(SD_CARD_FILENAME, TEST_OCR_REQUEST + offset, chunk_elems)) {
            printf("Failed to append data chunk to SD card\n");
            success = false;
            break;
        }
        
        offset += chunk_elems;

        sleep_ms(100);  // optional flush delay
    }
    if (!success) {
        printf("Data write to SD card failed.\n");
        while (1) tight_loop_contents();
    }

    printf("Large data written to SD card successfully\n");

    size_t file_size = sd_card.getFileSize(SD_CARD_FILENAME);
    printf("Written file size: %zu bytes\n", file_size);
    if (file_size != sizeof(TEST_OCR_REQUEST)) {
        printf("File size mismatched. Check the file content.\n");
    }

    printf("Data write to SD card completed.\n");

    multicore_fifo_push_blocking(DATA_WRITE_COMPLETE_FLAG); // indicate data write complete
    while (1) tight_loop_contents();
}

int prior_post_request_with_chunked_body_from_sd_card(void) {

    printf("Writing large data to SD card...\n");

    multicore_launch_core1(write_data_to_sd_card);

    while (true) {
        uint32_t g = multicore_fifo_pop_blocking();
        // Indicate data write complete
        if (g == DATA_WRITE_COMPLETE_FLAG) {
            printf("Data write to SD card completed.\n");
            break;
        }
    }

    printf("All data written to SD card.\n");
    printf("Destroying core 1...\n");
    multicore_reset_core1();

    return 0;
}

// ------ SD Card read and POST request function ------ //
void read_data_from_sd_card(void) {
    
    printf("[Publisher] initializing SD card for reading data...\n");
    SD_CardWrapper sd_card;
    printf("[Publisher] SD card wrapper initialized.\n");
    if (!sd_card.isMounted()) {
        printf("SD card not mounted, cannot read data\n");
        while (1) tight_loop_contents();
    }
    printf("[Publisher] SD card mounted successfully for reading data.\n");
    sleep_ms(100);  // optional flush delay

    // get total file size from SD card
    size_t file_size = sd_card.getFileSize(SD_CARD_FILENAME);
    printf("[Publisher] Reading file of size: %zu bytes\n", file_size);

    size_t offset = 0;
    size_t total_packets_count = (DATA_SIZE + REQUEST_CHUNK_SIZE - 1) / REQUEST_CHUNK_SIZE;
    std::vector<uint8_t> stored_data;

    for (int packet_index = 0; packet_index < total_packets_count; packet_index++) {
        size_t remaining = file_size - offset;
        size_t chunk_elems = (remaining > REQUEST_CHUNK_SIZE) ? REQUEST_CHUNK_SIZE : remaining;

        stored_data.clear();
        stored_data.resize(chunk_elems);

        // read chunked data from SD card
        if (!sd_card.readChunkedFile(SD_CARD_FILENAME, stored_data, chunk_elems, offset)) {
            printf("Failed to read data chunk from SD card\n");
            break;
        }
        printf("[Publisher] Read chunk of size %zu bytes from offset %zu\n", stored_data.size(), offset);

        mutex_enter_blocking(&mutex);
        memcpy((uint8_t *)g_ocr_packet.data, stored_data.data(), stored_data.size());
        g_ocr_packet.packet_index = packet_index;
        g_ocr_packet.total_packet_size = total_packets_count;
        g_ocr_packet.data_len = stored_data.size();
        mutex_exit(&mutex);


        // enqueue data to be sent in POST request
        multicore_fifo_push_blocking(PACKET_READY_FLAG);
        printf("[Publisher] Read packet index %d/%d bytes from SD card\n", packet_index, total_packets_count);

        uint32_t ack = multicore_fifo_pop_blocking();
        if (ack == REQUEST_FAILED_FLAG) {
            printf("Core 0 got failure notification from the main Core.\n");
            break;
        }

        offset += chunk_elems;
        printf("Core 0 got ack from the main Core.\n");
        printf("Processing the next chunk...\n");
        sleep_ms(100);  // optional flush delay
    }
    
    multicore_fifo_push_blocking(END_REQUEST_FLAG); // indicate end of request
    printf("Data read from SD card complete.\n");
    while (1) tight_loop_contents();
}


int post_request_with_chunked_body_from_sd_card(void) {

    printf("[Subscriber] Sending POST request with chunked body from SD card...\n");
    
    mutex_init(&mutex);  // Initialize the mutex
    
    // Set URL
    const char* post_url = "/api/v1/test_large_request";
    HttpRequest request(post_url);
    printf("[Subscriber] HTTP request object created.\n");

    sleep_ms(1000);  // optional flush delay

    multicore_launch_core1(read_data_from_sd_card);

    while (true) {
        uint32_t msg = multicore_fifo_pop_blocking();
        if (msg == END_REQUEST_FLAG) {
            printf("[Subscriber] All data chunks sent from SD card.\n");
            break;
        }
        
        // Copy data to shared buffer
        mutex_enter_blocking(&mutex);
        std::ostringstream chunk_stream;
        for (size_t i = 0; i < g_ocr_packet.data_len; i++) {
            chunk_stream << static_cast<unsigned>(g_ocr_packet.data[i]);
            if ((REQUEST_CHUNK_SIZE * g_ocr_packet.packet_index) + i < DATA_SIZE - 1) {
                chunk_stream << ",";
            }
        }
        std::string json_str;
        json_str.append("{\"x\": \"");
        json_str.append(chunk_stream.str());
        json_str.append("\", \"request_id\": \"");
        json_str.append(REQUEST_ID);
        json_str.append("\", \"packet_index\": ");
        json_str.append(std::to_string(g_ocr_packet.packet_index));
        json_str.append(", \"total_packets\": ");
        json_str.append(std::to_string(g_ocr_packet.total_packet_size));
        json_str.append("}");

        request.set_body_raw(json_str.c_str(), json_str.size());
        mutex_exit(&mutex);

        // Send POST request
        ResponseDataStr response = request.post();
        if (!response.status_ok) {
                printf("[Subscriber] Chunk send failed at packet index %zu.\n", g_ocr_packet.packet_index);
                multicore_fifo_push_blocking(REQUEST_FAILED_FLAG);
                sleep_ms(100);  // optional flush delay
                continue;
            }

        if (g_ocr_packet.packet_index == g_ocr_packet.total_packet_size - 1) {
            printf("[Subscriber] Sent last packet %d/%d\n", g_ocr_packet.packet_index + 1, g_ocr_packet.total_packet_size);
            printf("[Subscriber] Response: %s\n", response.y ? response.y : "No response");
        } else {
            printf("[Subscriber] Sent packet %d/%d\n", g_ocr_packet.packet_index + 1, g_ocr_packet.total_packet_size);
        }

        request.reinitialize();

        sleep_ms(300);  // optional flush delay
        multicore_fifo_push_blocking(DATA_ACK_FLAG); // acknowledge successful send

        if (response.y) {
            free(response.y); // free the response string if it was allocated
            response.y = NULL;
        }
    }

    // Destroy request object
    printf("[Subscriber] Destroying HTTP request object...\n");
    request.~HttpRequest();

    // destroy core 1
    printf("[Subscriber] Destroying core 1...\n");
    mutex_exit(&mutex);
    multicore_reset_core1();

    printf("[Subscriber] Exiting POST request function.\n");
    return 0;
}

int main(void)
{
    if (init_wifi_connection() != 0) {
        printf("Wi-Fi connection failed\n");
        return -1;
    }

    // Check WiFi POST request with MNIST data
    return post_request_with_mnist_data(); // Send MINIST POST request


    // Check WiFi POST request with chunked body stored in memory
    // return post_request_with_chunked_body(); // Send POST request with chunked body
    

    // Check WiFi POST request with chunked body stored in SD card
    // Before sending the POST request, first write data to SD card

    // Write text data to SD card
    // return prior_post_request_with_chunked_body_from_sd_card();

    // Then send POST request with chunked body read from SD card
    // return post_request_with_chunked_body_from_sd_card(); 
}