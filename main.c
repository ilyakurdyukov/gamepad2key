#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#define ERR_EXIT(...) \
	do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0)

typedef struct {
	int verbose;
	int js_fd;
	int js_axes, js_buttons;

	int axes_conf_num, thr_conf_num, buttons_conf_num;
	char **axes_conf, **thr_conf, **buttons_conf;
	Display *display;

	char *js_state;
	int *js_map;
	int *js_thr;
} sysctx_t;

#define strcasecmp strcasecmp_new
static int strcasecmp(const char *s1, const char *s2) {
	int a, b, c;
	do {
		a = *s1++; b = *s2++;
		if ((c = a - b))
			c = tolower(a) - tolower(b);
	} while (!c && b);
	return c;
}

static const struct {
	const char *name; int val;
} axis_names[] = {
#define X(name) { #name, ABS_##name },
	X(X) X(Y) X(Z)
	X(RX) X(RY) X(RZ)
	X(HAT0X) X(HAT0Y)
	X(HAT1X) X(HAT1Y)
	X(HAT2X) X(HAT2Y)
	X(HAT3X) X(HAT3Y)
#undef X
	{ NULL, 0 }
};

static const struct {
	const char *name; int val;
} button_names[] = {
#define X(name) { #name, BTN_##name },
	X(A) X(B) X(C) X(X) X(Y) X(Z)
	X(TL) X(TR) X(TL2) X(TR2)
	X(SELECT) X(START) X(MODE)
#ifdef BTN_THUMBL
	X(THUMBL) X(THUMBR)
#endif
#undef X
	{ NULL, 0 }
};

static int str2axis(const char *s) {
	int a, i;

	if ((unsigned)(s[0] - '0') < 10) {
		a = strtoul(s, NULL, 0);
		return a;
	}

	for (i = 0; axis_names[i].name; i++)
		if (!strcasecmp(s, axis_names[i].name))
			return axis_names[i].val;

	ERR_EXIT("unknown axis (%s)\n", s);
	return -1;
}

static int str2button(const char *s) {
	int a, i;

	if ((unsigned)(s[0] - '0') < 10) {
		a = strtoul(s, NULL, 0);
		return a;
	}

	for (i = 0; button_names[i].name; i++)
		if (!strcasecmp(s, button_names[i].name))
			return button_names[i].val;

	ERR_EXIT("unknown button (%s)\n", s);
	return -1;
}

static int str2key(const char *s) {
	int a, i;
	static const struct {
		const char *name; int val;
	} tab[] = {
#define X(name) { #name, XK_##name },
		X(Left) X(Up) X(Right) X(Down)
		X(Home) X(Page_Up) X(Page_Down) X(End)
		X(Insert) X(Delete) X(BackSpace)
		X(Return) X(Escape) X(Tab)
		X(space)

		// aliases
		{ "Enter", XK_Return },
		{ "Esc", XK_Escape },
		{ "Shift", XK_Shift_L },
		{ "Control", XK_Control_L },
		{ "Ctrl", XK_Control_L },
		{ "Alt", XK_Alt_L },
		{ "PgUp", XK_Page_Up },
		{ "PgDown", XK_Page_Down },

		X(Shift_L) X(Shift_R)
		X(Control_L) X(Control_R)
		X(Alt_L) X(Alt_R)
		X(Caps_Lock)
		X(F1) X(F2) X(F3) X(F4) X(F5) X(F6)
		X(F7) X(F8) X(F9) X(F10) X(F11) X(F12)
#undef X
		{ "none", -1 },
		{ NULL, 0 }
	};

	if (s[0] && !s[1]) return s[0];

	if (s[0] == '0' && tolower(s[1]) == 'x') {
		a = strtoul(s, NULL, 0);
		return a;
	}

	for (i = 0; tab[i].name; i++)
		if (!strcasecmp(s, tab[i].name))
			return tab[i].val;

	ERR_EXIT("unknown key (%s)\n", s);
	return -1;
}

static int str2thr(const char *str) {
	const char *s = str;
	int thr;
	if (*s == '-') s++;
	if (s[0] == '0' && tolower(s[1]) == 'x') {
		thr = strtol(str, NULL, 0);
	} else {
		double d = atof(str) * 0x7fff;
		thr = d < 0 ? d - 0.5 : d + 0.5;
	}
	return thr;
}

static void sys_gamepad_init(sysctx_t *sys) {
	uint8_t axmap[ABS_CNT];
	uint16_t btnmap[KEY_MAX - BTN_MISC + 1];
	uint8_t buttons, axes;
	int i, j;

	if (ioctl(sys->js_fd, JSIOCGAXES, &axes) < 0)
		ERR_EXIT("ioctl(JSIOCGAXES) failed\n");
	if (ioctl(sys->js_fd, JSIOCGAXMAP, axmap) < 0)
		ERR_EXIT("ioctl(JSIOCGAXMAP) failed\n");

	if (ioctl(sys->js_fd, JSIOCGBUTTONS, &buttons) < 0)
		ERR_EXIT("ioctl(JSIOCGBUTTONS) failed\n");
	if (ioctl(sys->js_fd, JSIOCGBTNMAP, btnmap) < 0)
		ERR_EXIT("ioctl(JSIOCGBTNMAP) failed\n");

	sys->js_axes = axes;
	sys->js_buttons = buttons;

	j = (buttons + axes * 2) * sizeof(*sys->js_map);
	if (!(sys->js_map = malloc(j)))
		ERR_EXIT("malloc failed\n");
	memset(sys->js_map, -1, j);

	j = axes * 2 * sizeof(*sys->js_thr);
	if (!(sys->js_thr = malloc(j)))
		ERR_EXIT("malloc failed\n");

	for (i = 0; i < axes; i++) {
		int x = axmap[i];
		if (sys->verbose >= 1) {
			const char *name = "???";
			for (j = 0; axis_names[j].name; j++)
				if (x == axis_names[j].val) {
					name = axis_names[j].name;
					break;
				}
			printf("axis %u: %s (0x%02x)\n", i, name, x);
		}
		for (j = 0; j < sys->axes_conf_num; j++) {
			int k = buttons + i * 2;
			char **conf = sys->axes_conf + j * 3;
			if (x != str2axis(conf[0])) continue;
			sys->js_map[k] = str2key(conf[1]);
			sys->js_map[k + 1] = str2key(conf[2]);
			break;
		}
		sys->js_thr[i * 2] = -0x4000;
		sys->js_thr[i * 2 + 1] = 0x4000;
		for (j = 0; j < sys->thr_conf_num; j++) {
			char **conf = sys->thr_conf + j * 3;
			if (x != str2axis(conf[0])) continue;
			sys->js_thr[i * 2] = str2thr(conf[1]);
			sys->js_thr[i * 2 + 1] = str2thr(conf[2]);
			break;
		}
	}

	for (i = 0; i < buttons; i++) {
		int x = btnmap[i];
		if (sys->verbose >= 1) {
			const char *name = "???";
			for (j = 0; button_names[j].name; j++)
				if (x == button_names[j].val) {
					name = button_names[j].name;
					break;
				}
			printf("button %u: %s (0x%02x)\n", i, name, x);
		}
		for (j = 0; j < sys->buttons_conf_num; j++) {
			if (x != str2button(sys->buttons_conf[j * 2])) continue;
			sys->js_map[i] = str2key(sys->buttons_conf[j * 2 + 1]);
			break;
		}
	}

	j = (buttons + axes * 2) * sizeof(*sys->js_state);
	sys->js_state = malloc(j);
	if (!sys->js_state) ERR_EXIT("malloc failed\n");
	memset(sys->js_state, 0, j);
}

static void send_key(sysctx_t *sys, int key, int state) {
	int keysym, keycode;
	if (sys->js_state[key] == state) return;
	sys->js_state[key] = state;
	keysym = sys->js_map[key];
	if (sys->verbose >= 2) {
		const char *type = "button";
		int val = key;
		if (val >= sys->js_buttons)
			val -= sys->js_buttons,
			type = val & 1 ? "axis_max" : "axis_min",
			val >>= 1;
		printf("%s = %u, %s", type, val, state ? "down" : "up");
		if (keysym != -1) printf(", key = 0x%x", keysym);
		printf("\n");
	}
	if (keysym == -1) return;
	keycode = XKeysymToKeycode(sys->display, keysym);
	if (!keycode) return;
	XTestFakeKeyEvent(sys->display, keycode, state, 0);
 	XSync(sys->display, False);
}

static void process_events(sysctx_t *sys) {
	struct js_event event;
	for (;;) {
		int n = read(sys->js_fd, &event, sizeof(event));
		if (n != sizeof(event)) {
			close(sys->js_fd);
			sys->js_fd = -1;
			break;
		}
		if (event.type == JS_EVENT_BUTTON) {
			if (event.number >= sys->js_buttons) continue;
			if (!(event.value & ~1))
				send_key(sys, event.number, event.value);
		} else if (event.type == JS_EVENT_AXIS) {
			int *thr = sys->js_thr + event.number * 2;
			if (event.number >= sys->js_axes) continue;
			n = sys->js_buttons + event.number * 2;
			send_key(sys, n, event.value <= thr[0]);
			send_key(sys, n + 1, event.value >= thr[1]);
		}
	}
}

static void test_gamepad(int js_fd) {
	struct js_event event;
	for (;;) {
		int n = read(js_fd, &event, sizeof(event));
		if (n != sizeof(event))
			ERR_EXIT("unexpected joystic event\n");
		printf("0x%08x 0x%04x 0x%02x 0x%02x\n",
				event.time, event.value & 0xffff, event.type, event.number);
	}
}

static void display_help(const char *progname) {
	printf(
"Usage: %s [options]\n"
"Options:\n"
"  -h, --help        Display help text and exit\n"
"  --verbose N       Set verbosity level\n"
"  --dev device      To specify gamepad device\n"
"                      (default is \"/dev/input/js0\")\n"
"  --display X       To specify X11 display\n"
"  --test            Print raw gamepad input\n"
"  --buttons  BTN1 KEY1  BTN2 KEY2...\n"
"  --axes  AXIS1 KEY1 KEY2  AXIS2 KEY3 KEY4 ...\n"
"  --axes_thr  AXIS1 THR1 THR2  AXIS2 THR3 THR4 ...\n"
"\n", progname);
}

int main(int argc, char **argv) {
	sysctx_t ctx;
	const char *js_fn = "/dev/input/js0";
	const char *display_name = NULL;
	int test = 0;
	const char *progname = argv[0];

	memset(&ctx, 0, sizeof(ctx));

	ctx.verbose = 1;

	if (argc == 1) {
		display_help(progname);
		return 1;
	}

	while (argc > 1) {
		if (!strcmp(argv[1], "--dev")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			js_fn = argv[2];
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--verbose")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			ctx.verbose = atoi(argv[2]);
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--display")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			display_name = argv[2];
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--buttons")) {
			int n = 0;
			argc -= 1; argv += 1;
			ctx.buttons_conf = argv + 1;
			while (argc > 2) {
				if (argv[1][0] == '-' && argv[1][1]) break;
				argc -= 2; argv += 2; n++;
			}
			ctx.buttons_conf_num = n;
		} else if (!strcmp(argv[1], "--axes")) {
			int n = 0;
			argc -= 1; argv += 1;
			ctx.axes_conf = argv + 1;
			while (argc > 3) {
				if (argv[1][0] == '-' && argv[1][1]) break;
				argc -= 3; argv += 3; n++;
			}
			ctx.axes_conf_num = n;
		} else if (!strcmp(argv[1], "--axes_thr")) {
			int n = 0;
			argc -= 1; argv += 1;
			ctx.thr_conf = argv + 1;
			while (argc > 3) {
				if (argv[1][0] == '-' && argv[1][1]) break;
				argc -= 3; argv += 3; n++;
			}
			ctx.thr_conf_num = n;
		} else if (!strcmp(argv[1], "--test")) {
			test = 1;
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			display_help(progname);
			return 1;
		} else ERR_EXIT("unknown option\n");
	}

	ctx.js_fd = open(js_fn, O_RDONLY);
	if (ctx.js_fd < 0)
		ERR_EXIT("open(\"%s\") failed\n", js_fn);
	sys_gamepad_init(&ctx);
	if (test) test_gamepad(ctx.js_fd);

	ctx.display = XOpenDisplay(display_name);
	if (!ctx.display) ERR_EXIT("XOpenDisplay failed\n");
	process_events(&ctx);
	// TODO: unreachable
 	XCloseDisplay(ctx.display);
}
