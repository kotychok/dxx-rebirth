/*
THE COMPUTER CODE CONTAINED HEREIN IS THE SOLE PROPERTY OF PARALLAX
SOFTWARE CORPORATION ("PARALLAX").  PARALLAX, IN DISTRIBUTING THE CODE TO
END-USERS, AND SUBJECT TO ALL OF THE TERMS AND CONDITIONS HEREIN, GRANTS A
ROYALTY-FREE, PERPETUAL LICENSE TO SUCH END-USERS FOR USE BY SUCH END-USERS
IN USING, DISPLAYING,  AND CREATING DERIVATIVE WORKS THEREOF, SO LONG AS
SUCH USE, DISPLAY OR CREATION IS FOR NON-COMMERCIAL, ROYALTY OR REVENUE
FREE PURPOSES.  IN NO EVENT SHALL THE END-USER USE THE COMPUTER CODE
CONTAINED HEREIN FOR REVENUE-BEARING PURPOSES.  THE END-USER UNDERSTANDS
AND AGREES TO THE TERMS HEREIN AND ACCEPTS THE SAME BY USE OF THIS FILE.  
COPYRIGHT 1993-1998 PARALLAX SOFTWARE CORPORATION.  ALL RIGHTS RESERVED.
*/

/*
 * $Source: /cvs/cvsroot/d2x/arch/dos/joyc.c,v $
 * $Revision: 1.6 $
 * $Author: schaffner $
 * $Date: 2004-08-28 23:17:45 $
 * 
 * Routines for joystick reading.
 * 
 */

#ifdef HAVE_CONFIG_H
#include <conf.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <dos.h>

//#define ARCADE 1

#include "pstypes.h"
#include "mono.h"
#include "joy.h"
#include "u_dpmi.h"
#include "timer.h"

#include "args.h"

extern int joy_bogus_reading;
int JOY_PORT = 513; //201h;
int joy_deadzone = 0;

int joy_read_stick_asm2( int read_masks, int * event_buffer, int timeout );
int joy_read_stick_friendly2( int read_masks, int * event_buffer, int timeout );
int joy_read_stick_polled2( int read_masks, int * event_buffer, int timeout );
int joy_read_stick_bios2( int read_masks, int * event_buffer, int timeout );
int joy_read_buttons_bios2();
void joy_read_buttons_bios_end2();


//In key.c
// ebx = read mask                                                                           
// edi = pointer to buffer																							
// returns number of events																						

char joy_installed = 0;
char joy_present = 0;

typedef struct Button_info {
	ubyte		ignore;
	ubyte		state;
	ubyte		last_state;
	int		timedown;
	ubyte		downcount;
	ubyte		upcount;
} Button_info;

typedef struct Joy_info {
	ubyte			present_mask;
	ubyte			slow_read;
	int			max_timer;
	int			read_count;
	ubyte			last_value;
	Button_info	buttons[MAX_BUTTONS];
	int			axis_min[4];
	int			axis_center[4];
	int			axis_max[4];
} Joy_info;

Joy_info joystick;

ubyte joy_read_buttons()
{
 return ((~(inp(JOY_PORT) >> 4))&0xf);
}

void joy_get_cal_vals(int *axis_min, int *axis_center, int *axis_max)
{
	int i;

	for (i = 0; i < JOY_NUM_AXES; i++)
	{
		axis_min[i] = joystick.axis_min[i];
		axis_center[i] = joystick.axis_center[i];
		axis_max[i] = joystick.axis_max[i];
	}
}

void joy_set_cal_vals(int *axis_min, int *axis_center, int *axis_max)
{
	int i;

	for (i = 0; i < JOY_NUM_AXES; i++)
	{
		joystick.axis_min[i] = axis_min[i];
		joystick.axis_center[i] = axis_center[i];
		joystick.axis_max[i] = axis_max[i];
	}
}


ubyte joy_get_present_mask()	{
	return joystick.present_mask;
}

void joy_set_timer_rate(int max_value )	{
	_disable();
	joystick.max_timer = max_value;
	_enable();
}

int joy_get_timer_rate()	{
	return joystick.max_timer;
}


void joy_flush()	{
	int i;

	if (!joy_installed) return;

	_disable();
	for (i=0; i<MAX_BUTTONS; i++ )	{
		joystick.buttons[i].ignore = 0;
                joystick.buttons[i].state = 0;
                joystick.buttons[i].timedown = 0;
		joystick.buttons[i].downcount = 0;	
		joystick.buttons[i].upcount = 0;	
	}
	_enable();

}


void joy_handler(int ticks_this_time)	{
	ubyte value;
	int i, state;
	Button_info * button;

//	joystick.max_timer = ticks_this_time;

	if ( joystick.slow_read & JOY_BIOS_READINGS )		{
		joystick.read_count++;
		if ( joystick.read_count > 7 )	{
			joystick.read_count = 0;
                        value = joy_read_buttons_bios2();
			joystick.last_value = value;
		} else {
			value = joystick.last_value;
		}		
	} else {
                value = joy_read_buttons(); //JOY_READ_BUTTONS;
	}

	for (i=0; i<MAX_BUTTONS; i++ )	{
		button = &joystick.buttons[i];
		if (!button->ignore) {
			if ( i < 5 )
				state = (value >> i) & 1;
			else if (i==(value+4))	
				state = 1;
			else
				state = 0;

			if ( button->last_state == state )	{
				if (state) button->timedown += ticks_this_time;
			} else {
				if (state)	{
					button->downcount += state;
					button->state = 1;
				} else {	
					button->upcount += button->state;
					button->state = 0;
				}
				button->last_state = state;
			}
		}
	}
}

void joy_handler_end()	{		// Dummy function to help calculate size of joystick handler function
}


ubyte joy_read_raw_buttons()	{
	if ( joystick.slow_read & JOY_BIOS_READINGS )	
                return joy_read_buttons_bios2();
	else 
                return joy_read_buttons(); //JOY_READ_BUTTONS;
}

void joy_set_slow_reading(int flag)
{
	joystick.slow_read |= flag;
	joy_set_cen();
}

ubyte joystick_read_raw_axis( ubyte mask, int * axis )
{
	ubyte read_masks, org_masks;
	int t, t1, t2, buffer[4*2+2];
	int e, i, num_channels, c;

	axis[0] = 0; axis[1] = 0;
	axis[2] = 0; axis[3] = 0;

	if (!joy_installed) return 0;

	read_masks = 0;
	org_masks = mask;

	mask &= joystick.present_mask;			// Don't read non-present channels
	if ( mask==0 )	{
		return 0;		// Don't read if no stick connected.
	}

	if ( joystick.slow_read & JOY_SLOW_READINGS )	{
		for (c=0; c<4; c++ )	{		
			if ( mask & (1 << c))	{
				// Time out at  (1/100th of a second)

				if ( joystick.slow_read & JOY_POLLED_READINGS )
                                        num_channels = joy_read_stick_polled2( (1 << c), buffer, 65536 );
				else if ( joystick.slow_read & JOY_BIOS_READINGS )
                                        num_channels = joy_read_stick_bios2( (1 << c), buffer, 65536 );
				else if ( joystick.slow_read & JOY_FRIENDLY_READINGS )
                                        num_channels = joy_read_stick_friendly2( (1 << c), buffer, (1193180/100) );
				else
                                        num_channels = joy_read_stick_asm2( (1 << c), buffer, (1193180/100) );
	
				if ( num_channels > 0 )	{
					t1 = buffer[0];
					e = buffer[1];
					t2 = buffer[2];
					if ( joystick.slow_read & (JOY_POLLED_READINGS|JOY_BIOS_READINGS) )	{
						t = t2 - t1;
					} else {			
						if ( t1 > t2 )
							t = t1 - t2;
						else				{
							t = t1 + joystick.max_timer - t2;
							//mprintf( 0, "%d, %d, %d, %d\n", t1, t2, joystick.max_timer, t );
						}
					}
	
					if ( e & 1 ) { axis[0] = t; read_masks |= 1; }
					if ( e & 2 ) { axis[1] = t; read_masks |= 2; }
					if ( e & 4 ) { axis[2] = t; read_masks |= 4; }
					if ( e & 8 ) { axis[3] = t; read_masks |= 8; }
				}
			}
		}
	} else {
		// Time out at  (1/100th of a second)
		if ( joystick.slow_read & JOY_POLLED_READINGS )
                        num_channels = joy_read_stick_polled2( mask, buffer, 65536 );
		else if ( joystick.slow_read & JOY_BIOS_READINGS )
                        num_channels = joy_read_stick_bios2( mask, buffer, 65536 );
		else if ( joystick.slow_read & JOY_FRIENDLY_READINGS )
                        num_channels = joy_read_stick_friendly2( mask, buffer, (1193180/100) );
		else 
                        num_channels = joy_read_stick_asm2( mask, buffer, (1193180/100) );
		//mprintf(( 0, "(%d)\n", num_channels ));
	
		for (i=0; i<num_channels; i++ )	{
			t1 = buffer[0];
			t2 = buffer[i*2+2];
			
			if ( joystick.slow_read & (JOY_POLLED_READINGS|JOY_BIOS_READINGS) )	{
				t = t2 - t1;
			} else {			
				if ( t1 > t2 )
					t = t1 - t2;
				else				{
					t = t1 + joystick.max_timer - t2;
					//mprintf(( 0, "%d, %d, %d, %d\n", t1, t2, joystick.max_timer, t ));
				}
			}		
			e = buffer[i*2+1];
	
			if ( e & 1 ) { axis[0] = t; read_masks |= 1; }
			if ( e & 2 ) { axis[1] = t; read_masks |= 2; }
			if ( e & 4 ) { axis[2] = t; read_masks |= 4; }
			if ( e & 8 ) { axis[3] = t; read_masks |= 8; }
		}
		
	}

	return read_masks;
}

#ifdef __GNUC__
#define near
#endif

int joy_init()  
{
	int i;
	int temp_axis[4];

//        if(FindArg("-joy209"))
//         use_alt_joyport=1;
        if(FindArg("-joy209"))
         JOY_PORT = 521;  //209h;
         
	joy_flush();

	_disable();
	for (i=0; i<MAX_BUTTONS; i++ )	
		joystick.buttons[i].last_state = 0;
	_enable();

	if ( !joy_installed )	{
		joy_present = 0;
		joy_installed = 1;
		//joystick.max_timer = 65536;
		joystick.slow_read = 0;
		joystick.read_count = 0;
		joystick.last_value = 0;

		//--------------- lock everything for the virtal memory ----------------------------------
                if (!dpmi_lock_region ((void near *)joy_read_buttons_bios2, (char *)joy_read_buttons_bios_end2 - (char near *)joy_read_buttons_bios2))   {
                        printf( "Error locking joystick handler (read bios)!\n" );
                        exit(1);
                }



                if (!dpmi_lock_region ((void near *)joy_handler, (char *)joy_handler_end - (char near *)joy_handler))   {
			printf( "Error locking joystick handler!\n" );
			exit(1);
		}

		if (!dpmi_lock_region (&joystick, sizeof(Joy_info)))	{
			printf( "Error locking joystick handler's data!\n" );
			exit(1);
		}

		timer_set_joyhandler(joy_handler);
	}

	// Do initial cheapy calibration...
	joystick.present_mask = JOY_ALL_AXIS;		// Assume they're all present
	do	{
		joystick.present_mask = joystick_read_raw_axis( JOY_ALL_AXIS, temp_axis );
	} while( joy_bogus_reading );

	if ( joystick.present_mask & 3 )
		joy_present = 1;
	else
		joy_present = 0;

	return joy_present;
}

void joy_close()	
{
	if (!joy_installed) return;
	joy_installed = 0;
}

void joy_set_ul()	
{
	joystick.present_mask = JOY_ALL_AXIS;		// Assume they're all present
	do	{
		joystick.present_mask = joystick_read_raw_axis( JOY_ALL_AXIS, joystick.axis_min );
	} while( joy_bogus_reading );
	if ( joystick.present_mask & 3 )
		joy_present = 1;
	else
		joy_present = 0;
}

void joy_set_lr()	
{
	joystick.present_mask = JOY_ALL_AXIS;		// Assume they're all present
	do {
		joystick.present_mask = joystick_read_raw_axis( JOY_ALL_AXIS, joystick.axis_max );
	} while( joy_bogus_reading );

	if ( joystick.present_mask & 3 )
		joy_present = 1;
	else
		joy_present = 0;
}

void joy_set_cen() 
{
	joystick.present_mask = JOY_ALL_AXIS;		// Assume they're all present
	do {
		joystick.present_mask = joystick_read_raw_axis( JOY_ALL_AXIS, joystick.axis_center );
	} while( joy_bogus_reading );

	if ( joystick.present_mask & 3 )
		joy_present = 1;
	else
		joy_present = 0;
}

void joy_set_cen_fake(int channel)	
{

	int i,n=0;
	int minx, maxx, cenx;
	
	minx=maxx=cenx=0;

	for (i=0; i<4; i++ )	{
		if ( (joystick.present_mask & (1<<i)) && (i!=channel) )	{
			n++;
			minx += joystick.axis_min[i];
			maxx += joystick.axis_max[i];
			cenx += joystick.axis_center[i];
		}
	}
	minx /= n;
	maxx /= n;
	cenx /= n;

	joystick.axis_min[channel] = minx;
	joystick.axis_max[channel] = maxx;
	joystick.axis_center[channel] = cenx;
}

int joy_get_scaled_reading( int raw, int axn )	
{
 int x, d, dz;

 // Make sure it's calibrated properly.
//added/changed on 8/14/98 to allow smaller calibrating sticks to work (by Eivind Brendryen)--was <5
   if ( joystick.axis_center[axn] - joystick.axis_min[axn] < 2 )
    return 0;
   if ( joystick.axis_max[axn] - joystick.axis_center[axn] < 2 )
    return 0;
//end change - Victor Rachels  (by Eivind Brendryen)

  raw -= joystick.axis_center[axn];

   if ( raw < 0 )
    d = joystick.axis_center[axn]-joystick.axis_min[axn];
   else
    d = joystick.axis_max[axn]-joystick.axis_center[axn];


   if ( d )
    x = (raw << 7) / d;
   else
    x = 0;


   if ( x < -128 )
    x = -128;
   if ( x > 127 )
    x = 127;

//added on 4/13/99 by Victor Rachels to add deadzone control
  dz =  (joy_deadzone) * 6;
   if ((x > (-1*dz)) && (x < dz))
    x = 0;
//end this section addition -VR

  return x;
}

int last_reading[4] = { 0, 0, 0, 0 };

void joy_get_pos( int *x, int *y )	
{
	ubyte flags;
	int axis[4];

	if ((!joy_installed)||(!joy_present)) { *x=*y=0; return; }

	flags=joystick_read_raw_axis( JOY_1_X_AXIS+JOY_1_Y_AXIS, axis );

	if ( joy_bogus_reading )	{
		axis[0] = last_reading[0];
		axis[1] = last_reading[1];
		flags = JOY_1_X_AXIS+JOY_1_Y_AXIS;
	} else {
		last_reading[0] = axis[0];
		last_reading[1] = axis[1];
	}

	if ( flags & JOY_1_X_AXIS )
		*x = joy_get_scaled_reading( axis[0], 0 );
	else
		*x = 0;

	if ( flags & JOY_1_Y_AXIS )
		*y = joy_get_scaled_reading( axis[1], 1 );
	else
		*y = 0;
}

ubyte joy_read_stick( ubyte masks, int *axis )	
{
	ubyte flags;
	int raw_axis[4];

	if ((!joy_installed)||(!joy_present)) { 
		axis[0] = 0; axis[1] = 0;
		axis[2] = 0; axis[3] = 0;
		return 0;  
	}

	flags=joystick_read_raw_axis( masks, raw_axis );

	if ( joy_bogus_reading )	{
		axis[0] = last_reading[0];
		axis[1] = last_reading[1];
		axis[2] = last_reading[2];
		axis[3] = last_reading[3];
		flags = masks;
	} else {
		last_reading[0] = axis[0];
		last_reading[1] = axis[1];
		last_reading[2] = axis[2];
		last_reading[3] = axis[3];
	}

	if ( flags & JOY_1_X_AXIS )
		axis[0] = joy_get_scaled_reading( raw_axis[0], 0 );
	else
		axis[0] = 0;

	if ( flags & JOY_1_Y_AXIS )
		axis[1] = joy_get_scaled_reading( raw_axis[1], 1 );
	else
		axis[1] = 0;

	if ( flags & JOY_2_X_AXIS )
		axis[2] = joy_get_scaled_reading( raw_axis[2], 2 );
	else
		axis[2] = 0;

	if ( flags & JOY_2_Y_AXIS )
		axis[3] = joy_get_scaled_reading( raw_axis[3], 3 );
	else
		axis[3] = 0;

	return flags;
}


int joy_get_btns()	
{
	if ((!joy_installed)||(!joy_present)) return 0;

	return joy_read_raw_buttons();
}

void joy_get_btn_down_cnt( int *btn0, int *btn1 ) 
{
	if ((!joy_installed)||(!joy_present)) { *btn0=*btn1=0; return; }

	_disable();
	*btn0 = joystick.buttons[0].downcount;
	joystick.buttons[0].downcount = 0;
	*btn1 = joystick.buttons[1].downcount;
	joystick.buttons[1].downcount = 0;
	_enable();
}

int joy_get_button_state( int btn )	
{
	int count;

	if ((!joy_installed)||(!joy_present)) return 0;

	if ( btn >= MAX_BUTTONS ) return 0;

	_disable();
	count = joystick.buttons[btn].state;
	_enable();
	
	return  count;
}

int joy_get_button_up_cnt( int btn ) 
{
	int count;

	if ((!joy_installed)||(!joy_present)) return 0;

	if ( btn >= MAX_BUTTONS ) return 0;

	_disable();
	count = joystick.buttons[btn].upcount;
	joystick.buttons[btn].upcount = 0;
	_enable();

	return count;
}

int joy_get_button_down_cnt( int btn ) 
{
	int count;

	if ((!joy_installed)||(!joy_present)) return 0;
	if ( btn >= MAX_BUTTONS ) return 0;

	_disable();
	count = joystick.buttons[btn].downcount;
	joystick.buttons[btn].downcount = 0;
	_enable();

	return count;
}

	
fix joy_get_button_down_time( int btn ) 
{
	fix count;

	if ((!joy_installed)||(!joy_present)) return 0;
	if ( btn >= MAX_BUTTONS ) return 0;

	_disable();
	count = joystick.buttons[btn].timedown;
	joystick.buttons[btn].timedown = 0;
	_enable();

	return fixmuldiv(count, 65536, 1193180 );
}

void joy_get_btn_up_cnt( int *btn0, int *btn1 ) 
{
	if ((!joy_installed)||(!joy_present)) { *btn0=*btn1=0; return; }

	_disable();
	*btn0 = joystick.buttons[0].upcount;
	joystick.buttons[0].upcount = 0;
	*btn1 = joystick.buttons[1].upcount;
	joystick.buttons[1].upcount = 0;
	_enable();
}

void joy_set_btn_values( int btn, int state, fix timedown, int downcount, int upcount )
{
	_disable();
	joystick.buttons[btn].ignore = 1;
	joystick.buttons[btn].state = state;
	joystick.buttons[btn].timedown = fixmuldiv( timedown, 1193180, 65536 );
	joystick.buttons[btn].downcount = downcount;
	joystick.buttons[btn].upcount = upcount;
	_enable();
}

void joy_poll()
{
	if ( joystick.slow_read & JOY_BIOS_READINGS )	
                joystick.last_value = joy_read_buttons_bios2();
}
