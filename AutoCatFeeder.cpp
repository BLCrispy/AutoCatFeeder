#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwipopts.h"
#include "lwip/apps/httpd.h"


// CGI handler which is run when a request for /led.cgi is detected
const char * cgi_led_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    for (int i = 0; i < iNumParams; i++) {
        // Check if an request for LED has been made (/led.cgi?led=x)
        if (strcmp(pcParam[i] , "led") == 0){
            // Look at the argument to check if LED is to be turned on (x=1) or off (x=0)
            if(strcmp(pcValue[i], "0") == 0)
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            else if(strcmp(pcValue[i], "1") == 0)
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        }
    }
    return "/status.shtml"; // Send the status.shtml file back to the client after the CGI request has been processed
}

// tCGI Struct
// Fill this with all of the CGI requests and their respective handlers
static const tCGI cgi_handlers[] = {
    {
        // Html request for "/led.cgi" triggers cgi_handler
        "/led.cgi", cgi_led_handler
    },
};

void cgi_init(void)
{
    http_set_cgi_handlers(cgi_handlers, 1);
}


// Set SSI tags
const char * ssi_tags[] = {"led"};

u16_t ssi_handler(int iIndex, char *pcInsert, int iInsertLen){
    int printed; // size_t to hold the number of characters we inject (ensures 32-bit unsigned int)

    if (iIndex == 0) {
        // Read the state of the LED (on or off)
        bool is_on = cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN);

        // Write '1' or '0' into the pcInsert buffer, which injects it into the file
        printed = snprintf(pcInsert, iInsertLen, "%d", is_on ? 1 : 0);

        if (printed < 0) {
            // If there was an error during snprintf, return 0 to indicate that we didn't inject anything
            return 0;
        }

        // Return the number of characters that we injected
        return (u16_t)printed;
    }

    // Return 0 if we didn't inject anything
    return 0;
}

void ssi_init(){
    // Configure SSI handler
    http_set_ssi_handler(ssi_handler, ssi_tags, LWIP_ARRAYSIZE(ssi_tags));

}


int main()
{
    stdio_init_all();

    // Initialise the Wi-Fi chip
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    // Enable wifi station
    cyw43_arch_enable_sta_mode();


    // CONNECT TO WIFI NETWORK
    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms("Pookies Mini Mansion", "YesBrother!69", CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return 1;
    } else {
        printf("Connected.\n");
        // Read the ip address in a human readable way
        uint8_t *ip_address = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
        printf("IP address %d.%d.%d.%d\n", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
    }

    // Initialize http webserver
    httpd_init();
    printf("Http server initialized.\n");

    // Initialize SSI & CGI
    ssi_init();
    printf("SSI Handler initialized.\n");

    cgi_init();
    printf("CGI Handler initialized.\n");

    // Loop that just prints hello world and blinks the on-board LED every second
    while (true) {  
    }


}
