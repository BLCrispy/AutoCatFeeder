#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwipopts.h"
#include "lwip/apps/httpd.h"
#include "lwip/tcp.h"
#include <string.h>
#include <stdlib.h>
#include "pico/multicore.h"
#include "displaylib/ssd1315.hpp"
#include "nowifi_icon16x16.h"
#include "wifi_icon16x16.h"
#include "hardware/rtc.h"
#include "pico/util/datetime.h"
#include "lwip/apps/sntp.h"
#include <time.h>

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

// =============== Function prototypes ================
// SSI Handlers
u16_t ssi_handler(int iIndex, char *pcInsert, int iInsertLen);
void ssi_init();

// CGI Handlers
const char *cgi_feed_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]);
const char *cgi_portion_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]);
void cgi_init(void);

// HX711 Functions
void hx711_setup();
void hx711_read_EMA();

// Runs food dispensing logic
void dispenseFood();

// OLED Display Functions
void SetupDisplay(void);
void updateDisplay(bool wifi_status, int32_t weight);

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
    if (printed < 0)
        return 0;
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

// ---------------------------------------------------------
// HX711 Load Cell Amplifier Setup and Reading Functions
// ---------------------------------------------------------

#define OUTLIER_THRESHOLD_HIGH_DISPENSING         44000   // 100g equivalent
#define OUTLIER_THRESHOLD_HIGH_IDLE               22000   // 50g equivalent
#define OUTLIER_THRESHOLD_LOW                    -22000   // -50g equivalent  

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
    int32_t val;
    if (hx711_get_value_timeout(&hx, &val, 250000))
    {
        if (first_reading) // If this is the first reading, set the smoothed value to the raw value and return
        {
            smoothed_value = val;
            first_reading = false;
            sleep_ms(100);
            return;
        }

        int32_t deviation = val - (int32_t)smoothed_value; // signed deviation

        int32_t threshold_high = feed_triggered ? OUTLIER_THRESHOLD_HIGH_DISPENSING : OUTLIER_THRESHOLD_HIGH_IDLE;

        if (deviation > threshold_high || deviation < OUTLIER_THRESHOLD_LOW) // Reject outliers based on thresholds
        {
            printf("Outlier rejected: %li (deviation: %li)\n", val, deviation);
            sleep_ms(100);
            return;
        }

        smoothed_value = (int32_t)((val * 0.1f) + ((float)smoothed_value * 0.9f)); // EMA smoothing with alpha = 0.1
        printf("Raw: %li | Smoothed: %li\n", val, smoothed_value);
        sleep_ms(100);
    }
    else // If the reading times out, print an error message
    {
        printf("Failed to read value from HX711 within timeout.\n");
    }
}

// ---------------------------------------------------------
// Food Dispensing Logic
// ---------------------------------------------------------

// Runs food dispensing logic
#define CALIBRATION_FACTOR 440 // Calibration factor to convert raw sensor values to grams
#define MOTOR_PIN 2            // GPIO pin connected to SONGLE Relay Pin to send triggers to motor
void dispenseFood()
{
    // Minimum dispense time in milliseconds based on portion size (40g takes 8 seconds)
    // This scales dispense time based on portion size, and was calibrated from 40g feedings taking 8 seconds to dispense
    uint32_t min_dispense_ms = (uint32_t)((portion_size / 40.0f) * 8000);
    absolute_time_t dispense_start = get_absolute_time(); // Start dispense timer
    bool min_time_met = false;                            // Flag to track if minimum dispense time has been met

    int32_t tare = smoothed_value; // Set the tare to the current weight before dispensing

    gpio_put(MOTOR_PIN, 0); // Turn motor on to start dispensing food      


    // Dispenses food until the target weight of food has been met
    // FIX-ME: Needs to have motor control logic implemented
    while (((smoothed_value - tare) < (int32_t)portion_size * CALIBRATION_FACTOR) && !min_time_met)
    {
        hx711_read_EMA(); // Update the smoothed value
        printf("Dispensing... Current weight: %li g\n", (smoothed_value - tare) / CALIBRATION_FACTOR);

                                             
        updateDisplay(true, (smoothed_value - tare) / CALIBRATION_FACTOR); // Update the OLED display with the latest information

        // Records the time elapsed since dispensing started and checks if the minimum dispense time has been met
        min_time_met = absolute_time_diff_us(dispense_start, get_absolute_time()) >= ((int64_t)min_dispense_ms * 1000);
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

// ---------------------------------------------------------
// SSD1315 OLED Display
// ---------------------------------------------------------

// Screen settings
#define myOLEDwidth 128
#define myOLEDheight 64
#define myScreenSize (myOLEDwidth * (myOLEDheight / 8)) // eg 1024 bytes = 128 * 64/8
uint8_t screenBuffer[myScreenSize];

// instantiate  an OLED object
SSD1315 myOLED(myOLEDwidth, myOLEDheight);

// I2C settings
const uint16_t SPEED = 100;
const uint8_t CLK_PIN = 1;
const uint8_t DATA_PIN = 0;

// ===================== Function Space =====================
void setupDisplay()
{
    busy_wait_ms(500);
    printf("Start!\r\n");
    while (myOLED.OLEDbegin(SSD1315::SSD1315_ADDR, i2c0, SPEED, DATA_PIN, CLK_PIN) != DisplayRet::Success)
    {
        printf("SetupTest ERROR : Failed to initialize OLED!\r\n");
        busy_wait_ms(1500);
    } // initialize the OLED
    if (myOLED.OLEDSetBufferPtr(myOLEDwidth, myOLEDheight, screenBuffer) != DisplayRet::Success)
    {
        printf("SetupTest : ERROR : OLEDSetBufferPtr Failed!\r\n");
        while (1)
        {
            busy_wait_ms(1000);
        }
    } // Initialize the buffer
    myOLED.OLEDFillScreen(0xF0, 0); // splash screen bars
    busy_wait_ms(1000);
}

void updateDisplay(bool wifi_status, int32_t weight)
{
    // Clears the screen buffer and sets the font to default
    myOLED.OLEDclearBuffer();
    myOLED.setFont(pFontDefault);

    // Display the current date and time on the OLED
    datetime_t now;
    rtc_get_datetime(&now);
    myOLED.setCursor(0, 0);
    myOLED.print(now.year);
    myOLED.print("-");
    myOLED.print(now.month);
    myOLED.print("-");
    myOLED.print(now.day);
    myOLED.print(" ");
    myOLED.print(now.hour);
    myOLED.print(":");
    myOLED.print(now.min);
    myOLED.print(":");
    myOLED.print(now.sec);

    // Display the Wi-Fi status icon on the OLED
    if (wifi_status)
    {
        myOLED.OLEDBitmap(112, 0, 16, 16, wifi_icon16x16, false);
    }
    else
    {
        myOLED.OLEDBitmap(112, 0, 16, 16, nowifi_icon16x16, false);
    }

    // Display the portion size and dispensing status on the OLED
    myOLED.setCursor(0, 16);
    myOLED.print("Portion: ");
    myOLED.print(portion_size);
    myOLED.print(" g");
    myOLED.setCursor(0, 32);
    if (feed_triggered)
    {
        myOLED.print("Dispensing...");
        myOLED.setCursor(0, 48);
        myOLED.print("Weight: ");
        myOLED.print(weight);
    }
    else
    {
        myOLED.print("Idle");
    }

    // Update the OLED display with the latest information
    myOLED.OLEDupdate();
}

// ---------------------------------------------------------
// RTC and SNTP
// ---------------------------------------------------------

// Forward declarations
extern "C" void set_rtc_from_ntp(uint32_t seconds);
bool is_central_daylight_time(struct tm *utc);

// Returns true if the given UTC time is currently in CDT (UTC-5)
// Returns false if in CST (UTC-6)
bool is_central_daylight_time(struct tm *utc)
{
    int month = utc->tm_mon + 1; // tm_mon is 0-11, convert to 1-12

    // Months fully outside DST
    if (month < 3 || month > 11)
        return false; // Nov-Feb = CST
    if (month > 3 && month < 11)
        return true; // Apr-Oct = CDT

    // March — DST starts second Sunday at 2:00 AM UTC (8:00 AM CST)
    if (month == 3)
    {
        // Find day of month of first Sunday in March
        // tm_wday is 0=Sunday, tm_mday is current day
        // First Sunday = current day minus current weekday
        int first_sunday = utc->tm_mday - utc->tm_wday;
        if (first_sunday <= 0)
            first_sunday += 7;

        // Second Sunday = first Sunday + 7
        int second_sunday = first_sunday + 7;

        // Before second Sunday = CST
        if (utc->tm_mday < second_sunday)
            return false;

        // On second Sunday, DST starts at 2:00 AM local (8:00 AM UTC)
        if (utc->tm_mday == second_sunday)
            return utc->tm_hour >= 8;

        // After second Sunday = CDT
        return true;
    }

    // November — DST ends first Sunday at 2:00 AM local (7:00 AM UTC)
    if (month == 11)
    {
        // Find first Sunday in November
        int first_sunday = utc->tm_mday - utc->tm_wday;
        if (first_sunday <= 0)
            first_sunday += 7;

        // Before first Sunday = CDT
        if (utc->tm_mday < first_sunday)
            return true;

        // On first Sunday, DST ends at 2:00 AM local (7:00 AM UTC)
        if (utc->tm_mday == first_sunday)
            return utc->tm_hour < 7;

        // After first Sunday = CST
        return false;
    }

    return false;
}

// Called by lwIP SNTP client when time is received from NTP server
extern "C" void set_rtc_from_ntp(uint32_t seconds)
{
    time_t unix_time = (time_t)seconds;
    struct tm *utc = gmtime(&unix_time);

    // Determine offset based on DST
    int offset = is_central_daylight_time(utc) ? -5 : -6;

    // Apply offset to get local hour
    // The +24 and %24 handle midnight wraparound correctly
    int local_hour = (utc->tm_hour + offset + 24) % 24;

    // Handle day rollover if offset pushes us into previous day
    int local_day = utc->tm_mday;
    if (utc->tm_hour + offset < 0)
        local_day -= 1;

    datetime_t t = {
        .year = (int16_t)(utc->tm_year + 1900),
        .month = (int8_t)(utc->tm_mon + 1),
        .day = (int8_t)local_day,
        .dotw = (int8_t)(utc->tm_wday),
        .hour = (int8_t)local_hour,
        .min = (int8_t)(utc->tm_min),
        .sec = (int8_t)(utc->tm_sec)};

    rtc_set_datetime(&t);

    printf("RTC synced: %04d-%02d-%02d %02d:%02d:%02d %s\n",
           t.year, t.month, t.day, t.hour, t.min, t.sec,
           is_central_daylight_time(utc) ? "CDT" : "CST");
}

// NTP initialization function
void ntp_init()
{
    rtc_init();
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    printf("NTP client started.\n");
}

// ---------------------------------------------------------
// Core 1 Main
// ---------------------------------------------------------
void core1_main()
{

    // Initialize the HX711 load cell amplifier and set it up to read values from the load cell
    hx711_setup();

    // Runs first reading and sets it to false to start taking EMA readings.
    hx711_read_EMA();

    // OLED Display Setup
    setupDisplay();

    bool connected_to_wifi = false; // Initialize wifi status variable
    while (true)
    {
        hx711_read_EMA();
        // Core 1 watches for trigger set by cgi_feed_handler in core0 to run dispense food action then reset trigger
        if (feed_triggered)
        {
            dispenseFood();
            feed_triggered = false;
        }

        // Reads from FIFO to know if still connected to Wifi
        // This block is to allow core1 to see connection status from core0
        if (multicore_fifo_rvalid())
        {
            uint8_t wifi_status = multicore_fifo_pop_blocking();
            if (wifi_status == 0) // If wifi is disconnectred
            {
                connected_to_wifi = false;
            }
            else
            { // If wifi is connected
                connected_to_wifi = true;
            }
        }

        updateDisplay(connected_to_wifi, 0); // Update the OLED display with the latest information

        sleep_ms(100); // Sleeps sets core1 to check for triggers every 100ms
    }
}

// ---------------------------------------------------------
// Core 0 Main
// ---------------------------------------------------------
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

    // After Wi-Fi connects and httpd_init:
    ntp_init();
    printf("NTP initialized.\n");

    datetime_t now;
    rtc_get_datetime(&now);
    printf("Current core 0 RTC time: %04d-%02d-%02d %02d:%02d:%02d\n", now.year, now.month, now.day, now.hour, now.min, now.sec);

    // Initialize and Configure gpio MOTOR_PIN
    gpio_init(MOTOR_PIN);
    gpio_put(MOTOR_PIN, 1);
    gpio_set_dir(MOTOR_PIN, GPIO_OUT);

    multicore_launch_core1(core1_main); // Launch core 1 to run the HX711 reading loop

    // Loop that just prints hello world and blinks the on-board LED every second
    while (true)
    {
        // Checks Wifi Connection status and pushes a status flag to FIFO
        // This block is to allow core1 to see connection status from core0
        if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP)
        {
            multicore_fifo_push_blocking(1); // 1 = Connected
        }
        else
        {
            multicore_fifo_push_blocking(0); // 0 = Disconnected
        }

        cyw43_arch_poll();                                       // Service the Wi-Fi driver
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(1)); // Yield briefly
    }

    return 0;
}
