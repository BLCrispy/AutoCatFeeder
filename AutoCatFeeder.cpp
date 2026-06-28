#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwipopts.h"
#include "lwip/apps/httpd.h"
#include "lwip/tcp.h"
#include <string.h>
#include <stdlib.h>
#include "pico/multicore.h"

extern "C"
{
#include "extern/hx711-pico-c/include/common.h"
}

// ---------------------------------------------------------
// Global State
// ---------------------------------------------------------
// Sets global value for what portion size is
volatile bool feed_triggered = false;
volatile bool first_reading = true;
volatile int8_t portion_size = 40; // 40g default as that is what we feed our cats per meal usually
volatile int32_t smoothed_value = 0;


// ---------------------------------------------------------
// SSI Handler
// ---------------------------------------------------------

// SSI tags must match exactly what's in status.shtml
const char *ssi_tags[] = {"portion", "feed"};

u16_t ssi_handler(int iIndex, char *pcInsert, int iInsertLen)
{
    int printed = 0;
    if (iIndex == 0)
        printed = snprintf(pcInsert, iInsertLen, "%d", (int)portion_size);
    else if (iIndex == 1)
        printed = snprintf(pcInsert, iInsertLen, "%d", (int)feed_triggered);
    if (printed < 0) return 0;
    return (u16_t)printed;
}

void ssi_init()
{
    http_set_ssi_handler(ssi_handler, ssi_tags, LWIP_ARRAYSIZE(ssi_tags));
}

// ---------------------------------------------------------
// CGI Handlers
// ---------------------------------------------------------

// CGI handler for manually dispensing food
// Expects URL format: /snack.cgi?snack=x where x is 0 or 1
const char *cgi_feed_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    for (int i = 0; i < iNumParams; i++)
    {
        if (strcmp(pcParam[i], "on") == 0)
        {
            if (strcmp(pcValue[i], "1") == 0 && !feed_triggered)
            {
                feed_triggered = true;
                printf("Feed triggered!\n");
            }
            // on=0 is acknowledged but ignored — dispensing completes naturally
            // feed_triggered resets itself at end of dispenseFood()
        }
    }
    return "/index.html";
}

// CGI handler for Portion Size Requests
// Expects URL format: /schedule.cgi?id=0&time=14
const char *cgi_portion_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    for (int i = 0; i < iNumParams; i++)
    {
        if (strcmp(pcParam[i], "grams") == 0)
        {
            portion_size = (int8_t)atoi(pcValue[i]);
            printf("Portion update: %d g\n", portion_size);
        }
    }

    return "/index.html";
}

// CGI handler table
static const tCGI cgi_handlers[] = {
    {"/feed.cgi", cgi_feed_handler},
    {"/portion.cgi", cgi_portion_handler}};

// CGI Initialization
void cgi_init(void)
{
    http_set_cgi_handlers(cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
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

void hx711_read_EMA()
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
            smoothed_value = (int32_t)((val * 0.2f) + ((float)smoothed_value * 0.8f));
        }

        printf("Raw: %li | Smoothed: %li\n", val, smoothed_value);

        // Keep a short delay to match the HX711's 10Hz sample rate
        // This prevents the Pico W from overwhelming the sensor
        sleep_ms(100);
    }
    else
    {
        printf("Failed to read value from HX711 within timeout.\n");
    }
}

// Runs food dispensing logic
#define CALIBRATION_FACTOR 440 // Calibration factor to convert raw sensor values to grams
#define MOTOR_PIN 2 // GPIO pin connected to SONGLE Relay Pin to send triggers to motor
void dispenseFood()
{
    int32_t tare = smoothed_value; // Set the tare to the current weight before dispensing

    // Dispenses food until the target weight of food has been met
    // FIX-ME: Needs to have motor control logic implemented
    while ((smoothed_value - tare) < (int32_t)portion_size * CALIBRATION_FACTOR)
    {
        hx711_read_EMA(); // Update the smoothed value
        printf("Dispensing... Current weight: %li g\n", (smoothed_value - tare) / CALIBRATION_FACTOR);
        gpio_put(MOTOR_PIN, 0); // Turn motor on to start dispensing food
    }

    printf("Target weight reached. Stopping dispensing.\n");
    gpio_put(MOTOR_PIN, 1); // Turn motor off after portion_size met

    // Actively read the sensor for 2 seconds instead of freezing.
    // This flushes out the HX711 hardware buffer and catches your finger lifting in real-time.
    printf("Waiting for scale to settle...\n");
    int32_t flush_val;
    for (int i = 0; i < 20; i++)
    {
        hx711_get_value_timeout(&hx, &flush_val, 250000);
        sleep_ms(100); // Matches the 10Hz rate
    }

    int32_t fresh_raw_baseline = 0;
    if (hx711_get_value_timeout(&hx, &fresh_raw_baseline, 250000))
    {
        smoothed_value = fresh_raw_baseline; // Reset the smoothed value to the new baseline after dispensing
        printf("Post-dispense raw baseline: %li\n", fresh_raw_baseline);
    }
}

void core1_main()
{
    

    // Initialize the HX711 load cell amplifier and set it up to read values from the load cell
    hx711_setup();

    

    // Runs first reading and sets it to false to start taking EMA readings.
    hx711_read_EMA();
    first_reading = false;

    while (true)
    {
        hx711_read_EMA();
        // Core 1 watches for trigger set by cgi_feed_handler in core0 to run dispense food action then reset trigger
        if (feed_triggered)
        {
            dispenseFood();
            feed_triggered = false;
        }
        sleep_ms(100); // Sleeps sets core1 to check for triggers every 100ms
    }
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

    // Initialize CGI handlers
    cgi_init();
    printf("CGI Handler initialized.\n");

    // Initialize SSI Handler
    ssi_init();
    printf("SSI Handler initialized.\n");

    // Create an array to hold the 6 bytes of the MAC address
    uint8_t mac[6];

    // Pull the MAC address from the Wi-Fi chip in Station Mode (STA)
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac);

    // Print it to your serial console
    printf("Pico W MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Initialize and Configure gpio MOTOR_PIN
    gpio_init(MOTOR_PIN);
    gpio_put(MOTOR_PIN, 1);
    gpio_set_dir(MOTOR_PIN, GPIO_OUT);

    multicore_launch_core1(core1_main); // Launch core 1 to run the HX711 reading loop


    // Loop that just prints hello world and blinks the on-board LED every second
    while (true)
    {
        cyw43_arch_poll();                                       // Service the Wi-Fi driver
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(1)); // Yield briefly
    }

    return 0;
}
