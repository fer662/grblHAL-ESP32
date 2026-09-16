// Render production LVGL widgets and exercise touch input without any hardware.
#include "NormalOperationMode.h"
#include "StateMachine.h"
#include "display.h"
#include "main.h"
#include "bridge.h"
#include <cassert>
#include <fstream>
#include <vector>

int mode=MODE_NORMAL, measure=MEASURE_METRIC, turnPasses=3, starts=2;
bool isOn=false, auxForward=false, buzzerEnabled=false, jogContinuous=true;
bool jogLimitsEnabled=true;
static bool limitsEditable=true;
long dupr=1000, moveStep=MOVE_STEP_1;
float coneRatio=.25f;
PitchType pitchType=PITCH_TYPE_MM_PER_TURN;
Axis x={'X',1200,10000}, z={'Z',400,20000};
static double work_offset[3];
const char *h5_ui_work_system() {return "G54";}
double h5_ui_work_offset(Axis *a) {return work_offset[a==&x?0:2];}
struct Jog {char axis; int sign; bool pressed;};
static std::vector<Jog> jogs;
extern "C" int64_t esp_timer_get_time() {return (int64_t)lv_tick_get()*1000;}
extern "C" void h5_audio_tone(unsigned,unsigned) {}
extern "C" bool h5_cycle_busy() {return false;}
extern "C" bool h5_cycle_advance() {return true;}
extern "C" bool h5_axis_change_pending() {return false;}
void h5_ui_sync() {
    static lv_obj_t *status;
    if (!status) {
        status=lv_label_create(lv_scr_act());
        lv_obj_set_width(status,1232);
        lv_obj_set_style_text_font(status,&lv_font_montserrat_18,0);
        lv_obj_set_style_text_color(status,lv_color_hex(0xFF9800),0);
        lv_obj_set_style_text_align(status,LV_TEXT_ALIGN_CENTER,0);
        lv_label_set_text(status,"Ready  |  Axis controls enabled  |  G54  |  Tap for diagnostics");
        lv_obj_align(status,LV_ALIGN_BOTTOM_MID,0,-18);
    }
}
void h5_ui_show_update() {}
void h5_ui_jog(Axis *a,int sign,bool pressed) {jogs.push_back({a->name,sign,pressed});}
void setModeFromTask(int value) {mode=value;isOn=false;}
void setDupr(long v) {dupr=v;}
void setTurnPasses(int v) {turnPasses=v;}
void setStarts(int v) {starts=v;}
void setAuxForward(bool v) {auxForward=v;}
void setConeRatio(float v) {coneRatio=v;}
void buttonOnOffPress(bool v) {isOn=v;}
void buttonMoveStepPress() {moveStep=MOVE_STEP_3;}
void buttonMeasurePress() {measure=measure==MEASURE_METRIC?MEASURE_INCH:MEASURE_METRIC;}
void markAxis0(Axis *a) {work_offset[a==&x?0:2]=a->pos/(a==&x?1200.0:200.0);}
void manualMoveAxis(Axis *,float) {}
void setAxisDisabled(Axis *a,bool v) {a->disabled=v;}
void setLeftStop(Axis *a,long v) {a->leftStop=v;}
void setRightStop(Axis *a,long v) {a->rightStop=v;}
bool isPassMode() {return mode==MODE_TURN || mode==MODE_FACE || mode==MODE_CUT || mode==MODE_THREAD || mode==MODE_ELLIPSE;}
int getApproxRpm() {return 420;}
static double steps(Axis *a) {return a==&x?1200:200;}
bool h5_ui_limits_editable() {return limitsEditable;}
bool h5_ui_set_jog_limits(bool enabled) {if(!limitsEditable)return false;jogLimitsEnabled=enabled;return true;}
double h5_ui_limit_coordinate(Axis *a,long value) {double mm=(double)value/steps(a)-h5_ui_work_offset(a);return measure==MEASURE_METRIC?mm:mm/25.4;}
bool h5_ui_limit_steps(Axis *a,double value,long *out) {
    double raw=(value*(measure==MEASURE_METRIC?1:25.4)+h5_ui_work_offset(a))*steps(a);
    if(!std::isfinite(raw)||raw<=INT32_MIN||raw>=INT32_MAX)return false;
    *out=lround(raw);return true;
}
const char *h5_ui_apply_limits(const long v[4]) {
    if(!limitsEditable)return "Stop motion and assisted operations before applying limits.";
    if(v[0]!=LONG_MIN&&v[1]!=LONG_MAX&&v[0]>=v[1])return "X- must be less than X+. Clear an endpoint to leave it unset.";
    if(v[2]!=LONG_MIN&&v[3]!=LONG_MAX&&v[2]>=v[3])return "Z- must be less than Z+. Clear an endpoint to leave it unset.";
    x.rightStop=v[0];x.leftStop=v[1];z.rightStop=v[2];z.leftStop=v[3];return nullptr;
}
String getAxisPos(Axis *a) {return format_decimal(h5_ui_limit_coordinate(a,a->pos),3);}
String getAxisLeftStop(Axis *a) {return a->leftStop==LONG_MAX?"-":format_decimal(h5_ui_limit_coordinate(a,a->leftStop),3);}
String getAxisRightStop(Axis *a) {return a->rightStop==LONG_MIN?"-":format_decimal(h5_ui_limit_coordinate(a,a->rightStop),3);}
String getAxisStopDiff(Axis *a) {return format_decimal((a->leftStop-a->rightStop)/steps(a),3);}
static lv_point_t pointer;
static bool down;
static void read_pointer(lv_indev_drv_t *,lv_indev_data_t *data) {data->point=pointer;data->state=down?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;}
static void touch(int x,int y,bool pressed) {pointer={(lv_coord_t)x,(lv_coord_t)y};down=pressed;lv_tick_inc(40);lv_timer_handler();}
static lv_obj_t *find(lv_obj_t *o,const char *text) {
    if(!lv_obj_is_visible(o))return nullptr;
    if(lv_obj_check_type(o,&lv_label_class) && !strcmp(lv_label_get_text(o),text))return o;
    for(int i=(int)lv_obj_get_child_cnt(o)-1;i>=0;i--) if(auto found=find(lv_obj_get_child(o,i),text))return found;
    return nullptr;
}
static lv_point_t center(const char *text) {
    lv_obj_update_layout(lv_scr_act());
    auto o=find(lv_scr_act(),text);assert(o);lv_area_t a;lv_obj_get_coords(o,&a);
    return {(lv_coord_t)((a.x1+a.x2)/2),(lv_coord_t)((a.y1+a.y2)/2)};
}
static void click(const char *text) {auto p=center(text);touch(p.x,p.y,true);touch(p.x,p.y,false);}
static void row_click(const char *name,unsigned action) {
    auto label=find(lv_scr_act(),name);assert(label);
    auto button=lv_obj_get_child(lv_obj_get_parent(label),action);lv_area_t a;
    lv_obj_get_coords(button,&a);int x=(a.x1+a.x2)/2,y=(a.y1+a.y2)/2;
    touch(x,y,true);touch(x,y,false);
}
static void coordinate(const char *name,const char *number) {
    row_click(name,1);
    for(const char *c=number;*c;c++) {char s[]={*c,0};click(*c=='-'?"+/-":s);}
    click("Enter");
}
static void collect_buttons(lv_obj_t *o,std::vector<lv_area_t>& areas) {
    if(!lv_obj_is_visible(o))return;
    if(lv_obj_check_type(o,&lv_btn_class)) {
        lv_area_t a;lv_obj_get_coords(o,&a);
        assert(a.x1>=0 && a.y1>=0 && a.x2<1280 && a.y2<800);
        for(auto b:areas) assert(a.x1>b.x2 || b.x1>a.x2 || a.y1>b.y2 || b.y1>a.y2);
        areas.push_back(a);
    }
    for(unsigned i=0;i<lv_obj_get_child_cnt(o);i++)collect_buttons(lv_obj_get_child(o,i),areas);
}
static void check_main_layout() {
    lv_obj_update_layout(lv_scr_act());std::vector<lv_area_t> areas;collect_buttons(lv_scr_act(),areas);
}
static void render(const char *name) {
    lv_obj_update_layout(lv_scr_act());
    auto snap=lv_snapshot_take(lv_scr_act(),LV_IMG_CF_TRUE_COLOR);assert(snap);
    std::ofstream out(name,std::ios::binary);out<<"P6\n"<<snap->header.w<<" "<<snap->header.h<<"\n255\n";
    auto pixels=reinterpret_cast<const lv_color_t *>(snap->data);
    for(unsigned i=0;i<snap->header.w*snap->header.h;i++) {
        lv_color32_t c; c.full=lv_color_to32(pixels[i]);
        char rgb[]={(char)c.ch.red,(char)c.ch.green,(char)c.ch.blue};out.write(rgb,3);
    }
    lv_snapshot_free(snap);
}
int main() {
    lv_init();static lv_color_t buffer[1280*20];static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw,buffer,nullptr,1280*20);
    lv_disp_drv_t driver;lv_disp_drv_init(&driver);driver.hor_res=1280;driver.ver_res=800;driver.draw_buf=&draw;
    driver.flush_cb=[](lv_disp_drv_t *d,const lv_area_t *,lv_color_t *){lv_disp_flush_ready(d);};lv_disp_drv_register(&driver);
    lv_indev_drv_t input;lv_indev_drv_init(&input);input.type=LV_INDEV_TYPE_POINTER;input.read_cb=read_pointer;lv_indev_drv_register(&input);
    Display display;display.begin();StateMachine screens(display);
    screens.switchMode(screens.createNormalOperationMode());screens.updateDisplay();
    check_main_layout();render("gearbox.ppm");
    click("JOG MODE\n\nHOLD");assert(!jogContinuous);check_main_layout();render("single-step.ppm");
    click("JOG MODE\n\nSINGLE\nSTEP");assert(jogContinuous);
    click("STEP");assert(moveStep==MOVE_STEP_3);screens.updateDisplay();
    // Rapids keeps the existing jog-mode preference but visibly forces Hold.
    moveStep=MOVE_STEP_RAPIDS;jogContinuous=false;screens.updateDisplay();
    check_main_layout();render("rapids.ppm");
    click("JOG MODE\n\nHOLD");assert(!jogContinuous && moveStep==MOVE_STEP_RAPIDS);
    click("RAPIDS");screens.updateDisplay();assert(moveStep==MOVE_STEP_3 && !jogContinuous);
    click("JOG MODE\n\nSINGLE\nSTEP");assert(jogContinuous);
    moveStep=MOVE_STEP_1;screens.updateDisplay();
    const char *labels[]={LV_SYMBOL_UP "  X+",LV_SYMBOL_DOWN "  X-",LV_SYMBOL_LEFT "  Z+","Z-  " LV_SYMBOL_RIGHT};
    const char axes[]={'X','X','Z','Z'};const int signs[]={1,-1,1,-1};
    for(unsigned i=0;i<4;i++) {
        jogs.clear();click(labels[i]);assert(jogs.size()==2);
        assert(jogs[0].axis==axes[i] && jogs[0].sign==signs[i] && jogs[0].pressed && !jogs[1].pressed);
    }
    // Drift directly from one direction to another: cancel, then await a lift.
    jogs.clear();auto up=center(labels[0]),left=center(labels[2]);
    touch(up.x,up.y,true);touch(left.x,left.y,true);touch(left.x,left.y,true);touch(left.x,left.y,false);
    assert(jogs.size()==2 && jogs[0].axis=='X' && !jogs[1].pressed);
    // Sliding into the new center control must not change jog mode.
    auto middle=center("JOG MODE\n\nHOLD");jogs.clear();
    touch(up.x,up.y,true);touch(middle.x,middle.y,true);touch(middle.x,middle.y,false);
    assert(jogContinuous && jogs.size()==2 && !jogs[1].pressed);
    jogs.clear();click("X+ limit\n-");assert(jogs.empty() && x.leftStop==x.pos);
    screens.updateDisplay();
    // Disabled axes cannot generate touch-driven jogs; Z still works.
    x.disabled=true;screens.updateDisplay();jogs.clear();click(labels[0]);assert(jogs.empty());
    click(labels[2]);assert(jogs.size()==2);render("disabled.ppm");x.disabled=false;
    const char *modes[]={"Async","Cone","Turn","Face","Cut","Thread","Ellipse","Gearbox"};
    const char *selected="Gearbox";
    for(auto name:modes) {click(selected);click(name);screens.updateDisplay();check_main_layout();selected=name;}
    click("Gearbox");click("Thread");screens.updateDisplay();render("thread.ppm");
    click("Thread");render("modes.ppm");click("Cancel");
    click("SHIFT OFF");jogs.clear();click(labels[0]);
    for(auto j:jogs)assert(!j.pressed);
    render("keypad.ppm");click("Cancel");
    click("X+ limit\n0.000");click("1");click(".");click("2");click("5");click("Enter");
    assert(x.leftStop==1500);for(auto j:jogs)assert(!j.pressed);
    // Toggle preserves all saved endpoints and refuses a change while busy.
    limitsEditable=false;click("JOG LIMITS\n\nON");assert(jogLimitsEnabled);
    limitsEditable=true;click("JOG LIMITS\n\nON");assert(!jogLimitsEnabled&&x.leftStop==1500);
    screens.updateDisplay();assert(find(lv_scr_act(),"X+ OFF\n1.250"));render("limits-off.ppm");
    click("JOG LIMITS\n\nOFF");assert(jogLimitsEnabled);
    // Cancel discards clears. Input uses signed display coordinates and zero offsets.
    click("EDIT\nLIMITS");row_click("X+",3);assert(x.leftStop==1500);click("Cancel");assert(x.leftStop==1500);
    work_offset[0]=1;work_offset[2]=-1;
    click("EDIT\nLIMITS");row_click("X-",1);click("+/-");click("2");
    render("limit-keypad.ppm");click("Enter");
    coordinate("X+","3");coordinate("Z-","-10");row_click("Z+",2);
    assert(x.leftStop==1500&&x.rightStop==LONG_MIN&&z.rightStop==LONG_MIN);
    assert(find(lv_scr_act(),"X span\n5.000 mm")&&find(lv_scr_act(),"Z span\n11.000 mm"));
    render("limit-editor.ppm");
    limitsEditable=false;click("Apply");assert(find(lv_scr_act(),"Machining limits")&&x.leftStop==1500);
    limitsEditable=true;click("Apply");assert(x.rightStop==-1200&&x.leftStop==4800&&z.rightStop==-2200&&z.leftStop==0);
    click("EDIT\nLIMITS");coordinate("X+","-3");click("Apply");assert(find(lv_scr_act(),"Machining limits")&&x.leftStop==4800);click("Cancel");
    measure=MEASURE_INCH;click("EDIT\nLIMITS");coordinate("Z-","-1");click("Apply");assert(z.rightStop==-5280);
    measure=MEASURE_METRIC;click("EDIT\nLIMITS");for(auto axis:{"X-","X+","Z-","Z+"})row_click(axis,3);click("Apply");
    assert(x.rightStop==LONG_MIN&&x.leftStop==LONG_MAX&&z.rightStop==LONG_MIN&&z.leftStop==LONG_MAX);
    // A work offset changed by a sender must not reinterpret an open editor draft.
    click("EDIT\nLIMITS");coordinate("X+","3");work_offset[0]+=1;
    click("Apply");assert(x.leftStop==LONG_MAX);
    assert(find(lv_scr_act(),"Work zero or units changed. Cancel and reopen the editor."));click("Cancel");
    click("EDIT\nLIMITS");row_click("X+",1);work_offset[2]+=1;
    click("2");click("Enter");assert(x.leftStop==LONG_MAX);
    assert(find(lv_scr_act(),"Coordinate out of range or work zero/units changed. Cancel and reopen the editor."));click("Cancel");
    work_offset[0]=work_offset[2]=0;
    click("Thread");click("Settings");render("settings.ppm");click("CLOSE");
    click("PITCH");render("pitches.ppm");
    puts("PASS: four jog directions, release, slide-out cancellation, limit separation, disabled axes, all eight mode selectors");
}
