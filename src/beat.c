#include <pebble.h>

// TODO: Fix this if and when we get native access to timezone data.
#define DEFAULT_UTC_OFFSET "+0000"

static char utc_offset_str[] = DEFAULT_UTC_OFFSET;
static int utc_offset_seconds = 0;
static int request_timezone_tries_left = 10;  // Try this many times before giving up.
#define INT_FROM_DIGIT(X) ((int)X - '0')


enum {
    BEAPOCH_KEY_UTC_OFFSET = 0x00,
};


static Window *window;

static Layer *background_layer;
static TextLayer *text_date_layer;
static TextLayer *text_time_layer;
static TextLayer *text_beat_layer;

#define BACKGROUND_COLOR GColorBlack
#define FOREGROUND_COLOR GColorWhite

/* #define LAYOUT_DEBUG */


#define TEXT_FG_COLOR FOREGROUND_COLOR
#define TEXT_BG_COLOR GColorClear
#define BORDER_COLOR FOREGROUND_COLOR
#define WINDOW_COLOR BACKGROUND_COLOR

#ifdef LAYOUT_DEBUG
#undef TEXT_FG_COLOR
#undef TEXT_BG_COLOR
#undef BORDER_COLOR
#undef WINDOW_COLOR
#define TEXT_FG_COLOR GColorWhite
#define TEXT_BG_COLOR GColorBlack
#define BORDER_COLOR GColorBlack
#define WINDOW_COLOR GColorWhite
#endif


// Some layout constants so we can tweak things more easily.

#define ISO_TOP 0
#define ISO_HPADDING 4
#define ISO_WDAY_HEIGHT 10
#define ISO_DATE_HEIGHT 26
#define ISO_TIME_HEIGHT 36
#define ISO_BORDER_PADDING 12

#define ISO_WIDTH (144 - 2 * ISO_HPADDING)
#define ISO_HEIGHT (ISO_WDAY_HEIGHT + ISO_DATE_HEIGHT + ISO_TIME_HEIGHT + 2 * (ISO_HPADDING + 2))
#define ISO_TEXT_WIDTH (ISO_WIDTH - 2 * (ISO_HPADDING + 2))
#define ISO_RECT GRect(ISO_HPADDING, ISO_TOP, ISO_WIDTH, ISO_HEIGHT)
#define ISO_WDAY_TOP (ISO_TOP + ISO_HPADDING + 2)
#define ISO_DATE_TOP (ISO_WDAY_TOP + ISO_WDAY_HEIGHT)
#define ISO_TIME_TOP (ISO_DATE_TOP + ISO_DATE_HEIGHT)
#define ISO_WDAY_RECT GRect(ISO_HPADDING + ISO_HPADDING + 2, ISO_WDAY_TOP, ISO_TEXT_WIDTH, ISO_WDAY_HEIGHT)
#define ISO_DATE_RECT GRect(ISO_HPADDING + ISO_HPADDING + 2, ISO_DATE_TOP, ISO_TEXT_WIDTH, ISO_DATE_HEIGHT)
#define ISO_TIME_RECT GRect(ISO_HPADDING + ISO_HPADDING + 2, ISO_TIME_TOP, ISO_TEXT_WIDTH, ISO_TIME_HEIGHT)

#define BEAT_TOP 90
#define BEAT_LEFT 0
#define BEAT_HEIGHT 64
#define BEAT_WIDTH (144 - BEAT_LEFT)
#define BEAT_RECT GRect(BEAT_LEFT, BEAT_TOP, BEAT_WIDTH, BEAT_HEIGHT)

#define RTOP(rect) (rect.origin.y)
#define RBOTTOM(rect) (rect.origin.y + rect.size.h)
#define RLEFT(rect) (rect.origin.x)
#define RRIGHT(rect) (rect.origin.x + rect.size.w)


void set_timezone_offset(char tz_offset[]) {
    int hour = 0;
    int min = 0;

    strncpy(utc_offset_str, tz_offset, sizeof(utc_offset_str));

    hour = (INT_FROM_DIGIT(tz_offset[1]) * 10) + INT_FROM_DIGIT(tz_offset[2]);
    min = (INT_FROM_DIGIT(tz_offset[3]) * 10) + INT_FROM_DIGIT(tz_offset[4]);

    utc_offset_seconds = hour*60*60 + min*60;
    if(tz_offset[0] != '+') {
        utc_offset_seconds = -1 * utc_offset_seconds;
    }
}


void update_background_callback(Layer *layer, GContext *gctx) {
    (void) layer;

    graphics_context_set_stroke_color(gctx, BORDER_COLOR);
    graphics_context_set_fill_color(gctx, BORDER_COLOR);
}


time_t calc_unix_seconds(struct tm *tick_time) {
    // This function is necessary because mktime() doesn't work (probably
    // because there's no native timezone support) and just calling time()
    // gives us no guarantee that the seconds haven't changed since tick_time
    // was made.

    int years_since_epoch;
    int days_since_epoch;

    // This uses a naive algorithm for calculating leap years and will
    // therefore fail in 2100.
    years_since_epoch = tick_time->tm_year - 70;
    days_since_epoch = (years_since_epoch * 365 +
                        (years_since_epoch + 1) / 4 +
                        tick_time->tm_yday);
    return (days_since_epoch * 86400 +
            tick_time->tm_hour * 3600 +
            tick_time->tm_min * 60 +
            tick_time->tm_sec);
}


time_t calc_swatch_beats(time_t unix_seconds) {
    return (((unix_seconds + 3600) % 86400) * 1000) / 86400;
}


void display_time(struct tm *tick_time) {
    // Static, because we pass them to the system.
    static char date_text[] = "9999-99-99";
    static char time_text[] = "99:99:99";
    static char beat_text[] = "@999";

    time_t unix_seconds;

    // Date.

    strftime(date_text, sizeof(date_text), "%Y-%m-%d", tick_time);
    text_layer_set_text(text_date_layer, date_text);

    // Time.

    strftime(time_text, sizeof(time_text), "%H:%M:%S", tick_time);
    if (time_text[0] == ' ') {
        time_text[0] = '0';
    }
    text_layer_set_text(text_time_layer, time_text);

    // Unix timestamp.

    unix_seconds = calc_unix_seconds(tick_time) - utc_offset_seconds;

    // Swatch .beats.

    snprintf(beat_text, sizeof(beat_text), "@%0ld", calc_swatch_beats(unix_seconds));
    if (beat_text[3] == '\0') {
        beat_text[4] = '\0';
        beat_text[3] = beat_text[2];
        beat_text[2] = beat_text[1];
        beat_text[1] = '0';
    } else if (beat_text[2] == '\0') {
        beat_text[4] = '\0';
        beat_text[3] = beat_text[1];
        beat_text[2] = '\0';
        beat_text[1] = '0';
    } else {
        // do nothing
    }
    text_layer_set_text(text_beat_layer, beat_text);
}


TextLayer *init_text_layer(GRect rect, GTextAlignment align, uint32_t font_res_id) {
    TextLayer *layer = text_layer_create(rect);
    text_layer_set_text_color(layer, TEXT_FG_COLOR);
    text_layer_set_background_color(layer, TEXT_BG_COLOR);
    text_layer_set_text_alignment(layer, align);
    text_layer_set_font(layer, fonts_load_custom_font(resource_get_handle(font_res_id)));
    layer_add_child(window_get_root_layer(window), text_layer_get_layer(layer));
    return layer;
}


static void store_timezone() {
    int result;

    result = persist_write_string(1, utc_offset_str);
    if (result < 0) {
        APP_LOG(APP_LOG_LEVEL_WARNING, "Storing timezone failed: %d", result);
    } else {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Stored timezone: %s", utc_offset_str);
    }
}


static void get_stored_timezone(void) {
    char stored_str[sizeof(utc_offset_str)];

    if (!persist_exists(1)) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "No stored TZ offset.");
        // We don't have it persisted, so give up.
        return;
    }
    persist_read_string(1, stored_str, sizeof(stored_str));
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Looked up UTC offset from local storage: %s", stored_str);
    set_timezone_offset(stored_str);
}


static void request_timezone(void) {
    Tuplet utc_offset_tuple = TupletInteger(BEAPOCH_KEY_UTC_OFFSET, 1);

    APP_LOG(APP_LOG_LEVEL_DEBUG, "Requesting timezone, tries left: %d", request_timezone_tries_left);
    if (request_timezone_tries_left > 0) {
        request_timezone_tries_left -= 1;
    }

    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);

    if (iter == NULL) {
        return;
    }

    dict_write_tuplet(iter, &utc_offset_tuple);
    dict_write_end(iter);

    app_message_outbox_send();
}


void in_received_handler(DictionaryIterator *received, void *context) {
    Tuple *utc_offset_tuple = dict_find(received, BEAPOCH_KEY_UTC_OFFSET);

    APP_LOG(APP_LOG_LEVEL_DEBUG, "Received timezone: %s", utc_offset_tuple->value->cstring);

    set_timezone_offset(utc_offset_tuple->value->cstring);
    // Store our timezone for next time.
    store_timezone();

    if (request_timezone_tries_left > 0) {
        request_timezone_tries_left = 0;
    }
}


void in_dropped_handler(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Dropped! (%u)", reason);
}


void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Send Failed! (%u)", reason);
}


static void handle_second_tick(struct tm *tick_time, TimeUnits units_changed) {
    if (request_timezone_tries_left) {
        // If we don't already have a UTC offset, ask the phone for one.
        request_timezone();
    }
    display_time(tick_time);
}


static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);

    window_set_background_color(window, WINDOW_COLOR);
    background_layer = layer_create(layer_get_bounds(window_layer));
    layer_set_update_proc(background_layer, &update_background_callback);
    layer_add_child(window_layer, background_layer);

    text_date_layer = init_text_layer(ISO_DATE_RECT, GTextAlignmentCenter, RESOURCE_ID_FONT_ISO_DATE_23);
    text_time_layer = init_text_layer(ISO_TIME_RECT, GTextAlignmentCenter, RESOURCE_ID_FONT_ISO_TIME_31);
    text_beat_layer = init_text_layer(BEAT_RECT, GTextAlignmentLeft, RESOURCE_ID_FONT_SWATCH_BEATS_46);

    get_stored_timezone();
    time_t now = time(NULL);
    display_time(localtime(&now));
    tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);
}


static void window_unload(Window *window) {
    bluetooth_connection_service_unsubscribe();
    battery_state_service_unsubscribe();
    tick_timer_service_unsubscribe();
    text_layer_destroy(text_beat_layer);
    text_layer_destroy(text_time_layer);
    text_layer_destroy(text_date_layer);
    layer_destroy(background_layer);
}


static void init(void) {
    set_timezone_offset(DEFAULT_UTC_OFFSET);

    // AppMessage setup.
    app_message_register_inbox_received(in_received_handler);
    app_message_register_inbox_dropped(in_dropped_handler);
    app_message_register_outbox_failed(out_failed_handler);
    app_message_open(64, 64);

    // Window setup.
    window = window_create();
    window_set_window_handlers(window, (WindowHandlers) {
        .load = window_load,
        .unload = window_unload,
    });
    window_stack_push(window, true /* Animated */);
}


static void deinit(void) {
    window_destroy(window);
}


int main(void) {
  init();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);

  app_event_loop();
  deinit();
}
