#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "time.h"
#include <lvgl.h>
#include "LGFX_Setup.h" // Your custom driver file
#include "secrets.h"    // Include your secrets file

// ==========================================
// CONFIGURATION START
// ==========================================

const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// Your Google Calendar "Secret address in iCal format"
// Ensure it starts with "https://"
const char* ical_url = ICAL_URL;

// Time settings
const long  gmtOffset_sec = -6 * 3600;  // CST (adjust as needed)
const int   daylightOffset_sec = 3600;

// SET THIS TO FALSE TO USE REAL TIME
const bool USE_STATIC_DATE = false; 
const int STATIC_YEAR = 2023;
const int STATIC_MONTH = 10;
const int STATIC_DAY = 25;

// ==========================================
// CONFIGURATION END
// ==========================================

static LGFX tft;
static lv_disp_draw_buf_t draw_buf;
// INCREASED BUFFER: 10 rows -> 40 rows for smoother scrolling
static lv_color_t buf[800 * 40]; 

lv_obj_t * calendar;
lv_obj_t * event_list_textarea;

/* Display Flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    if (tft.getStartCount() == 0) tft.startWrite();
    tft.pushImageDMA(area->x1, area->y1, w, h, (uint16_t *)&color_p->full);
    if (tft.getStartCount() == 0) tft.endWrite();
    lv_disp_flush_ready(disp);
}

/* Touch Reading */
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

/* UI Builder */
void build_ui() {
    lv_obj_t * scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101010), 0);

    calendar = lv_calendar_create(scr);
    lv_obj_set_size(calendar, 350, 350);
    lv_obj_align(calendar, LV_ALIGN_LEFT_MID, 20, 0);
    lv_calendar_header_arrow_create(calendar);

    lv_obj_t * label = lv_label_create(scr);
    lv_label_set_text(label, "Upcoming Events");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0); 
    lv_obj_align(label, LV_ALIGN_TOP_RIGHT, -180, 40);

    event_list_textarea = lv_textarea_create(scr);
    lv_obj_set_size(event_list_textarea, 380, 350);
    lv_obj_align(event_list_textarea, LV_ALIGN_RIGHT_MID, -20, 20);
    
    // RESTORE CLICKABILITY FOR SCROLLING
    lv_obj_add_flag(event_list_textarea, LV_OBJ_FLAG_CLICKABLE); 
    lv_obj_add_flag(event_list_textarea, LV_OBJ_FLAG_SCROLLABLE); 
    // Disable text selection/cursor to make it look like a list
    lv_textarea_set_cursor_click_pos(event_list_textarea, false);
    
    lv_textarea_set_text(event_list_textarea, "Waiting for sync...\n");
    lv_obj_set_style_text_color(event_list_textarea, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_bg_color(event_list_textarea, lv_color_hex(0x303030), 0);
    lv_obj_set_style_text_font(event_list_textarea, LV_FONT_DEFAULT, 0); 
}

void fetchCalendar() {
  Serial.println("--- Fetching Calendar ---");
  lv_textarea_set_text(event_list_textarea, "Updating..."); 
  lv_timer_handler(); 

  // 1. Time Setup
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    lv_textarea_set_text(event_list_textarea, "Time Sync Error");
    return;
  }

  int currentYear = timeinfo.tm_year + 1900;
  int currentMonth = timeinfo.tm_mon + 1;
  int currentDay = timeinfo.tm_mday;
  time_t referenceTime;

  if (USE_STATIC_DATE) {
      Serial.println("MODE: Static Date (Debugging)");
      currentYear = STATIC_YEAR;
      currentMonth = STATIC_MONTH;
      currentDay = STATIC_DAY;
      struct tm staticTm = {0};
      staticTm.tm_year = STATIC_YEAR - 1900;
      staticTm.tm_mon = STATIC_MONTH - 1;
      staticTm.tm_mday = STATIC_DAY;
      staticTm.tm_isdst = -1;
      referenceTime = mktime(&staticTm);
  } else {
      Serial.println("MODE: Real Time (NTP)");
      time(&referenceTime);
  }

  // Update UI Calendar
  lv_calendar_set_today_date(calendar, currentYear, currentMonth, currentDay);
  lv_calendar_set_showed_date(calendar, currentYear, currentMonth);

  // 2. Calculate Window (Past 7 Days -> Next 90 Days)
  time_t startTime = referenceTime - (7 * 24 * 60 * 60); 
  struct tm startInfo;
  localtime_r(&startTime, &startInfo);
  char startString[16];
  sprintf(startString, "%04d%02d%02d", startInfo.tm_year + 1900, startInfo.tm_mon + 1, startInfo.tm_mday);

  time_t endTime = referenceTime + (90 * 24 * 60 * 60); // INCREASED TO 90 DAYS
  struct tm endInfo;
  localtime_r(&endTime, &endInfo);
  char endString[16];
  sprintf(endString, "%04d%02d%02d", endInfo.tm_year + 1900, endInfo.tm_mon + 1, endInfo.tm_mday);

  Serial.printf("Searching events between: %s AND %s\n", startString, endString);

  // 3. Connect & Download
  WiFiClientSecure *client = new WiFiClientSecure;
  if(client) client->setInsecure();
  
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  if (http.begin(*client, ical_url)) { 
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      WiFiClient *stream = http.getStreamPtr();
      
      String currentEventSummary = "";
      String currentEventDate = "";
      bool insideEvent = false;
      int eventsFound = 0;
      
      lv_textarea_set_text(event_list_textarea, ""); // Clear list

      while (http.connected() && (stream->available() > 0 || stream->available() == -1)) {
        String line = stream->readStringUntil('\n');
        line.trim();

        if (line.startsWith("BEGIN:VEVENT")) {
          insideEvent = true;
          currentEventSummary = "";
          currentEventDate = "";
        } 
        else if (line.startsWith("END:VEVENT")) {
          if (currentEventDate.length() >= 8) {
             String eventDateOnly = currentEventDate.substring(0, 8);
             
             // Check Filter
             if (eventDateOnly >= String(startString) && eventDateOnly <= String(endString)) {
                eventsFound++;
                String displayDate = eventDateOnly.substring(0,4) + "-" + 
                                     eventDateOnly.substring(4,6) + "-" + 
                                     eventDateOnly.substring(6,8);
                
                String entry = displayDate + "\n" + currentEventSummary + "\n\n";
                lv_textarea_add_text(event_list_textarea, entry.c_str());
                Serial.printf("[MATCH] %s: %s\n", eventDateOnly.c_str(), currentEventSummary.c_str());
                lv_timer_handler(); 
             } else {
                // Debug rejected events to see why
                // Serial.printf("[SKIP] %s (Outside Window)\n", eventDateOnly.c_str());
             }
          }
          insideEvent = false;
        }
        else if (insideEvent) {
          if (line.startsWith("SUMMARY:")) {
            currentEventSummary = line.substring(8);
          }
          else if (line.startsWith("DTSTART")) {
            int colonIndex = line.lastIndexOf(':');
            if (colonIndex != -1) {
              currentEventDate = line.substring(colonIndex + 1);
            }
          }
        }
        
        if(stream->available() == 0) { delay(10); if(stream->available() == 0) break; }
      }
      
      if(eventsFound == 0) {
          lv_textarea_set_text(event_list_textarea, "No events found.\nCheck Serial Monitor for debug info.");
          Serial.println("Finished search. Found 0 events.");
      }
    } else {
      Serial.printf("HTTP Failed: %d\n", httpCode);
      lv_textarea_set_text(event_list_textarea, "Download Error");
    }
    http.end();
  } else {
    Serial.println("Connection Failed");
    lv_textarea_set_text(event_list_textarea, "Connection Failed");
  }
  delete client;
}

void setup() {
  Serial.begin(115200);
  
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(128);
  tft.fillScreen(TFT_BLACK);

  lv_init();
  // Update init to use the larger buffer size
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, 800 * 40);

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

  build_ui();
  
  // Show connection status
  lv_textarea_set_text(event_list_textarea, "Connecting to WiFi...");
  for(int i=0; i<5; i++) { lv_timer_handler(); delay(50); }

  WiFi.begin(ssid, password);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    retry++;
    lv_timer_handler();
  }

  if (WiFi.status() == WL_CONNECTED) {
      lv_textarea_set_text(event_list_textarea, "WiFi Connected!\nSyncing Time...");
      lv_timer_handler();
      
      configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");
      struct tm timeinfo;
      // Wait for time sync
      int timeRetry = 0;
      while(!getLocalTime(&timeinfo) && timeRetry < 10){
        delay(500);
        timeRetry++;
        Serial.print(".");
      }
      
      if(timeRetry < 10) {
         fetchCalendar();
      } else {
         lv_textarea_set_text(event_list_textarea, "Time Sync Failed!");
      }
  } else {
      lv_textarea_set_text(event_list_textarea, "WiFi Failed");
  }
}

void loop() {
  lv_timer_handler();
  delay(5);
  
  static unsigned long lastFetch = 0;
  if (millis() - lastFetch > 15 * 60 * 1000) { 
      lastFetch = millis();
      if (WiFi.status() == WL_CONNECTED) fetchCalendar();
  }
}