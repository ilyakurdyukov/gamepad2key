#ifndef _X11_XLIB_H_
#define _X11_XLIB_H_

#ifndef _Xconst
#define _Xconst const
#endif /* _Xconst */

#define Bool int
#define True 1
#define False 0

typedef void Display;

Display *XOpenDisplay(_Xconst char*);
int XCloseDisplay(Display*);
int XSync(Display*, Bool);

typedef unsigned CARD32;
typedef unsigned char CARD8;

#define KeyCode CARD8
#define KeySym CARD32

KeyCode XKeysymToKeycode(Display*, KeySym);

#endif
