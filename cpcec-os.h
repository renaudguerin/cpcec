 //  ####  ######    ####  #######   ####    ----------------------- //
//  ##  ##  ##  ##  ##  ##  ##   #  ##  ##  CPCEC, plain text Amstrad //
// ##       ##  ## ##       ## #   ##       CPC emulator written in C //
// ##       #####  ##       ####   ##       as a postgraduate project //
// ##       ##     ##       ## #   ##       by Cesar Nicolas-Gonzalez //
//  ##  ##  ##      ##  ##  ##   #  ##  ##  since 2018-12-01 till now //
 //  ####  ####      ####  #######   ####    ----------------------- //

// Because the goal of the emulation itself is to be OS-independent,
// the interactions between the emulator and the OS are kept behind an
// interface of variables and procedures that don't require particular
// knowledge of the emulation's intrinsic properties.

// To compile the emulator for Windows 5.0+, the default platform,
// "$(CC) -xc cpcec.c -luser32 -lgdi32 -lcomdlg32 -lshell32 -lwinmm"
// or the equivalent options as defined by your preferred compiler.
// Optional DirectDraw support is enabled by appending "-DDDRAW -lddraw"
// Succesfully tested compilers: GCC 4.6.3 (-std=gnu99), 4.9.2, 5.1.0,
// 8.3.0 ; TCC 0.9.27; CLANG 3.7.1, 7.0.1 ; Pelles C 4.50.113 ; etc.

char session_caption[]=MY_CAPTION " " MY_VERSION;
unsigned char session_scratch[1<<18]; // at least 256k!

#define INLINE // 'inline' is useless in TCC and GCC4, and harmful in GCC5!
INLINE int ucase(int i) { return i>='a'&&i<='z'?i-32:i; }
INLINE int lcase(int i) { return i>='A'&&i<='Z'?i+32:i; }
INLINE int hex16(int i) { return i<10?'0'+i:'7'+i; }
#define length(x) (sizeof(x)/sizeof(*(x)))

#include "cpcec-a7.h" //unsigned char *onscreen_chrs;
#define ONSCREEN_SIZE (sizeof(onscreen_chrs)/95)

#ifndef SDL2
#if defined(SDL_MAIN_HANDLED)||!defined(_WIN32)
#define SDL2 // fallback!
#endif
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1 // reduce dependencies
#endif

#ifdef DEBUG
#define logprintf(...) (fprintf(stdout,__VA_ARGS__))
#else
#define logprintf(...) 0
#endif

#ifdef SDL2 // SDL2 is mandatory outside Win32 and optional inside Win32

#include "cpcec-ox.h"

#else // START OF WINDOWS 5.0+ DEFINITIONS ========================== //

#include <windows.h> // KERNEL32.DLL, USER32.DLL, GDI32.DLL, WINMM.DLL, COMDLG32.DLL, SHELL32.DLL
#include <commdlg.h> // COMDLG32.DLL: getOpenFileName()...
#include <mmsystem.h> // WINMM.DLL: waveOutWrite()...
#include <shellapi.h> // SHELL32.DLL: DragQueryFile()...

#define STRMAX 288 // widespread in Windows
#define PATHCHAR '\\' // '/' in POSIX
#define strcasecmp _stricmp
#define fsetsize(f,l) _chsize(_fileno(f),(l))
#include <io.h> // _chsize(),_fileno()...

#define MESSAGEBOX_WIDETAB "\t" // expect proportional font

#define AUDIO_N_FRAMES 8 // safe on almost all Windows machines

// general engine constants and variables --------------------------- //

#define VIDEO_UNIT DWORD // 0x00RRGGBB style
#define VIDEO1(x) (x) // no conversion required!

//#define VIDEO_FILTER_HALF(x,y) (((((x&0XFF00FF)+(y&0XFF00FF)+(x<y?0X10001:0))&0X1FE01FE)+(((x&0XFF00)+(y&0XFF00)+(x<y?0X100:0))&0X1FE00))>>1) // 50%:50%
#define VIDEO_FILTER_HALF(x,y) ((x<y?((0X10001+(x&0XFF00FF)+(y&0XFF00FF))&0X1FE01FE)+((0X100+(x&0XFF00)+(y&0XFF00))&0X1FE00):(((x&0XFF00FF)+(y&0XFF00FF))&0X1FE01FE)+(((x&0XFF00)+(y&0XFF00))&0X1FE00))>>1) // 50:50
//#define VIDEO_FILTER_BLUR(r,x,y,z) r=VIDEO_FILTER_HALF(y,z),y=z
//#define VIDEO_FILTER_BLUR(r,x,y,z) r=VIDEO_FILTER_HALF(y,z),y=r
//#define VIDEO_FILTER_BLUR(r,x,y,z) r=(z&0XFF0000)+(y&0XFF00)+(x&0XFF),x=y,y=z // colour bleed
//#define VIDEO_FILTER_BLUR(r,x,y,z) r=((z&0XFEFEFE)>>1)+((((z)*3)>>16)+(((z&0XFF00)*9)>>8)+(((z&0XFF)))+13)/26*0X10101 // desaturation
#define VIDEO_FILTER_BLUR(r,x,y,z) r=(x<y?((0X10001+(z&0XFF00FF)+(y&0XFF00FF))&0X1FE01FE)+((0X100+(y&0XFF00)+(x&0XFF00))&0X1FE00):(((z&0XFF00FF)+(y&0XFF00FF))&0X1FE01FE)+(((y&0XFF00)+(x&0XFF00))&0X1FE00))>>1,x=y,y=z // 50:50 bleed
//#define VIDEO_FILTER_X1(x) (((x>>1)&0X7F7F7F)+0X2B2B2B) // average
//#define VIDEO_FILTER_X1(x) (((x>>2)&0X3F3F3F)+0X404040) // heavier
//#define VIDEO_FILTER_X1(x) (((x>>2)&0X3F3F3F)*3+0X161616) // lighter
//#define VIDEO_FILTER_X1(x) (((((((x&0XFF0000)>>8)+(x&0XFF00))>>8)+(x&0XFF)+1)/3)*0X10101) // fast but imprecise greyscale
//#define VIDEO_FILTER_X1(x) ((((x&0XFF0000)*60+(x&0XFF00)*(176<<8)+(x&0XFF)*(20<<16)+128)>>24)*0X10101) // greyscale 3:9:1
#define VIDEO_FILTER_X1(x) ((((x&0XFF0000)*76+(x&0XFF00)*(150<<8)+(x&0XFF)*(30<<16)+128)>>24)*0X10101) // natural greyscale

#if 0 // 8 bits
	#define AUDIO_UNIT unsigned char
	#define AUDIO_BITDEPTH 8
	#define AUDIO_ZERO 128
	#define AUDIO1(x) (x)
#else // 16 bits
	#define AUDIO_UNIT signed short
	#define AUDIO_BITDEPTH 16
	#define AUDIO_ZERO 0
	#define AUDIO1(x) (x)
#endif // bitsize
#define AUDIO_CHANNELS 2 // 1 mono, 2 stereo

VIDEO_UNIT *video_frame,*video_blend; // video frame, allocated on runtime
AUDIO_UNIT *audio_frame,audio_buffer[AUDIO_LENGTH_Z*AUDIO_CHANNELS],audio_memory[AUDIO_N_FRAMES*AUDIO_LENGTH_Z*AUDIO_CHANNELS]; // audio frame, cycles during playback
VIDEO_UNIT *video_target; // pointer to current video pixel
AUDIO_UNIT *audio_target; // pointer to current audio sample
int video_pos_x,video_pos_y,audio_pos_z; // counters to keep pointers within range
BYTE video_interlaced=0,video_interlaces=0; // video scanline status
char video_framelimit=0,video_framecount=0; // video frameskip counters; must be signed!
BYTE audio_disabled=0,audio_session=0; // audio status and counter
unsigned char session_path[STRMAX],session_parmtr[STRMAX],session_tmpstr[STRMAX],session_substr[STRMAX],session_info[STRMAX]="";

int session_timer,session_event=0; // timing synchronisation and user command
BYTE session_fast=0,session_wait=0,session_audio=1,session_softblit=1,session_hardblit; // timing and devices ; software blitting is enabled by default because it's safer
BYTE session_stick=1,session_shift=0,session_key2joy=0; // keyboard and joystick
BYTE video_scanline=0,video_scanlinez=8; // 0 = solid, 1 = scanlines, 2 = full interlace, 3 = half interlace
BYTE video_filter=0,audio_filter=0; // filter flags
BYTE session_intzoom=0;
FILE *session_wavefile=NULL; // audio recording is done on each session update

RECT session_ideal; // ideal rectangle where the window fits perfectly
JOYINFOEX session_joy; // joystick buffer
HWND session_hwnd; // window handle
HMENU session_menu=NULL; // menu handle
int session_hidemenu=0; // normal or pop-up
HDC session_dc1,session_dc2=NULL; HGDIOBJ session_dib=NULL; // video structs
HWAVEOUT session_wo; WAVEHDR session_wh; MMTIME session_mmtime; // audio structs

VIDEO_UNIT *debug_frame;
BYTE debug_buffer[DEBUG_LENGTH_X*DEBUG_LENGTH_Y]; // [0] can be a valid character, 128 (new redraw required) or 0 (redraw not required)
HGDIOBJ session_dbg=NULL;

#ifdef DDRAW
	#include <ddraw.h>
	LPDIRECTDRAW lpdd=NULL;
	LPDIRECTDRAWCLIPPER lpddclip=NULL;
	LPDIRECTDRAWSURFACE lpddfore=NULL,lpddback=NULL;
	DDSURFACEDESC ddsd;
	LPDIRECTDRAWSURFACE lpdd_dbg=NULL;
#endif

BYTE session_paused=0,session_signal=0;
#define SESSION_SIGNAL_FRAME 1
#define SESSION_SIGNAL_DEBUG 2
#define SESSION_SIGNAL_PAUSE 4
BYTE session_dirtymenu=1; // to force new status text

#define kbd_bit_set(k) (kbd_bit[k/8]|=1<<(k%8))
#define kbd_bit_res(k) (kbd_bit[k/8]&=~(1<<(k%8)))
#define joy_bit_set(k) (joy_bit[k/8]|=1<<(k%8))
#define joy_bit_res(k) (joy_bit[k/8]&=~(1<<(k%8)))
#define kbd_bit_tst(k) ((kbd_bit[k/8]|joy_bit[k/8])&(1<<(k%8)))
BYTE kbd_bit[16],joy_bit[16]; // up to 128 keys in 16 rows of 8 bits

// A modern keyboard as seen by Windows through WM_KEYDOWN and WK_KEYUP; extended keys are shown here with bit 7 on.
// +----+   +-------------------+ +-------------------+ +-------------------+ +--------------+ *1 = trapped by Win32
// | 01 |   | 3B | 3C | 3D | 3E | | 3F | 40 | 41 | 42 | | 43 | *1 | 57 | 58 | | *1 | 46 | 45 | *2 = sequence "1D B8"
// +----+   +-------------------+ +-------------------+ +-------------------+ +--------------+ *3 = "DB" ; *4 = "DC"
// +------------------------------------------------------------------------+ +--------------+ +-------------------+
// | 29 | 02 | 03 | 04 | 05 | 06 | 07 | 08 | 09 | 0A | 0B | 0C | 0D | 0E    | | D2 | C7 | C9 | | C5 | B5 | 37 | 4A |
// +------------------------------------------------------------------------+ +--------------+ +-------------------+
// | 0F  | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 1A | 1B |      | | D3 | CF | D1 | | 47 | 48 | 49 |    |
// +------------------------------------------------------------------+ 1C  | +--------------+ +--------------+ 4E |
// | 3A   | 1E | 1F | 20 | 21 | 22 | 23 | 24 | 25 | 26 | 27 | 28 | 2B |     |                  | 4B | 4C | 4D |    |
// +------------------------------------------------------------------------+      +----+      +-------------------+
// | 2A  | 56 | 2C | 2D | 2E | 2F | 30 | 31 | 32 | 33 | 34 | 35 | 36        |      | C8 |      | 4F | 50 | 51 |    |
// +------------------------------------------------------------------------+ +--------------+ +--------------+ 9C |
// | 1D  | *3  | *1  | 39                           | *2  | *4  | DD  | 9D  | | CB | D0 | CD | | 52      | 53 |    |
// +------------------------------------------------------------------------+ +--------------+ +-------------------+
// watch out: we use the HARDWARE keyboard symbols rather than the SOFTWARE ones,
// to ensure that the emulator works regardless of the operating system language!

// function keys
#define	KBCODE_F1	0x3B
#define	KBCODE_F2	0x3C
#define	KBCODE_F3	0x3D
#define	KBCODE_F4	0x3E
#define	KBCODE_F5	0x3F
#define	KBCODE_F6	0x40
#define	KBCODE_F7	0x41
#define	KBCODE_F8	0x42
#define	KBCODE_F9	0x43
#define	KBCODE_F10	0x44
#define	KBCODE_F11	0x57
#define	KBCODE_F12	0x58
// leftmost keys
#define	KBCODE_ESCAPE	0x01
#define	KBCODE_TAB	0x0F
#define	KBCODE_CAP_LOCK	0x3A
#define	KBCODE_L_SHIFT	0x2A
#define	KBCODE_L_CTRL	0x1D
//#define KBCODE_L_ALT	0x38 // trapped by Win32
// alphanumeric row 1
#define	KBCODE_1	0x02
#define	KBCODE_2	0x03
#define	KBCODE_3	0x04
#define	KBCODE_4	0x05
#define	KBCODE_5	0x06
#define	KBCODE_6	0x07
#define	KBCODE_7	0x08
#define	KBCODE_8	0x09
#define	KBCODE_9	0x0A
#define	KBCODE_0	0x0B
#define	KBCODE_CHR1_1	0x0C
#define	KBCODE_CHR1_2	0x0D
// alphanumeric row 2
#define	KBCODE_Q	0x10
#define	KBCODE_W	0x11
#define	KBCODE_E	0x12
#define	KBCODE_R	0x13
#define	KBCODE_T	0x14
#define	KBCODE_Y	0x15
#define	KBCODE_U	0x16
#define	KBCODE_I	0x17
#define	KBCODE_O	0x18
#define	KBCODE_P	0x19
#define	KBCODE_CHR2_1	0x1A
#define	KBCODE_CHR2_2	0x1B
// alphanumeric row 3
#define	KBCODE_A	0x1E
#define	KBCODE_S	0x1F
#define	KBCODE_D	0x20
#define	KBCODE_F	0x21
#define	KBCODE_G	0x22
#define	KBCODE_H	0x23
#define	KBCODE_J	0x24
#define	KBCODE_K	0x25
#define	KBCODE_L	0x26
#define	KBCODE_CHR3_1	0x27
#define	KBCODE_CHR3_2	0x28
#define	KBCODE_CHR3_3	0x2B
// alphanumeric row 4
#define	KBCODE_Z	0x2C
#define	KBCODE_X	0x2D
#define	KBCODE_C	0x2E
#define	KBCODE_V	0x2F
#define	KBCODE_B	0x30
#define	KBCODE_N	0x31
#define	KBCODE_M	0x32
#define	KBCODE_CHR4_1	0x33
#define	KBCODE_CHR4_2	0x34
#define	KBCODE_CHR4_3	0x35
#define	KBCODE_CHR4_4	0x56
#define	KBCODE_CHR4_5	0x29
// rightmost keys
#define	KBCODE_SPACE	0x39
#define	KBCODE_BKSPACE	0x0E
#define	KBCODE_ENTER	0x1C
#define	KBCODE_R_SHIFT	0x36
#define	KBCODE_R_CTRL	0x9D
//#define KBCODE_R_ALT	0xB8 // trapped by Win32
// extended keys
//#define KBCODE_PRINT	0x54 // trapped by Win32
#define	KBCODE_SCR_LOCK	0x46
#define	KBCODE_HOLD	0x45
#define	KBCODE_INSERT	0xD2
#define	KBCODE_DELETE	0xD3
#define	KBCODE_HOME	0xC7
#define	KBCODE_END	0xCF
#define	KBCODE_PRIOR	0xC9
#define	KBCODE_NEXT	0xD1
#define	KBCODE_UP	0xC8
#define	KBCODE_DOWN	0xD0
#define	KBCODE_LEFT	0xCB
#define	KBCODE_RIGHT	0xCD
#define	KBCODE_NUM_LOCK	0xC5
// numeric keypad
#define	KBCODE_X_7	0x47
#define	KBCODE_X_8	0x48
#define	KBCODE_X_9	0x49
#define	KBCODE_X_4	0x4B
#define	KBCODE_X_5	0x4C
#define	KBCODE_X_6	0x4D
#define	KBCODE_X_1	0x4F
#define	KBCODE_X_2	0x50
#define	KBCODE_X_3	0x51
#define	KBCODE_X_0	0x52
#define	KBCODE_X_DOT	0x53
#define	KBCODE_X_ENTER	0x9C
#define	KBCODE_X_ADD	0x4E
#define	KBCODE_X_SUB	0x4A
#define	KBCODE_X_MUL	0x37
#define	KBCODE_X_DIV	0xB5

const BYTE kbd_k2j[]= // these keys can simulate a 4-button joystick
	{ KBCODE_UP, KBCODE_DOWN, KBCODE_LEFT, KBCODE_RIGHT, KBCODE_Z, KBCODE_X, KBCODE_C, KBCODE_V };

unsigned char kbd_map[256]; // key-to-key translation map

// general engine functions and procedures -------------------------- //

int session_user(int k); // handle the user's commands; 0 OK, !0 ERROR. Must be defined later on!
void session_debug_show(void);
int session_debug_user(int k); // debug logic is a bit different: 0 UNKNOWN COMMAND, !0 OK
int debug_xlat(int k); // translate debug keys into codes. Must be defined later on!
INLINE void audio_playframe(int q,AUDIO_UNIT *ao); // handle the sound filtering; is defined in CPCEC-RT.H!
#define session_audioqueue audio_session // the audio device is the timer

void session_please(void) // stop activity for a short while
{
	if (!session_wait)
	{
		if (session_audio)
			waveOutPause(session_wo);
		//video_framecount=-1;
		session_wait=1;
	}
}

void session_kbdclear(void)
{
	memset(kbd_bit,0,sizeof(kbd_bit));
	memset(joy_bit,0,sizeof(joy_bit));
}
#define session_kbdreset() memset(kbd_map,~~~0,sizeof(kbd_map)) // init and clean key map up
void session_kbdsetup(const unsigned char *s,char l) // maps a series of virtual keys to the real ones
{
	session_kbdclear();
	while (l--)
	{
		int k=*s++;
		kbd_map[k]=*s++;
	}
}
int session_key_n_joy(int k) // handle some keys as joystick motions
{
	if (session_key2joy)
		for (int i=0;i<KBD_JOY_UNIQUE;++i)
			if (kbd_k2j[i]==k)
				return kbd_joy[i];
	return kbd_map[k];
}

BYTE session_dontblit=0;
void session_redraw(HWND hwnd,HDC h) // redraw the window contents
{
	if (session_dontblit)
	{
		session_dontblit=0;
		return;
	}
	int ox,oy;
	if (session_signal&SESSION_SIGNAL_DEBUG)
		ox=0,oy=0;
	else
		ox=VIDEO_OFFSET_X,oy=VIDEO_OFFSET_Y;
	RECT r; GetClientRect(hwnd,&r); int xx,yy; // calculate window area
	if ((xx=(r.right-=r.left))>0&&(yy=(r.bottom-=r.top))>0) // divisions by zero happen on WM_PAINT during window resizing!
	{
		if (xx>yy*VIDEO_PIXELS_X/VIDEO_PIXELS_Y) // window area is too wide?
			xx=yy*VIDEO_PIXELS_X/VIDEO_PIXELS_Y;
		if (yy>xx*VIDEO_PIXELS_Y/VIDEO_PIXELS_X) // window area is too tall?
			yy=xx*VIDEO_PIXELS_Y/VIDEO_PIXELS_X;
		if (session_intzoom) // integer zoom? (100%, 150%, 200%, 250%, 300%...)
			xx=((xx*17)/VIDEO_PIXELS_X/8)*VIDEO_PIXELS_X/2,
			yy=((yy*17)/VIDEO_PIXELS_Y/8)*VIDEO_PIXELS_Y/2;
		if (xx<VIDEO_PIXELS_X||yy<VIDEO_PIXELS_Y)
			xx=VIDEO_PIXELS_X,yy=VIDEO_PIXELS_Y; // window area is too small!
		int x=(r.right-xx)/2,y=(r.bottom-yy)/2; // locate bitmap on window center
		HGDIOBJ session_oldselect;

		#ifdef DDRAW
		if (lpddback)
		{
			LPDIRECTDRAWSURFACE l=(session_signal&SESSION_SIGNAL_DEBUG)?lpdd_dbg:lpddback;
			IDirectDrawSurface_Unlock(l,0);

			int q=1; // don't redraw if something went wrong
			if (IDirectDrawSurface_IsLost(lpddfore))
				q=0,IDirectDrawSurface_Restore(lpddfore);
			if (IDirectDrawSurface_IsLost(lpddback))
				q=0,IDirectDrawSurface_Restore(lpddback);
			if (IDirectDrawSurface_IsLost(lpdd_dbg))
				q=0,IDirectDrawSurface_Restore(lpdd_dbg);

			if (q) // not sure if we can redraw even when !q ...
			{
				POINT p; p.x=0; p.y=0;
				if (ClientToScreen(hwnd,&p)) // can this ever fail!?
				{
					RECT rr;
					rr.right=(rr.left=ox)+VIDEO_PIXELS_X;
					rr.bottom=(rr.top=oy)+VIDEO_PIXELS_Y;
					r.right=(r.left=p.x+x)+xx;
					r.bottom=(r.top=p.y+y)+yy;
					IDirectDrawSurface_Blt(lpddfore,&r,l,&rr,DDBLT_WAIT,0);
				}
			}

			ddsd.dwSize=sizeof(ddsd);
			IDirectDrawSurface_Lock(l,0,&ddsd,DDLOCK_SURFACEMEMORYPTR|DDLOCK_WAIT,0);
			if (session_signal&SESSION_SIGNAL_DEBUG)
				debug_frame=ddsd.lpSurface;
			else
			{
				int dummy=video_target-video_frame; // the old cursor...
				video_frame=ddsd.lpSurface;
				video_target=video_frame+dummy; // ...may need to move!
			}
		}
		else
		#endif
		{
			if (session_oldselect=SelectObject(session_dc2,session_signal&SESSION_SIGNAL_DEBUG?session_dbg:session_dib))
			{
				if (session_hardblit=(xx<=VIDEO_PIXELS_X||yy<=VIDEO_PIXELS_Y)) // window area is a perfect fit?
					BitBlt(h,x,y,VIDEO_PIXELS_X,VIDEO_PIXELS_Y,session_dc2,ox,oy,SRCCOPY); // fast :-)
				else
					StretchBlt(h,x,y,xx,yy,session_dc2,ox,oy,VIDEO_PIXELS_X,VIDEO_PIXELS_Y,SRCCOPY); // slow :-(
				SelectObject(session_dc2,session_oldselect);
			}
		}
	}
}
#define session_clrscr() InvalidateRect(session_hwnd,NULL,1)
int session_fullscreen=0;
void session_togglefullscreen(void)
{
	if (IsZoomed(session_hwnd))
	{
		SetWindowLong(session_hwnd,GWL_STYLE,(GetWindowLong(session_hwnd,GWL_STYLE)|WS_CAPTION));
			//&~WS_POPUP&~WS_CLIPCHILDREN // show caption and buttons
		if (!session_hidemenu) SetMenu(session_hwnd,session_menu); // show menu
		RECT r; GetWindowRect(session_hwnd,&r); // adjust to screen center
		ShowWindow(session_hwnd,SW_RESTORE);
		r.left+=((r.right-r.left)-session_ideal.right)/2;
		r.top+=((r.bottom-r.top)-session_ideal.bottom)/2;
		MoveWindow(session_hwnd,r.left,r.top,session_ideal.right,session_ideal.bottom,1);
		session_fullscreen=0;
	}
	else
	{
		SetWindowLong(session_hwnd,GWL_STYLE,(GetWindowLong(session_hwnd,GWL_STYLE)&~WS_CAPTION));
			//|WS_POPUP|WS_CLIPCHILDREN // hide caption and buttons
		/*if (!session_hidemenu)*/ SetMenu(session_hwnd,NULL); // hide menu
		ShowWindow(session_hwnd,SW_MAXIMIZE); // adjust to entire screen
		session_fullscreen=1;
	}
	session_dirtymenu=1; // update "Full screen" option (if any)
}
int session_contextmenu(void) // used only when the normal menu is disabled
{
	POINT p;
	if (session_hidemenu&&session_menu)
		return GetCursorPos(&p),TrackPopupMenu(session_menu,0,p.x,p.y,0,session_hwnd,NULL);
	return 0;
}
LRESULT CALLBACK mainproc(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam) // window callback function
{
	int k; switch (msg)
	{
		case WM_CLOSE:
			DestroyWindow(hwnd);
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		case WM_SIZING:
			if (((LPRECT)lparam)->right-((LPRECT)lparam)->left<session_ideal.right) // too narrow?
				switch (wparam)
				{
					case WMSZ_TOPLEFT: case WMSZ_LEFT: case WMSZ_BOTTOMLEFT:
						((LPRECT)lparam)->left=((LPRECT)lparam)->right-session_ideal.right;
						break;
					case WMSZ_RIGHT: case WMSZ_TOPRIGHT: case WMSZ_BOTTOMRIGHT:
						((LPRECT)lparam)->right=((LPRECT)lparam)->left+session_ideal.right;
						break;
				}
			if (((LPRECT)lparam)->bottom-((LPRECT)lparam)->top<session_ideal.bottom) // too short?
				switch (wparam)
				{
					case WMSZ_TOPLEFT: case WMSZ_TOP: case WMSZ_TOPRIGHT:
						((LPRECT)lparam)->top=((LPRECT)lparam)->bottom-session_ideal.bottom;
						break;
					case WMSZ_BOTTOMLEFT: case WMSZ_BOTTOM: case WMSZ_BOTTOMRIGHT:
						((LPRECT)lparam)->bottom=((LPRECT)lparam)->top+session_ideal.bottom;
						break;
				}
			break;
		case WM_SIZE:
			session_clrscr(); // force full update! there's dirt otherwise!
			break;
		//case WM_SETFOCUS: // force full redraw
		case WM_PAINT:
			session_dontblit=0;
			PAINTSTRUCT ps; HDC h;
			if (h=BeginPaint(hwnd,&ps))
			{
				session_redraw(hwnd,h);
				EndPaint(hwnd,&ps);
			}
			break;
		case WM_COMMAND:
			if (0x3F00==(WORD)wparam) // Exit
				PostMessage(hwnd,WM_CLOSE,0,0);
			else
			{
				session_shift=!!(wparam&0x4000); // bit 6 means SHIFT KEY ON
				session_event=wparam&0xBFFF; // cfr infra: bit 7 means CONTROL KEY OFF
			}
			break;
		case WM_RBUTTONUP:
			session_contextmenu();
			break;
		case WM_KEYDOWN:
			session_shift=GetKeyState(VK_SHIFT)<0;
			if (session_signal&SESSION_SIGNAL_DEBUG) // only relevant inside debugger, see below
				session_event=debug_xlat(((lparam>>16)&127)+((lparam>>17)&128));
			if ((k=session_key_n_joy(((lparam>>16)&127)+((lparam>>17)&128)))<128) // normal key
			{
				if (!(session_signal&SESSION_SIGNAL_DEBUG)) // only relevant outside debugger
					kbd_bit_set(k);
			}
			else if (!session_event) // special key, but only if not already set by debugger
				session_event=(k-(GetKeyState(VK_CONTROL)<0?128:0))<<8;
			break;
		case WM_CHAR: // always follows WM_KEYDOWN
			if (session_signal&SESSION_SIGNAL_DEBUG) // only relevant inside debugger
				session_event=wparam>32&&wparam<=255?wparam:0; // exclude SPACE and non-visible codes!
			break;
		case WM_KEYUP:
			if ((k=session_key_n_joy(((lparam>>16)&127)+((lparam>>17)&128)))<128) // normal key
				kbd_bit_res(k);
			break;
		case WM_DROPFILES:
			DragQueryFile((HDROP)wparam,0,(LPTSTR)session_parmtr,STRMAX);
			DragFinish((HDROP)wparam);
			session_shift=GetKeyState(VK_SHIFT)<0,session_event=0x8000;
			break;
		case WM_ENTERSIZEMOVE: // pause before moving the window
		case WM_ENTERMENULOOP: // pause before showing the menus
			session_please();
		case WM_KILLFOCUS: // no `break`!
			session_kbdclear(); // loss of focus: no keys!
		default: // no `break`!
			if (msg==WM_SYSKEYDOWN)
			{
				if (wparam==VK_RETURN) // ALT+RETURN toggles fullscreen
					return session_togglefullscreen(),0; // skip OS
				else if (wparam==VK_F10&&session_contextmenu()) // F10 shows the popup menu
					return 0; // skip OS if the popup menu is allowed
			}
			return DefWindowProc(hwnd,msg,wparam,lparam);
	}
	return 0;
}

OSVERSIONINFO win32_version; char session_version[8];
INLINE char* session_create(char *s) // create video+audio devices and set menu; 0 OK, !0 ERROR
{
	win32_version.dwOSVersionInfoSize=sizeof(win32_version); GetVersionEx(&win32_version);
	sprintf(session_version,"%i.%i",win32_version.dwMajorVersion,win32_version.dwMinorVersion);
	HMENU session_submenu=NULL;
	char c,*t; int i;
	/*if (!session_softblit)*/ while (c=*s++)
	{
		if (c=='=') // separator?
		{
			while (*s++!='\n') // ignore remainder
				;
			AppendMenu(session_submenu,MF_SEPARATOR,0,0);
		}
		else if (c=='0') // menu item?
		{
			t=--s;
			i=strtol(t,&s,0); // allow either hex or decimal
			t=session_tmpstr;
			++s;
			while ((c=*s++)!='\n') // string with shortcuts
				*t++=c=='_'?'&':c;
			*t=0;
			AppendMenu(session_submenu,MF_STRING,i,session_tmpstr);
		}
		else // menu block
		{
			if (!session_menu)
				session_menu=session_hidemenu?CreatePopupMenu():CreateMenu();
			if (session_submenu)
				AppendMenu(session_menu,MF_POPUP,(UINT_PTR)session_submenu,session_parmtr);
			session_submenu=CreateMenu();
			t=session_parmtr;
			*t++='&'; // shortcut
			--s;
			while ((c=*s++)!='\n') // string, no shortcuts
				*t++=c;
			*t=0;
		}
	}
	if (session_menu&&session_submenu)
		AppendMenu(session_menu,MF_POPUP,(UINT_PTR)session_submenu,session_parmtr);
	WNDCLASS wc;
	wc.style=wc.cbClsExtra=wc.cbWndExtra=0;
	wc.lpfnWndProc=mainproc;
	wc.hInstance=GetModuleHandle(0);
	wc.hIcon=LoadIcon(wc.hInstance,MAKEINTRESOURCE(34002));//IDI_APPLICATION
	wc.hCursor=LoadCursor(NULL,IDC_ARROW);
	wc.hbrBackground=(HBRUSH)(1+COLOR_WINDOWTEXT);//(COLOR_WINDOW+2);//0;//
	wc.lpszMenuName=NULL;
	wc.lpszClassName=MY_CAPTION;
	RegisterClass(&wc);
	session_ideal.left=session_ideal.top=0; // calculate ideal size
	session_ideal.right=VIDEO_PIXELS_X;
	session_ideal.bottom=VIDEO_PIXELS_Y;
	if (!session_submenu)
		session_hidemenu=1,session_menu=NULL;
	AdjustWindowRect(&session_ideal,i=WS_OVERLAPPEDWINDOW,!session_hidemenu);
	session_ideal.right-=session_ideal.left;
	session_ideal.bottom-=session_ideal.top;
	session_ideal.left=session_ideal.top=0; // ensure that the ideal area is defined as (0,0,WIDTH,HEIGHT)
	if (!(session_hwnd=CreateWindow(wc.lpszClassName,session_caption,i,CW_USEDEFAULT,CW_USEDEFAULT,session_ideal.right,session_ideal.bottom,NULL,session_hidemenu?NULL:session_menu,wc.hInstance,NULL))
		||!(video_blend=malloc(sizeof(VIDEO_UNIT)*VIDEO_PIXELS_Y/2*VIDEO_PIXELS_X)))
		return "cannot create window";
	DragAcceptFiles(session_hwnd,1);

	// hardware-able DirectDraw
	#ifdef DDRAW
	if (!session_softblit)
	{
		session_softblit=1; // fallback!
		if (DirectDrawCreate(NULL,&lpdd,NULL)>=0)
		{
			IDirectDraw_SetCooperativeLevel(lpdd,session_hwnd,DDSCL_NORMAL);
			//ZeroMemory(&ddsd,sizeof(ddsd));
			ddsd.dwSize=sizeof(ddsd);
			ddsd.dwFlags=DDSD_CAPS;
			ddsd.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE;
			IDirectDraw_CreateSurface(lpdd,&ddsd,&lpddfore,NULL);

			DDPIXELFORMAT ddpf; ddpf.dwSize=sizeof(ddpf);
			IDirectDrawSurface_GetPixelFormat(lpddfore,&ddpf);
			if (ddpf.dwRGBBitCount!=32) // lazy check, translating ARGB8888 to other bitdepths is a task better left to GDI
				;//IDirectDrawSurface_Release(lpddback),lpddback=NULL; // failure
			else
			{
				IDirectDraw_CreateClipper(lpdd,0,&lpddclip,NULL);
				IDirectDrawClipper_SetHWnd(lpddclip,0,session_hwnd);
				IDirectDrawSurface_SetClipper(lpddfore,lpddclip);
				ddsd.dwSize=sizeof(ddsd); ddsd.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT;
				ddsd.dwWidth=VIDEO_LENGTH_X; ddsd.dwHeight=VIDEO_LENGTH_Y;
				ddsd.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY;//DDSCAPS_VIDEOMEMORY;//
				IDirectDraw_CreateSurface(lpdd,&ddsd,&lpddback,NULL);
				IDirectDrawSurface_Lock(lpddback,0,&ddsd,DDLOCK_SURFACEMEMORYPTR|DDLOCK_WAIT,0),video_frame=ddsd.lpSurface;
				session_softblit=ddsd.lPitch!=4*VIDEO_LENGTH_X; // success (1/2)
				ddsd.dwSize=sizeof(ddsd); ddsd.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT;
				ddsd.dwWidth=VIDEO_PIXELS_X; ddsd.dwHeight=VIDEO_PIXELS_Y;
				ddsd.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY;//DDSCAPS_VIDEOMEMORY;//
				IDirectDraw_CreateSurface(lpdd,&ddsd,&lpdd_dbg,NULL);
				IDirectDrawSurface_Lock(lpdd_dbg,0,&ddsd,DDLOCK_SURFACEMEMORYPTR|DDLOCK_WAIT,0),debug_frame=ddsd.lpSurface;
				session_softblit|=ddsd.lPitch!=4*VIDEO_PIXELS_X; // success (2/2)
			}
		}
	}
	// software-only GDI bitmap
	if (session_softblit)
	#endif
	{
		BITMAPINFO bmi;
		memset(&bmi,0,sizeof(bmi));
		bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth=VIDEO_LENGTH_X;
		bmi.bmiHeader.biHeight=-VIDEO_LENGTH_Y; // negative values make a top-to-bottom bitmap; Windows' default bitmap is bottom-to-top
		bmi.bmiHeader.biPlanes=1;
		bmi.bmiHeader.biBitCount=32; // cfr. VIDEO_UNIT
		bmi.bmiHeader.biCompression=BI_RGB;
		session_dc1=GetDC(session_hwnd); // caution: we assume that if CreateWindow() succeeds all other USER and GDI calls will succeed too
		session_dc2=CreateCompatibleDC(session_dc1); // ditto
		session_dib=CreateDIBSection(session_dc1,&bmi,DIB_RGB_COLORS,(void**)&video_frame,NULL,0); // ditto
		bmi.bmiHeader.biWidth=VIDEO_PIXELS_X;
		bmi.bmiHeader.biHeight=-VIDEO_PIXELS_Y;
		session_dbg=CreateDIBSection(session_dc1,&bmi,DIB_RGB_COLORS,(void**)&debug_frame,NULL,0);
	}

	// sound setup and cleanup

	ShowWindow(session_hwnd,SW_SHOWDEFAULT);
	UpdateWindow(session_hwnd);
	session_timer=GetTickCount();
	session_joy.dwSize=sizeof(session_joy);
	session_joy.dwFlags=JOY_RETURNALL;
	if (session_stick)
	{
		JOYCAPS jc; int i=joyGetNumDevs(),j=0;
		logprintf("Detected %i joystick[s]: ",i);
		while (j<i&&((!joyGetDevCaps(j,&jc,sizeof(jc))&&logprintf("Joystick/controller #%i = '%s'. ",j,jc.szPname)),joyGetPosEx(j,&session_joy))) // scan joysticks until we run out or one is OK
			++j;
		session_stick=(j<i)?j+1:0; // ID+1 if available, 0 if missing
		logprintf(session_stick?"Joystick enabled!\n":"No joystick!\n");
	}
	session_wo=0; // no audio unless device is detected
	if (session_audio)
	{
		memset(&session_mmtime,0,sizeof(session_mmtime));
		session_mmtime.wType=TIME_SAMPLES; // Windows doesn't always provide TIME_MS!
		WAVEFORMATEX wfex;
		memset(&wfex,0,sizeof(wfex)); wfex.wFormatTag=WAVE_FORMAT_PCM;
		wfex.nBlockAlign=(wfex.wBitsPerSample=AUDIO_BITDEPTH)/8*(wfex.nChannels=AUDIO_CHANNELS);
		wfex.nAvgBytesPerSec=wfex.nBlockAlign*(wfex.nSamplesPerSec=AUDIO_PLAYBACK);
		if (session_audio=!waveOutOpen(&session_wo,WAVE_MAPPER,&wfex,0,0,0))
		{
			memset(&session_wh,0,sizeof(WAVEHDR));
			memset(audio_frame=audio_memory,AUDIO_ZERO,sizeof(audio_memory));
			session_wh.lpData=(BYTE *)audio_memory;
			session_wh.dwBufferLength=AUDIO_N_FRAMES*AUDIO_LENGTH_Z*wfex.nBlockAlign;
			session_wh.dwFlags=WHDR_BEGINLOOP|WHDR_ENDLOOP; // circular buffer
			session_wh.dwLoops=-1; // loop forever!
			waveOutPrepareHeader(session_wo,&session_wh,sizeof(WAVEHDR));
			session_timer=waveOutWrite(session_wo,&session_wh,sizeof(WAVEHDR)); // should be zero!
		}
	}
	session_please();
	return NULL;
}

void session_menuinfo(void); // set the current menu flags. Must be defined later on!

INLINE int session_listen(void) // handle all pending messages; 0 OK, !0 EXIT
{
	static int s=0; // catch DEBUG and PAUSE signals
	if (s!=session_signal)
		s=session_signal,session_dirtymenu=1;
	if (session_dirtymenu)
		session_dirtymenu=0,session_menuinfo();
	if (session_signal)
	{
		if (session_signal&SESSION_SIGNAL_DEBUG)
		{
			if (*debug_buffer==128)
				session_please(),session_debug_show();
			if (*debug_buffer)//!=0
				session_redraw(session_hwnd,session_dc1),*debug_buffer=0;
		}
		else if (!session_paused) // set the caption just once
		{
			session_please();
			sprintf(session_tmpstr,"%s | %s | PAUSED",session_caption,session_info);
			SetWindowText(session_hwnd,session_tmpstr);
			session_paused=1;
		}
		WaitMessage(); //session_dontblit=1; // reduce flickering
	}
	int q=0; for (MSG msg;PeekMessage(&msg,0,0,0,PM_REMOVE);)
	{
		TranslateMessage(&msg);
		q|=msg.message==WM_QUIT;
		DispatchMessage(&msg);
		if (session_event)
		{
			if (!((session_signal&SESSION_SIGNAL_DEBUG)&&session_debug_user(session_event)))
				q|=session_user(session_event);
			session_event=0;
		}
	}
	return q;
}

void session_writewave(AUDIO_UNIT *t); // save the current sample frame. Must be defined later on!
FILE *session_filmfile=NULL; void session_writefilm(void); // must be defined later on, too!
INLINE void session_render(void) // update video, audio and timers
{
	int i,j;
	static int performance_t=-9999,performance_f=0,performance_b=0; ++performance_f;
	if (!video_framecount) // do we need to hurry up?
	{
		if ((video_interlaces=!video_interlaces)||!video_interlaced)
			++performance_b,session_redraw(session_hwnd,session_dc1);
		if (session_stick&&!session_key2joy) // do we need to check the joystick?
		{
			session_joy.dwSize=sizeof(session_joy);
			session_joy.dwFlags=JOY_RETURNBUTTONS|JOY_RETURNPOVCTS|JOY_RETURNX|JOY_RETURNY|JOY_RETURNZ|JOY_RETURNR|JOY_RETURNCENTERED;
			if (!joyGetPosEx(session_stick-1,&session_joy))
			{
				j=((session_joy.dwPOV<0||session_joy.dwPOV>=36000)?(session_joy.dwYpos< 0x4000?1:0)+(session_joy.dwYpos>=0xC000?2:0)+(session_joy.dwXpos< 0x4000?4:0)+(session_joy.dwXpos>=0xC000?8:0) // axial
				:(session_joy.dwPOV< 2250?1:session_joy.dwPOV< 6750?9:session_joy.dwPOV<11250?8:session_joy.dwPOV<15750?10: // angular: U (0), U-R (4500), R (9000), R-D (13500)
				session_joy.dwPOV<20250?2:session_joy.dwPOV<24750?6:session_joy.dwPOV<29250?4:session_joy.dwPOV<33750?5:1)) // D (18000), D-L (22500), L (27000), L-U (31500)
				+((session_joy.dwButtons&(JOY_BUTTON1/*|JOY_BUTTON5*/))?16:0)+((session_joy.dwButtons&(JOY_BUTTON2/*|JOY_BUTTON6*/))?32:0) // FIRE1, FIRE2 ...
				+((session_joy.dwButtons&(JOY_BUTTON3/*|JOY_BUTTON7*/))?64:0)+((session_joy.dwButtons&(JOY_BUTTON4/*|JOY_BUTTON8*/))?128:0) // FIRE3, FIRE4 ...
				/*+((session_joy.dwZpos<0x4000?4:0)+(session_joy.dwZpos>0xC000?8:0))
				+((session_joy.dwRpos<0x4000?1:0)+(session_joy.dwRpos>0xC000?2:0))*/ // this is safe on my controller (Axis Z + Rotation Z) but perhaps not on other controllers
				;
			}
			else
				j=0; // joystick failure, release its keys
			memset(joy_bit,0,sizeof(joy_bit));
			for (i=0;i<length(kbd_joy);++i)
				if (j&(1<<i))
					joy_bit_set(kbd_joy[i]); // key is down
		}
	}
	audio_target=&audio_memory[AUDIO_LENGTH_Z*AUDIO_CHANNELS*audio_session];
	if (!audio_disabled) // avoid conflicts when realtime is off: output and playback buffers clash!
	{
		if (audio_filter) // audio filter: sample averaging
			audio_playframe(audio_filter,audio_frame==audio_buffer?audio_target:audio_frame);
		else if (audio_frame==audio_buffer)
			memcpy(audio_target,audio_buffer,sizeof(audio_buffer));
	}
	if (session_wavefile) // record audio output, if required
		session_writewave(audio_target);
	if (session_wavefile||session_filmfile)
		audio_frame=audio_buffer; // secondary buffer
	else
		audio_frame=audio_target; // primary buffer
	session_writefilm(); // record film frame

	if (session_audio) // use audio as clock
	{
		static BYTE s=1;
		if (s!=audio_disabled)
			if (s=audio_disabled) // silent mode needs cleanup
				memset(audio_memory,AUDIO_ZERO,sizeof(audio_memory));
		static BYTE o=1;
		if (o!=(session_fast|audio_disabled)) // sound needs higher priority, but only on realtime
		{
			SetPriorityClass(GetCurrentProcess(),(o=session_fast|audio_disabled)?BELOW_NORMAL_PRIORITY_CLASS:ABOVE_NORMAL_PRIORITY_CLASS);
			//SetThreadPriority(GetCurrentThread(),(o=session_fast|audio_disabled)?THREAD_PRIORITY_NORMAL:THREAD_PRIORITY_ABOVE_NORMAL);
		}
		waveOutGetPosition(session_wo,&session_mmtime,sizeof(MMTIME));
		//if (!=MMSYSERR_NOERROR) session_audio=0,audio_disabled=-1; // audio device is lost! // can this really happen!?
		static int u=0; if (!u) u=session_mmtime.u.sample; // reference
		i=session_mmtime.u.sample-u,j=AUDIO_PLAYBACK; // questionable -- this will break the timing every 13 hours of emulation at 44100 Hz :-(
	}
	else // use internal tick count as clock
		i=GetTickCount(),j=1000; // questionable for similar reasons, albeit every 23 days :-(
	if (session_wait||session_fast)
	{
		audio_session=((i/(AUDIO_LENGTH_Z))+AUDIO_N_FRAMES-1)%AUDIO_N_FRAMES;
		session_timer=i; // ensure that the next frame can be valid!
	}
	else
	{
		if ((i=((session_timer+=(j/VIDEO_PLAYBACK))-i))>0)
		{
			if (i=(1000*i/j)) // avoid zero, it has a special value in Windows!
				Sleep(i>1000/VIDEO_PLAYBACK?1+1000/VIDEO_PLAYBACK:i);
		}
		else if (i<0&&!session_filmfile)
			video_framecount=-2; // automatic frameskip if timing ever breaks!
		audio_session=(audio_session+1)%AUDIO_N_FRAMES;
	}
	if (session_wait) // resume activity after a pause
	{
		if (session_audio)
			waveOutRestart(session_wo);
		session_wait=0;
	}
	if ((i=GetTickCount())>(performance_t+1000)) // performance percentage
	{
		if (performance_t)
		{
			sprintf(session_tmpstr,"%s | %s | %g%% CPU %g%% %s %s",
				session_caption,session_info,
				performance_f*100.0/VIDEO_PLAYBACK,performance_b*100.0/VIDEO_PLAYBACK,
			#ifdef DDRAW
				lpddback?"DDRAW":
			#endif
				session_hardblit?"GDI":"gdi",session_version);
			SetWindowText(session_hwnd,session_tmpstr);
		}
		performance_t=i,performance_f=performance_b=session_paused=0;
	}
}

INLINE void session_byebye(void) // delete video+audio devices
{
	if (session_wo)
	{
		waveOutReset(session_wo);
		waveOutUnprepareHeader(session_wo,&session_wh,sizeof(WAVEHDR));
		waveOutClose(session_wo);
	}
	if (session_menu)
		DestroyMenu(session_menu);

	#ifdef DDRAW
	if (lpddfore) IDirectDrawSurface_SetClipper(lpddfore,NULL),IDirectDrawSurface_Release(lpddfore);
	if (lpddback) IDirectDrawSurface_Unlock(lpddback,0),IDirectDrawSurface_Release(lpddback);
	if (lpdd_dbg) IDirectDrawSurface_Unlock(lpdd_dbg,0),IDirectDrawSurface_Release(lpdd_dbg);
	if (lpddclip) IDirectDrawClipper_Release(lpddclip);
	if (lpdd) IDirectDraw_Release(lpdd);
	#endif

	if (session_dbg) DeleteObject(session_dbg);
	if (session_dc2) DeleteDC(session_dc2);
	if (session_dib) DeleteObject(session_dib);
	ReleaseDC(session_hwnd,session_dc1);
	free(video_blend);
}

#define session_getscanline(i) (&video_frame[i*VIDEO_LENGTH_X+VIDEO_OFFSET_X]) // no transformations required, VIDEO_UNIT is ARGB8888
void session_writebitmap(FILE *f,int half) // write current OS-dependent bitmap into a RGB888 BMP file
{
	static BYTE r[VIDEO_PIXELS_X*3];
	for (int i=VIDEO_OFFSET_Y+VIDEO_PIXELS_Y-half-1;i>=VIDEO_OFFSET_Y;fwrite(r,1,VIDEO_PIXELS_X*3>>half,f),i-=half+1)
		if (half)
		{
			BYTE *t=r; VIDEO_UNIT *s=session_getscanline(i);
			for (int j=0;j<VIDEO_PIXELS_X;j+=2) // soft scale 2x RGBA (32 bits) into 1x RGB (24 bits)
			{
				VIDEO_UNIT v=VIDEO_FILTER_HALF(s[0],s[1]);
				*t++=v, // copy B
				*t++=v>>8, // copy G
				*t++=v>>16, // copy R
				s+=2;
			}
		}
		else
		{
			BYTE *t=r,*s=(BYTE *)session_getscanline(i);
			for (int j=0;j<VIDEO_PIXELS_X;++j) // turn RGBA (32 bits) into RGB (24 bits)
				*t++=*s++, // copy B
				*t++=*s++, // copy G
				*t++=*s++, // copy R
				s++; // skip A
		}
}

// menu item functions ---------------------------------------------- //

void session_menucheck(int id,int q) // set the state of option `id` as `q`
{
	if (session_menu)
		CheckMenuItem(session_menu,id,MF_BYCOMMAND+(q?MF_CHECKED:MF_UNCHECKED));
}
void session_menuradio(int id,int a,int z) // set the option `id` in the range `a-z`
{
	if (session_menu)
		CheckMenuRadioItem(session_menu,a,z,id,MF_BYCOMMAND);
}

// message box ------------------------------------------------------ //

void session_message(char *s,char *t) // show multi-lined text `s` under caption `t`
	{ session_please(); MessageBox(session_hwnd,s,t,strchr(t,'?')?MB_ICONQUESTION:(strchr(t,'!')?MB_ICONEXCLAMATION:MB_OK)); }
void session_aboutme(char *s,char *t) // special case: "About.."
{
	session_please();
	MSGBOXPARAMS mbp;
	mbp.cbSize=sizeof(MSGBOXPARAMS);
	mbp.hwndOwner=session_hwnd;
	mbp.hInstance=GetModuleHandle(0);
	mbp.lpszText=s;
	mbp.lpszCaption=t;
	mbp.dwStyle=MB_OK|MB_USERICON;
	mbp.lpszIcon=MAKEINTRESOURCE(34002);
	mbp.dwContextHelpId=0;
	mbp.lpfnMsgBoxCallback=NULL;
	mbp.dwLanguageId=0;
	MessageBoxIndirect(&mbp);
}

HWND session_dialog_item;
char *session_dialog_text;
int session_dialog_return;

// input dialog ----------------------------------------------------- //

LRESULT CALLBACK inputproc(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam) // dialog callback function
{
	switch (msg)
	{
		case WM_INITDIALOG:
			{
				SetWindowText(hwnd,session_dialog_text);
				session_dialog_item=GetDlgItem(hwnd,12345);
				SendMessage(session_dialog_item,WM_SETTEXT,0,lparam);
				SendMessage(session_dialog_item,EM_SETLIMITTEXT,STRMAX-1,0);
			}
			break;
		/*case WM_SIZE:
			{
				RECT r; GetClientRect(hwnd,&r);
				SetWindowPos(session_dialog_item,NULL,0,0,r.right,r.bottom*1/2,SWP_NOZORDER);
				SetWindowPos(GetDlgItem(hwnd,IDOK),NULL,0,r.bottom*1/2,r.right/2,r.bottom/2,SWP_NOZORDER);
				SetWindowPos(GetDlgItem(hwnd,IDCANCEL),NULL,r.right/2,r.bottom*1/2,r.right/2,r.bottom/2,SWP_NOZORDER);
			}
			break;*/
		case WM_COMMAND:
			if (LOWORD(wparam)==IDCANCEL)
				EndDialog(hwnd,0);
			else if (!HIWORD(wparam)&&LOWORD(wparam)==IDOK)
			{
				session_dialog_return=SendMessage(session_dialog_item,WM_GETTEXT,STRMAX,(LPARAM)session_parmtr);
				EndDialog(hwnd,0);
			}
			break;
		default:
			return 0;
	}
	return 1;
}
int session_input(char *s,char *t) // `s` is the target string (empty or not), `t` is the caption; returns -1 on error or LENGTH on success
{
	session_dialog_return=-1;
	session_dialog_text=t;
	session_please();
	DialogBoxParam(GetModuleHandle(0),(LPCSTR)34004,session_hwnd,(DLGPROC)inputproc,(LPARAM)s);
	return session_dialog_return; // the string is in `session_parmtr`
}

// list dialog ------------------------------------------------------ //

LRESULT CALLBACK listproc(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam) // dialog callback function
{
	switch (msg)
	{
		case WM_INITDIALOG:
			{
				SetWindowText(hwnd,session_dialog_text);
				session_dialog_item=GetDlgItem(hwnd,12345);
				char *l=(char *)lparam;
				while (*l)
				{
					SendMessage(session_dialog_item,LB_ADDSTRING,0,(LPARAM)l);
					while (*l++)
						;
				}
				SendMessage(session_dialog_item,LB_SETCURSEL,session_dialog_return,0); // select item
				session_dialog_return=-1;
			}
			break;
		/*case WM_SIZE:
			{
				RECT r; GetClientRect(hwnd,&r);
				SetWindowPos(session_dialog_item,NULL,0,0,r.right,r.bottom*15/16,SWP_NOZORDER);
				SetWindowPos(GetDlgItem(hwnd,IDOK),NULL,0,r.bottom*15/16,r.right/2,r.bottom/16,SWP_NOZORDER);
				SetWindowPos(GetDlgItem(hwnd,IDCANCEL),NULL,r.right/2,r.bottom*15/16,r.right/2,r.bottom/16,SWP_NOZORDER);
			}
			break;*/
		case WM_COMMAND:
			if (LOWORD(wparam)==IDCANCEL)
				EndDialog(hwnd,0);
			else if ((HIWORD(wparam)==LBN_DBLCLK)||(!HIWORD(wparam)&&LOWORD(wparam)==IDOK))
				if ((session_dialog_return=SendMessage(session_dialog_item,LB_GETCURSEL,0,0))>=0)
				{
					SendMessage(session_dialog_item,LB_GETTEXT,session_dialog_return,(LPARAM)session_parmtr);
					EndDialog(hwnd,0);
				}
			break;
		default:
			return 0;
	}
	return 1;
}
int session_list(int i,char *s,char *t) // `s` is a list of ASCIZ entries, `i` is the default chosen item, `t` is the caption; returns -1 on error or 0..n-1 on success
{
	if (!*s) // empty?
		return -1;
	session_dialog_return=i;
	session_dialog_text=t;
	session_please();
	DialogBoxParam(GetModuleHandle(0),(LPCSTR)34003,session_hwnd,(DLGPROC)listproc,(LPARAM)s);
	return session_dialog_return; // the string is in `session_parmtr`
}

// file dialog ------------------------------------------------------ //

OPENFILENAME session_ofn;
int getftype(char *s) // <0 = not exist, 0 = file, >0 = directory
{
	if (!s||!*s) return -1; // not even a valid path!
	int i=GetFileAttributes(s); return i>0?i&FILE_ATTRIBUTE_DIRECTORY:i; // not i>=0!
}
int session_filedialog(char *r,char *s,char *t,int q,int f) // auxiliar function, see below
{
	memset(&session_ofn,0,sizeof(session_ofn));
	session_ofn.lStructSize=sizeof(OPENFILENAME);
	session_ofn.hwndOwner=session_hwnd;
	if (!r) r=session_path; // NULL path = default!
	if (r!=(char*)session_tmpstr)
		strcpy(session_tmpstr,r); // copy path, if required
	int i=strlen(session_tmpstr); // sanitize path
	if (i&&session_tmpstr[--i]=='\\') // pure path?
		session_tmpstr[i]=0;
	*session_parmtr=0; // no file by default
	if ((i=getftype(session_tmpstr))<0)
		strcpy(session_tmpstr,"."); // invalid path: no path, no file
	else if (i>0)
		; // directory: path only
	else if (r=strrchr(session_tmpstr,'\\'))
		strcpy(session_parmtr,++r),*r=0; // file with path
	else
		strcpy(session_parmtr,session_tmpstr),strcpy(session_tmpstr,"."); // file without path
	strcpy(session_substr,s);
	strcpy(&session_substr[strlen(s)+1],s); // one NULL char between two copies of the same string
	session_substr[strlen(s)*2+2]=session_substr[strlen(s)*2+3]=0;
	// tmpstr: "C:\ABC"; parmtr: "XYZ.EXT"; substr: "*.EX1;*.EX2"(x2)
	session_ofn.lpstrFilter=session_substr;
	session_ofn.nFilterIndex=1;
	session_ofn.lpstrFile=session_parmtr;
	session_ofn.nMaxFile=sizeof(session_parmtr);
	session_ofn.lpstrInitialDir=session_tmpstr;
	session_ofn.lpstrTitle=t;
	session_ofn.lpstrDefExt=((t=strrchr(s,'.'))&&(*++t!='*'))?t:NULL;
	session_ofn.Flags=OFN_PATHMUSTEXIST|OFN_NONETWORKBUTTON|OFN_NOCHANGEDIR|(q?OFN_OVERWRITEPROMPT:(OFN_FILEMUSTEXIST|f));
	session_please();
	return q?GetSaveFileName(&session_ofn):GetOpenFileName(&session_ofn);
}
#define session_filedialog_get_readonly() (session_ofn.Flags&OFN_READONLY)
#define session_filedialog_set_readonly(q) (q?(session_ofn.Flags|=OFN_READONLY):(session_ofn.Flags&=~OFN_READONLY))
char *session_newfile(char *r,char *s,char *t) // "Create File" | ...and returns NULL on failure, or a string on success.
	{ return session_filedialog(r,s,t,1,0)?session_parmtr:NULL; }
char *session_getfile(char *r,char *s,char *t) // "Open a File" | lists files in path `r` matching pattern `s` under caption `t`, etc.
	{ return session_filedialog(r,s,t,0,OFN_HIDEREADONLY)?session_parmtr:NULL; }
char *session_getfilereadonly(char *r,char *s,char *t,int q) // "Open a File" with Read Only option | lists files in path `r` matching pattern `s` under caption `t`; `q` is the default Read Only value, etc.
	{ return session_filedialog(r,s,t,0,q?OFN_READONLY:0)?session_parmtr:NULL; }

// final definitions ------------------------------------------------ //

// dummy SDL2 definitions
#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER SDL_LIL_ENDIAN

// main-WinMain bootstrap
#ifdef DEBUG
#define BOOTSTRAP
#else
#ifndef __argc
	extern int __argc; extern char **__argv; // GCC5's -std=gnu99 does NOT define them by default, despite being part of STDLIB.H and MSVCRT!
#endif
#define BOOTSTRAP int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) { return main(__argc,__argv); }
#endif

#endif // =========================== END OF WINDOWS 5.0+ DEFINITIONS //
