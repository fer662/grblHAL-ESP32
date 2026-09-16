/* Run the actual core planner, segment generator and step ISR on a virtual clock. */
#include "grbl/hal.h"
#include "grbl/stepper.h"
#include "grbl/state_machine.h"
#include "grbl/task.h"
#include "grbl/ioports.h"
#include "cycle_plan.h"
#include "thread_path.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
grbl_hal_t hal;
grbl_t grbl;
system_t sys;
settings_t settings;
parser_state_t gc_state;
void st_spindle_sync_cfg(settings_t *, settings_changed_flags_t);
static spindle_t spindle;
static spindle_data_t spindle_data;
static double seconds, rpm, phase_error, first_x_z, last_x_z;
static bool timer_running;
static uint32_t period;
static unsigned blocks, pulses_x, pulses_z;
static h5_cycle_plan_t profile;
static double initial_lag, turns, base_rpm, slew, last_z_time, max_z_gap, x_times[32];
static foreground_task_ptr delayed;
static void *delayed_data;
static double due, cancel_after;
static bool cancel_sent,aux_forward=true;
static unsigned thread_starts=1;
static spindle_data_t *get_spindle(spindle_data_request_t request) {
 (void)request;spindle_data.rpm=rpm;spindle_data.angular_position=turns;return &spindle_data;
}
spindle_t *gc_spindle_get(spindle_num_t id) {(void)id;return &spindle;}
void gc_clear_output_commands(output_command_t *p) {(void)p;}
void gc_output_message(char *p) {free(p);}
void report_add_realtime(report_tracking_t r) {(void)r;}
void report_plain(void *p) {(void)p;}
bool task_add_delayed(foreground_task_ptr fn,void *data,uint32_t ms) {delayed=fn;delayed_data=data;due=seconds+ms/1000.0;return true;}
bool task_add_immediate(foreground_task_ptr fn,void *data) {(void)fn;(void)data;return true;}
bool task_run_on_startup(foreground_task_ptr fn,void *data) {(void)fn;(void)data;return true;}
void task_delete(foreground_task_ptr fn,void *data) {(void)data;if(delayed==fn)delayed=NULL;}
bool ioport_analog_out(uint8_t port,float v) {(void)port;(void)v;return true;}
bool ioport_digital_out(uint8_t port,uint32_t v) {(void)port;(void)v;return true;}
float spindle_set_rpm(spindle_ptrs_t *s,float r,override_t o) {(void)s;(void)o;return r;}
sys_state_t state_get(void) {return timer_running?STATE_CYCLE:STATE_IDLE;}
static void set_bits(volatile uint_fast16_t *v,uint_fast16_t bits) {*v|=bits;}
static void enable(axes_signals_t a,bool hold) {(void)a;(void)hold;}
static void idle(bool reset) {(void)reset;timer_running=false;}
static void wake(void) {timer_running=true;}
static void cycles(uint32_t c) {period=c;}
static void pulse(stepper_t *s) {
 if(s->new_block) blocks++;
 double z=sys.position[Z_AXIS]/settings.axis[Z_AXIS].steps_per_mm;
 if(s->step_out.x) {
  if(!pulses_x)first_x_z=z;last_x_z=z;
  if(pulses_x>=32) assert((32.0/1200)/(seconds-x_times[pulses_x%32])<=1.01*settings.axis[X_AXIS].max_rate/60);
  x_times[pulses_x++%32]=seconds;
 }
 if(s->step_out.z) {
  if((z-profile.entry_begin)*profile.direction>0 && (profile.exit_end-z)*profile.direction>0)
   max_z_gap=fmax(max_z_gap,seconds-last_z_time);
  last_z_time=seconds;pulses_z++;
 }
 // Assess full-depth thread only. Entry/exit still must have continuous Z pulses.
 if((z-profile.full_begin)*profile.direction>=.02 && (profile.full_end-z)*profile.direction>=.02) {
  double error=fabs(z-profile.approach)+initial_lag-spindle_data.angular_position*profile.lead;
  if(fabs(error)>phase_error)phase_error=fabs(error);
 }
}

static bool axes_available=true,update_active;
bool h5_motion_idle(void) {return !timer_running;}
bool h5_motion_axes_available(void) {return axes_available;}
bool h5_update_active(void) {return update_active;}
void limits_soft_check(float *target,planner_cond_t condition) {
 (void)condition;
 assert(target[Z_AXIS]>=profile.config.z_min-.00001 && target[Z_AXIS]<=profile.config.z_max+.00001);
 assert(!timer_running && pulses_x==0 && pulses_z==0); // Every target checked before movement.
}
void system_convert_array_steps_to_mpos(float *target,int32_t *steps) {
 for(unsigned i=0;i<N_AXIS;i++)target[i]=steps[i]/settings.axis[i].steps_per_mm;
}
void mc_override_ctrl_update(gc_override_flags_t flags) {sys.override.control=flags;}
bool protocol_buffer_synchronize(void) {
 // Hardware index wait is simulated; it can only begin after all blocks exist.
 assert(plan_get_block_buffer_available()==100-H5_THREAD_BLOCKS);
 assert(sys.position[X_AXIS]==lround(profile.clearance*1200) && !pulses_x);
 if(cancel_after<0) {sys.abort=true;return false;}
 st_prep_buffer();initial_lag=st_get_spindle_sync_offset();st_wake_up();
 unsigned interrupts=0;
 while(timer_running) {
  assert(++interrupts<10000000);double dt=(double)period/hal.f_step_timer;
  double next_rpm=fmax(.8*base_rpm,fmin(1.2*base_rpm,base_rpm+slew*(seconds+dt)));
  turns+=dt*(rpm+next_rpm)/120;rpm=next_rpm;seconds+=dt;
  if(cancel_after>0 && seconds>=cancel_after && !cancel_sent) {
   sys.step_control.execute_hold=On;st_update_plan_block_parameters(false);cancel_sent=true;
  }
  get_spindle(SpindleData_AngularPosition);
  stepper_driver_interrupt_handler();
  if(delayed && seconds>=due) {foreground_task_ptr fn=delayed;void *data=delayed_data;delayed=NULL;fn(data);}
  st_prep_buffer();
 }
 if(cancel_sent) {sys.abort=true;return false;}
 return true;
}
static void run(double speed,int direction,unsigned pass) {
 h5_cycle_config_t c={.operation=H5_THREAD,.passes=5,.starts=thread_starts,.pitch=.5*direction,.aux_forward=aux_forward,
  .x_min=0,.x_max=1,.z_min=0,.z_max=thread_starts>1?40:10,.rpm_limit=speed*1.25};
 h5_cycle_machine_t m={0,0,speed,100,960,200,25,settings.axis[X_AXIS].max_rate,1200};char error[96];
 assert(h5_cycle_plan(&c,&m,&profile,error,sizeof error));
 cancel_sent=false;
 base_rpm=rpm=speed;turns=seconds=phase_error=last_z_time=max_z_gap=0;pulses_x=pulses_z=blocks=0;timer_running=false;period=1000;
 memset(&sys,0,sizeof sys);sys.override.feed_rate=sys.override.rapid_rate=100;
 st_reset();
 double x,z;h5_thread_point(&profile,pass,0,&x,&z);
 sys.position[X_AXIS]=lround(x*1200);sys.position[Z_AXIS]=lround(z*200);
 assert(plan_reset());plan_sync_position();
 axes_available=false;assert(h5_thread_execute(&profile,pass)==Status_IdleError);
 axes_available=true;update_active=true;assert(h5_thread_execute(&profile,pass)==Status_IdleError);
 update_active=false;assert(!plan_get_current_block() && !pulses_x && !pulses_z);
 if(cancel_after!=0) {
  assert(h5_thread_execute(&profile,pass)==Status_IdleError && !timer_running);
  assert(sys.position[Z_AXIS]>=0 && sys.position[Z_AXIS]<=lround(profile.config.z_max*200));
  assert(sys.position[X_AXIS]>=-600 && sys.position[X_AXIS]<=1200);
  if(cancel_after<0)assert(!pulses_x && !pulses_z);
  puts("PASS: cancellation during index wait / continuous motion remains bounded");return;
 }
 assert(h5_thread_execute(&profile,pass)==Status_OK);
 assert(blocks==H5_THREAD_BLOCKS);
 assert(pulses_z==lround(fabs(profile.finish-profile.approach)*200));
 assert(sys.position[X_AXIS]==lround(profile.clearance*1200));
 assert(sys.position[Z_AXIS]==lround(profile.finish*200));
 assert((first_x_z-profile.entry_begin)*direction>=-.0051);
 assert((profile.exit_end-last_x_z)*direction>=-.0051);
 printf("rpm %.0f dir %d pass %u: %u blocks, X/Z %u/%u, peak phase %.6f mm\n",rpm,direction,pass,blocks,pulses_x,pulses_z,phase_error);
 assert(phase_error<.02);
 assert(max_z_gap<3*60/(.8*base_rpm*profile.lead*200));
}
int main(void) {
 settings.planner_buffer_blocks=100;settings.junction_deviation=.01;settings.steppers.idle_lock_time=255;
 settings.axis[0].steps_per_mm=1200;settings.axis[2].steps_per_mm=200;settings.axis[1].steps_per_mm=200;
 settings.axis[0].max_rate=300;settings.axis[2].max_rate=960;settings.axis[1].max_rate=960;
 settings.axis[0].acceleration=25*3600;settings.axis[2].acceleration=100*3600;settings.axis[1].acceleration=100*3600;
 settings.position.pid.p_gain=.25;
 hal.set_bits_atomic=set_bits;hal.f_step_timer=10000000;hal.stepper.enable=enable;hal.stepper.go_idle=idle;hal.stepper.wake_up=wake;
 hal.stepper.cycles_per_tick=cycles;hal.stepper.pulse_start=pulse;hal.spindle_data.get=get_spindle;
 hal.driver_cap.spindle_encoder=On;settings.spindle.ppr=1200;
 static spindle_ptrs_t driver;driver.get_data=get_spindle;spindle.hal=&driver;spindle.state.on=On;
 st_spindle_sync_cfg(&settings,(settings_changed_flags_t){0});
 for(unsigned speed=50;speed<=500;speed+=50)
  for(int d=-1;d<=1;d+=2)for(unsigned pass=0;pass<5;pass+=4)run(speed,d,pass);
 slew=5;run(100,1,4);slew=-5;run(100,-1,4);
 slew=0;settings.axis[0].max_rate=60;run(100,1,4);settings.axis[0].max_rate=300;
 aux_forward=false;run(100,1,4);thread_starts=3;run(100,-1,4);
 thread_starts=1;aux_forward=true;cancel_after=-1;run(100,1,4);cancel_after=4;run(100,-1,4);
 puts("PASS: actual grblHAL planner/segments/ISR: clear run-up, moving entry/exit, endpoints and continuous phase");
}
