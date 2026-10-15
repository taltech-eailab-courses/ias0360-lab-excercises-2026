#ifndef MAIN_H
#define MAIN_H

#define RESULT_W 180
#define RESULT_H 60

#define DATA_SIZE RESULT_W*RESULT_H
#define REQUEST_CHUNK_SIZE 2048
#define REQUEST_ID "id_00002"

#define DATA_WRITE_COMPLETE_FLAG 0x1234ABCD

#define PACKET_READY_FLAG 0x1111AAAA
#define DATA_ACK_FLAG     0xAAAA1111
#define REQUEST_FAILED_FLAG 0x2222AAAA
#define END_REQUEST_FLAG  0xFFFFFFFF
#define EXIT_PROGRAM_FLAG 0xDEADDEAD


// TODO: Set your Wi-Fi SSID and password here
const char WIFI_SSID[] = "your_wifi_ssid";
const char WIFI_PASSWORD[] = "your_wifi_password";

const char SD_CARD_FILENAME[] = "large_request_data_test.bin";

typedef struct {
    int packet_index;
    int total_packet_size;
    size_t data_len;
    uint8_t data[REQUEST_CHUNK_SIZE];  // fixed-size chunk for simplicity
} OCR_Packet;


#endif // MAIN_H