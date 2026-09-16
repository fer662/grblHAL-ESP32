#pragma once
#include "grbl/hal.h"
#include "cycle_plan.h"
status_code_t h5_thread_execute(const h5_cycle_plan_t *plan, unsigned pass);
