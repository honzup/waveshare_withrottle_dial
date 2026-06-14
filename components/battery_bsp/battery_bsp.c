#include "battery_bsp.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"

static const char *TAG = "battery_bsp";

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t         s_cali_handle;
static bool                      s_cali_ok = false;

void battery_bsp_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_0, &chan_cfg));

    // Curve-fitting calibration (ESP32-S3 supports this scheme)
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle);
    s_cali_ok = (err == ESP_OK);
    if (!s_cali_ok) {
        ESP_LOGW(TAG, "Curve-fitting calibration unavailable, using raw formula");
    } else {
        ESP_LOGI(TAG, "Battery ADC initialised (GPIO1, calibrated)");
    }
}

float battery_get_voltage(void)
{
    // Average several samples to suppress ADC noise (the rail is noisy under
    // Wi-Fi load). The raw reads are cheap.
    const int N   = 16;
    long      acc = 0;
    int       got = 0;
    for (int i = 0; i < N; ++i) {
        int raw = 0;
        if (adc_oneshot_read(s_adc_handle, ADC_CHANNEL_0, &raw) == ESP_OK) {
            acc += raw;
            ++got;
        }
    }
    if (got == 0) {
        return 0.0f;
    }
    int raw_avg = (int)(acc / got);

    float v_adc;
    if (s_cali_ok) {
        int mv = 0;
        adc_cali_raw_to_voltage(s_cali_handle, raw_avg, &mv);
        v_adc = mv * 0.001f;
    } else {
        v_adc = (float)raw_avg * 3.3f / 4096.0f;
    }

    return v_adc * 2.0f;  // compensate for the 1:1 voltage divider on the board
}

// Approximate single-cell LiPo discharge curve (light load): voltage -> percent,
// linearly interpolated between points. Far closer to reality than a straight
// 3.0–4.2 V line, which over-reports because a LiPo sits near 3.7–3.9 V for most
// of its usable charge.
static int lipo_pct_from_voltage(float v)
{
    static const float volts[] = {3.00f, 3.50f, 3.69f, 3.73f, 3.77f, 3.80f, 3.84f,
                                   3.87f, 3.92f, 3.97f, 4.02f, 4.08f, 4.13f, 4.20f};
    static const float pct[]   = {0.f,   5.f,   10.f,  20.f,  30.f,  40.f,  50.f,
                                   60.f,  70.f,  80.f,  88.f,  93.f,  97.f,  100.f};
    const int n = (int)(sizeof(volts) / sizeof(volts[0]));
    if (v <= volts[0])     return 0;
    if (v >= volts[n - 1]) return 100;
    for (int i = 1; i < n; ++i) {
        if (v < volts[i]) {
            float f = (v - volts[i - 1]) / (volts[i] - volts[i - 1]);
            return (int)(pct[i - 1] + f * (pct[i] - pct[i - 1]) + 0.5f);
        }
    }
    return 100;
}

int battery_get_pct(void)
{
    float v = battery_get_voltage();
    // Exponential moving average across calls so a momentary load sag (e.g. a
    // Wi-Fi TX burst) doesn't make the reading jump around.
    static float s_vf = -1.0f;
    if (v <= 0.0f) {
        v = (s_vf > 0.0f) ? s_vf : 3.7f;  // bad read: hold the last good value
    }
    s_vf = (s_vf < 0.0f) ? v : (s_vf + 0.3f * (v - s_vf));
    return lipo_pct_from_voltage(s_vf);
}

bool battery_is_charging(void)
{
    return usb_serial_jtag_is_connected();
}
