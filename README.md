## Gamepad buttons to X11 keys converter

Requires X11 and Xtst headers and libraries.

### Gamepad Example

```
  ./gamepad2key --dev /dev/input/js1 \
    --buttons  A Shift  B Space  X Control  Y Tab  \
      TL ","  TR "."  SELECT Escape  START Enter  MODE Set2  \
    --axes  X Left Right  Y Up Down  HAT0X Left Right  HAT0Y Up Down \
    --axes_thr  X -0.5 0.5  Y -0x4000 0x4000
```

If one gamepad button is defined as `Set2`, then when it is pressed, the gamepad button presses are converted using the `--buttons2` and `--axes2` tables.

### Dancepad Example

Example for the cheapest dancepad, `lsusb` lists it as:

`ID 0079:0011 DragonRise Inc. Gamepad`

A similar layout was used with `prboom+` at the celebration on September 6, 2025 at the Dmitry Bachilo Museum.

```
btn_left=TRIGGER
btn_down=THUMB
btn_up=THUMB2
btn_right=TOP
btn_square=TOP2
btn_triangle=PINKIE
btn_cross=BASE
btn_circle=BASE2
btn_select=BASE3
btn_start=BASE4

  ./gamepad2key --dev /dev/input/js1 \
    --buttons  $btn_left "a"  $btn_down "s"  $btn_up "w"  $btn_right "d" \
      $btn_square Right  $btn_triangle Left  $btn_cross Control  $btn_circle Space \
      $btn_start Set2  $btn_select mwup \
    --buttons2  $btn_left Left  $btn_down Down  $btn_up Up  $btn_right Right \
      $btn_square Caps_Lock  $btn_triangle Tab  $btn_cross Escape  $btn_circle Enter \
      $btn_select mwdn
```

This dancepad also has a key in the center, you can bind it using `--axes Y none <something>`.


