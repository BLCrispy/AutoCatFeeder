#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwipopts.h"
#include "lwip/apps/httpd.h"

extern "C"
{
#include "extern/hx711-pico-c/include/common.h"
}

// CGI handler which is run when a request for /led.cgi is detected
const char *cgi_led_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    for (int i = 0; i < iNumParams; i++)
    {
        // Check if an request for LED has been made (/led.cgi?led=x)
        if (strcmp(pcParam[i], "led") == 0)
        {
            // Look at the argument to check if LED is to be turned on (x=1) or off (x=0)
            if (strcmp(pcValue[i], "0") == 0)
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            else if (strcmp(pcValue[i], "1") == 0)
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        }
    }
    return "/status.shtml"; // Send the status.shtml file back to the client after the CGI request has been processed
}

// tCGI Struct
// Fill this with all of the CGI requests and their respective handlers
static const tCGI cgi_handlers[] = {
    {// Html request for "/led.cgi" triggers cgi_handler
     "/led.cgi", cgi_led_handler},
};

void cgi_init(void)
{
    http_set_cgi_handlers(cgi_handlers, 1);
}

// Set SSI tags
const char *ssi_tags[] = {"led"};

u16_t ssi_handler(int iIndex, char *pcInsert, int iInsertLen)
{
    int printed; // size_t to hold the number of characters we inject (ensures 32-bit unsigned int)

    if (iIndex == 0)
    {
        // Read the state of the LED (on or off)
        bool is_on = cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN);

        // Write '1' or '0' into the pcInsert buffer, which injects it into the file
        printed = snprintf(pcInsert, iInsertLen, "%d", is_on ? 1 : 0);

        if (printed < 0)
        {
            // If there was an error during snprintf, return 0 to indicate that we didn't inject anything
            return 0;
        }

        // Return the number of characters that we injected
        return (u16_t)printed;
    }

    // Return 0 if we didn't inject anything
    return 0;
}

void ssi_init()
{
    // Configure SSI handler
    http_set_ssi_handler(ssi_handler, ssi_tags, LWIP_ARRAYSIZE(ssi_tags));
}

// HX711 Load Cell Amplifier Setup and global hx declaration
hx711_t hx;
void hx711_setup()
{
    // Initialize the HX711 load cell amplifier
    // I am pretty much pulling the entire HX711 initialization process from the example code in the hx711-pico-c repo,
    // So for further details about how this process works, see: https://github.com/endail/hx711-pico-c

    // 1. Set configuration
    hx711_config_t hxcfg;
    hx711_get_default_config(&hxcfg);

    hxcfg.clock_pin = 14; // GPIO pin connected to HX711 clock pin
    hxcfg.data_pin = 15;  // GPIO pin connected to HX711 data pin

    // I don't need this but I am keeping this here to remember this is running on pio0
    // by default, the underlying PIO program will run on pio0
    // if you need to change this, you can do:
    // hxcfg.pio = pio1;

    // 2. Initialise
    hx711_init(&hx, &hxcfg);

    // 3. Power up the hx711 and set gain on chip
    hx711_power_up(&hx, hx711_gain_128);

    // 4. This step is optional. Only do this if you want to
    // change the gain AND save it to the HX711 chip
    //
    // hx711_set_gain(&hx, hx711_gain_64);
    // hx711_power_down(&hx);
    // hx711_wait_power_down();
    // hx711_power_up(&hx, hx711_gain_64);

    // 5. Wait for readings to settle
    hx711_wait_settle(hx711_rate_10);

    // 6. Read values
    // You can now...
}

void hx711_read_EMA(bool first_reading, int32_t &smoothed_value)
{
    // Val to hold the raw reading from the sensor
    int32_t val;
    // Timeout method of val reading
    if (hx711_get_value_timeout(&hx, &val, 250000))
    {

        // Initialize the average on the very first read
        if (first_reading)
        {
            smoothed_value = val;
        }
        else
        {
            // The EMA Formula: 10% new reading, 90% old average
            // This acts as a shock absorber for the data
            smoothed_value = (val * 0.2f) + (smoothed_value * 0.8f);
        }

        printf("Raw: %li | Smoothed: %li\n", val, smoothed_value);

        // Keep a short delay to match the HX711's 10Hz sample rate
        // This prevents the Pico W from overwhelming the sensor
        sleep_ms(100);
    }
    else {
        printf("Failed to read value from HX711 within timeout.\n");
    }
}

#define TARGET_WEIGHT 500 // Target weight in grams
#define CALIBRATION_FACTOR 440 // Calibration factor to convert raw sensor values to grams
void dispenseFood(int32_t &smoothed_value) {
    // This function will eventually control a servo motor to dispense food
    // For now, it's just a placeholder to show where that code will go

    int32_t tare = smoothed_value; // Set the tare to the current weight before dispensing

    while ((smoothed_value - tare) < TARGET_WEIGHT * CALIBRATION_FACTOR) {
        hx711_read_EMA(false, smoothed_value); // Update the smoothed value
        printf("Dispensing... Current weight: %li g\n", (smoothed_value - tare) / CALIBRATION_FACTOR);
    }

    printf("Target weight reached. Stopping dispensing.\n");
    printf("Waiting for scale to settle...\n");
    // Actively read the sensor for 2 seconds instead of freezing.
    // This flushes out the HX711 hardware buffer and catches your finger lifting in real-time.
    int32_t flush_val;
    for (int i = 0; i < 20; i++) { 
        hx711_get_value_timeout(&hx, &flush_val, 250000);
        sleep_ms(100); // Matches the 10Hz rate
    }


    int32_t fresh_raw_baseline = 0;
    if (hx711_get_value_timeout(&hx, &fresh_raw_baseline, 250000)) {
        smoothed_value = fresh_raw_baseline; // Reset the smoothed value to the new baseline after dispensing
        printf("Post-dispense raw baseline: %li\n", fresh_raw_baseline);
    } 
}

int core1_main()
{

    return 0;
}

int main()
{
    stdio_init_all();

    // Initialise the Wi-Fi chip
    if (cyw43_arch_init())
    {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    // Enable wifi station
    cyw43_arch_enable_sta_mode();

    // CONNECT TO WIFI NETWORK
    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms("Pookies Mini Mansion", "YesBrother!69", CYW43_AUTH_WPA2_AES_PSK, 30000))
    {
        printf("failed to connect.\n");
        return 1;
    }
    else
    {
        printf("Connected.\n");
        // Read the ip address in a human readable way
        uint8_t *ip_address = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
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

    // Initialize the HX711 load cell amplifier and set it up to read values from the load cell
    hx711_setup();

    // Add this outside your while loop to store the running average
    int32_t smoothed_value = 0;
    bool first_reading = true;
    

    //Test vars
    int count = 0;

    // Loop that just prints hello world and blinks the on-board LED every second
    while (true)
    {
        if (first_reading) {
            printf("Performing first reading...\n");
            hx711_read_EMA(first_reading, smoothed_value);
            first_reading = false; // Set first_reading to false after the first read
        }
        else {
            hx711_read_EMA(first_reading, smoothed_value);
        }
        
        sleep_ms(1000);
        if (count == 5)
        {
            dispenseFood(smoothed_value);
            
            count = 0;
        }
        count++;
    }

    return 0;
}
