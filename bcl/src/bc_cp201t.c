#include <bc_adc.h>
#include <bc_cp201t.h>

typedef struct
{
    bc_module_sensor_channel_t channel;
    bc_adc_channel_t channel_adc;
    bc_scheduler_task_id_t task_id_interval;
    bc_scheduler_task_id_t task_id_measure;
    void (*event_handler)(bc_module_sensor_channel_t, bc_cp201t_event_t, void *);
    void *event_param;
    int32_t thermistor_data;
    float temperature;
    bc_tick_t update_interval;
    bool initialized;
    bool measurement_active;

} bc_cp201t_t;

static bc_cp201t_t _bc_cp201t[2] =
{
    [0] =
    {
        .channel = BC_MODULE_SENSOR_CHANNEL_A,
        .channel_adc = BC_ADC_CHANNEL_A4,
        .initialized = false,
    },

    [1] =
    {
        .channel = BC_MODULE_SENSOR_CHANNEL_B,
        .channel_adc = BC_ADC_CHANNEL_A5,
        .initialized = false,
    }
};

static const uint16_t _thermistor_conversion_table[1024];

static void _bc_cp201t_adc_event_handler(bc_adc_channel_t channel, bc_adc_event_t event, void *param);

static void _bc_cp201t_task_interval(void *param);

static void _bc_cp201t_task_measure(void *param);

bool bc_cp201t_init(bc_module_sensor_channel_t channel)
{
    bc_cp201t_t *cp201t = &_bc_cp201t[channel];

    if (!cp201t->initialized)
    {
        if (!bc_module_sensor_init())
        {
            return false;
        }

        // Initialize ADC to measure voltage on cp201t (temperature)
        bc_adc_init(cp201t->channel_adc, BC_ADC_FORMAT_16_BIT);
        bc_adc_set_event_handler(cp201t->channel_adc, _bc_cp201t_adc_event_handler, cp201t);

        cp201t->task_id_interval = bc_scheduler_register(_bc_cp201t_task_interval, cp201t, BC_TICK_INFINITY);
        cp201t->task_id_measure = bc_scheduler_register(_bc_cp201t_task_measure, cp201t, BC_TICK_INFINITY);

        cp201t->initialized = true;
    }

    return true;
}

void bc_cp201t_set_event_handler(bc_module_sensor_channel_t channel, void (*event_handler)(bc_module_sensor_channel_t, bc_cp201t_event_t, void *), void *event_param)
{
    bc_cp201t_t *cp201t = &_bc_cp201t[channel];

    cp201t->event_handler = event_handler;
    cp201t->event_param = event_param;
}

void bc_cp201t_set_update_interval(bc_module_sensor_channel_t channel, bc_tick_t interval)
{
    bc_cp201t_t *cp201t = &_bc_cp201t[channel];

    cp201t->update_interval = interval;

    if (cp201t->update_interval == BC_TICK_INFINITY)
    {
        bc_scheduler_plan_absolute(cp201t->task_id_interval, BC_TICK_INFINITY);
    }
    else
    {
        bc_scheduler_plan_relative(cp201t->task_id_interval, cp201t->update_interval);
    }
}

bool bc_cp201t_measure(bc_cp201t_t *self)
{
    if (self->measurement_active)
    {
        return false;
    }

    self->measurement_active = true;

    bc_scheduler_plan_now(self->task_id_measure);

    return true;
}

bool bc_cp201t_get_temperature_celsius(bc_module_sensor_channel_t channel, float *celsius)
{
    float vdda_voltage;
    int16_t temp_code;
    bc_cp201t_t *cp201t = &_bc_cp201t[channel];
    uint16_t data = cp201t->thermistor_data;

    // Get actual VDDA and accurate data
    bc_adc_get_vdda_voltage(&vdda_voltage);
    data *= 3.3 / vdda_voltage;

    // Software shuffle of pull-up and NTC with each other (So that the table can be used)
    data = 0xffff - data;

    // Find temperature in LUT
    temp_code = _thermistor_conversion_table[data >> 6];

    // If temperature is valid...
    if (temp_code != 0x7fff)
    {
        // Conversion from fixed point to float
        *celsius = temp_code / 10.;

        return true;
    }
    else
    {
        return false;
    }
}

static void _bc_cp201t_task_interval(void *param)
{
    bc_cp201t_t *cp201t = param;

    bc_cp201t_measure(cp201t);

    // Plan self task
    bc_scheduler_plan_current_relative(cp201t->update_interval);
}

static void _bc_cp201t_task_measure(void *param)
{
    bc_cp201t_t *cp201t = param;

    // Connect pull-up
    bc_module_sensor_set_pull(cp201t->channel, BC_MODULE_SENSOR_PULL_UP_4K7);

    // Start another reading
    bc_adc_async_read(cp201t->channel_adc);
}

static void _bc_cp201t_adc_event_handler(bc_adc_channel_t channel, bc_adc_event_t event, void *param)
{
    (void) channel;

    bc_cp201t_t *cp201t = param;

    cp201t->measurement_active = false;

    if (event == BC_ADC_EVENT_DONE)
    {
        // Disconnect pull-up
        bc_module_sensor_set_pull(cp201t->channel, BC_MODULE_SENSOR_PULL_NONE);

        bc_adc_get_result(cp201t->channel_adc, &cp201t->thermistor_data);

        cp201t->event_handler(cp201t->channel, BC_CP201T_EVENT_UPDATE, cp201t->event_param);
    }
    else // if  (event == BC_ADC_EVENT_ERROR)
    {
        cp201t->event_handler(cp201t->channel, BC_CP201T_EVENT_ERROR, cp201t->event_param);
    }
}

// A value of 0x7FFF indicates out of range signal
static const uint16_t _thermistor_conversion_table[] =
{
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0xFE70,
    0xFE7A, 0xFE7E, 0xFE84, 0xFE8E, 0xFE93, 0xFE98, 0xFEA2, 0xFEA8,
    0xFEAC, 0xFEB1, 0xFEB6, 0xFEBA, 0xFEC0, 0xFECA, 0xFED0, 0xFED4,
    0xFED9, 0xFEDE, 0xFEE2, 0xFEE5, 0xFEE8, 0xFEED, 0xFEF2, 0xFEF7,
    0xFEFC, 0xFEFF, 0xFF02, 0xFF06, 0xFF0B, 0xFF10, 0xFF14, 0xFF16,
    0xFF1A, 0xFF1F, 0xFF24, 0xFF28, 0xFF2B, 0xFF2E, 0xFF31, 0xFF35,
    0xFF38, 0xFF3B, 0xFF3F, 0xFF42, 0xFF45, 0xFF49, 0xFF4C, 0xFF50,
    0xFF53, 0xFF56, 0xFF59, 0xFF5B, 0xFF5D, 0xFF60, 0xFF63, 0xFF67,
    0xFF6A, 0xFF6D, 0xFF6F, 0xFF71, 0xFF74, 0xFF77, 0xFF7B, 0xFF7E,
    0xFF81, 0xFF83, 0xFF86, 0xFF88, 0xFF8A, 0xFF8D, 0xFF8F, 0xFF92,
    0xFF95, 0xFF97, 0xFF9A, 0xFF9C, 0xFF9E, 0xFFA1, 0xFFA3, 0xFFA6,
    0xFFA9, 0xFFAB, 0xFFAE, 0xFFB0, 0xFFB2, 0xFFB4, 0xFFB6, 0xFFB8,
    0xFFBA, 0xFFBC, 0xFFBF, 0xFFC2, 0xFFC4, 0xFFC6, 0xFFC8, 0xFFCA,
    0xFFCC, 0xFFCE, 0xFFD0, 0xFFD3, 0xFFD6, 0xFFD8, 0xFFDA, 0xFFDC,
    0xFFDE, 0xFFE0, 0xFFE2, 0xFFE4, 0xFFE6, 0xFFE8, 0xFFEA, 0xFFEC,
    0xFFEE, 0xFFF0, 0xFFF2, 0xFFF4, 0xFFF6, 0xFFF8, 0xFFFA, 0xFFFC,
    0xFFFE, 0x0000, 0x0002, 0x0004, 0x0006, 0x0008, 0x000A, 0x000C,
    0x000D, 0x000F, 0x0011, 0x0012, 0x0014, 0x0016, 0x0018, 0x001A,
    0x001C, 0x001E, 0x0020, 0x0022, 0x0023, 0x0025, 0x0026, 0x0028,
    0x002A, 0x002B, 0x002D, 0x002E, 0x0030, 0x0032, 0x0034, 0x0036,
    0x0038, 0x003A, 0x003C, 0x003E, 0x0040, 0x0041, 0x0043, 0x0044,
    0x0046, 0x0048, 0x0049, 0x004B, 0x004D, 0x004E, 0x0050, 0x0052,
    0x0053, 0x0055, 0x0057, 0x0058, 0x005A, 0x005B, 0x005D, 0x005E,
    0x0060, 0x0061, 0x0063, 0x0064, 0x0066, 0x0067, 0x0069, 0x006B,
    0x006C, 0x006E, 0x0070, 0x0071, 0x0073, 0x0075, 0x0076, 0x0078,
    0x007A, 0x007B, 0x007C, 0x007E, 0x007F, 0x0081, 0x0082, 0x0083,
    0x0085, 0x0086, 0x0087, 0x0089, 0x008A, 0x008C, 0x008E, 0x008F,
    0x0091, 0x0093, 0x0094, 0x0096, 0x0098, 0x0099, 0x009A, 0x009C,
    0x009D, 0x009F, 0x00A0, 0x00A1, 0x00A3, 0x00A4, 0x00A6, 0x00A7,
    0x00A9, 0x00AA, 0x00AB, 0x00AD, 0x00AE, 0x00B0, 0x00B1, 0x00B3,
    0x00B4, 0x00B5, 0x00B7, 0x00B8, 0x00BA, 0x00BB, 0x00BD, 0x00BE,
    0x00BF, 0x00C1, 0x00C2, 0x00C4, 0x00C5, 0x00C7, 0x00C8, 0x00C9,
    0x00CB, 0x00CC, 0x00CE, 0x00CF, 0x00D1, 0x00D2, 0x00D3, 0x00D5,
    0x00D6, 0x00D8, 0x00D9, 0x00DB, 0x00DC, 0x00DD, 0x00DF, 0x00E0,
    0x00E2, 0x00E3, 0x00E5, 0x00E6, 0x00E7, 0x00E9, 0x00EA, 0x00EC,
    0x00ED, 0x00EF, 0x00F0, 0x00F1, 0x00F3, 0x00F4, 0x00F5, 0x00F6,
    0x00F7, 0x00F9, 0x00FA, 0x00FB, 0x00FD, 0x00FE, 0x0100, 0x0101,
    0x0103, 0x0104, 0x0105, 0x0107, 0x0108, 0x010A, 0x010B, 0x010D,
    0x010E, 0x010F, 0x0111, 0x0112, 0x0113, 0x0114, 0x0115, 0x0117,
    0x0118, 0x0119, 0x011B, 0x011C, 0x011E, 0x011F, 0x0121, 0x0122,
    0x0123, 0x0125, 0x0126, 0x0128, 0x0129, 0x012B, 0x012C, 0x012D,
    0x012F, 0x0130, 0x0131, 0x0132, 0x0133, 0x0135, 0x0136, 0x0137,
    0x0139, 0x013A, 0x013C, 0x013D, 0x013F, 0x0140, 0x0141, 0x0143,
    0x0144, 0x0145, 0x0146, 0x0147, 0x0149, 0x014A, 0x014B, 0x014D,
    0x014E, 0x0150, 0x0151, 0x0153, 0x0154, 0x0155, 0x0157, 0x0158,
    0x0159, 0x015A, 0x015B, 0x015D, 0x015E, 0x015F, 0x0161, 0x0162,
    0x0164, 0x0165, 0x0167, 0x0168, 0x0169, 0x016B, 0x016C, 0x016E,
    0x016F, 0x0171, 0x0172, 0x0173, 0x0175, 0x0176, 0x0177, 0x0178,
    0x0179, 0x017B, 0x017C, 0x017D, 0x017F, 0x0180, 0x0182, 0x0183,
    0x0185, 0x0186, 0x0187, 0x0189, 0x018A, 0x018C, 0x018D, 0x018F,
    0x0190, 0x0191, 0x0193, 0x0194, 0x0195, 0x0196, 0x0197, 0x0199,
    0x019A, 0x019B, 0x019D, 0x019E, 0x01A0, 0x01A1, 0x01A3, 0x01A4,
    0x01A5, 0x01A7, 0x01A8, 0x01AA, 0x01AB, 0x01AD, 0x01AE, 0x01AF,
    0x01B1, 0x01B2, 0x01B4, 0x01B5, 0x01B7, 0x01B8, 0x01B9, 0x01BB,
    0x01BC, 0x01BE, 0x01BF, 0x01C1, 0x01C2, 0x01C3, 0x01C5, 0x01C6,
    0x01C8, 0x01C9, 0x01CB, 0x01CC, 0x01CD, 0x01CF, 0x01D0, 0x01D2,
    0x01D3, 0x01D5, 0x01D6, 0x01D7, 0x01D9, 0x01DA, 0x01DC, 0x01DD,
    0x01DF, 0x01E0, 0x01E1, 0x01E3, 0x01E4, 0x01E6, 0x01E7, 0x01E9,
    0x01EA, 0x01EB, 0x01ED, 0x01EE, 0x01F0, 0x01F1, 0x01F3, 0x01F4,
    0x01F5, 0x01F7, 0x01F8, 0x01FA, 0x01FB, 0x01FD, 0x01FE, 0x01FF,
    0x0201, 0x0202, 0x0204, 0x0205, 0x0206, 0x0208, 0x020A, 0x020B,
    0x020D, 0x020F, 0x0210, 0x0212, 0x0213, 0x0215, 0x0216, 0x0218,
    0x0219, 0x021A, 0x021C, 0x021E, 0x021F, 0x0221, 0x0223, 0x0224,
    0x0226, 0x0228, 0x0229, 0x022A, 0x022C, 0x022D, 0x022F, 0x0230,
    0x0232, 0x0233, 0x0235, 0x0237, 0x0238, 0x023A, 0x023C, 0x023D,
    0x023F, 0x0241, 0x0242, 0x0244, 0x0246, 0x0247, 0x0249, 0x024B,
    0x024C, 0x024E, 0x0250, 0x0251, 0x0253, 0x0255, 0x0256, 0x0258,
    0x025A, 0x025B, 0x025D, 0x025F, 0x0260, 0x0262, 0x0264, 0x0265,
    0x0267, 0x0269, 0x026A, 0x026C, 0x026E, 0x026F, 0x0271, 0x0272,
    0x0274, 0x0276, 0x0278, 0x027A, 0x027C, 0x027E, 0x0280, 0x0282,
    0x0283, 0x0285, 0x0287, 0x0288, 0x028A, 0x028C, 0x028E, 0x0290,
    0x0292, 0x0294, 0x0296, 0x0297, 0x0299, 0x029B, 0x029C, 0x029E,
    0x02A0, 0x02A2, 0x02A4, 0x02A6, 0x02A8, 0x02AA, 0x02AC, 0x02AE,
    0x02B0, 0x02B2, 0x02B4, 0x02B6, 0x02B8, 0x02BA, 0x02BC, 0x02BE,
    0x02C0, 0x02C2, 0x02C4, 0x02C6, 0x02C8, 0x02CA, 0x02CC, 0x02CE,
    0x02D0, 0x02D2, 0x02D4, 0x02D6, 0x02D8, 0x02DA, 0x02DC, 0x02DE,
    0x02E0, 0x02E2, 0x02E4, 0x02E6, 0x02E9, 0x02EC, 0x02EE, 0x02F0,
    0x02F2, 0x02F4, 0x02F6, 0x02F8, 0x02FA, 0x02FD, 0x0300, 0x0302,
    0x0304, 0x0306, 0x0308, 0x030A, 0x030C, 0x030E, 0x0311, 0x0313,
    0x0316, 0x0319, 0x031B, 0x031E, 0x0320, 0x0322, 0x0325, 0x0327,
    0x032A, 0x032D, 0x032F, 0x0332, 0x0334, 0x0337, 0x0339, 0x033C,
    0x033E, 0x0340, 0x0343, 0x0345, 0x0348, 0x034B, 0x034D, 0x0350,
    0x0352, 0x0354, 0x0357, 0x0359, 0x035C, 0x035F, 0x0363, 0x0366,
    0x0369, 0x036B, 0x036D, 0x0370, 0x0373, 0x0377, 0x037A, 0x037D,
    0x037F, 0x0381, 0x0384, 0x0387, 0x038A, 0x038E, 0x0392, 0x0395,
    0x0398, 0x039B, 0x039D, 0x039F, 0x03A2, 0x03A5, 0x03A9, 0x03AC,
    0x03AF, 0x03B3, 0x03B6, 0x03B9, 0x03BD, 0x03C0, 0x03C3, 0x03C7,
    0x03CA, 0x03CD, 0x03D0, 0x03D4, 0x03D9, 0x03DE, 0x03E2, 0x03E5,
    0x03E8, 0x03EB, 0x03EE, 0x03F2, 0x03F7, 0x03FC, 0x03FF, 0x0402,
    0x0406, 0x040B, 0x0410, 0x0414, 0x0416, 0x041A, 0x041F, 0x0424,
    0x0428, 0x042B, 0x042E, 0x0433, 0x0438, 0x043D, 0x0442, 0x0447,
    0x044C, 0x0451, 0x0456, 0x045B, 0x0460, 0x0465, 0x046A, 0x046F,
    0x0474, 0x0479, 0x047E, 0x0483, 0x0488, 0x048D, 0x0492, 0x0497,
    0x049C, 0x04A0, 0x04A6, 0x04B0, 0x04B7, 0x04BA, 0x04BD, 0x04C4,
    0x04CE, 0x04D4, 0x04D8, 0x04E2, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF
};
