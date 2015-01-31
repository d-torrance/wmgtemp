#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/types.h>
#include <X11/Xlib.h>
#include <X11/xpm.h>
#include <X11/extensions/shape.h>
#include <sensors/sensors.h>
#include <sensors/chips.h>
#include <sensors/error.h>
#include "wmgeneral/wmgeneral.h"
#include "wmgeneral/misc.h"
#include "wmgtemp-interface.xpm"
#include "wmgtemp-interface-mask.xbm"
#include <math.h>
#include <signal.h>
#include <getopt.h>

/* Defines */
#define BitOff(a,x)  ((void)((a) &= ~(1 << (x))))
#define BitOn(a,x)   ((void)((a) |=  (1 << (x))))
#define BitFlip(a,x) ((void)((a) ^=  (1 << (x))))
#define IsOn(a,x)    ((a) & (1 << (x)))

// Display flags.
#define CPU               0
#define SYS               1
#define WARN_NONE         2
#define WARN_WARN         3
#define WARN_HIGH         4
#define TSCALE_CELCIUS    5
#define TSCALE_FAHRENHEIT 6
#define TSCALE_KELVIN     7
#define GRAPH_LINE        8
#define GRAPH_BLOCK       9
#define HIGH_CPU         10
#define HIGH_SYS         11

#define D_MIN 0
#define D_MAX 1

#define CPU_YPOS 3
#define SYS_YPOS 53

#define BLOCK 0
#define LINE  1

#define DEBUG 0 /* 0 disable 1 enable  */

// Local sensors type defines.
#define VIA686A  0
#define W83781D  1
#define W83627HF 2
#define AS99127F 3
#define ADM1021  4

#define OPT_STRING	"g:s:hH:w:m:M:a:e:u:t"

#define TEMPTOFAHRENHEIT(t) ((int)((t * (1.8) + 32)))
#define TEMPTOKELVIN(t)     ((int)(t + 273))
#define TEMPTOCELCIUS(t)    (t)
#define TEMPTODISPLAYSCALE(temp, display_flags) (IsOn((display_flags), TSCALE_CELCIUS) ? TEMPTOCELCIUS((temp)) : (IsOn((display_flags), TSCALE_KELVIN) ? TEMPTOKELVIN((temp)) : TEMPTOFAHRENHEIT((temp))))

/* Prototypes */
int init_sensors();
int process_config(int argc, char **argv);
int recompute_range(double cpu_high, double cpu_low, double sys_high, double sys_low);
void display_usage();
void process_xevents();
void draw_scale_indicator();
void add_to_graph(double temp, int type, short blank, double range, int pos);
void draw_range_line(double temp, double range, short type);
void update_display();
void update_sensor_data();
void do_sensors(int val);
inline double highest_temp(double *temp_array);
inline double lowest_temp(double *temp_array);
inline void draw_temp(short value, int type);
inline void draw_warning_lights(double current_temp);
inline void draw_max(int type);
inline void blank_max(int type);
inline void draw_type(int type);
inline void blank_type(int type);
inline void cycle_temptype();

/* Globals */
int delay      = 1;
short chiptype = -1;	
const sensors_chip_name *name;
char *exec_app = NULL;

short SENSOR_DISP    = 0;
short SENSOR_DEF_CPU = 0;
short SENSOR_DEF_SYS = 0;

double cpu_history[59];
double sys_history[59];

double display_min = 20;
double display_max = 35;

double range_upper = 35;
double range_lower = 20;
double range_step  = 5.0;

double warn_temp = 45;
double high_temp = 50;

double run_cpu_high = 0;
double run_sys_high = 0;

double execat       = 0;
short  execed       = 0;
short  swap_types   = 0;

int main(int argc, char **argv) {
  char *chipname = NULL;
  int chip_nr = 0;
  int i = 0;
  short tmp_swap;

  BitOn(SENSOR_DISP, WARN_NONE);
  BitOn(SENSOR_DISP, TSCALE_CELCIUS);
  BitOn(SENSOR_DISP, GRAPH_LINE);

  if(!process_config(argc, argv)) {
    exit(-1);
  }
  
  if(!init_sensors()) {
    printf("wmgtemp: Error initilising lm_sensors.");
    exit(-1);
  }
  
  /* Get the chip name */
  name = sensors_get_detected_chips(&chip_nr);
  while(chiptype == -1 && name != NULL) {
    if(!strcmp(name->prefix, SENSORS_VIA686A_PREFIX)) {
      /* set the chip name */
      chipname = SENSORS_VIA686A_PREFIX;
      BitOn(SENSOR_DISP, CPU);
      BitOn(SENSOR_DISP, SYS);
      SENSOR_DEF_CPU = SENSORS_VIA686A_TEMP2;
      SENSOR_DEF_SYS = SENSORS_VIA686A_TEMP;
      chiptype = VIA686A;

      /*
      sensors_get_feature(*name, SENSORS_VIA686A_TEMP2_OVER, &cpu_limit);
      sensors_get_feature(*name, SENSORS_VIA686A_TEMP_OVER, &sys_limit);
      printf("cpu limit: %f", cpu_limit);
      printf("sys limit: %f", sys_limit);
      */
    }
    else if(!strcmp(name->prefix, SENSORS_ADM1021_PREFIX)) {
      chipname = SENSORS_ADM1021_PREFIX;
      BitOn(SENSOR_DISP, CPU);
      BitOn(SENSOR_DISP, SYS);
      SENSOR_DEF_CPU = SENSORS_ADM1021_REMOTE_TEMP;
      SENSOR_DEF_SYS = SENSORS_ADM1021_TEMP;
      chiptype = ADM1021;
    }
    else if(!strcmp(name->prefix, SENSORS_W83781D_PREFIX)) {
      chipname = SENSORS_W83781D_PREFIX;
      BitOn(SENSOR_DISP, CPU);
      BitOn(SENSOR_DISP, SYS);
      SENSOR_DEF_CPU = SENSORS_W83781D_TEMP2;
      SENSOR_DEF_SYS = SENSORS_W83781D_TEMP1;
      chiptype = W83781D;
    }
    else if(!strcmp(name->prefix, SENSORS_AS99127F_PREFIX)) {
      chipname = SENSORS_AS99127F_PREFIX;
      BitOn(SENSOR_DISP, CPU);
      BitOn(SENSOR_DISP, SYS);
      SENSOR_DEF_CPU = SENSORS_W83781D_TEMP2;
      SENSOR_DEF_SYS = SENSORS_W83781D_TEMP1;
      chiptype = AS99127F;
    }
    else if(!strcmp(name->prefix, SENSORS_W83627HF_PREFIX)) {
      /* The lm_sensors W83781D driver is compatible with the Winbond W83627HF chip 
	 so we'll use the same options, at least until the chips.h header file includes
	 the W83627HF's defines -- we could provide these ourselves but they may 
	 conflict with a later version of the sensors library */
      chipname = SENSORS_W83627HF_PREFIX;
      BitOn(SENSOR_DISP, CPU);
      BitOn(SENSOR_DISP, SYS);
      SENSOR_DEF_CPU = SENSORS_W83781D_TEMP2;
      SENSOR_DEF_SYS = SENSORS_W83781D_TEMP1;
      chiptype = W83627HF;
    }
    
    if(chiptype == -1) 
      name = sensors_get_detected_chips(&chip_nr);
  }
  if(chiptype == -1) {
    fprintf(stderr,"wmgtemp: Chip not supported.\n");
    exit(0);
  }

  /* output the name of the sensor if found. */
  printf("wmgtemp: Primary Sensor - %s on %s\n", name->prefix, sensors_get_adapter_name(name->bus));
  if(swap_types) {
    printf("wmgtemp: swapping temps\n");
    tmp_swap = SENSOR_DEF_SYS;
    SENSOR_DEF_SYS = SENSOR_DEF_CPU;
    SENSOR_DEF_CPU = tmp_swap;
  }

  chip_nr = 0;

  openXwindow(argc, argv, wmgtemp_interface_xpm, wmgtemp_interface_mask_bits,
	      wmgtemp_interface_mask_width, wmgtemp_interface_mask_height);

  AddMouseRegion(0, 2, 12, 61, 51);  /* Graph area */
  AddMouseRegion(1, 34, 2, 51, 11);  /* CPU temp area */
  AddMouseRegion(2, 34, 52, 51, 61); /* SYS temp area */
  AddMouseRegion(3, 10, CPU_YPOS, 28, CPU_YPOS + 7);  /* CPU label area */
  AddMouseRegion(4, 10, SYS_YPOS, 28, SYS_YPOS + 7); /* SYS label area */
  AddMouseRegion(5, 55, CPU_YPOS, 60, CPU_YPOS + 7); /* CPU C/K/F scale indicator */
  AddMouseRegion(6, 55, SYS_YPOS, 60, SYS_YPOS + 7); /* SYS C/K/F scale indicator */
  
  // Add blanking of SYS and CPU for chip type.
  // <<==---
  if(!IsOn(SENSOR_DISP, CPU)) {
    blank_type(CPU);
  }
  if(!IsOn(SENSOR_DISP, SYS)) {
    blank_type(SYS);
  }

  draw_scale_indicator();

  // Initialise the temperature arrays.
  for(i = 0; i < 59; i++) {
    cpu_history[i] = -1;
    sys_history[i] = -1;
  }

  do_sensors(0);
  RedrawWindow();
    
  // Set up signal handler
  signal(SIGALRM, do_sensors);
  alarm(delay);

  process_xevents();
  
  return 0;
}

void draw_scale_indicator() {
  if(IsOn(SENSOR_DISP, TSCALE_CELCIUS)) {
    if(IsOn(SENSOR_DISP, CPU))
      copyXPMArea(61, 65, 5, 7, 55, CPU_YPOS);
    if(IsOn(SENSOR_DISP, SYS))
      copyXPMArea(61, 65, 5, 7, 55, SYS_YPOS);
  }
  else if(IsOn(SENSOR_DISP, TSCALE_FAHRENHEIT)) {
    if(IsOn(SENSOR_DISP, CPU))
      copyXPMArea(67, 65, 5, 7, 55, CPU_YPOS);
    if(IsOn(SENSOR_DISP, SYS))
      copyXPMArea(67, 65, 5, 7, 55, SYS_YPOS);
  }
  else if(IsOn(SENSOR_DISP, TSCALE_KELVIN)) {
    if(IsOn(SENSOR_DISP, CPU))
      copyXPMArea(73, 65, 5, 7, 55, CPU_YPOS);
    if(IsOn(SENSOR_DISP, SYS))
      copyXPMArea(73, 65, 5, 7, 55, SYS_YPOS);
  }
}

void process_xevents() {
  int button_area = 0;

  XEvent Event;
  
  while(1) {
    while(XPending(display)) {
      XNextEvent(display, &Event);
      switch(Event.type) {
      case Expose:
	RedrawWindow();
	break;
      case DestroyNotify:
	XCloseDisplay(display);
	exit(0);
	break;
      case ButtonRelease:
	button_area = CheckMouseRegion(Event.xbutton.x, Event.xbutton.y);
	switch(button_area) {
	case 0:
	  if(IsOn(SENSOR_DISP, GRAPH_LINE)) {
	    BitOff(SENSOR_DISP, GRAPH_LINE);
	    BitOn(SENSOR_DISP, GRAPH_BLOCK);
	  }
	  else if(IsOn(SENSOR_DISP, GRAPH_BLOCK)) {
	    BitOff(SENSOR_DISP, GRAPH_BLOCK);
	    BitOn(SENSOR_DISP, GRAPH_LINE);
	  }
	  update_display();
	  RedrawWindow();
	  break;
	case 1:
	  if(IsOn(SENSOR_DISP, HIGH_CPU)) {
	    BitOff(SENSOR_DISP, HIGH_CPU);
	    blank_max(CPU);
	  } 
	  else {
	    BitOn(SENSOR_DISP, HIGH_CPU);
	    draw_max(CPU);
	  }
	  update_display();
	  RedrawWindow();
	  break;
	case 2:
	  if(IsOn(SENSOR_DISP, HIGH_SYS)) {
	    BitOff(SENSOR_DISP, HIGH_SYS);
	    blank_max(SYS);
	  } 
	  else {
	    BitOn(SENSOR_DISP, HIGH_SYS);
	    draw_max(SYS);
	  }
	  update_display();
	  RedrawWindow();
	  break;
	case 3:
	  if(SENSOR_DEF_CPU) {
	    if(IsOn(SENSOR_DISP, CPU)) {
	      BitOff(SENSOR_DISP, CPU); 
	      blank_type(CPU);
	    }
	    else {
	      BitOn(SENSOR_DISP, CPU);
	      draw_type(CPU);
	    }
	  }
	  update_display();
	  RedrawWindow();
	  break;
	case 4:
	  if(SENSOR_DEF_SYS) {
	    if(IsOn(SENSOR_DISP, SYS)) {
	      BitOff(SENSOR_DISP, SYS);
	      blank_type(SYS);
	    } 
	    else {
	      BitOn(SENSOR_DISP, SYS);
	      draw_type(SYS);
	    }
	  }
	  update_display();
	  RedrawWindow();
	  break;
	case 5:
	case 6:
	  cycle_temptype();
	  draw_scale_indicator();
	  update_display();
	  RedrawWindow();
	  break;
	}
	break;
      }
    }
    usleep(100000);
  }
}

void do_sensors(int val) {
  alarm(delay);

  update_sensor_data();
  update_display();
  RedrawWindow();
  
  if(execat != 0 && cpu_history[58] >= execat && !execed) {
    execed = 1;
    execCommand(exec_app);
  }
  if(execat != 0 && cpu_history[58] < execat && execed) {
    execed = 0;
  }
}


void update_sensor_data() {
  int i = 0;
  double cpu_high = highest_temp(cpu_history);
  double sys_high = highest_temp(sys_history);

  /* Shift the arrays */
  for(i = 0; i < 59; i++) {
    cpu_history[i] = cpu_history[i + 1];
    sys_history[i] = sys_history[i + 1];
  }
  
  // Read the new values from the sensors into the temperature arrays.
  sensors_get_feature(*name, SENSOR_DEF_SYS, &sys_history[58]);
  sensors_get_feature(*name, SENSOR_DEF_CPU, &cpu_history[58]);

  // Update the run high/low values.
  if(cpu_high > run_cpu_high)
    run_cpu_high = cpu_high;
  if(sys_high > run_sys_high)
    run_sys_high = sys_high;
}


void update_display() {
  int j = 0;


  // Rescale the display if needed.
  while(recompute_range(highest_temp(cpu_history), lowest_temp(cpu_history),
			highest_temp(sys_history), lowest_temp(sys_history)));

  // Display warning.
  draw_warning_lights(cpu_history[58]);
  
  // ReDraw temperature numbers
  if(SENSOR_DEF_CPU) {
    copyXPMArea(78, 65, 5, 7, 34, CPU_YPOS);
    copyXPMArea(78, 65, 5, 7, 40, CPU_YPOS);
    copyXPMArea(78, 65, 5, 7, 46, CPU_YPOS);
    draw_temp(TEMPTODISPLAYSCALE(IsOn(SENSOR_DISP, HIGH_CPU) == 0 ? cpu_history[58] : run_cpu_high, SENSOR_DISP), CPU);
  }
  if(SENSOR_DEF_SYS) {
    copyXPMArea(78, 65, 5, 7, 34, SYS_YPOS);
    copyXPMArea(78, 65, 5, 7, 40, SYS_YPOS);
    copyXPMArea(78, 65, 5, 7, 46, SYS_YPOS);
    draw_temp(TEMPTODISPLAYSCALE(IsOn(SENSOR_DISP, HIGH_SYS) == 0 ? sys_history[58] : run_sys_high, SENSOR_DISP), SYS);
  }


  // ReDraw the graph
  for(j = 0; j < 59; j++) {
    // Clear a line
    copyXPMArea(65, 0, 1, 39, j + 2, 12);
    
    // Draw the temperatures on the graph.
    if(IsOn(SENSOR_DISP, CPU)) {
      add_to_graph(cpu_history[j], CPU, 1, range_upper - range_lower, j + 2);
    }
    if(IsOn(SENSOR_DISP, SYS)) {
      add_to_graph(sys_history[j], SYS, 0, range_upper - range_lower, j + 2);
    }
  }
  
  // Draw range lines if needed
  if(range_upper > display_max) {
    draw_range_line(display_max, range_upper - range_lower, D_MAX);
  }
  if(range_lower < display_min) {
    draw_range_line(display_min, range_upper - range_lower, D_MIN);
  }
}

int recompute_range(double cpu_high, double cpu_low, double sys_high, double sys_low)
{
  short modified = 0;
  
  if(IsOn(SENSOR_DISP, CPU)) {
    if(cpu_high > range_upper) {
      range_upper += range_step;
      modified = 1;
    }
    if(cpu_low < range_lower) { 
      range_lower -= range_step;  
      modified = 1;  
    }  
  }
  if(IsOn(SENSOR_DISP, SYS)) {
    if(sys_high > range_upper) {
      range_upper += range_step;
      modified = 1;
    }
    if(sys_low < range_lower) {  
      range_lower -= range_step;  
      modified = 1;  
    }  
  }
  
  // --------
  if(IsOn(SENSOR_DISP, CPU) && IsOn(SENSOR_DISP, SYS)) {
    if((cpu_high < (range_upper - range_step) && sys_high < (range_upper - range_step)) && (range_upper - range_step) >= display_max) {
      range_upper -= range_step;
      modified = 1;
    }
    if((cpu_low > (range_lower + range_step) && sys_low > (range_lower + range_step)) && (range_lower + range_step) <= display_min ) {  
      range_lower += range_step;  
      modified = 1;  
    }  
  }
  else if(IsOn(SENSOR_DISP, CPU) && !IsOn(SENSOR_DISP, SYS)) {
    if(cpu_high < (range_upper - range_step) && (range_upper - range_step) >= display_max) {
      range_upper -= range_step;
      modified = 1;
    }    
    if(cpu_low > (range_lower + range_step) && (range_lower + range_step) <= display_min) { 
      range_lower += range_step;  
      modified = 1;  
    } 
  }
  else if(!IsOn(SENSOR_DISP, CPU) && IsOn(SENSOR_DISP, SYS)) {
    if(sys_high < (range_upper - range_step) && (range_upper - range_step) >= display_max) {
      range_upper -= range_step;
      modified = 1;
    }    
    if(sys_low > (range_lower + range_step) && (range_lower + range_step) <= display_min) { 
      range_lower += range_step;  
      modified = 1;  
    }
  }

  return modified;
}

inline double highest_temp(double *temp_array) {
  int i = 0;
  double high = 0;
  for(i = 0; i < 59; i++) {
    if(temp_array[i] > high)
      high = temp_array[i];
  }
  return high;
}

inline double lowest_temp(double *temp_array) {
  int i = 0;
  double low = 500;
  for(i = 0; i < 59; i++) {
    if((temp_array[i] < low) && (temp_array[i] != -1))
      low = temp_array[i];
  }
  return low;
}

void add_to_graph(double temp, int type, short blank, double range, int pos) {
  double each = (double)39 / range;
  short length = each * (temp - range_lower);

  // Draw the graphs
  if(IsOn(SENSOR_DISP, GRAPH_BLOCK)) {
    copyXPMArea(type == CPU ? 67 : 68, 0, 1, length, pos, 51 - length);
  }
  else if(IsOn(SENSOR_DISP, GRAPH_LINE)) {
    copyXPMArea(type == CPU ? 67 : 68, 0, 1, 1, pos, 51 - length);
  }
}


inline void draw_temp(short value, int type) {
  short digit;

  if(value > 0) {
    digit = value % 10;
    copyXPMArea((digit * 6) + 1, 65, 5, 7, 46, type == CPU ? CPU_YPOS : SYS_YPOS);
    if(value > 9) {
      digit = ((value % 100) - digit) / 10;
      copyXPMArea((digit * 6) + 1, 65, 5, 7, 40, type == CPU ? CPU_YPOS : SYS_YPOS);
      if(value > 99) {
	digit = (value - (value % 100)) / 100;
	copyXPMArea((digit * 6) + 1, 65, 5, 7, 34, type == CPU ? CPU_YPOS : SYS_YPOS);
      }
    }
  } 
}


inline void draw_clear_temps() {
  if(IsOn(SENSOR_DISP, CPU)) {
    copyXPMArea(78, 65, 5, 7, 34, CPU_YPOS);
    copyXPMArea(78, 65, 5, 7, 40, CPU_YPOS);
    copyXPMArea(78, 65, 5, 7, 46, CPU_YPOS);
  }
  if(IsOn(SENSOR_DISP, SYS)) {
    copyXPMArea(78, 65, 5, 7, 34, SYS_YPOS);
    copyXPMArea(78, 65, 5, 7, 40, SYS_YPOS);
    copyXPMArea(78, 65, 5, 7, 46, SYS_YPOS);
  }
}

void draw_range_line(double temp, double range, short type) {
  double each = (double)39 / range;
  short length = each * (temp - range_lower);

  copyXPMArea(0, type == D_MAX ? 73 : 74, 59, 1, 2, 51 - length);
}


int init_sensors() {
  FILE *config_file;
  int res;

  /* Initialise sensors using sensors config file */
  config_file = fopen("/etc/sensors.conf", "r");
  res = sensors_init(config_file);

  if(res != 0) {
    config_file = fopen("/usr/etc/sensors.conf", "r");
    res = sensors_init(config_file);

    if(res != 0) {
      config_file = fopen("/usr/local/etc/sensors.conf", "r");
      res = sensors_init(config_file);

      if(res != 0) {
	fprintf(stderr,"%s\n", sensors_strerror(res));
	return 0;
      }
    }
  }
  fclose(config_file);
  return 1;
}

void display_usage() {
  printf("Usage: wmgtemp [options]
Options:
   -s, --scale=SCALE  display temperatures in SCALE
                      SCALE=kelvin, fahrenheit
                      [Default: celcius]
   -g, --graph=STYLE  display graph as STYLE
                      STYLE=line, block
                      [Default: line]
   -H, --high=TEMP    Display red warning light at TEMP degrees celcius
                      [Default: 50]
   -w, --warn=TEMP    Display amber warning light at TEMP degrees celcius
                      [Default: 45]
   -u, --update=SEC   update the display every SEC seconds
                      [Default: 1]
   -m, --min=TEMP     set the lower bound of the graph to TEMP degrees celcius
                      [Default: 20]
   -M, --max=TEMP     set the upper bound of the graph to TEMP degrees celcius
                      [Default: 35]
   -a, --execat=TEMP  Execute a command at TEMP degrees celcius
   -e, --exec=COMMAND Execute COMMAND when the 'execat' temperature is reached
   -t, --swap         Swap CPU and SYS temps
   -h, --help         Displays this help screen
");
  
}

void draw_warning_lights(double current_temp) {
  if(current_temp >= warn_temp && IsOn(SENSOR_DISP, WARN_NONE)) {
    // Switch from ok to warning.
    BitOff(SENSOR_DISP, WARN_NONE);
    BitOn(SENSOR_DISP, WARN_WARN);
    copyXPMArea(10, 75, 5, 5, 4, 4);
  }
  if(current_temp < warn_temp && IsOn(SENSOR_DISP, WARN_WARN)) {
    // Switch from warning to ok.
    BitOff(SENSOR_DISP, WARN_WARN);
    BitOn(SENSOR_DISP, WARN_NONE);
    copyXPMArea(0, 75, 5, 5, 4, 4);
  }
  if(current_temp >= high_temp && IsOn(SENSOR_DISP, WARN_WARN)) {
    // Switch from warning to high.
    BitOff(SENSOR_DISP, WARN_WARN);
    BitOn(SENSOR_DISP, WARN_HIGH);
    copyXPMArea(15, 75, 5, 5, 4, 4);
  }
  if(current_temp < high_temp && IsOn(SENSOR_DISP, WARN_HIGH)) {
    // Switch from high to warning.
    BitOff(SENSOR_DISP, WARN_HIGH);
    BitOn(SENSOR_DISP, WARN_WARN);
    copyXPMArea(10, 75, 5, 5, 4, 4);
  }
}

inline void blank_type(int type) {
  switch(type) {
  case CPU:
    copyXPMArea(78, 65, 5, 7, 11, CPU_YPOS);
    copyXPMArea(78, 65, 5, 7, 17, CPU_YPOS);
    copyXPMArea(78, 65, 5, 7, 23, CPU_YPOS);
    break;
  case SYS:
    copyXPMArea(78, 65, 5, 7, 11, SYS_YPOS);
    copyXPMArea(78, 65, 5, 7, 17, SYS_YPOS);
    copyXPMArea(78, 65, 5, 7, 23, SYS_YPOS);
    break;
  }
}

inline void draw_max(int type) {
  //  copyXPMArea(1, 81, 17, 7, 11, type == CPU ? CPU_YPOS : SYS_YPOS);
  copyXPMArea(24, 75, 4, 3, 29, type == CPU ? CPU_YPOS : SYS_YPOS);
}

inline void blank_max(int type) {
  //  copyXPMArea(1, 81, 17, 7, 11, type == CPU ? CPU_YPOS : SYS_YPOS);
  copyXPMArea(20, 75, 4, 3, 29, type == CPU ? CPU_YPOS : SYS_YPOS);
}

inline void draw_type(int type) {
  switch(type) {
  case CPU:
    copyXPMArea(65, 40, 17, 7, 11, CPU_YPOS);
    break;
  case SYS:
    copyXPMArea(65, 47, 17, 7, 11, SYS_YPOS);
    break;
  }
}

inline void cycle_temptype() {
  if(IsOn(SENSOR_DISP, TSCALE_CELCIUS)) {
    BitOff(SENSOR_DISP, TSCALE_CELCIUS);
    BitOn(SENSOR_DISP, TSCALE_KELVIN);
  }
  else if(IsOn(SENSOR_DISP, TSCALE_KELVIN)) {
    BitOff(SENSOR_DISP, TSCALE_KELVIN);
    BitOn(SENSOR_DISP, TSCALE_FAHRENHEIT);
  }
  else if(IsOn(SENSOR_DISP, TSCALE_FAHRENHEIT)) {
    BitOff(SENSOR_DISP, TSCALE_FAHRENHEIT);
    BitOn(SENSOR_DISP, TSCALE_CELCIUS);
  }
}


int process_config(int argc, char **argv) {
  char *rc_graph  = NULL;
  char *rc_scale  = NULL;
  char *rc_high   = NULL;
  char *rc_warn   = NULL;
  char *rc_min    = NULL;
  char *rc_max    = NULL;
  char *rc_execat = NULL;
  char *rc_exec   = NULL;
  char *rc_delay  = NULL;
  char *rc_swap   = NULL;
  short parse_ok  = 1;
  int opt_index;
  int opt;
  char *p;
  char temp[128];

  rckeys wmgtemp_keys[] = {
    { "graph", &rc_graph },
    { "scale", &rc_scale },
    { "high", &rc_high },
    { "warn", &rc_warn },
    { "min", &rc_min },
    { "max", &rc_max },
    { "execat", &rc_execat },
    { "exec", &rc_exec },
    { "update", &rc_delay },
    { "swap", &rc_swap },
    { NULL, NULL }
  };
  
  static struct option long_options[] = {
    {"graph",  required_argument, 0, 'g'},
    {"scale",  required_argument, 0, 's'},
    {"high",   required_argument, 0, 'H'},
    {"warn",   required_argument, 0, 'w'},
    {"min",    required_argument, 0, 'm'},
    {"max",    required_argument, 0, 'M'},
    {"execat", required_argument, 0, 'a'},
    {"exec",   required_argument, 0, 'e'},
    {"update", required_argument, 0, 'u'},
    {"swap",   no_argument,       0, 't'},
    {"help",   no_argument,       0, 'h'},
    {0, 0, 0, 0}
  };
  
  p = getenv("HOME");
  strcpy(temp, p);
  strcat(temp, "/.wmgtemprc");
  parse_rcfile(temp, wmgtemp_keys);

  // Do getopt stuff.
  while ((opt = getopt_long(argc, argv, OPT_STRING, long_options, &opt_index)) != -1) {
    switch(opt) {
    case 'g':
      rc_graph = strdup(optarg);
      break;
    case 's':
      rc_scale = strdup(optarg);
      break;
    case 'H':
      rc_high = strdup(optarg);
      break;
    case 'w':
      rc_warn = strdup(optarg);
      break;
    case 'm':
      rc_min = strdup(optarg);
      break;
    case 'M':
      rc_max = strdup(optarg);
      break;
    case 'a':
      rc_execat = strdup(optarg);
      break;
    case 'e':
      rc_exec = strdup(optarg);
      break;
    case 'u':
      rc_delay = strdup(optarg);
      break;
    case 't':
      rc_swap = "y";
      break;
    case 'h':
      display_usage();
      exit(0);
    default:
      display_usage();
      exit(-1);
    }
  }

  if(rc_graph != NULL) {
    if(!strncmp(rc_graph, "l", 1)) {
      BitOff(SENSOR_DISP, GRAPH_BLOCK);
      BitOn(SENSOR_DISP, GRAPH_LINE);
    }
    else if(!strncmp(rc_graph, "b", 1)) {
      BitOff(SENSOR_DISP, GRAPH_LINE);
      BitOn(SENSOR_DISP, GRAPH_BLOCK);
    }
    else {
      printf("Invalid graph type: %s\n", rc_graph);
      parse_ok = 0;
    }
  }
  if(rc_scale != NULL) {
    if(!strncmp(rc_scale, "c", 1)) {
    }
    else if(!strncmp(rc_scale, "f", 1)) {
      BitOff(SENSOR_DISP, TSCALE_KELVIN);
      BitOff(SENSOR_DISP, TSCALE_CELCIUS);
      BitOn(SENSOR_DISP, TSCALE_FAHRENHEIT);
    }
    else if(!strncmp(rc_scale, "k", 1)) {
      BitOff(SENSOR_DISP, TSCALE_CELCIUS);
      BitOff(SENSOR_DISP, TSCALE_FAHRENHEIT);
      BitOn(SENSOR_DISP, TSCALE_KELVIN);
    }
    else {
      printf("Invalid scale type: %s\n", rc_scale);
      parse_ok = 0;
    }
  }

  if(rc_high != NULL) {
    high_temp = (double)atoi(rc_high);
    if(!high_temp) {
      printf("Invalid temperature\n");
      parse_ok = 0;
    }
    else {
      printf("wmgtemp: high temp set to %d degrees celcius.\n", (int)high_temp);
    }
  }
  if(rc_warn != NULL) {
    warn_temp = (double)atoi(rc_warn);
    if(!warn_temp) {
      printf("Invalid temperature\n");
      parse_ok = 0;
    }
    else {
      printf("wmgtemp: warning temp set to %d degrees celcius.\n", (int)warn_temp);
    }
  }
  if(rc_max != NULL) {
    display_max = range_upper = (double)atoi(rc_max);
    if(!range_upper) {
      printf("Invalid temperature\n");
      parse_ok = 0;
    }
    else {
      printf("wmgtemp: Upper range set to %d degrees celcius.\n", (int)range_upper);
    }
  }
  if(rc_min != NULL) {
    display_min = range_lower = (double)atoi(rc_min);
    if(!range_lower) {
      printf("Invalid temperature\n");
      parse_ok = 0;
    }
    else {
      printf("wmgtemp: Lower range set to %d degrees celcius.\n", (int)range_lower);
    }
  }
  if(rc_delay != NULL) {
    delay = atoi(rc_delay);
    if(!delay) {
      printf("Invalid delay\n");
      parse_ok = 0;
    }
    else {
      printf("wmgtemp: update delay set to %d seconds.\n", delay);
    }
  }
  if(rc_execat != NULL) {
    execat = (double)atoi(rc_execat);
    if(!execat) {
      printf("Invalid temperature\n");
      parse_ok = 0;
    }
    else {
      if(rc_exec != NULL) {
	if(strcmp(rc_exec, "")) {
	  exec_app = strdup(rc_exec);
	  printf("wmgtemp: Executing \"%s\" at %d degrees celcius.\n", exec_app, (int)execat);
	}
	else {
	  printf("You must supply an command to execute\n");
	  parse_ok = 0;
	}
      }
      else {
	printf("You must supply an command to execute\n");
	parse_ok = 0;
      }
    }
  }
  if(rc_swap != NULL) {
    if(!strncmp(rc_swap, "y", 1)) {
      swap_types = 1;
    }
    else if(!strncmp(rc_swap, "n", 1)) {
      swap_types = 0;
    }
    else {
      printf("Supply 'y' or 'n' for swap temps\n");
      parse_ok = 0;
    }
  }

  return parse_ok;
}