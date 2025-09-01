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

	int btn_set2;
	int *axes_conf[2], *thr_conf, *buttons_conf[2];
	Display *display;

	char *js_state;
	int *js_map, *js_map2;
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
	X(THROTTLE) X(RUDDER)
	X(WHEEL) X(GAS) X(BRAKE)
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
	X(THUMBL) X(THUMBR)
#undef X
	{ NULL, 0 }
};

#define KEY_SET2 -2

static const struct {
	const char *name; int val;
} key_names[] = {
#define X(name) { #name, XK_##name },
	X(Left) X(Up) X(Right) X(Down)
	X(Home) X(Page_Up) X(Page_Down) X(End)
	X(Insert) X(Delete) X(BackSpace)
	X(Return) X(Escape) X(Tab)
	X(space)
	X(Shift_L) X(Shift_R)
	X(Control_L) X(Control_R)
	X(Alt_L) X(Alt_R)

	// aliases
	{ "Enter", XK_Return },
	{ "Esc", XK_Escape },
	{ "Shift", XK_Shift_L },
	{ "Control", XK_Control_L },
	{ "Ctrl", XK_Control_L },
	{ "Alt", XK_Alt_L },
	{ "PgUp", XK_Page_Up },
	{ "PgDown", XK_Page_Down },

	X(Caps_Lock)
	X(F1) X(F2) X(F3) X(F4) X(F5) X(F6)
	X(F7) X(F8) X(F9) X(F10) X(F11) X(F12)

	X(KP_Enter) X(KP_Home)
	X(KP_Left) X(KP_Up) X(KP_Right) X(KP_Down)
	X(KP_Page_Up) X(KP_Page_Down)
	X(KP_End) X(KP_Insert) X(KP_Delete) X(KP_Equal)
	X(KP_Multiply) X(KP_Add)
	X(KP_Separator) X(KP_Subtract) X(KP_Divide)
	X(KP_0) X(KP_1) X(KP_2) X(KP_3) X(KP_4)
	X(KP_5) X(KP_6) X(KP_7) X(KP_8) X(KP_9)
#undef X
	{ "set2", KEY_SET2 },
	{ "none", -1 },
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

	if (s[0] && !s[1]) return s[0];

	if (s[0] == '0' && tolower(s[1]) == 'x') {
		a = strtoul(s, NULL, 0);
		return a;
	}

	for (i = 0; key_names[i].name; i++)
		if (!strcasecmp(s, key_names[i].name))
			return key_names[i].val;

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

	j = (buttons + axes * 2) * 2 * sizeof(*sys->js_map);
	if (!(sys->js_map = malloc(j)))
		ERR_EXIT("malloc failed\n");
	memset(sys->js_map, -1, j);
	sys->js_map2 = sys->js_map + buttons + axes * 2;

	j = axes * 2 * sizeof(*sys->js_thr);
	if (!(sys->js_thr = malloc(j)))
		ERR_EXIT("malloc failed\n");

	for (i = 0; i < axes; i++) {
		int x = axmap[i], n, j, *conf;
		if (sys->verbose >= 1) {
			const char *name = "???";
			for (j = 0; axis_names[j].name; j++)
				if (x == axis_names[j].val) {
					name = axis_names[j].name;
					break;
				}
			printf("axis %u: %s (0x%02x)\n", i, name, x);
		}
		for (j = 0; j < 2; j++) {
			int *map = j ? sys->js_map2 : sys->js_map;
			conf = sys->axes_conf[j];
			if (conf)
			for (n = *conf++; n; n--, conf += 3) {
				int k = buttons + i * 2;
				if (x != conf[0]) continue;
				if (conf[1] == KEY_SET2) sys->btn_set2 = k;
				map[k] = conf[1];
				if (conf[2] == KEY_SET2) sys->btn_set2 = k + 1;
				map[k + 1] = conf[2];
				break;
			}
		}
		sys->js_thr[i * 2] = -0x4000;
		sys->js_thr[i * 2 + 1] = 0x4000;
		conf = sys->thr_conf;
		if (conf)
		for (n = *conf++; n; n--, conf += 3) {
			if (x != conf[0]) continue;
			sys->js_thr[i * 2] = conf[1];
			sys->js_thr[i * 2 + 1] = conf[2];
			break;
		}
	}

	for (i = 0; i < buttons; i++) {
		int x = btnmap[i], n, j, *conf;
		if (sys->verbose >= 1) {
			const char *name = "???";
			for (j = 0; button_names[j].name; j++)
				if (x == button_names[j].val) {
					name = button_names[j].name;
					break;
				}
			printf("button %u: %s (0x%02x)\n", i, name, x);
		}
		for (j = 0; j < 2; j++) {
			int *map = j ? sys->js_map2 : sys->js_map;
			conf = sys->buttons_conf[j];
			if (conf)
			for (n = *conf++; n; n--, conf += 2) {
				if (x != conf[0]) continue;
				if (conf[1] == KEY_SET2) sys->btn_set2 = i;
				else map[i] = conf[1];
				break;
			}
		}
	}

	j = (buttons + axes * 2) * sizeof(*sys->js_state);
	if (!(sys->js_state = malloc(j)))
		ERR_EXIT("malloc failed\n");
	memset(sys->js_state, 0, j);
}

static void send_key(sysctx_t *sys, int key, int state) {
	int keysym, keycode;
	int old_state = sys->js_state[key];
	int j, *map;
	if ((old_state & 15) == state) return;
	if (!state) {
		j = old_state >> 4;
	} else {
		j = sys->btn_set2;
		j = j == -1 ? 0 : sys->js_state[j] & 1;
	}
	sys->js_state[key] = state | j << 4;
	map = j ? sys->js_map2 : sys->js_map;
	keysym = map[key];
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
		if (n != sizeof(event))
			ERR_EXIT("unexpected joystic event\n");
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

static sysctx_t ctx_glob;

static void sys_cleanup(void) {
	sysctx_t *sys = &ctx_glob;
	if (sys->display) XCloseDisplay(sys->display), sys->display = NULL;
	if (sys->js_fd >= 0) close(sys->js_fd), sys->js_fd = -1;
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

static int parse_list(int argc, char **argv, int **res, int cols,
		int (*fn1)(const char*), int (*fn2)(const char*)) {
	int i, j, n = 0, *p;
	char **conf = argv + 2;
	if (*res)
		ERR_EXIT("%s list is already specified\n", argv[1] + 2);
	argc -= 1; argv += 1;
	while (argc > cols) {
		if (argv[1][0] == '-' && argv[1][1]) break;
		argc -= cols; argv += cols; n++;
	}
	if (!(p = malloc((n * cols + 1) * sizeof(int))))
		ERR_EXIT("malloc failed\n");
	*res = p; *p++ = n;
	for (i = 0; i < n; i++) {
		*p++ = fn1(*conf++);
		for (j = 1; j < cols; j++)
			*p++ = fn2(*conf++);
	}
	return n * cols + 1;
}

int main(int argc, char **argv) {
	sysctx_t *sys = &ctx_glob;
	const char *js_fn = "/dev/input/js0";
	const char *display_name = NULL;
	int test = 0;
	const char *progname = argv[0];

	memset(sys, 0, sizeof(*sys));
	sys->js_fd = -1;
	sys->verbose = 1;
	sys->btn_set2 = -1;

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
			sys->verbose = atoi(argv[2]);
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--display")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			display_name = argv[2];
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--buttons")) {
			int n = parse_list(argc, argv, &sys->buttons_conf[0], 2, str2button, str2key);
			argc -= n; argv += n;
		} else if (!strcmp(argv[1], "--axes")) {
			int n = parse_list(argc, argv, &sys->axes_conf[0], 3, str2axis, str2key);
			argc -= n; argv += n;
		} else if (!strcmp(argv[1], "--buttons2")) {
			int n = parse_list(argc, argv, &sys->buttons_conf[1], 2, str2button, str2key);
			argc -= n; argv += n;
		} else if (!strcmp(argv[1], "--axes2")) {
			int n = parse_list(argc, argv, &sys->axes_conf[1], 3, str2axis, str2key);
			argc -= n; argv += n;
		} else if (!strcmp(argv[1], "--axes_thr")) {
			int n = parse_list(argc, argv, &sys->thr_conf, 3, str2axis, str2thr);
			argc -= n; argv += n;
		} else if (!strcmp(argv[1], "--test")) {
			test = 1;
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			display_help(progname);
			return 1;
		} else ERR_EXIT("unknown option\n");
	}

	sys->js_fd = open(js_fn, O_RDONLY);
	if (sys->js_fd < 0)
		ERR_EXIT("open(\"%s\") failed\n", js_fn);
	sys_gamepad_init(sys);

	atexit(sys_cleanup);

	if (test) {
		test_gamepad(sys->js_fd);
	} else {
		sys->display = XOpenDisplay(display_name);
		if (!sys->display) ERR_EXIT("XOpenDisplay failed\n");
		process_events(sys);
	}
	return 0;
}
