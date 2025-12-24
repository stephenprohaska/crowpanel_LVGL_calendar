#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include "time.h"
#include <lvgl.h>
#include "LGFX_Setup.h"
#include <vector>
#include <algorithm>

// ==========================================
// CONFIGURATION
// ==========================================

const long  gmtOffset_sec = -6 * 3600; 
const int   daylightOffset_sec = 3600;

char ical_url[512] = ""; 
String setupPassword;   

static LGFX tft;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1;
static lv_color_t *buf2;

lv_obj_t * event_list_cont; // Replaces text area
lv_obj_t * status_label;

// Clock Objects
lv_obj_t * main_time_label;
lv_obj_t * main_date_label;

Preferences preferences;

// Structure to hold event data for sorting
struct CalendarEvent {
    String dtStart;
    String dtEnd;
    String summary;
    String location;

    // Operator to sort by date string (ISO8601 strings sort lexicographically)
    bool operator<(const CalendarEvent& other) const {
        return dtStart < other.dtStart;
    }
};

// ==========================================
// UTILITIES
// ==========================================

String generatePassword(int len) {
    const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    String pwd = "";
    for (int i = 0; i < len; i++) {
        pwd += chars[esp_random() % (sizeof(chars) - 1)];
    }
    return pwd;
}

// Helper to format "20231025T143000" to "Oct 25, 2:30pm" (12-hour format)
String formatTime12(String timeStr) {
    if (timeStr.length() < 4) return "";
    int hour = timeStr.substring(0, 2).toInt();
    String min = timeStr.substring(2, 4);
    String period = "am";
    
    if (hour >= 12) {
        period = "pm";
        if (hour > 12) hour -= 12;
    }
    if (hour == 0) hour = 12;
    
    return String(hour) + ":" + min + period;
}

// Helper to format full date string with start and optional end time
String formatEventDateTime(String dtStart, String dtEnd) {
    // DTSTART:20231025T143000Z
    if (dtStart.length() < 8) return dtStart;

    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int month = dtStart.substring(4, 6).toInt() - 1;
    String day = dtStart.substring(6, 8);
    
    String formatted = String(months[month]) + " " + day;

    // Check for Time (T indicates time follows)
    int tIndexStart = dtStart.indexOf('T');
    if (tIndexStart != -1 && dtStart.length() >= tIndexStart + 5) {
        String startTime = formatTime12(dtStart.substring(tIndexStart + 1));
        formatted += ", " + startTime;
        
        // Add End Time if present
        int tIndexEnd = dtEnd.indexOf('T');
        if (tIndexEnd != -1 && dtEnd.length() >= tIndexEnd + 5) {
            // Only show end time if it's on the same day (simple check)
            if (dtEnd.substring(0, 8) == dtStart.substring(0, 8)) {
                 String endTime = formatTime12(dtEnd.substring(tIndexEnd + 1));
                 formatted += " - " + endTime;
            }
        }
    }
    return formatted;
}

// ==========================================
// LGFX & LVGL DRIVERS
// ==========================================

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    if (tft.getStartCount() == 0) tft.startWrite();
    tft.pushImageDMA(area->x1, area->y1, w, h, (uint16_t *)&color_p->full);
    tft.waitDMA(); 
    if (tft.getStartCount() == 0) tft.endWrite();
    lv_disp_flush_ready(disp);
}

void my_touch_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    uint16_t touchX, touchY;
    bool touched = tft.getTouch(&touchX, &touchY);
    if (touched) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touchX;
        data->point.y = touchY;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ==========================================
// UI FUNCTIONS
// ==========================================

static void reset_wifi_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_LONG_PRESSED) {
        // Visual feedback
        lv_obj_t * label = lv_obj_get_child((lv_obj_t*)e->user_data, 0); // Get label from button
        if(label) lv_label_set_text(label, "Resetting...");
        
        for(int i=0; i<10; i++) { lv_timer_handler(); delay(10); }
        delay(1000);
        
        WiFiManager wm;
        wm.resetSettings(); 
        ESP.restart();      
    }
}

void show_setup_ui(String ssid_name, String pwd) {
    tft.fillScreen(TFT_BLACK); 
    lv_obj_clean(lv_scr_act()); 
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x202020), 0);
    lv_obj_invalidate(lv_scr_act());

    lv_obj_t * title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "Setup Mode");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFD700), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t * cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, 380, 300);
    lv_obj_align(cont, LV_ALIGN_LEFT_MID, 20, 20);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x303030), 0);

    lv_obj_t * instr = lv_label_create(cont);
    lv_label_set_long_mode(instr, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(instr, 340);
    String info = "Connect to WiFi:\nSSID: " + ssid_name + "\nPass: " + pwd + "\n\nConfigure in browser.";
    lv_label_set_text(instr, info.c_str());
    lv_obj_set_style_text_color(instr, lv_color_white(), 0);

    String qr_data = "WIFI:T:WPA;S:" + ssid_name + ";P:" + pwd + ";;";
    lv_obj_t * qr = lv_qrcode_create(lv_scr_act(), 220, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
    if (qr) {
        lv_qrcode_update(qr, qr_data.c_str(), qr_data.length());
        lv_obj_align(qr, LV_ALIGN_RIGHT_MID, -40, 20);
    }
}

// Helper to create a Material Style Event Card
void add_event_card(lv_obj_t * parent, String dateStr, String summary, String location) {
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100)); // Full width of parent
    lv_obj_set_height(card, LV_SIZE_CONTENT); // Auto height (CORRECTED SYNTAX)
    lv_obj_set_style_bg_color(card, lv_color_hex(0x303030), 0); // Dark card background
    lv_obj_set_style_radius(card, 10, 0); // Rounded corners
    lv_obj_set_style_border_width(card, 0, 0);
    // Add subtle shadow (optional, might affect perf)
    // lv_obj_set_style_shadow_width(card, 20, 0);
    // lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    // lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_gap(card, 5, 0);

    // Date Label (Accent Color)
    lv_obj_t * lbl_date = lv_label_create(card);
    lv_label_set_text(lbl_date, dateStr.c_str());
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(0x4CAF50), 0); // Green accent
    lv_obj_set_style_text_font(lbl_date, LV_FONT_DEFAULT, 0); 

    // Summary Label (White, Larger)
    lv_obj_t * lbl_summary = lv_label_create(card);
    lv_label_set_text(lbl_summary, summary.c_str());
    lv_label_set_long_mode(lbl_summary, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_summary, LV_PCT(100));
    lv_obj_set_style_text_color(lbl_summary, lv_color_white(), 0);
    // If you have a larger font enabled, use it here, e.g. &lv_font_montserrat_20
    lv_obj_set_style_text_font(lbl_summary, LV_FONT_DEFAULT, 0); 

    // Location Label (Gray, if exists)
    if (location.length() > 0) {
        lv_obj_t * lbl_loc = lv_label_create(card);
        lv_label_set_text(lbl_loc, location.c_str());
        lv_label_set_long_mode(lbl_loc, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl_loc, LV_PCT(100));
        lv_obj_set_style_text_color(lbl_loc, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(lbl_loc, LV_FONT_DEFAULT, 0); 
    }
}

// Function to update the clock labels
void update_clock() {
    if (!main_time_label || !main_date_label) return;
    
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)){
        // 1. Format Time: 12:30 pm
        char timeString[12];
        int hour = timeinfo.tm_hour;
        const char* ampm = (hour >= 12) ? "pm" : "am";
        if (hour > 12) hour -= 12;
        if (hour == 0) hour = 12;
        sprintf(timeString, "%d:%02d %s", hour, timeinfo.tm_min, ampm);
        lv_label_set_text(main_time_label, timeString);

        // 2. Format Date: Monday, Oct 25
        char dateString[30];
        // Full weekday name, Abbreviated Month name, Day of month
        strftime(dateString, sizeof(dateString), "%A, %b %d", &timeinfo);
        lv_label_set_text(main_date_label, dateString);
    }
}

void build_main_ui() {
    lv_obj_clean(lv_scr_act()); 
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x101010), 0);

    // Layout Specs
    int widget_width = 370;
    int widget_height = 360; 
    int x_margin = 20;
    int y_start = 80;

    // Header
    lv_obj_t * header_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(header_cont, 800, 60);
    lv_obj_align(header_cont, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_opa(header_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header_cont, 0, 0);
    
    // Transparent Reset Button covering title
    lv_obj_t * btn_reset = lv_btn_create(header_cont);
    lv_obj_set_size(btn_reset, 400, 50);
    lv_obj_align(btn_reset, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(btn_reset, LV_OPA_TRANSP, 0); 
    lv_obj_set_style_shadow_width(btn_reset, 0, 0);
    lv_obj_add_event_cb(btn_reset, reset_wifi_event_handler, LV_EVENT_ALL, btn_reset); // Pass button as user_data

    lv_obj_t * label = lv_label_create(btn_reset);
    lv_label_set_text(label, "Upcoming Events");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0); // Use larger font if avail
    lv_obj_center(label);

    status_label = lv_label_create(header_cont);
    lv_label_set_text(status_label, "(Long press to reset WiFi)");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888), 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 18);

    // ------------------------------------------------
    // LEFT SIDE: CLOCK CONTAINER
    // ------------------------------------------------
    lv_obj_t * clock_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(clock_cont, widget_width, widget_height);
    lv_obj_align(clock_cont, LV_ALIGN_TOP_LEFT, x_margin, y_start);
    lv_obj_set_style_bg_color(clock_cont, lv_color_hex(0x202020), 0); // Dark card background
    lv_obj_set_style_border_color(clock_cont, lv_color_hex(0x404040), 0);
    
    // Time Label (Large, Centered)
    main_time_label = lv_label_create(clock_cont);
    lv_label_set_text(main_time_label, "--:--");
    lv_obj_set_style_text_color(main_time_label, lv_color_hex(0x00BFFF), 0); // Deep Sky Blue
    // Try to scale up the font if possible, or use largest available
    // Since we don't have a specific huge font enabled, we'll use 20 and maybe scale
    lv_obj_set_style_text_font(main_time_label, &lv_font_montserrat_20, 0); 
    // You can use transform zoom to make it bigger if supported, but let's stick to simple align for now
    lv_obj_align(main_time_label, LV_ALIGN_CENTER, 0, -20);
    
    // Date Label (Smaller, Below Time)
    main_date_label = lv_label_create(clock_cont);
    lv_label_set_text(main_date_label, "Loading Date...");
    lv_obj_set_style_text_color(main_date_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(main_date_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(main_date_label, LV_ALIGN_CENTER, 0, 20);

    // ------------------------------------------------
    // RIGHT SIDE: EVENT LIST Container (Flex Layout for Cards)
    // ------------------------------------------------
    event_list_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(event_list_cont, widget_width, widget_height);
    lv_obj_align(event_list_cont, LV_ALIGN_TOP_RIGHT, -x_margin, y_start);
    lv_obj_set_flex_flow(event_list_cont, LV_FLEX_FLOW_COLUMN); // Stack children
    lv_obj_set_style_pad_all(event_list_cont, 10, 0);
    lv_obj_set_style_pad_gap(event_list_cont, 10, 0); // Gap between cards
    lv_obj_set_style_bg_color(event_list_cont, lv_color_hex(0x202020), 0);
    lv_obj_set_style_border_color(event_list_cont, lv_color_hex(0x404040), 0);
    
    // Add a placeholder label initially
    lv_obj_t * ph = lv_label_create(event_list_cont);
    lv_label_set_text(ph, "Initializing...");
    lv_obj_set_style_text_color(ph, lv_color_hex(0x888888), 0);
}

// ==========================================
// CALENDAR FETCHING
// ==========================================

void fetchCalendar() {
  Serial.println("--- Fetching Calendar ---");
  
  // 1. UI Prep
  if(event_list_cont) {
      lv_obj_clean(event_list_cont);
      // Add "Updating..." indicator
      lv_obj_t * loading = lv_label_create(event_list_cont);
      lv_label_set_text(loading, "Updating events...");
      lv_obj_set_style_text_color(loading, lv_color_hex(0xAAAAAA), 0);
      lv_timer_handler(); 
  }

  if (strlen(ical_url) < 10) return;

  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Time Error");
    return;
  }

  // 2. Set Time Window
  time_t referenceTime;
  time(&referenceTime);

  // CHANGED: Start window is NOW (referenceTime) instead of 7 days ago.
  // This ensures we only see Today's events and future events.
  time_t startTime = referenceTime; 
  
  struct tm startInfo;
  localtime_r(&startTime, &startInfo);
  char startString[16];
  sprintf(startString, "%04d%02d%02d", startInfo.tm_year + 1900, startInfo.tm_mon + 1, startInfo.tm_mday);

  time_t endTime = referenceTime + (90 * 24 * 60 * 60); 
  struct tm endInfo;
  localtime_r(&endTime, &endInfo);
  char endString[16];
  sprintf(endString, "%04d%02d%02d", endInfo.tm_year + 1900, endInfo.tm_mon + 1, endInfo.tm_mday);

  WiFiClientSecure *client = new WiFiClientSecure;
  if(client) client->setInsecure();
  
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  // Create a buffer to store events for sorting
  std::vector<CalendarEvent> upcomingEvents;

  if (http.begin(*client, ical_url)) { 
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      WiFiClient *stream = http.getStreamPtr();
      
      String currentSummary = "";
      String currentDate = "";
      String currentEndDate = ""; // New: Store End Date
      String currentLocation = "";
      bool insideEvent = false;
      
      // Clear the "Updating..." label now that we have data
      lv_obj_clean(event_list_cont);

      while (http.connected() && (stream->available() > 0 || stream->available() == -1)) {
        String line = stream->readStringUntil('\n');
        line.trim();

        if (line.startsWith("BEGIN:VEVENT")) {
          insideEvent = true;
          currentSummary = "";
          currentDate = "";
          currentEndDate = "";
          currentLocation = "";
        } 
        else if (line.startsWith("END:VEVENT")) {
          if (currentDate.length() >= 8) {
             String eventDateOnly = currentDate.substring(0, 8);
             
             // Filter: Only add if within valid range (Start Date -> End Date)
             if (eventDateOnly >= String(startString) && eventDateOnly <= String(endString)) {
                
                // Store in vector instead of displaying immediately
                CalendarEvent newEvent;
                newEvent.dtStart = currentDate;
                newEvent.dtEnd = currentEndDate;
                newEvent.summary = currentSummary;
                newEvent.location = currentLocation;
                upcomingEvents.push_back(newEvent);
             }
          }
          insideEvent = false;
        }
        else if (insideEvent) {
          if (line.startsWith("SUMMARY:")) {
            currentSummary = line.substring(8);
          }
          else if (line.startsWith("LOCATION:")) {
            currentLocation = line.substring(9);
            // Handle escaped commas/backslashes if needed, basic trim here
            currentLocation.replace("\\,", ",");
          }
          else if (line.startsWith("DTSTART")) {
            int colonIndex = line.lastIndexOf(':');
            if (colonIndex != -1) {
              currentDate = line.substring(colonIndex + 1);
            }
          }
          else if (line.startsWith("DTEND")) { // Parse End Time
            int colonIndex = line.lastIndexOf(':');
            if (colonIndex != -1) {
              currentEndDate = line.substring(colonIndex + 1);
            }
          }
        }
        
        if(stream->available() == 0) { delay(10); if(stream->available() == 0) break; }
      }
      
      // 5. Sort and Display
      // Sort the collected events (soonest first)
      std::sort(upcomingEvents.begin(), upcomingEvents.end());

      if(upcomingEvents.size() == 0) {
         lv_obj_t * lbl = lv_label_create(event_list_cont);
         lv_label_set_text(lbl, "No upcoming events.");
         lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
      } else {
         for(const auto& evt : upcomingEvents) {
             String readableDate = formatEventDateTime(evt.dtStart, evt.dtEnd);
             add_event_card(event_list_cont, readableDate, evt.summary, evt.location);
             
             // Keep UI alive during rendering of many items
             lv_timer_handler(); 
         }
      }
    }
    http.end();
  }
  delete client;
}

// ==========================================
// WIFI MANAGER
// ==========================================

void saveConfigCallback () {
  Serial.println("Should save config");
}

void configModeCallback (WiFiManager *myWiFiManager) {
    show_setup_ui(myWiFiManager->getConfigPortalSSID(), setupPassword);
    for(int i=0; i<50; i++) { lv_timer_handler(); delay(10); }
}

// ==========================================
// MAIN
// ==========================================

void setup() {
  Serial.begin(115200);
  
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(128);
  tft.fillScreen(TFT_BLACK);

  lv_init();
  buf1 = (lv_color_t *)heap_caps_aligned_alloc(16, 800 * 480 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  buf2 = (lv_color_t *)heap_caps_aligned_alloc(16, 800 * 480 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (!buf1 || !buf2) { while(1); }

  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 800 * 480);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 800;
  disp_drv.ver_res = 480;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touch_read;
  lv_indev_drv_register(&indev_drv);

  // Initial loader
  lv_obj_t* loader = lv_label_create(lv_scr_act());
  lv_label_set_text(loader, "Initializing...");
  lv_obj_center(loader);
  lv_obj_set_style_text_color(loader, lv_color_white(), 0);
  for(int i=0; i<10; i++) { lv_timer_handler(); delay(10); }

  setupPassword = generatePassword(8);
  
  preferences.begin("calendar_app", false);
  String saved_url = preferences.getString("ical_url", "");
  if (saved_url.length() > 0) strcpy(ical_url, saved_url.c_str());
  
  WiFiManager wm;
  wm.setAPCallback(configModeCallback);
  wm.setSaveConfigCallback(saveConfigCallback);
  WiFiManagerParameter custom_ical_url("ical", "Google Calendar URL", ical_url, 512);
  wm.addParameter(&custom_ical_url);

  if (!wm.autoConnect("CrowPanel-Setup", setupPassword.c_str())) {
    ESP.restart(); 
  }

  strcpy(ical_url, custom_ical_url.getValue());
  if (String(ical_url).length() > 0) preferences.putString("ical_url", ical_url);
  preferences.end();

  build_main_ui();
  
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");
  struct tm timeinfo;
  int timeRetry = 0;
  while(!getLocalTime(&timeinfo) && timeRetry < 20){
    delay(500);
    timeRetry++;
  }
  
  if(timeRetry < 20) fetchCalendar();
}

void loop() {
  lv_timer_handler();
  delay(5);
  
  // Update clock every second
  static unsigned long lastClockUpdate = 0;
  if (millis() - lastClockUpdate > 1000) {
      lastClockUpdate = millis();
      update_clock();
  }
  
  static unsigned long lastFetch = 0;
  if (millis() - lastFetch > 15 * 60 * 1000) { 
      lastFetch = millis();
      if (WiFi.status() == WL_CONNECTED) fetchCalendar();
  }
}