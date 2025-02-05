 //  ####  ######    ####  #######   ####    ----------------------- //
//  ##  ##  ##  ##  ##  ##  ##   #  ##  ##  CPCEC, plain text Amstrad //
// ##       ##  ## ##       ## #   ##       CPC emulator written in C //
// ##       #####  ##       ####   ##       as a postgraduate project //
// ##       ##     ##       ## #   ##       by Cesar Nicolas-Gonzalez //
//  ##  ##  ##      ##  ##  ##   #  ##  ##  since 2018-12-01 till now //
 //  ####  ####      ####  #######   ####    ----------------------- //

// SDL2 is the second supported platform; compiling the emulator needs
// "$(CC) -DSDL2 -xc cpcec.c -lSDL2" for GCC, TCC et al.
// Support for Unix-like systems is provided by providing different
// snippets of code upon whether the symbol "_WIN32" exists or not.

// START OF SDL 2.0+ DEFINITIONS ==================================== //

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED // required!
#endif

#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef _WIN32
	#define STRMAX 288 // widespread in Windows
	#define PATHCHAR '\\' // WIN32
	#include <windows.h> // FindFirstFile...
	#include <io.h> // _chsize(),_fileno()...
	#define fsetsize(f,l) _chsize(_fileno(f),(l))
	#define strcasecmp _stricmp // SDL_strcasecmp
#else
	#ifdef PATH_MAX
		#define STRMAX PATH_MAX
	#else
		#define STRMAX 512 // MacOS lacks PATH_MAX; TCC's LIMITS.H sets it to 512 and seems safe, with the exception of realpath()
	#endif
	#define PATHCHAR '/' // POSIX
	#include <dirent.h> // opendir()...
	#include <sys/stat.h> // stat()...
	#include <unistd.h> // ftruncate(),fileno()...
	#define fsetsize(f,l) (!ftruncate(fileno(f),(l)))
	#define BYTE Uint8
	#define WORD Uint16
	#define DWORD Uint32
#endif

#define MESSAGEBOX_WIDETAB "\t\t" // rely on monospace font

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

VIDEO_UNIT *video_frame,*menus_frame,*video_blend; // video and UI frames, allocated on runtime
AUDIO_UNIT *audio_frame,audio_buffer[AUDIO_LENGTH_Z*AUDIO_CHANNELS]; // audio frame
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
BYTE session_intzoom=0; int session_joybits=0;
FILE *session_wavefile=NULL; // audio recording is done on each session update

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

// SDL2 follows the USB keyboard standard, so we're using the same values here!

// function keys
#define	KBCODE_F1	 58
#define	KBCODE_F2	 59
#define	KBCODE_F3	 60
#define	KBCODE_F4	 61
#define	KBCODE_F5	 62
#define	KBCODE_F6	 63
#define	KBCODE_F7	 64
#define	KBCODE_F8	 65
#define	KBCODE_F9	 66
#define	KBCODE_F10	 67
#define	KBCODE_F11	 68
#define	KBCODE_F12	 69
// leftmost keys
#define	KBCODE_ESCAPE	 41
#define	KBCODE_TAB	 43
#define	KBCODE_CAP_LOCK	 57
#define	KBCODE_L_SHIFT	225
#define	KBCODE_L_CTRL	224
//#define KBCODE_L_ALT	226 // trapped by Win32
// alphanumeric row 1
#define	KBCODE_1	 30
#define	KBCODE_2	 31
#define	KBCODE_3	 32
#define	KBCODE_4	 33
#define	KBCODE_5	 34
#define	KBCODE_6	 35
#define	KBCODE_7	 36
#define	KBCODE_8	 37
#define	KBCODE_9	 38
#define	KBCODE_0	 39
#define	KBCODE_CHR1_1	 45
#define	KBCODE_CHR1_2	 46
// alphanumeric row 2
#define	KBCODE_Q	 20
#define	KBCODE_W	 26
#define	KBCODE_E	  8
#define	KBCODE_R	 21
#define	KBCODE_T	 23
#define	KBCODE_Y	 28
#define	KBCODE_U	 24
#define	KBCODE_I	 12
#define	KBCODE_O	 18
#define	KBCODE_P	 19
#define	KBCODE_CHR2_1	 47
#define	KBCODE_CHR2_2	 48
// alphanumeric row 3
#define	KBCODE_A	  4
#define	KBCODE_S	 22
#define	KBCODE_D	  7
#define	KBCODE_F	  9
#define	KBCODE_G	 10
#define	KBCODE_H	 11
#define	KBCODE_J	 13
#define	KBCODE_K	 14
#define	KBCODE_L	 15
#define	KBCODE_CHR3_1	 51
#define	KBCODE_CHR3_2	 52
#define	KBCODE_CHR3_3	 49
// alphanumeric row 4
#define	KBCODE_Z	 29
#define	KBCODE_X	 27
#define	KBCODE_C	  6
#define	KBCODE_V	 25
#define	KBCODE_B	  5
#define	KBCODE_N	 17
#define	KBCODE_M	 16
#define	KBCODE_CHR4_1	 54
#define	KBCODE_CHR4_2	 55
#define	KBCODE_CHR4_3	 56
#define	KBCODE_CHR4_4	 53
#define	KBCODE_CHR4_5	100
// rightmost keys
#define	KBCODE_SPACE	 44
#define	KBCODE_BKSPACE	 42
#define	KBCODE_ENTER	 40
#define	KBCODE_R_SHIFT	229
#define	KBCODE_R_CTRL	228
// #define KBCODE_R_ALT	230 // trapped by Win32
// extended keys
// #define KBCODE_PRINT	 70 // trapped by Win32
#define	KBCODE_SCR_LOCK	 71
#define	KBCODE_HOLD	 72
#define	KBCODE_INSERT	 73
#define	KBCODE_DELETE	 76
#define	KBCODE_HOME	 74
#define	KBCODE_END	 77
#define	KBCODE_PRIOR	 75
#define	KBCODE_NEXT	 78
#define	KBCODE_UP	 82
#define	KBCODE_DOWN	 81
#define	KBCODE_LEFT	 80
#define	KBCODE_RIGHT	 79
#define	KBCODE_NUM_LOCK	 83
// numeric keypad
#define	KBCODE_X_7	 95
#define	KBCODE_X_8	 96
#define	KBCODE_X_9	 97
#define	KBCODE_X_4	 92
#define	KBCODE_X_5	 93
#define	KBCODE_X_6	 94
#define	KBCODE_X_1	 89
#define	KBCODE_X_2	 90
#define	KBCODE_X_3	 91
#define	KBCODE_X_0	 98
#define	KBCODE_X_DOT	 99
#define	KBCODE_X_ENTER	 88
#define	KBCODE_X_ADD	 87
#define	KBCODE_X_SUB	 86
#define	KBCODE_X_MUL	 85
#define	KBCODE_X_DIV	 84

const BYTE kbd_k2j[]= // these keys can simulate a 4-button joystick
	{ KBCODE_UP, KBCODE_DOWN, KBCODE_LEFT, KBCODE_RIGHT, KBCODE_Z, KBCODE_X, KBCODE_C, KBCODE_V };

unsigned char kbd_map[256]; // key-to-key translation map

// general engine functions and procedures -------------------------- //

int session_user(int k); // handle the user's commands; 0 OK, !0 ERROR. Must be defined later on!
void session_debug_show(void);
int session_debug_user(int k); // debug logic is a bit different: 0 UNKNOWN COMMAND, !0 OK
int debug_xlat(int k); // translate debug keys into codes. Must be defined later on!
INLINE void audio_playframe(int q,AUDIO_UNIT *ao); // handle the sound filtering; is defined in CPCEC-RT.H!
int session_audioqueue; // unlike in Windows, we cannot use the audio device as the timer in SDL2

void session_please(void) // stop activity for a short while
{
	if (!session_wait)
	{
		if (session_audio)
			SDL_PauseAudioDevice(session_audio,1);
		//video_framecount=-1;
		session_wait=1;
	}
}

void session_kbdclear(void)
{
	session_joybits=0;
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

void *session_joy=NULL; int session_pad; // SDL_Joystick + SDL_GameController
SDL_Window *session_hwnd=NULL;

// unlike Windows, where the user interface is internally provided by the system and as well as the compositing work,
// we must provide our own UI here, and this means we must use one canvas for the emulation and another one for the UI.
// we show the first canvas during normal operation, but switch to the second one when the UI is active.
// the graphical debugger effectively behaves as a temporary substitute of the emulation canvas.
// notice that SDL_Texture, unlike SDL_Surface, doesn't rely on anything like SDL_GetWindowSurface()

VIDEO_UNIT *debug_frame;
BYTE debug_buffer[DEBUG_LENGTH_X*DEBUG_LENGTH_Y]; // [0] can be a valid character, 128 (new redraw required) or 0 (redraw not required)
SDL_Texture *session_dbg=NULL;
#define session_hidemenu *debug_buffer
SDL_Texture *session_dib=NULL,*session_gui_dib=NULL; SDL_Renderer *session_blitter=NULL;
SDL_Rect session_ideal; // used for calculations, see below

void session_backupvideo(VIDEO_UNIT *t); // make a clipped copy of the current screen. Must be defined later on!
void session_redraw(BYTE q)
{
	int xx,yy; // calculate window area
	SDL_GetRendererOutputSize(session_blitter,&session_ideal.w,&session_ideal.h);
	if ((xx=session_ideal.w)>0&&(yy=session_ideal.h)>0) // don't redraw invalid windows!
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
		session_ideal.x=(session_ideal.w-xx)/2; session_ideal.w=xx;
		session_ideal.y=(session_ideal.h-yy)/2; session_ideal.h=yy;
		SDL_Texture *s; VIDEO_UNIT *t; int ox,oy;
		if (!q)
			s=session_gui_dib,ox=0,oy=0;
		else if (session_signal&SESSION_SIGNAL_DEBUG)
			s=session_dbg,ox=0,oy=0;
		else
			s=session_dib,ox=VIDEO_OFFSET_X,oy=VIDEO_OFFSET_Y;
		SDL_Rect r;
		r.x=ox; r.w=VIDEO_PIXELS_X;
		r.y=oy; r.h=VIDEO_PIXELS_Y;
		SDL_UnlockTexture(s); // prepare for sending
		if (SDL_RenderCopy(session_blitter,s,&r,&session_ideal)>=0) // send! (warning: this operation has a memory leak on several SDL2 versions)
			SDL_RenderPresent(session_blitter); // update window!
		int dummy; SDL_LockTexture(s,NULL,(void**)&t,&dummy); // allow editing again
		if (!q)
			menus_frame=t;
		else if (session_signal&SESSION_SIGNAL_DEBUG)
			debug_frame=t;
		else
		{
			dummy=video_target-video_frame; // the old cursor...
			video_frame=t;
			video_target=video_frame+dummy; // ...may need to move!
		}
	}
}

#ifdef __TINYC__
#define session_clrscr() 0 // Tiny C Compiler causes a segmentation fault (!?)
#else
#define session_clrscr() SDL_RenderClear(session_blitter) // defaults to black
#endif
int session_fullscreen=0;
void session_togglefullscreen(void)
{
	SDL_SetWindowFullscreen(session_hwnd,session_fullscreen=((SDL_GetWindowFlags(session_hwnd)&SDL_WINDOW_FULLSCREEN_DESKTOP)?0:SDL_WINDOW_FULLSCREEN_DESKTOP));
	session_clrscr(); // SDL2 cleans up, but not on all systems
	session_dirtymenu=1; // update "Full screen" option (if any)
}

// extremely tiny graphical user interface: SDL2 provides no widgets! //

#define SESSION_UI_HEIGHT 14

BYTE session_ui_chrs[96*SESSION_UI_HEIGHT];//,session_ui_chr_size[96];
void session_ui_makechrs(void)
{
	memset(session_ui_chrs,0,sizeof(session_ui_chrs));
	for (int i=0;i<96;++i)
		for (int j=0;j<ONSCREEN_SIZE;++j)
		{
			int z=onscreen_chrs[i*ONSCREEN_SIZE+j];
			session_ui_chrs[i*SESSION_UI_HEIGHT+(SESSION_UI_HEIGHT-ONSCREEN_SIZE)/2+j]=z|(z>>1); // normal, not thin or bold
		}
	/*{ // experimental monospace-to-proportional font rendering
		int bits=0;
		for (int j=0;j<SESSION_UI_HEIGHT;++j)
			bits|=session_ui_chrs[i*SESSION_UI_HEIGHT+j];
		if (bits)
		{
			int k=0,l=8+1; // extra space after char
			while (bits<128)
				++k,bits<<=1;
			while (!(bits&1))
				--l,bits>>=1;
			session_ui_chr_size[i]=l;
			for (int j=0;j<SESSION_UI_HEIGHT;++j)
				l=session_ui_chrs[i*SESSION_UI_HEIGHT+j]<<k;
		}
	}
	session_ui_chr_size[0]=session_ui_chr_size[1];*/ // space is as wide as "!"
}

BYTE session_ui_menudata[1<<12],session_ui_menusize; // encoded menu data
#ifdef _WIN32
int session_ui_drives=0; char session_ui_drive[]="::\\";
#else
SDL_Surface *session_ui_icon=NULL; // the window icon
#endif

void session_fillrect(int rx,int ry,int rw,int rh,VIDEO_UNIT a)
{
	for (int y=0;y<rh;++y)
	{
		VIDEO_UNIT *p=&menus_frame[(y+ry)*VIDEO_PIXELS_X+rx];
		for (int x=0;x<rw;++x)
			p[x]=a;
	}
}
#define session_ui_fillrect(x,y,w,h,a) session_fillrect((x)*8,(y)*SESSION_UI_HEIGHT,(w)*8,(h)*SESSION_UI_HEIGHT,(a))

void session_ui_drawframes(int x,int y,int w,int h)
{
	x*=8; w*=8; y*=SESSION_UI_HEIGHT; h*=SESSION_UI_HEIGHT;
	int rx,ry,rw,rh;
	// top border
	rx=x-2; ry=y-2;
	rw=w+4; rh=2; session_fillrect(rx,ry,rw,rh,VIDEO1(0x00C0C0C0));
	// bottom border
	ry=y+h; session_fillrect(rx,ry,rw,rh,VIDEO1(0x00404040));
	// left border
	ry=y-1;
	rw=2; rh=h+2; session_fillrect(rx,ry,rw,rh,VIDEO1(0x00808080));
	// right border
	rx=x+w; session_fillrect(rx,ry,rw,rh,VIDEO1(0x00808080));
}
int session_ui_printglyph(VIDEO_UNIT *p,BYTE z)
{
	BYTE const *r=&session_ui_chrs[((z-32)&127)*SESSION_UI_HEIGHT];
	const int w=8; //=session_ui_chr_size[(z-32)&127];
	VIDEO_UNIT q0=VIDEO1(0x00FFFFFF),q1=VIDEO1(0);
	if (z&128)
		q0=VIDEO1(0),q1=VIDEO1(0x00FFFFFF);
	for (int yy=0;yy<SESSION_UI_HEIGHT;++yy)
	{
		BYTE rr=*r++;
		for (int xx=0;xx<w;++xx)
			*p++=(rr&(128>>xx))?q1:q0;
		p+=VIDEO_PIXELS_X-w;
	}
	return w; // pixel width
}
int session_ui_printasciz(char *s,int x,int y,int prae,int w,int post,int q)
{
	if (q) q=128; if (w<0) w=strlen(s);
	int n=prae+w+post; // remember for later
	VIDEO_UNIT *t=&menus_frame[x*8+y*SESSION_UI_HEIGHT*VIDEO_PIXELS_X];
	while (prae-->0)
		t+=session_ui_printglyph(t,' '+q);
	int i=w-strlen(s);
	if (i>=0)
	{
		post+=i;
		while (i=*s++)
			t+=session_ui_printglyph(t,i+q);
	}
	else
	{
		w-=2; // ellipsis, see below
		while (w-->0)
			t+=session_ui_printglyph(t,(*s++)+q);
		t+=session_ui_printglyph(t,127+q); // ellipsis,
		t+=session_ui_printglyph(t,'.'+q); // see above
	}
	while (post-->0)
		t+=session_ui_printglyph(t,' '+q);
	return n;
}

int session_ui_base_x,session_ui_base_y,session_ui_size_x,session_ui_size_y; // used to calculate mouse clicks relative to widget
int session_ui_maus_x,session_ui_maus_y; // mouse X+Y, when the "key" is -1 (move), -2 (left click) or -3 (right click)
int session_ui_char,session_ui_shift; // ASCII+Shift of the latest keystroke
#define SESSION_UI_MAXX (VIDEO_PIXELS_X/8-6)
#define SESSION_UI_MAXY (VIDEO_PIXELS_Y/SESSION_UI_HEIGHT-2)

int session_ui_exchange(void) // update window and wait for a keystroke
{
	session_ui_char=0; for (SDL_Event event;SDL_WaitEvent(&event);)
		switch (event.type)
		{
			case SDL_WINDOWEVENT:
				if (event.window.event==SDL_WINDOWEVENT_EXPOSED)
					SDL_RenderPresent(session_blitter);//SDL_UpdateWindowSurface(session_hwnd); // fast redraw
				break;
			case SDL_MOUSEMOTION:
			case SDL_MOUSEBUTTONUP:
			case SDL_MOUSEBUTTONDOWN:
				session_ui_maus_x=(event.button.x-session_ideal.x)*VIDEO_PIXELS_X/session_ideal.w/8-session_ui_base_x,session_ui_maus_y=(event.button.y-session_ideal.y)*VIDEO_PIXELS_Y/session_ideal.h/SESSION_UI_HEIGHT-session_ui_base_y;
				return event.type==SDL_MOUSEBUTTONDOWN?event.button.button==SDL_BUTTON_RIGHT?-3:-2:-1;
			case SDL_MOUSEWHEEL:
				if (event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED) event.wheel.y=-event.wheel.y;
				return event.wheel.y>0?KBCODE_PRIOR:event.wheel.y<0?KBCODE_NEXT:0;
			case SDL_KEYDOWN:
				session_ui_shift=!!(event.key.keysym.mod&KMOD_SHIFT);
				return event.key.keysym.mod&KMOD_ALT?0:event.key.keysym.scancode;
			case SDL_TEXTINPUT: // always follows SDL_KEYDOWN
				if ((session_ui_char=event.text.text[0])&128) // UTF-8? (not that it matters, we stick to ASCII)
					session_ui_char=128+(session_ui_char&1)*64+(event.text.text[1]&63);
				return 0;
			case SDL_QUIT:
				return KBCODE_ESCAPE;
		}
}

void session_ui_loop(void) // get background painted again to erase old widgets
{
	if (session_signal&SESSION_SIGNAL_DEBUG)
		memcpy(menus_frame,debug_frame,sizeof(VIDEO_UNIT)*VIDEO_PIXELS_X*VIDEO_PIXELS_Y); // use the debugger as the background, rather than the emulation
	else
		session_backupvideo(menus_frame);
}
void session_ui_init(void) { session_kbdclear(); session_please(); session_ui_loop(); }
void session_ui_exit(void) // wipe all widgets and restore the window contents
{
	if (session_signal&(SESSION_SIGNAL_DEBUG|SESSION_SIGNAL_PAUSE)) // redraw if waiting
		session_redraw(1);
}

void session_ui_menu(void) // show the menu and set session_event accordingly
{
	if (!session_ui_menusize)
		return;
	session_ui_init();
	int menuxs[64],events[64]; // more than 64 menus, or 64 items in a menu? overkill!!!
	int menu=0,menus=0,menuz=-1,item=0,items=0,itemz=-1,itemx=0,itemw=0;
	char *zz,*z;

	int done,ox=session_ui_base_x=1,oy=session_ui_base_y=1,q=1;
	session_event=0x8000;
	while (session_event==0x8000) // empty menu items must be ignored
	{
		done=0;
		while (!done) // redraw menus and items as required, then obey the user
		{
			if (menuz!=menu)
			{
				menuz=menu; item=items=0;
				q=itemz=-1; int menux=menus=0;
				BYTE *m=session_ui_menudata;
				while (*m) // scan menu data for menus
				{
					if (menus==menu)
					{
						itemx=menux;
						menux+=session_ui_printasciz(m,ox+menux,oy+0,1,-1,1,1);
					}
					else
						menux+=session_ui_printasciz(m,ox+menux,oy+0,1,-1,1,0);
					int i=0,j=0,k=0;
					while (*m++) // skip menu name
						++j;
					while (*m++|*m++) // skip menu items
					{
						++k; i=0;
						while (*m++) // skip item name
							++i;
						if (j<i)
							j=i;
					}
					if (menus==menu)
						itemw=j,items=k;
					menuxs[menus++]=menux;
				}
				session_ui_drawframes(ox,oy,menux,1);
			}
			if (itemz!=item)
			{
				itemz=item;
				q=1; int i=0;
				BYTE *m=session_ui_menudata;
				while (*m) // scan menu data for items
				{
					while (*m++) // skip menu name
						;
					int j,k=0;
					if (i==menu)
						zz=m;
					while (j=*m++<<8,j+=*m++,j)
					{
						if (i==menu)
						{
							if (item==k)
								z=&m[-2],session_event=j;
							session_ui_printasciz(m,itemx+ox+0,1+oy+k,1,itemw,1,item==k);
							events[k++]=j;
						}
						while (*m++) // skip item name
							;
					}
					++i;
				}
				session_ui_drawframes(itemx+ox,1+oy,itemw+2,items);
			}
			if (q)
				session_redraw(q=0);
			switch (session_ui_exchange())
			{
				case KBCODE_UP:
					if (--item<0)
				case KBCODE_END:
						item=items-1;
					break;
				case KBCODE_DOWN:
					if (++item>=items)
				case KBCODE_HOME:
						item=0;
					break;
				case KBCODE_PRIOR:
				case KBCODE_LEFT:
					if (--menu<0)
						menu=menus-1;
					break;
				case KBCODE_NEXT:
				case KBCODE_RIGHT:
					if (++menu>=menus)
						menu=0;
					break;
				case KBCODE_ESCAPE:
				case KBCODE_F10:
					done=-1;
					break;
				case KBCODE_X_ENTER:
				case KBCODE_ENTER:
					//if (session_event!=0x8000) // gap?
						done=1;
					break;
				case -1: // mouse move
					if (!session_ui_maus_y&&session_ui_maus_x>=0&&session_ui_maus_x<session_ui_menusize) // select menu?
					{
						menu=menus; while (menu>0&&session_ui_maus_x<menuxs[menu-1]) --menu;
					}
					else if (session_ui_maus_y>0&&session_ui_maus_y<=items&&session_ui_maus_x>=itemx&&session_ui_maus_x<itemx+itemw+2) // select item?
						item=session_ui_maus_y-1; // hover, not click!
					break;
				case -3: // mouse right click
				case -2: // mouse left click
					if (!session_ui_maus_y&&session_ui_maus_x>=0&&session_ui_maus_x<session_ui_menusize) // select menu?
						; // already done above
					else if (session_ui_maus_y>0&&session_ui_maus_y<=items&&session_ui_maus_x>=itemx&&session_ui_maus_x<itemx+itemw+2) // select item?
						session_event=events[session_ui_maus_y-1],done=1;
					else // quit?
						session_event=0,done=1;
					break;
				default:
					if (session_ui_char>=32&&session_ui_char<=128)
						for (int o=lcase(session_ui_char),n=items;n;--n)
						{
							++z,++z; // skip ID
							while (*z++)
								;
							if (++item>=items)
								item=0,z=zz;
							if (lcase(z[4])==o)
								break;
						}
					break;
			}
			if (menuz!=menu)
				session_ui_loop(); // redrawing the items doesn't need any wiping, unlike the menus
		}
	}
	session_ui_exit();
	if (done<0)
		session_event=0;
	else
		session_shift=!!(session_event&0x4000),session_event&=0xBFFF; // cfr.shift
	return;
}

int session_ui_text(char *s,char *t,char q) // see session_message
{
	if (!s||!t)
		return -1;
	session_ui_init();
	int i,j,textw=0,texth=1+2; // caption + blank + text + blank
	unsigned char *m=t;
	while (*m++)
		++textw;
	i=0;
	m=s;
	while (*m) // get full text width
	{
		if (*m=='\n')
		{
			++texth;
			if (textw<i)
				textw=i;
			i=0;
		}
		else if (*m=='\t')
			i=(i|7)+1;
		else
			++i;
		++m;
	}
	if (textw<i)
		textw=i;
	if (textw>SESSION_UI_MAXX)
		textw=SESSION_UI_MAXX;
	if (i)
		++texth;
	if (q)
		textw+=q=6; // 6: four characters for the icon, plus one extra char per side

	textw+=2; // include left+right margins
	int textx=((VIDEO_PIXELS_X/8)-textw)/2,texty=((VIDEO_PIXELS_Y/SESSION_UI_HEIGHT)-texth)/2;
	session_ui_drawframes(session_ui_base_x=textx,session_ui_base_y=texty,session_ui_size_x=textw,session_ui_size_y=texth);
	textw-=2;

	session_ui_printasciz(t,textx+0,texty+0,1,textw,1,1);
	i=j=0;
	m=session_parmtr;
	session_ui_printasciz("",textx+q,texty+(++i),1,textw-q,1,0); // blank
	while (*s) // render text proper
	{
		int k=(*m++=*s++);
		++j;
		if (k=='\n')
		{
			m[-1]=j=0;
			session_ui_printasciz(m=session_parmtr,textx+q,texty+(++i),1,textw-q,1,0);
		}
		else if (k=='\t')
		{
			m[-1]=' ';
			while (j&7)
				++j,*m++=' ';
		}
	}
	if (m!=session_parmtr)
	{
		*m=0;
		session_ui_printasciz(m=session_parmtr,textx+q,texty+(++i),1,textw-q,1,0);
	}
	session_ui_printasciz("",textx+q,texty+(++i),1,textw-q,1,0); // blank
	if (q) // draw icon?
	{
		session_ui_fillrect(textx,texty+1,q,texth-1,VIDEO1(0x00C0C0C0));
		//for (int z=0;z<q;++z) session_ui_fillrect(textx+z,texty+1,1,texth-1,VIDEO1(0x00010101*((0xFF*z+0xC0*(q-z)+q/2)/q))); // gradient!
		VIDEO_UNIT *tgt=&menus_frame[(texty+2)*SESSION_UI_HEIGHT*VIDEO_PIXELS_X+(textx+1)*8];
		for (int z=0,y=0;y<32;++y,tgt+=VIDEO_PIXELS_X-32)
			for (int a,x=0;x<32;++x,++tgt)
				if ((a=session_icon32xx16[z++])&0x8000)
					*tgt=VIDEO1((a&0x00F)*0x0011+(a&0x0F0)*0x00110+(a&0xF00)*0x1100);
	}
	session_redraw(0);
	for (;;)
		switch (session_ui_exchange())
		{
			case -3: // mouse right click
			case -2: // mouse left click
				//if (session_ui_maus_x>=0&&session_ui_maus_x<session_ui_size_x&&session_ui_maus_y>=0&&session_ui_maus_y<session_ui_size_y) break; // click anywhere, it doesn't matter
			case KBCODE_ESCAPE:
			case KBCODE_SPACE:
			case KBCODE_X_ENTER:
			case KBCODE_ENTER:
				session_ui_exit();
				return 0;
		}
}

int session_ui_input(char *s,char *t) // see session_input
{
	int i,j,first,dirty=1,textw=SESSION_UI_MAXX;
	if ((i=j=first=strlen(s))>=textw)
		return -1; // error!
	strcpy(session_substr,s);

	session_ui_init();
	textw+=2; // include left+right margins
	int textx=((VIDEO_PIXELS_X/8)-textw)/2,texty=((VIDEO_PIXELS_Y/SESSION_UI_HEIGHT)-2)/2;
	session_ui_drawframes(session_ui_base_x=textx,session_ui_base_y=texty,session_ui_size_x=textw,session_ui_size_y=2);
	textw-=2;
	session_ui_printasciz(t,textx+0,texty+0,1,textw,1,1);
	int done=0;
	while (!done)
	{
		if (dirty)
		{
			if (first)
			{
				char *m=session_substr; while (*m) *m^=128,++m; // inverse video for all text
				session_ui_printasciz(session_substr,textx+0,texty+1,1,textw,1,0);
				m=session_substr; while (*m) *m^=128,++m; // restore video
			}
			else if (i<j)
			{
				session_substr[i]^=128; // inverse video
				session_ui_printasciz(session_substr,textx+0,texty+1,1,textw,1,0);
				session_substr[i]^=128; // restore video
			}
			else
			{
				session_substr[i]=160; session_substr[i+1]=0; // inverse space
				session_ui_printasciz(session_substr,textx+0,texty+1,1,textw,1,0);
				session_substr[i]=0; // end of string
			}
			session_redraw(0);
			dirty=0;
		}
		switch (session_ui_exchange())
		{
			case KBCODE_LEFT:
				if (first||--i<0)
			case KBCODE_HOME:
					i=0;
				first=0;
				dirty=1;
				break;
			case KBCODE_RIGHT:
				if (first||++i>j)
			case KBCODE_END:
					i=j;
				first=0;
				dirty=1;
				break;
			case -3: // mouse right click
			case -2: // mouse left click
				if (session_ui_maus_x<0||session_ui_maus_x>=session_ui_size_x||session_ui_maus_y<0||session_ui_maus_y>=session_ui_size_y)
					j=-1,done=1; // quit!
				else if (session_ui_maus_y==1)
				{
					first=0;
					if ((i=session_ui_maus_x-1)>j)
						i=j;
					else if (i<0)
						i=0;
				}
				dirty=1;
				break;
			case KBCODE_ESCAPE:
				j=-1;
				//break;
			case KBCODE_X_ENTER:
			case KBCODE_ENTER:
				done=1;
				break;
			case KBCODE_BKSPACE:
				if (first)
					*session_substr=i=j=0;
				else if (i>0)
				{
					memmove(&session_substr[i-1],&session_substr[i],j-i+1);
					--i; --j;
				}
				first=0;
				dirty=1;
				break;
			case KBCODE_DELETE:
				if (first)
					*session_substr=i=j=0;
				if (i<j)
				{
					memmove(&session_substr[i],&session_substr[i+1],j-i+1);
					--j;
				}
				first=0;
				dirty=1;
				break;
			default:
				if (session_ui_char>=32&&session_ui_char<=128&&j<textw-1)
				{
					if (first)
					{
						*session_substr=session_ui_char;
						session_substr[i=j=1]=0;
					}
					else
					{
						memmove(&session_substr[i+1],&session_substr[i],j-i+1);
						session_substr[i]=session_ui_char;
						++i; ++j;
					}
					first=0;
					dirty=1;
				}
				break;
		}
	}
	if (j>=0)
		strcpy(s,session_substr);
	session_ui_exit();
	return j;
}

int session_ui_list(int item,char *s,char *t,void x(void),int q) // see session_list
{
	int i,dirty=1,dblclk=0,items=0,itemz=-2,listw=0,listh,listz=0;
	char *m=t,*z=NULL;
	while (*m++)
		++listw;
	m=s;
	while (*m)
	{
		++items; i=0;
		while (*m++)
			++i;
		if (listw<i)
			listw=i;
	}
	if (!items)
		return -1;
	session_ui_init();
	if (listw>SESSION_UI_MAXX)
		listw=SESSION_UI_MAXX;
	if ((listh=items+1)>SESSION_UI_MAXY)
		listh=SESSION_UI_MAXY;
	int listx=((VIDEO_PIXELS_X/8)-listw-2)/2,listy=((VIDEO_PIXELS_Y/SESSION_UI_HEIGHT)-listh)/2;
	if (items>=listh) // list is long enough?
		if (item>=listh/2) // go to center?
			if ((listz=item-listh/2+1)>items-listh) // too deep?
				listz=items-listh+1;
	int done=0;
	while (!done)
	{
		if (dirty) // showing x() must request a redraw!
		{
			session_ui_drawframes(session_ui_base_x=listx,session_ui_base_y=listy,session_ui_size_x=listw+2,session_ui_size_y=listh);
			session_ui_printasciz(t,listx+0,listy+0,1,listw,1,1);
			dirty=0; itemz=~item;
		}
		if (itemz!=item)
		{
			itemz=item;
			if (listz>item)
				if ((listz=item)<0)
					listz=0; // allow index -1
			if (listz<item-listh+2)
				listz=item-listh+2;
			m=s; i=0;
			while (*m&&i<listz+listh-1)
			{
				if (i>=listz)
				{
					if (i==item)
						z=m;
					session_ui_printasciz(m,listx+0,listy+1+i-listz,1,listw,1,i==item);
				}
				if (*m) while (*m++)
					;
				++i;
			}
			session_redraw(0);
		}
		switch (session_ui_exchange())
		{
			case KBCODE_PRIOR:
			case KBCODE_LEFT:
				item-=listh-2;
			case KBCODE_UP:
				if (--item<0)
			case KBCODE_HOME:
					item=0;
				dblclk=0;
				break;
			case KBCODE_NEXT:
			case KBCODE_RIGHT:
				item+=listh-2;
			case KBCODE_DOWN:
				if (++item>=items)
			case KBCODE_END:
					item=items-1;
				dblclk=0;
				break;
			case KBCODE_TAB:
				if (q) // browse items if `q` is true
				{
					if (session_ui_shift)
					{
						if (--item<0) // prev?
							item=items-1; // wrap
					}
					else
					{
						if (++item>=items) // next?
							item=0; // wrap
					}
				}
				else if (x) // special context widget
					x(),dblclk=0,dirty=1;
				break;
			case -1: // mouse move
				if (q&&session_ui_maus_x>=0&&session_ui_maus_x<session_ui_size_x&&session_ui_maus_y>0&&session_ui_maus_y<=items) // single click mode?
					item=session_ui_maus_y<2?0:1;
				break;
			case -3: // mouse right click
				if (x&&session_ui_maus_x>=0&&session_ui_maus_x<session_ui_size_x&&session_ui_maus_y>0&&session_ui_maus_y<session_ui_size_y)
					x(),dblclk=0,dirty=1;
				else
			case -2: // mouse left click
				if (session_ui_maus_x<0||session_ui_maus_x>=session_ui_size_x||(items<listh&&(session_ui_maus_y<0||session_ui_maus_y>=session_ui_size_y)))
					item=-1,done=1; // quit!
				else if (session_ui_maus_y<1) // scroll up?
				{
					if ((item-=listh-2)<0)
						item=0;
					dblclk=0;
				}
				else if (session_ui_maus_y>=session_ui_size_y) // scroll down?
				{
					if ((item+=listh-2)>=items)
						item=items-1;
					dblclk=0;
				}
				else // select an item?
				{
					int button=listz+session_ui_maus_y-1;
					if (q||(item==button&&dblclk)) // `q` (single click mode, f.e. "YES"/"NO") or same item? (=double click)
						item=button,done=1;
					else // different item!
					{
						item=button; dblclk=1;
						if (item<0)
							item=0;
						else if (item>=items)
							item=items-1;
					}
				}
				break;
			case KBCODE_ESCAPE:
			case KBCODE_F10:
				item=-1;
				//break;
			case KBCODE_X_ENTER:
			case KBCODE_ENTER:
				done=1;
				break;
			default:
				if (session_ui_char>=32&&session_ui_char<=128)
				{
					int itemz=item,n=items;
					for (int o=lcase(session_ui_char);n;--n)
					{
						if (item<0)
							item=0,z=s;
						else
						{
							++item;
							while (*z++)
								;
						}
						if (!(*z))
							item=0,z=s;
						if (lcase(*z)==o)
							break;
					}
					if (!n)
						item=itemz; // allow rewinding to -1
					dblclk=0;
				}
				break;
		}
	}
	if (item>=0)
		strcpy(session_parmtr,z);
	session_ui_exit();
	return item;
}

int multiglobbing(char *w,char *t,int q); // multi-pattern globbing; must be defined later on!
int sortedinsert(char *t,int z,char *s); // ordered list inserting; must be defined later on!
int sortedsearch(char *t,int z,char *s); // ordered list searching; must be defined later on!
int session_ui_fileflags;
char *session_ui_filedialog_sanitizepath(char *s) // restore PATHCHAR at the end of a path name
{
	if (s&&*s)
	{
		char *t=s;
		while (*t)
			++t;
		if (t[-1]!=PATHCHAR)
			*t=PATHCHAR,t[1]=0;
	}
	return s;
}
void session_ui_filedialog_tabkey(void)
{
	char *l={"Read/Write\000Read-Only\000"};
	int i=session_ui_list(session_ui_fileflags&1,l,"File access",NULL,1);
	if (i>=0)
		session_ui_fileflags=i;
}
int getftype(char *s) // <0 = not exist, 0 = file, >0 = directory
{
	if (!s||!*s) return -1; // not even a valid path!
	#ifdef _WIN32
	int i=GetFileAttributes(s); return i>0?i&FILE_ATTRIBUTE_DIRECTORY:i; // not i>=0!
	#else
	struct stat a; int i=stat(s,&a); return i>=0?S_ISDIR(a.st_mode):i; // not i>0!
	#endif
}
int session_ui_filedialog(char *r,char *s,char *t,int q,int f) // see session_filedialog; q here means TAB is available or not, and f means the starting TAB value (q) or whether we're reading (!q)
{
	int i; char *m,*n,basepath[8<<9],pastname[STRMAX],basefind[STRMAX]; // realpath() is an exception, see below
	session_ui_fileflags=f;
	if (!r) r=session_path; // NULL path = default!
	if (m=strrchr(r,PATHCHAR))
		i=m[1],m[1]=0,strcpy(basepath,r),m[1]=i,strcpy(pastname,&m[1]);
	else
		*basepath=0,strcpy(pastname,r);
	for (;;)
	{
		i=0; m=session_scratch;

		#ifdef _WIN32
		if (getftype(basepath)<=0) // ensure that basepath exists and is a valid directory
		{
			GetCurrentDirectory(STRMAX,basepath); // fall back to current directory
			session_ui_filedialog_sanitizepath(basepath);
		}
		char *half=m;
		for (session_ui_drive[0]='A';session_ui_drive[0]<='Z';++session_ui_drive[0])
			if (session_ui_drives&(1<<(session_ui_drive[0]-'A')))
				++i,m+=sortedinsert(m,0,session_ui_drive); // append, already sorted
		if (basepath[3]) // not root directory?
			++i,m+=sortedinsert(m,0,"..\\"); // ".." + PATHCHAR
		half=m;
		WIN32_FIND_DATA wfd; HANDLE h; strcpy(basefind,basepath); strcat(basefind,"*");
		if ((h=FindFirstFile(basefind,&wfd))!=INVALID_HANDLE_VALUE)
		{
			do
				if (!(wfd.dwFileAttributes&FILE_ATTRIBUTE_HIDDEN)&&(wfd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) // reject invisible directories
					if (strcmp(n=wfd.cFileName,".")&&strcmp(n=wfd.cFileName,"..")) // add directory name with the separator at the end
						++i,m=&half[sortedinsert(half,m-half,session_ui_filedialog_sanitizepath(n))];
			while (FindNextFile(h,&wfd)&&m-(char *)session_scratch<sizeof(session_scratch)-STRMAX);
			FindClose(h);
		}
		half=m;
		if ((h=FindFirstFile(basefind,&wfd))!=INVALID_HANDLE_VALUE)
		{
			do
				if (!(wfd.dwFileAttributes&FILE_ATTRIBUTE_HIDDEN)&&!(wfd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) // reject invisible files
					if (multiglobbing(s,n=wfd.cFileName,1)) // add file name
						++i,m=&half[sortedinsert(half,m-half,n)];
			while (FindNextFile(h,&wfd)&&m-(char *)session_scratch<sizeof(session_scratch)-STRMAX);
			FindClose(h);
		}
		#else
		DIR *d; struct dirent *e;
		if (getftype(basepath)<=0) // ensure that basepath exists and is a valid directory
		{
			if (getcwd(basepath,STRMAX)) // fall back to current directory
				session_ui_filedialog_sanitizepath(basepath);
		}
		if (basepath[1]) // not root directory?
			++i,m+=sortedinsert(m,0,"../"); // ".." + PATHCHAR
		char *half=m;
		if (d=opendir(basepath))
		{
			while ((e=readdir(d))&&m-(char *)session_scratch<sizeof(session_scratch)-STRMAX)
				if (n=e->d_name,n[0]!='.') // reject ".*"
					if (getftype(strcat(strcpy(basefind,basepath),n))>0) // add directory name with the separator at the end
						++i,m=&half[sortedinsert(half,m-half,session_ui_filedialog_sanitizepath(n))];
			closedir(d);
		}
		half=m;
		if (d=opendir(basepath))
		{
			while ((e=readdir(d))&&m-(char *)session_scratch<sizeof(session_scratch)-STRMAX)
				if (n=e->d_name,n[0]!='.'&&multiglobbing(s,n,1)) // reject ".*"
					if (!getftype(strcat(strcpy(basefind,basepath),n))) // add file name
						++i,m=&half[sortedinsert(half,m-half,n)];
			closedir(d);
		}
		#endif

		if (!q&&!f)
			m=&m[sortedinsert(m,0,"** NEW **")]; // point at "*" by default on SAVE, rather than on any past names
		if (!q&&!f&&!*pastname)
			; // no previous filename? keep pointing at "NEW"
		else
			half=session_scratch,i=sortedsearch(half,m-half,pastname); // look for past name, if any
		*m=0; // end of list

		sprintf(session_tmpstr,"%s -- %s",t,basepath); //,s
		if (session_ui_list(i,session_scratch,session_tmpstr,q?session_ui_filedialog_tabkey:NULL,0)<0)
			return 0; // user closed the dialog!

		m=session_parmtr;
		while (*m)
			++m;
		if (m[-1]==PATHCHAR) // the user chose a directory
		{
			strcat(strcpy(session_scratch,basepath),session_parmtr);
			#ifdef _WIN32
			if (session_parmtr[1]==':')
				*pastname=0,strcpy(basepath,session_parmtr);
			else
			{
				if (!strcmp(session_parmtr,strcpy(pastname,"..\\")))
				{
					m=strrchr(basepath,PATHCHAR);
					while (m&&m>basepath&&*--m!=PATHCHAR)
						;
					if (m)
						strcpy(pastname,&m[1]); // allow returning to previous directory
					else
						*pastname=0;
				}
				GetFullPathName(session_scratch,STRMAX,basepath,&m);
			}
			#else
			if (!strcmp(session_parmtr,strcpy(pastname,"../")))
			{
				m=strrchr(basepath,PATHCHAR);
				while (m&&m>basepath&&*--m!=PATHCHAR)
					;
				if (m)
					strcpy(pastname,&m[1]); // allow returning to previous directory
				else
					*pastname=0;
			}
			!realpath(session_scratch,basepath); // realpath() requires the target to be at least 4096 bytes long in Linux 4.10.0-38-generic, Linux 4.15.0-96-generic...!
			#endif
			session_ui_filedialog_sanitizepath(basepath);
		}
		else // the user chose a file
		{
			if (*session_parmtr=='*') // the user wants to create a file
			{
				*session_parmtr=0;
				if (session_ui_input(session_parmtr,t)>0)
				{
					if (multiglobbing(s,session_parmtr,1)!=1) // not the first extension?
					{
						m=session_parmtr; n=s;
						while (*m) // go to end of target string
							++m;
						while (*n!='*') // look for asterisks in source
							++n;
						while (*n=='*') // skip all asterisks
							++n;
						while (*n&&*n!=';') // copy till end of extension
							*m++=*n++;
						*m=0; // and so we append the default extension
					}
				}
				else
					return 0; // failure, quit!
			}
			strcat(strcpy(session_scratch,basepath),session_parmtr); // build full name
			strcpy(session_parmtr,session_scratch); // copy to target, but keep the source...
			if (q||f||getftype(session_parmtr)<0)
				return 1; // the user succesfully chose a file, either extant for reading or new for writing!
			if (session_ui_list(0,"YES\000NO\000","Overwrite?",NULL,1)==0)
				return strcpy(session_parmtr,session_scratch),1; // ...otherwise session_parmtr would hold "YES"!
		}
	}
}

// create, handle and destroy session ------------------------------- //

SDL_version sdl_version; char session_version[8];
INLINE char* session_create(char *s) // create video+audio devices and set menu; 0 OK, !0 ERROR
{
	SDL_SetMainReady();
	SDL_GetVersion(&sdl_version); sprintf(session_version,"%i.%i.%i",sdl_version.major,sdl_version.minor,sdl_version.patch);
	if (SDL_Init(SDL_INIT_EVENTS|SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER|SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER)<0)
		return (char *)SDL_GetError();
	if (!(session_hwnd=SDL_CreateWindow(session_caption,SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,VIDEO_PIXELS_X,VIDEO_PIXELS_Y,0))
		||!(video_blend=malloc(sizeof(VIDEO_UNIT)*VIDEO_PIXELS_Y/2*VIDEO_PIXELS_X)))
		return SDL_Quit(),(char *)SDL_GetError();
	if (session_hardblit=1,session_softblit||!(session_blitter=SDL_CreateRenderer(session_hwnd,-1,SDL_RENDERER_ACCELERATED)))
		if (session_hardblit=0,session_softblit=1,!(session_blitter=SDL_CreateRenderer(session_hwnd,-1,SDL_RENDERER_SOFTWARE)))
			return SDL_Quit(),(char *)SDL_GetError();

	SDL_SetRenderTarget(session_blitter,NULL); // necessary?
	// ARGB8888 equates to masks A = 0xFF000000, R = 0x00FF0000, G = 0x0000FF00, B = 0x000000FF ; it provides the best performance AFAIK.
	session_dib=SDL_CreateTexture(session_blitter,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,VIDEO_LENGTH_X,VIDEO_LENGTH_Y);
	session_gui_dib=SDL_CreateTexture(session_blitter,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,VIDEO_PIXELS_X,VIDEO_PIXELS_Y);
	SDL_SetTextureBlendMode(session_dib,SDL_BLENDMODE_NONE);
	SDL_SetTextureBlendMode(session_gui_dib,SDL_BLENDMODE_NONE); // ignore alpha!
	int dummy; SDL_LockTexture(session_dib,NULL,(void*)&video_frame,&dummy); // pitch must always equal VIDEO_LENGTH_X*4 !!!
	if (dummy!=4*VIDEO_LENGTH_X) return SDL_Quit(),"pitch mismatch"; // can this EVER happen!?!?
	SDL_LockTexture(session_gui_dib,NULL,(void*)&menus_frame,&dummy); // ditto, pitch must always equal VIDEO_PIXELS_X*4 !!!

	session_dbg=SDL_CreateTexture(session_blitter,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,VIDEO_PIXELS_X,VIDEO_PIXELS_Y);
	SDL_SetTextureBlendMode(session_dbg,SDL_BLENDMODE_NONE);
	SDL_LockTexture(session_dbg,NULL,(void*)&debug_frame,&dummy);

	if (session_stick)
	{
		int i=SDL_NumJoysticks();
		logprintf("Detected %i joystick[s]: ",i);
		while (--i>=0&&!((session_pad=SDL_IsGameController(i)),(logprintf("%s #%i = '%s'. ",session_pad?"Controller":"Joystick",i,session_pad?SDL_GameControllerNameForIndex(i):SDL_JoystickNameForIndex(i))),
			session_joy=(session_pad?(void*)SDL_GameControllerOpen(i):(void*)SDL_JoystickOpen(i)))) // scan joysticks and game controllers until we run out or one is OK
			; // unlike Win32, SDL2 lists the joysticks from last to first
		session_stick=i>=0;
		logprintf(session_stick?"Joystick enabled!\n":"No joystick!\n");
	}
	if (session_audio)
	{
		SDL_AudioSpec spec;
		SDL_zero(spec);
		spec.freq=AUDIO_PLAYBACK;
		spec.format=AUDIO_BITDEPTH>8?AUDIO_S16SYS:AUDIO_U8;
		spec.channels=AUDIO_CHANNELS;
		spec.samples=4096; // safe value?
		if (session_audio=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0))
			audio_frame=audio_buffer;
	}

	session_timer=SDL_GetTicks();

	#ifdef _WIN32
	long int z;
	for (session_ui_drive[0]='A';session_ui_drive[0]<='Z';++session_ui_drive[0]) // scan session_ui_drive just once
		if (GetDriveType(session_ui_drive)>2&&GetDiskFreeSpace(session_ui_drive,&z,&z,&z,&z))
			session_ui_drives|=1<<(session_ui_drive[0]-'A');
	#else
	session_ui_icon=SDL_CreateRGBSurfaceFrom(session_icon32xx16,32,32,16,32*2,0xF00,0xF0,0xF,0xF000); // ARGB4444
	SDL_SetWindowIcon(session_hwnd,session_ui_icon); // SDL2 already handles WIN32 icons alone!
	#endif
	session_ui_makechrs();

	// translate menu data into custom format
	BYTE *t=session_ui_menudata; session_ui_menusize=0;
	while (*s)
	{
		// scan and generate menu header
		while ((*t++=*s++)!='\n')
			++session_ui_menusize;
		session_ui_menusize+=2;
		t[-1]=0;
		// first scan: maximum item width
		char *r=s; int mxx=0;
		for (;;)
		{
			if (*r=='=')
				while (*r++!='\n')
					;
			else if (*r=='0')
			{
				while (*r++!=' ')
					;
				int n=0;
				while (*r++>=' ')
					++n;
				if (mxx<n)
					mxx=n;
				--r;
				while (*r++!='\n')
					;
			}
			else
				break;
		}
		// second scan: generate items
		for (;;)
			if (*s=='=')
			{
				*t++=0x80; *t++=0;*t++=0; // fake DROP code!
				while (*s++!='\n')
					;
			}
			else if (*s=='0')
			{
				int i=strtol(s,&s,0); // allow either hex or decimal
				*t++=i>>8; *t++=i; *t++=' ';*t++=' ';
				++s; int h=-2; // spaces between body and shortcut
				while ((i=(*t++=*s++))!='\n')
					if (i=='\t') // keyboard shortcut?
					{
						--t;
						for (int n=h;n<mxx;++n)
							*t++=' ';
					}
					else if (i=='_') // string shortcut?
						--t;
					else
						++h;
				t[-1]=0;
			}
			else
				break;
		*t++=0;*t++=0;
	}
	*t=0;
	SDL_StartTextInput();
	session_please();
	return NULL;
}

int session_pad2bit(int i) // translate motions and buttons into codes
{
	switch (i)
	{
		case SDL_CONTROLLER_BUTTON_A: // button 0; the order isn't the same used in Win32
		//case 4: // SDL_CONTROLLER_BUTTON_BACK?
			return 16;
		case SDL_CONTROLLER_BUTTON_B: // button 1
		//case 5: // SDL_CONTROLLER_BUTTON_GUIDE?
			return 32;
		case SDL_CONTROLLER_BUTTON_X: // button 2
		//case 6: // SDL_CONTROLLER_BUTTON_START?
			return 64;
		case SDL_CONTROLLER_BUTTON_Y: // button 3
		//case 7: // SDL_CONTROLLER_BUTTON_???
			return 128;
		case SDL_CONTROLLER_BUTTON_DPAD_UP:
			return 1;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
			return 2;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			return 4;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
			return 8;
		default:
			return 0;
	}
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
				session_redraw(1),*debug_buffer=0;
		}
		else if (!session_paused) // set the caption just once
		{
			session_please();
			sprintf(session_tmpstr,"%s | %s | PAUSED",session_caption,session_info);
			SDL_SetWindowTitle(session_hwnd,session_tmpstr);
			session_paused=1;
		}
		SDL_WaitEvent(NULL);
	}
	int q=0,k; SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
			case SDL_WINDOWEVENT:
				if (event.window.event==SDL_WINDOWEVENT_EXPOSED)
					session_clrscr(),session_redraw(1);// clear and redraw
				break;
			case SDL_MOUSEBUTTONDOWN:
				if (event.button.button==SDL_BUTTON_RIGHT)
					session_event=0x8080; // show menu
				break;
			case SDL_KEYDOWN:
				if (event.key.keysym.mod&KMOD_ALT) // ALT+RETURN toggles fullscreen
				{
					if (event.key.keysym.sym==SDLK_RETURN)
						session_togglefullscreen();
					break;
				}
				session_shift=!!(event.key.keysym.mod&KMOD_SHIFT);
				if (event.key.keysym.sym==SDLK_F10)
				{
					session_event=0x8080; // F10 shows the popup menu
					break;
				}
				if (session_signal&SESSION_SIGNAL_DEBUG) // only relevant inside debugger, see below
					session_event=debug_xlat(event.key.keysym.scancode);
				if ((k=session_key_n_joy(event.key.keysym.scancode))<128) // normal key
				{
					if (!(session_signal&SESSION_SIGNAL_DEBUG)) // only relevant outside debugger
						kbd_bit_set(k);
				}
				else if (!session_event) // special key, but only if not already set by debugger
					session_event=(k-((event.key.keysym.mod&KMOD_CTRL)?128:0))<<8;
				break;
			case SDL_TEXTINPUT: // always follows SDL_KEYDOWN
				if (session_signal&SESSION_SIGNAL_DEBUG) // only relevant inside debugger
					if (event.text.text[0]>32) // exclude SPACE and non-visible codes!
						if ((session_event=event.text.text[0])&128) // UTF-8?
							session_event=128+(session_event&31)*64+(event.text.text[1]&63);
				break;
			case SDL_KEYUP:
				if ((k=session_key_n_joy(event.key.keysym.scancode))<128)
					kbd_bit_res(k);
				break;
			case SDL_JOYAXISMOTION:
				if (!session_pad)
					switch (event.jaxis.axis) // warning, there can be more than two axes
					{
						case SDL_CONTROLLER_AXIS_LEFTX: // safe
						//case SDL_CONTROLLER_AXIS_RIGHTX: // unsafe?
							session_joybits=(session_joybits&~(4+8))+(event.jaxis.value<-0x4000?4:event.jaxis.value>=0x4000?8:0); break;
						case SDL_CONTROLLER_AXIS_LEFTY: // safe
						//case SDL_CONTROLLER_AXIS_RIGHTY: // unsafe?
							session_joybits=(session_joybits&~(1+2))+(event.jaxis.value<-0x4000?1:event.jaxis.value>=0x4000?2:0); break;
					}
				break;
			case SDL_CONTROLLERAXISMOTION:
				if (session_pad)
					switch (event.caxis.axis) // only the first two axes (X and Y) are safe
					{
						case 0: session_joybits=(session_joybits&~(4+8))+(event.caxis.value<-0x4000?4:event.caxis.value>=0x4000?8:0); break;
						case 1: session_joybits=(session_joybits&~(1+2))+(event.caxis.value<-0x4000?1:event.caxis.value>=0x4000?2:0); break;
					}
				break;
			case SDL_JOYBUTTONDOWN:
				if (!session_pad)
					session_joybits|=16<<event.jbutton.button;
				break;
			case SDL_JOYBUTTONUP:
				if (!session_pad)
					session_joybits&=~(16<<event.jbutton.button);
				break;
			case SDL_CONTROLLERBUTTONDOWN:
				if (session_pad)
					session_joybits|=session_pad2bit(event.cbutton.button);
				break;
			case SDL_CONTROLLERBUTTONUP:
				if (session_pad)
					session_joybits&=~session_pad2bit(event.cbutton.button);
				break;
			case SDL_WINDOWEVENT_FOCUS_LOST:
				session_kbdclear(); // loss of focus: no keys!
				break;
			case SDL_DROPFILE:
				strcpy(session_parmtr,event.drop.file);
				SDL_free(event.drop.file);
				session_event=0x8000;
				break;
			case SDL_QUIT:
				q=1;
				break;
		}
		if (session_event==0x8080) // F10?
			session_ui_menu();
		if (session_event==0x3F00) // Exit
			q=1;
		else if (session_event)
		{
			if (!((session_signal&SESSION_SIGNAL_DEBUG)&&session_debug_user(session_event)))
				q|=session_user(session_event);
			session_event=0;
		}
	}
	if (session_dirtymenu)
		session_dirtymenu=0,session_menuinfo();
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
			++performance_b,session_redraw(1);
		if (session_stick&&!session_key2joy) // do we need to check the joystick?
		{
			memset(joy_bit,0,sizeof(joy_bit));
			//for (i=0;i<length(kbd_joy);++i) joy_bit_res(kbd_joy[i]); // clean keys, allow redundancy
			for (i=0;i<length(kbd_joy);++i)
				if (session_joybits&(1<<i))
					joy_bit_set(kbd_joy[i]); // key is down
		}
	}

	if (!audio_disabled)
		if (audio_filter) // audio filter: sample averaging
			audio_playframe(audio_filter,audio_buffer);
	if (session_wavefile) // record audio output, if required
		session_writewave(audio_frame);
	session_writefilm(); // record film frame

	i=SDL_GetTicks();
	if (session_wait||session_fast)
		session_timer=i; // ensure that the next frame can be valid!
	else
	{
		j=1000/VIDEO_PLAYBACK-(i-session_timer);
		if (j>0)
			SDL_Delay(j>1000/VIDEO_PLAYBACK?1+1000/VIDEO_PLAYBACK:j);
		else if (j<0&&!session_filmfile)
			video_framecount=-2; // automatic frameskip!
		session_timer+=1000/VIDEO_PLAYBACK;
	}
	if (session_audio)
	{
		static BYTE s=1;
		if (s!=audio_disabled)
			if (s=audio_disabled) // silent mode needs cleanup
				memset(audio_buffer,AUDIO_ZERO,sizeof(audio_buffer));
		#ifdef SDL2_DOUBLE_QUEUE // audio buffer workaround (f.e. SliTaz v5)
			#define AUDIO_N_FRAMES 16
		#else
			#define AUDIO_N_FRAMES 8
		#endif
		session_audioqueue=SDL_GetQueuedAudioSize(session_audio)/sizeof(audio_buffer);
		for (j=session_audioqueue?session_audioqueue>AUDIO_N_FRAMES?0:1:AUDIO_N_FRAMES;j>0;--j) // pump audio
			SDL_QueueAudio(session_audio,audio_buffer,sizeof(audio_buffer));
	}

	if (session_wait) // resume activity after a pause
	{
		if (session_audio)
			SDL_PauseAudioDevice(session_audio,0);
		session_wait=0;
	}

	if (i>(performance_t+1000)) // performance percentage
	{
		if (performance_t)
		{
			sprintf(session_tmpstr,"%s | %s | %g%% CPU %g%% %s %s",
				session_caption,session_info,
				performance_f*100.0/VIDEO_PLAYBACK,performance_b*100.0/VIDEO_PLAYBACK,
				session_hardblit?"SDL":"sdl",session_version);
			SDL_SetWindowTitle(session_hwnd,session_tmpstr);
		}
		performance_t=i,performance_f=performance_b=session_paused=0;
	}
}

INLINE void session_byebye(void) // delete video+audio devices
{
	if (session_joy) session_pad?SDL_GameControllerClose(session_joy):SDL_JoystickClose(session_joy);
	if (session_audio) SDL_ClearQueuedAudio(session_audio),SDL_CloseAudioDevice(session_audio);
	SDL_StopTextInput();
	SDL_UnlockTexture(session_dib);
	SDL_DestroyTexture(session_dib);
	SDL_UnlockTexture(session_gui_dib);
	SDL_DestroyTexture(session_gui_dib);
	SDL_UnlockTexture(session_dbg);
	SDL_DestroyTexture(session_dbg);
	SDL_DestroyRenderer(session_blitter);
	SDL_DestroyWindow(session_hwnd);
	#ifdef _WIN32
	#else
	SDL_FreeSurface(session_ui_icon);
	#endif
	SDL_Quit();
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
				#if SDL_BYTEORDER == SDL_BIG_ENDIAN
				*t++=v>>24, // copy B
				*t++=v>>16, // copy G
				*t++=v>>8, // copy R
				#else
				*t++=v, // copy B
				*t++=v>>8, // copy G
				*t++=v>>16, // copy R
				#endif
				s+=2;
			}
		}
		else
		{
			BYTE *t=r,*s=(BYTE *)session_getscanline(i);
			for (int j=0;j<VIDEO_PIXELS_X;++j) // turn ARGB (32 bits) into RGB (24 bits)
			{
				#if SDL_BYTEORDER == SDL_BIG_ENDIAN
				*t++=s[3], // copy B
				*t++=s[2], // copy G
				*t++=s[1], // copy R
				s+=4; // skip A
				#else
				*t++=*s++, // copy B
				*t++=*s++, // copy G
				*t++=*s++, // copy R
				s++; // skip A
				#endif
			}
		}
}

// menu item functions ---------------------------------------------- //

void session_menucheck(int id,int q) // set the state of option `id` as `q`
{
	BYTE *m=session_ui_menudata; int j;
	while (*m) // scan menu data for items
	{
		while (*m++) // skip menu name
			;
		while (j=*m++<<8,j+=*m++)
		{
			if (j==id)
				*m=q?'*':' ';
			while (*m++) // skip item name
				;
		}
	}
}
void session_menuradio(int id,int a,int z) // set the option `id` in the range `a-z`
{
	BYTE *m=session_ui_menudata; int j;
	while (*m) // scan menu data for items
	{
		while (*m++) // skip menu name
			;
		while (j=*m++<<8,j+=*m++)
		{
			if (j>=a&&j<=z)
				*m=j==id?'>':' ';
			while (*m++) // skip item name
				;
		}
	}
}

// message box ------------------------------------------------------ //

void session_message(char *s,char *t) { session_ui_text(s,t,0); } // show multi-lined text `s` under caption `t`
void session_aboutme(char *s,char *t) { session_ui_text(s,t,1); } // special case: "About.."

// input dialog ----------------------------------------------------- //

int session_input(char *s,char *t) { return session_ui_input(s,t); } // `s` is the target string (empty or not), `t` is the caption; returns -1 on error or LENGTH on success

// list dialog ------------------------------------------------------ //

int session_list(int i,char *s,char *t) { return session_ui_list(i,s,t,NULL,0); } // `s` is a list of ASCIZ entries, `i` is the default chosen item, `t` is the caption; returns -1 on error or 0..n-1 on success

// file dialog ------------------------------------------------------ //

#define session_filedialog_get_readonly() (session_ui_fileflags&1)
#define session_filedialog_set_readonly(q) (q?(session_ui_fileflags|=1):(session_ui_fileflags&=~1))
char *session_newfile(char *r,char *s,char *t) // "Create File" | ...and returns NULL on failure, or a string on success.
	{ return session_ui_filedialog(r,s,t,0,0)?session_parmtr:NULL; }
char *session_getfile(char *r,char *s,char *t) // "Open a File" | lists files in path `r` matching pattern `s` under caption `t`, etc.
	{ return session_ui_filedialog(r,s,t,0,1)?session_parmtr:NULL; }
char *session_getfilereadonly(char *r,char *s,char *t,int q) // "Open a File" with Read Only option | lists files in path `r` matching pattern `s` under caption `t`; `q` is the default Read Only value, etc.
	{ return session_ui_filedialog(r,s,t,1,q)?session_parmtr:NULL; }

// final definitions ------------------------------------------------ //

#define BOOTSTRAP // SDL2 doesn't require a main-WinMain bootstrap

// ====================================== END OF SDL 2.0+ DEFINITIONS //
