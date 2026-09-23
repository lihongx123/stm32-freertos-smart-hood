#include "app_types.h"
#include "app_config.h"
void hood_step(HoodState *state, const SensorData *data, uint32_t faults)
{
    SystemState previous=state->state;
    const int warning=data->smoke>=APP_SMOKE_BOOST || data->differential_pressure<=APP_BACKFLOW_DP;
    if (faults) { state->state=SYS_FAULT; state->recovery_cycles=0; }
    else if (state->state==SYS_FAULT) { state->state=SYS_RECOVERY; state->recovery_cycles=0; }
    else if (state->state==SYS_RECOVERY && ++state->recovery_cycles<APP_RECOVERY_CYCLES) { /* require sustained validity */ }
    else if (data->valid) state->state=warning ? SYS_WARNING : SYS_NORMAL;
    if (state->state!=previous) {
        ++state->transitions;
        if (state->state==SYS_FAULT) ++state->faults;
    }
    state->light_on=data->valid && data->light<APP_DARK_LIGHT;
    if (state->state==SYS_FAULT || warning) state->fan=FAN_BOOST;
    else if (!data->valid) state->fan=FAN_OFF;
    else if (data->smoke>=APP_SMOKE_HIGH) state->fan=FAN_HIGH;
    else if (data->smoke>=APP_SMOKE_MEDIUM) state->fan=FAN_MEDIUM;
    else if (data->smoke>=APP_SMOKE_LOW) state->fan=FAN_LOW;
    else state->fan=FAN_OFF;
}
