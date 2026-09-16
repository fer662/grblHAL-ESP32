/* Run the actual core planner, segment generator and step ISR on a virtual clock. */
#include "grbl/hal.h"
#include "grbl/stepper.h"
#include "grbl/state_machine.h"
#include "grbl/task.h"
#include "grbl/ioports.h"
#include "cycle_plan.h"
#include "thread_entry.h"
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
static double seconds, rpm, phase_error;
static bool timer_running;
static uint32_t period;
static unsigned blocks, pulses_x, pulses_z;
static h5_cycle_plan_t profile;
static double phase_margin;
static double last_x_time, first_z_time, z_block_time, entry_time, maximum_entry_error;
static int32_t depth_steps;
static unsigned phase_waits;
static double initial_lag, turns, base_rpm, slew;
static foreground_task_ptr delayed;
static void *delayed_data;
static double due, cancel_after;
static bool cancel_sent,aux_forward=true;
static unsigned thread_starts=1;
#define IRAM_ATTR
static bool entry_armed, entry_cutting, waiting, tracking;
static double entry_seconds, entry_lead, entry_acceleration;
static int64_t entry_advance, origin, furthest, first_edge, last_edge;
static uint32_t phase,index_phase,block_pulses,axis_pulses[2],sample_count;
static unsigned trace_axis;
static float phase_compensation,lead_distance,block_pitch,block_steps_mm;
static int64_t oriented_position(void) {return (int64_t)floor(turns*1200);}
static int64_t floor_turn(int64_t c) {return c>=0?c/1200:-((-c+1199)/1200);}
float h5_spindle_profile_rpm(void) {return rpm;}
ENTRY_ARM
ENTRY_RESET
ENTRY_BLOCK
static spindle_data_t *get_spindle(spindle_data_request_t request) {
 (void)request;
 spindle_data.rpm=rpm;
 spindle_data.angular_position=(float)(oriented_position()-origin)/1200;
 spindle_data.index_count=(uint32_t)floor_turn(oriented_position()-index_phase);
 return &spindle_data;
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
 if(s->new_block) {
  blocks++;
  h5_spindle_block(s);
  if(s->exec_block->sync_preloaded) {
   z_block_time=seconds;
   assert(sys.position[X_AXIS]==depth_steps);
   double error=fabs((oriented_position()-origin)/1200.0*profile.lead);
   if(error>maximum_entry_error)maximum_entry_error=error;
  } else if(s->exec_block->sync_preload) entry_time=seconds;
 }
 if(s->step_out.x) {assert(!pulses_z);pulses_x++;last_x_time=seconds;}
 if(s->step_out.z) {assert(sys.position[X_AXIS]==depth_steps);if(!pulses_z)first_z_time=seconds;pulses_z++;}
 double z=sys.position[Z_AXIS]/settings.axis[Z_AXIS].steps_per_mm;
 if(pulses_z && (z-profile.approach)*profile.direction>=phase_margin && (profile.finish-z)*profile.direction>=phase_margin) {
  double error=fabs(z-profile.approach)+initial_lag-spindle_data.angular_position*profile.lead;
  if(fabs(error)>phase_error)phase_error=fabs(error);
 }
}

static bool reject_target;
void limits_soft_check(float *target,planner_cond_t condition) {
 (void)condition;
 if(reject_target && target[Z_AXIS]!=sys.position[Z_AXIS]/settings.axis[Z_AXIS].steps_per_mm)sys.abort=true;
}
void system_convert_array_steps_to_mpos(float *target,int32_t *steps) {
 for(unsigned i=0;i<N_AXIS;i++)target[i]=steps[i]/settings.axis[i].steps_per_mm;
}
void mc_override_ctrl_update(gc_override_flags_t flags) {sys.override.control=flags;}
static void advance_time(double dt) {
 double next_rpm=fmax(.8*base_rpm,fmin(1.2*base_rpm,base_rpm+slew*(seconds+dt)));
 turns+=dt*(rpm+next_rpm)/120;rpm=next_rpm;seconds+=dt;
}
static uint32_t ticks(void) {return seconds*1000;}
static void realtime(sys_state_t state) {
 (void)state;advance_time(.0001);
 assert(!pulses_x && !pulses_z); // All index waiting happens with the tool clear.
 if(cancel_after<0 && seconds>.05)sys.rt_exec_state|=EXEC_STOP;
}
void system_raise_alarm(alarm_code_t alarm) {(void)alarm;assert(!"Unexpected alarm");}
static void acquire(plan_block_t *block) {
 sys_state_t sys_state=STATE_CYCLE;
 phase_waits++;
 INDEX_WAIT
}
bool protocol_buffer_synchronize(void) {
 assert(plan_get_block_buffer_available()==98);
 plan_block_t *entry=plan_get_current_block();
 assert(entry->sync_preload && !entry->spindle.state.synchronized);
 assert(entry->next->sync_preloaded && entry->next->spindle.state.synchronized);
 assert(entry->next->entry_speed_sqr==0 && entry->next->max_entry_speed_sqr==0);
 initial_lag=pow(profile.lead*rpm/60,2)/(2*settings.axis[Z_AXIS].acceleration/3600);
 st_prep_buffer();acquire(entry);
 if(sys.rt_exec_state&EXEC_RESET) {sys.abort=true;return false;}
 st_wake_up();
 unsigned interrupts=0;
 while(timer_running) {
  assert(++interrupts<20000000);
  advance_time((double)period/hal.f_step_timer);
  if(cancel_after>0 && seconds-entry_time>=cancel_after && entry_time>0 && !cancel_sent) {
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
static void run(double speed,int direction,unsigned pass,double clearance,unsigned phase_counts) {
 h5_cycle_config_t c={.operation=H5_THREAD,.passes=5,.starts=thread_starts,.pitch=.5*direction,.aux_forward=aux_forward,
  .x_min=0,.x_max=1,.z_min=0,.z_max=thread_starts>1?40:10};
 h5_cycle_machine_t m={0,0,speed,100,960,200,settings.axis[X_AXIS].acceleration/3600,settings.axis[X_AXIS].max_rate,1200};char error[96];
 assert(h5_cycle_plan(&c,&m,&profile,error,sizeof error));
 base_rpm=rpm=speed;turns=.137;seconds=phase_error=0;
 pulses_x=pulses_z=blocks=phase_waits=0;timer_running=cancel_sent=false;period=1000;
 last_x_time=first_z_time=entry_time=z_block_time=0;entry_armed=entry_cutting=false;phase=phase_counts;
 memset(&sys,0,sizeof sys);sys.override.feed_rate=sys.override.rapid_rate=100;
 st_reset();
 double x=h5_cycle_depth(&profile,pass), z=profile.approach;
 double v=profile.lead*speed/60;phase_margin=v*v/(2*m.z_acceleration)+.15;
 depth_steps=lround(x*1200);
 sys.position[X_AXIS]=lround((x+clearance)*1200);sys.position[Z_AXIS]=lround(z*200);
 unsigned expected_x=abs(sys.position[X_AXIS]-depth_steps);
 assert(plan_reset());plan_sync_position();
 if(speed==50 && pass==0 && direction==1) {
  assert(h5_thread_entry_execute(x,profile.finish,-1)==Status_GcodeMaxFeedRateExceeded);
  assert(!plan_get_current_block());
  reject_target=true;
  assert(h5_thread_entry_execute(x,profile.finish,profile.lead)==Status_IdleError);
  assert(!plan_get_current_block());reject_target=false;sys.abort=false;
 }
 assert(h5_thread_entry_execute(x,profile.finish,profile.lead)==Status_OK);
 bool done=!sys.abort;
 if(cancel_after!=0) {
  assert(!done && !timer_running);
  assert(sys.position[Z_AXIS]>=0 && sys.position[Z_AXIS]<=lround(c.z_max*200));
  if(cancel_after<0)assert(!pulses_x && !pulses_z);
  if(cancel_after==.02)assert(!pulses_z && !entry_cutting);
  return;
 }
 assert(!sys.override.control.feed_rates_disable && !sys.override.control.feed_hold_disable);
 assert(done && blocks==2 && phase_waits==1 && entry_cutting);
 assert(pulses_x==expected_x && sys.position[X_AXIS]==depth_steps);
 assert(pulses_z==lround(fabs(profile.finish-profile.approach)*200));
 assert(sys.position[Z_AXIS]==lround(profile.finish*200));
 // At hand-turn RPM a physical Z step itself takes longer than 50 ms.
 double first_step_allowance=.05+60/(speed*profile.lead*settings.axis[Z_AXIS].steps_per_mm);
 assert(first_z_time>=last_x_time && first_z_time-last_x_time<first_step_allowance);
 // Compare the expected absolute spindle phase too, not merely the local PID origin.
 double absolute_phase=fmod((double)origin/1200+initial_lag/profile.lead-(double)phase/1200,1.0);
 absolute_phase=fmin(fabs(absolute_phase),fabs(1-fabs(absolute_phase)));
 assert(absolute_phase*profile.lead<.002);
 if(phase_error>=.02)fprintf(stderr,"FAIL speed=%g dir=%d pass=%u Xacc=%g clear=%g slew=%g phase_error=%g entry_duration=%g actual=%g gap=%g\n",speed,direction,pass,m.x_acceleration,clearance,slew,phase_error,entry_seconds,z_block_time-entry_time,first_z_time-last_x_time);
 assert(phase_error<.02);
 printf("entry rpm %.0f pass %u Xacc %.0f clear %.3f: X/Z %u/%u, handoff %.3f ms, phase %.6f mm\n",speed,pass,m.x_acceleration,clearance,pulses_x,pulses_z,1000*(first_z_time-last_x_time),phase_error);
}
int main(void) {
 settings.planner_buffer_blocks=100;settings.junction_deviation=.01;settings.steppers.idle_lock_time=255;
 settings.axis[0].steps_per_mm=1200;settings.axis[1].steps_per_mm=settings.axis[2].steps_per_mm=200;
 settings.axis[0].max_rate=300;settings.axis[1].max_rate=settings.axis[2].max_rate=960;
 settings.axis[0].acceleration=25*3600;settings.axis[1].acceleration=settings.axis[2].acceleration=100*3600;
 settings.position.pid.p_gain=.25;
 hal.set_bits_atomic=set_bits;hal.f_step_timer=10000000;hal.get_elapsed_ticks=ticks;
 hal.stepper.enable=enable;hal.stepper.go_idle=idle;hal.stepper.wake_up=wake;
 hal.stepper.cycles_per_tick=cycles;hal.stepper.pulse_start=pulse;hal.spindle_data.get=get_spindle;
 grbl.on_execute_realtime=realtime;
 hal.driver_cap.spindle_encoder=On;settings.spindle.ppr=1200;
 static spindle_ptrs_t driver;driver.get_data=get_spindle;driver.reset_data=reset_data;
 spindle.hal=&driver;spindle.state.on=On;
 st_spindle_sync_cfg(&settings,(settings_changed_flags_t){0});
 for(unsigned speed=50;speed<=500;speed+=50)
  for(int d=-1;d<=1;d+=2)for(unsigned pass=0;pass<5;pass++)run(speed,d,pass,-.5-(pass+1)/5.0,317);
 for(unsigned speed=1;speed<30;speed+=4)run(speed,1,0,-.7,1199);
 for(unsigned pass=0;pass<5;pass++)run(451,1,pass,-.5-(pass+1)/5.0,0);
 for(unsigned a=0;a<3;a++) {
  settings.axis[0].acceleration=(a==0?10:a==1?25:500)*3600;
  run(451,1,4,-1.5,0);run(451,-1,4,.003,601);run(451,1,4,-.03,37);
 }
 settings.axis[0].acceleration=25*3600;
 slew=5;run(100,1,4,-1.5,0);slew=-5;run(100,-1,4,-1.5,0);slew=0;
 aux_forward=false;run(100,1,4,1.5,0);thread_starts=3;
 for(unsigned start=0;start<3;start++)run(100,-1,4,1.5,start*400);
 thread_starts=1;aux_forward=true;
 cancel_after=-1;run(100,1,4,-1.5,0);
 cancel_after=.02;run(100,1,4,-1.5,0);
 cancel_after=1;run(100,-1,4,-1.5,0);
 assert(maximum_entry_error<.02);
 printf("PASS: queued native entry/cut, clear-tool index wait, exact-stop handoff, endpoints, both X/Z directions, phases, rate changes, cancellation; maximum entry phase error %.6f mm\n",maximum_entry_error);
}
