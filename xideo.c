/* 
 * Xideo.c
 *
 * A screen recorder to efficiently record your 'live' X Display as a 
 * Flash 'FLV' stream ( via the Screen Video Format codec ), this is 
 * then also embedded into a created flash movie for easy playback.
 *
 * It intended for creating demos of software for easy playback in a web 
 * browser. 

 * Authored by Matthew Allum <mallum@o-hand.com>
 * Licensed under the GPL v2 or greater.
 *
 * ========================================================================
 *
 * To build, this needs libflv which you can grab from; 
 * 
 * http://klaus.geekserver.net/libflv/
 *
 * Also 'libMing' is needed, ( http://ming.sf.net ). Ming is a little crazed.
 * I used pre built debs via;
 *
 * http://klaus.geekserver.net/ming/
 *
 * You also need an X Server supporting the Damage extension such as X.org
 * or a kdrive X from freedesktop.org.
 *
 * Then build with something like;
 *
 * gcc -Wall xideo.c -o xideo `pkg-config --libs --cflags xdamage` -lflv -lming
 *
 * You then run like, 'foo mymovie', Recording will start after 5 secs.
 * Hit Ctrl-c to stop recording. You should then a created mymove.flv and
 * mymovie.swf ( with the .flv embedded in it ) in your current dir.
 *
 * Note; This is just a quick hack. Lots more could be done with it and
 *       improved. It works for me, I hope it works for you too :).
 *
 * =========================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>  

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>

#include <libflv.h>

#include <ming.h>

#define abs(x) ((x) > 0 ? (x) : -1*(x))

/* Globals needed by sig handler */
char *OutputSWF = NULL;
char *OutputFLV = NULL;
int   NFrames   = 0;
int   MovieWidth;
int   MovieHeight;

unsigned char *backing = NULL;
int backingx = 0, backingy = 0, backingw = 0, backingh = 0; 

int curs_width = 0, curs_height = 0;

typedef unsigned short ush;

#define blend(dst, fg, alpha, bg) {                               \
    ush temp;                                                     \
    if ((alpha) == 0)                                             \
       (dst) = (bg);                                              \
    else if ((alpha) == 255)                                      \
        (dst) = (fg);                                             \
    else {                                                        \
        temp = ((ush)(fg)*(ush)(alpha) +                          \
                (ush)(bg)*(ush)(255 - (ush)(alpha)) + (ush)128);  \
    (dst) = (ush)((temp + (temp >> 8)) >> 8);                   } }



void
cursor_clear_scratch(Display       *xdpy, 
		     unsigned char *scratch )
{
  int            y;
  unsigned char *p = NULL;
  
  if (backing != NULL)
    {
      p = scratch + (MovieWidth * backingy * 3) + (backingx * 3);
      for (y=0; y < backingh; y++)
	{
	  memcpy(p,
		 backing + (y * backingw * 3), 
		 (backingw * 3));
	  p += (MovieWidth * 3);
	}
      
      free(backing);
    }
}

void
cursor_to_scratch(Display       *xdpy, 
		  unsigned char *scratch )
{
  XFixesCursorImage *curs;

  if ((curs = XFixesGetCursorImage (xdpy)) != NULL)
    {
      unsigned char *p;
      int x, y, i = 0, srcw = 0, srch = 0, srcx = 0, srcy = 0;
      if (curs->x + curs->width < 0 || curs->y + curs->height < 0
	  || curs->x > MovieWidth || curs->y > MovieHeight)
	return; 		/* offscreen */

      /* 'clip' */
      srcx = curs->x; srcy = curs->y;
      srch = curs->height; srcw = curs->width;

      /*
      printf("src x before xlip %i, %i, %ix%i\n", srcx, srcy, curs->xhot, curs->yhot);
      */

      if (srcx < 0) 
	srcx = 0, srcw = curs->width + curs->x;

      if (srcy < 0) 
	srcy = 0, srch = curs->height + curs->y;

      if ((srcx + srcw) > MovieWidth)
	  srcw = MovieWidth - srcx;

      if ((srcy + srch) > MovieHeight)
	  srch = MovieHeight - srcy;

      curs_width  = curs->width;
      curs_height = curs->height;

      backing = malloc(srcw * srch * 3);

      p = scratch + (MovieWidth * srcy * 3) + (srcx * 3);

      /* Make a copy of what were about write over */
      backingx = srcx; backingy = srcy;
      backingw = srcw; backingh = srch;

      for (y=0; y < srch; y++)
	{
	  memcpy(backing + (y * srcw * 3), 
		 p, 
		 (srcw * 3));
	  p += (MovieWidth * 3);
	}

      p = scratch + (MovieWidth * srcy * 3) + (srcx * 3);

      i  = (abs(curs->y - srcy) * curs->width) + abs(curs->x - srcx);

      for (y=0; y < srch; y++)
	{
	  for (x=0; x < srcw; x++)
	    {
	      unsigned char a, r, g, b;

	      a = curs->pixels[i] >> 24;
	      r = (curs->pixels[i] >> 16) & 0xff;
	      g = (curs->pixels[i] >> 8) & 0xff;
	      b = curs->pixels[i] & 0xff;

	      blend(*p, r, a, *p);
	      p++;
	      blend(*p, g, a, *p);
	      p++;
	      blend(*p, b, a, *p);
	      p++;

	      i++;
	    }
	  p += (( MovieWidth - srcw ) * 3);
	  i += (curs->width - srcw);
	}
    }
}

int
update_scratch(Display       *xdpy,
	       int            xscr,
	       XRectangle    *rect,
	       unsigned char *scratch )
{
  XImage        *ximg;
  unsigned char *p = NULL;
  int            x,y,br,bg,bb,mg,mb,mr,lr,lg,lb;
  unsigned long  xpixel;
  int            width  = DisplayWidth(xdpy, xscr);

  XGrabServer(xdpy);

  cursor_clear_scratch(xdpy, scratch);

  /* XX Should really use some SHMage here to speed things along a little */
  ximg = XGetImage(xdpy, 
		   RootWindow(xdpy, xscr), 
		   rect->x, rect->y, 
		   rect->width, rect->height, 
		   -1, 
		   ZPixmap);


  XFlush(xdpy);

  if (ximg == NULL) return 0;

  switch (DefaultDepth(xdpy, xscr)) 
    {
    case 15:
      br = 7; bg = 2; bb = 3;
      mr = mg = mb = 0xf8;
      lr = lg = lb = 0;
      break;
    case 16:
      br = 8; bg = 3; lb = 3;
      bb = lr = lg = 0;
      mr = mb = 0xf8;
      mg = 0xfc;
      break;
    case 24:
    case 32:
      br = 16;  bg = 8; bb = 0;
      lr = lg = lb = 0;
      mr = mg = mb = 0xff;
      break;
    default:
      /* Likely 8bpp */
      fprintf(stderr, "Cant handle depth %i\n", DefaultDepth(xdpy, xscr));
      exit(-1);
    }

  p = scratch + (width * rect->y * 3) + (rect->x * 3);
  
  for (y = 0; y < rect->height; y++)
    {
      for (x = 0; x < rect->width; x++)
	{
	  xpixel = XGetPixel(ximg, x, y);
	  *p++ = (((xpixel >> br) << lr) & mr);      /* r */
	  *p++ = (((xpixel >> bg) << lg) & mg);      /* g */
	  *p++ = (((xpixel >> bb) << lb) & mb);      /* b */
	}
      p += ((width-rect->width) * 3);
    }


  cursor_to_scratch(xdpy, scratch);

  XDestroyImage (ximg);
  ximg = NULL;

  XUngrabServer(xdpy);

  return 1;
}

unsigned char *
init_scratch_pixbuf(int width, int height)
{
  unsigned char *data = NULL;

  data = malloc(width*height*3);
  memset(data, 0, (width*height*3));

  return data;
}

void 
catch_int(int sig_num)
{
  SWFMovie       movie;
  SWFVideoStream stream;
  FILE          *f; 
  int            i;

  printf("Caught Ctrl-C, ");

  if(NFrames == 0)
    {
      fprintf(stderr, "No frames to write!\n");
      exit(-1);
    }

  printf("writing %i frames to '%s' \n", NFrames, OutputSWF); 

  f = fopen(OutputFLV, "r");

  if(!f)
    {
      fprintf(stderr, "Failed to open '%s' for reading\n", OutputFLV);
      exit(-1);
    }

  movie = newSWFMovie();

  SWFMovie_setDimension(movie, MovieWidth, MovieHeight);
  SWFMovie_setRate(movie, 8); 	/* What Should this be ? */

  stream = newSWFVideoStream_fromFile(f);

  SWFMovie_add(movie, stream); 	/* Why warning ? */
  
  for(i = 0; i < NFrames; i++) 
    SWFMovie_nextFrame(movie);
  
  SWFMovie_save(movie, OutputSWF);

  exit(0);
}

int
main(int argc, char **argv)
{
  Display          *xdpy;
  int               xscr;
  Window            xrootwin;
  int               err, damage_ev, init_timestamp = 0 ,i;
  Damage            damage;

  unsigned char    *scratch;
  Stream           *vid_stream;
  FlvStream        *flv_stream;
  struct pixel_data flv_data;

  if (argc < 2) 
    {
      fprintf(stderr, "%s usage: %s <output basename>\n", argv[0], argv[0]); 
      exit(-1);
    }
  
  OutputSWF = malloc(strlen(argv[1])+5);
  sprintf(OutputSWF, "%s.swf", argv[1]);

  OutputFLV = malloc(strlen(argv[1])+5);
  sprintf(OutputFLV, "%s.flv", argv[1]);

  if ((xdpy = XOpenDisplay(getenv("DISPLAY"))) == NULL)
    {
      fprintf(stderr, "%s: Cant open display\n", argv[0]);
      exit(-1);
    }

  xscr = DefaultScreen(xdpy);
  xrootwin = RootWindow(xdpy, xscr);

  XSelectInput(xdpy, xrootwin, PointerMotionMask);

  MovieWidth  = DisplayWidth(xdpy, xscr);
  MovieHeight = DisplayHeight(xdpy, xscr);

  if (!XDamageQueryExtension (xdpy, &damage_ev, &err))
    {
      fprintf (stderr, "%s: no damage extension found\n", argv[0]);
      return 1;
    }

  signal(SIGINT, catch_int);

  for (i=1; i > 0; i--)
    {
      printf("Starting recording in %i Second%s\n",
	     i, i > 1 ? "s" : "");
      sleep(1);
    }

  printf("*RECORDING* - Ctrl-C to stop.\n");

  scratch = init_scratch_pixbuf (MovieWidth, MovieHeight);

  damage = XDamageCreate (xdpy, xrootwin,  XDamageReportBoundingBox);

  vid_stream = ScreenVideo_newStream(MovieWidth, 
				     MovieHeight,
				     64, /* block size */
				     9  /* ZLib factor */ 
				     );

  flv_stream = FlvStream_newStream(OutputFLV, 
				   NULL, 
				   vid_stream, 
				   FLV_VERSION_1);

  flv_data.width      = MovieWidth;
  flv_data.height     = MovieHeight;
  flv_data.data       = scratch;
  flv_data.rowOrder   = TOPDOWN;
  flv_data.n_channels = 3;
  flv_data.rowPadding = 0;

  for (;;)
    {
      while (XPending(xdpy)) 
	{
	  XEvent              xev;
	  XDamageNotifyEvent *dev;
	  
	  XNextEvent(xdpy, &xev);
	  
	  if (xev.type == damage_ev + XDamageNotify) 
	    {
	      dev = (XDamageNotifyEvent*)&xev;

	      if (update_scratch(xdpy, xscr, &dev->area, scratch))
		{
		  XDamageSubtract(xdpy, dev->damage, None, None);

		  FlvStream_writeVideoTag(flv_stream, 
					  init_timestamp ? 
					  dev->timestamp - init_timestamp : 0, 
					  0, &flv_data);

		  /* clear the cursor from scratch */

		  if (!init_timestamp)
		    init_timestamp = dev->timestamp;
		  NFrames++;
		}
	      else fprintf(stderr, "Frame grab failed\n");
	    }
	  else if (xev.type == MotionNotify)
	    {
	      cursor_clear_scratch(xdpy, scratch);

	      cursor_to_scratch(xdpy, scratch); 

	      FlvStream_writeVideoTag(flv_stream, 
				      init_timestamp ? 
				      xev.xmotion.time - init_timestamp : 0, 
				      0, &flv_data);

	      if (!init_timestamp)
		init_timestamp = xev.xmotion.time;

	      NFrames++;
	    }
	}
    }
}
