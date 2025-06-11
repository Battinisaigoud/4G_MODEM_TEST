#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_system.h"

#define UART_NUM UART_NUM_1
#define BUF_SIZE 1024
#define TXD_PIN (GPIO_NUM_17)
#define RXD_PIN (GPIO_NUM_16)

static const char *TAG = "SIM700L_TEST";

typedef struct {
    char restart_status[16];
    char imei[32];
    char imsi[32];
    char sim_number[64];
    char sim_pin_status[32];
    char network_status[32];
    int rssi;
    const char *rssi_description;
} sim700l_test_result_t;

static void uart_init()
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static void send_at_command(const char *cmd, char *response, size_t max_len)
{
    uart_flush(UART_NUM);
    uart_write_bytes(UART_NUM, cmd, strlen(cmd));
    uart_write_bytes(UART_NUM, "\r\n", 2);
    vTaskDelay(pdMS_TO_TICKS(1000));
    int len = uart_read_bytes(UART_NUM, (uint8_t *)response, max_len - 1, pdMS_TO_TICKS(3000));
    if (len > 0) {
        response[len] = '\0';
        for (int i = 0; i < len; i++) {
            if ((unsigned char)response[i] < 32 && response[i] != '\r' && response[i] != '\n') {
                response[i] = ' ';
            }
        }
    } else {
        strcpy(response, "ERROR");
    }
    ESP_LOGI(TAG, "CMD: %s", cmd);
    ESP_LOGI(TAG, "RESP: %s", response);
}

static void extract_line_value(const char *resp, char *output, const char *prefix)
{
    char buffer[BUF_SIZE];
    strcpy(buffer, resp);
    char *line = strtok(buffer, "\r\n");
    while (line != NULL) {
        if ((prefix && strstr(line, prefix)) || (!prefix && strspn(line, "0123456789") > 10)) {
            while (*line == ' ') line++;
            strcpy(output, line);
            return;
        }
        line = strtok(NULL, "\r\n");
    }
    strcpy(output, "N/A");
}

static void parse_rssi(const char *resp, int *rssi_value, const char **desc)
{
    char buffer[BUF_SIZE];
    strcpy(buffer, resp);
    char *line = strtok(buffer, "\r\n");
    while (line != NULL) {
        if (strstr(line, "+CSQ:")) {
            int rssi;
            if (sscanf(line, "+CSQ: %d", &rssi) == 1) {
                *rssi_value = rssi;
                if (rssi == 99)
                    *desc = "Unknown";
                else if (rssi < 10)
                    *desc = "Poor";
                else if (rssi < 15)
                    *desc = "Fair";
                else if (rssi < 20)
                    *desc = "Moderate";
                else if (rssi < 26)
                    *desc = "Good";
                else
                    *desc = "Excellent";
                return;
            }
        }
        line = strtok(NULL, "\r\n");
    }
    *rssi_value = -1;
    *desc = "Invalid";
}

static void parse_creg_status(const char *resp, char *output)
{
    if (strstr(resp, "+CREG: 0,1"))
        strcpy(output, "Registered (Home)");
    else if (strstr(resp, "+CREG: 0,5"))
        strcpy(output, "Registered (Roaming)");
    else if (strstr(resp, "+CREG: 0,2"))
        strcpy(output, "Searching");
    else if (strstr(resp, "+CREG: 0,3"))
        strcpy(output, "Denied");
    else if (strstr(resp, "+CREG: 0,0"))
        strcpy(output, "Not Registered");
    else
        strcpy(output, "Unknown");
}

static void print_json(const sim700l_test_result_t *result)
{
    printf("{\n");
    printf("  \"test_results\": {\n");
    printf("    \"restart_module\": \"%s\",\n", result->restart_status);
    printf("    \"imei\": \"%s\",\n", result->imei);
    printf("    \"imsi\": \"%s\",\n", result->imsi);
    printf("    \"sim_number\": \"%s\",\n", result->sim_number);
    printf("    \"sim_pin_status\": \"%s\",\n", result->sim_pin_status);
    printf("    \"network_status\": \"%s\",\n", result->network_status);
    printf("    \"signal_quality\": {\n");
    printf("      \"rssi\": %d,\n", result->rssi);
    printf("      \"description\": \"%s\"\n", result->rssi_description);
    printf("    }\n");
    printf("  }\n");
    printf("}\n");
}

static void sim700l_test_task(void *arg)
{
    char resp[BUF_SIZE];
    sim700l_test_result_t result = {
        .restart_status = "Failed",
        .imei = "N/A",
        .imsi = "N/A",
        .sim_number = "N/A",
        .sim_pin_status = "Unknown",
        .network_status = "Unknown",
        .rssi = -1,
        .rssi_description = "N/A"
    };

    send_at_command("AT+CFUN=1,1", resp, sizeof(resp));
    if (strstr(resp, "OK")) strcpy(result.restart_status, "OK");

    vTaskDelay(pdMS_TO_TICKS(7500));

    send_at_command("AT", resp, sizeof(resp));

    send_at_command("AT+GSN", resp, sizeof(resp));
    extract_line_value(resp, result.imei, NULL);

    send_at_command("AT+CIMI", resp, sizeof(resp));
    extract_line_value(resp, result.imsi, NULL);

    send_at_command("AT+CPIN?", resp, sizeof(resp));
    extract_line_value(resp, result.sim_pin_status, NULL);// AT+CNUM

    send_at_command("AT+CNUM", resp, sizeof(resp));
    extract_line_value(resp, result.sim_number, NULL);

    // send_at_command("AT+CCID", resp, sizeof(resp));
    // extract_line_value(resp, result.sim_number, NULL); // Raw number allowed

    // Network registration (retry loop)
    for (int i = 0; i < 12; i++) {
        send_at_command("AT+CREG?", resp, sizeof(resp));
        parse_creg_status(resp, result.network_status);

        // Debug CGREG and COPS
        send_at_command("AT+CGREG?", resp, sizeof(resp));
        ESP_LOGI(TAG, "GPRS Status: %s", resp);

        send_at_command("AT+COPS?", resp, sizeof(resp));
        ESP_LOGI(TAG, "Operator: %s", resp);

        if (strstr(result.network_status, "Registered")) break;

        // Try forcing auto registration
        if (i == 8) {
            ESP_LOGW(TAG, "Trying to force auto-operator selection...");
            send_at_command("AT+COPS=0", resp, sizeof(resp));
        }

        vTaskDelay(pdMS_TO_TICKS(4000));
    }

    send_at_command("AT+CSQ", resp, sizeof(resp));
    parse_rssi(resp, &result.rssi, &result.rssi_description);

    print_json(&result);

    vTaskDelete(NULL);
}

void app_main()
{
     const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "Project name:     %s", app_desc->project_name);
    ESP_LOGI(TAG, "Firmware version: %s", app_desc->version);
    uart_init();
    xTaskCreate(sim700l_test_task, "sim700l_test_task", 4096, NULL, 5, NULL);
}
