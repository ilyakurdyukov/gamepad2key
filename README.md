## Gamepad buttons to X11 keys converter

Requires X11 and Xtst headers and libraries.

### Example

```
  ./gamepad2key --dev /dev/input/js1 \
    --buttons  A Shift  B Space  X Control  Y Tab  \
      TL ","  TR "."  SELECT Escape  START Enter  \
    --axes  X Left Right  Y Up Down  HAT0X Left Right  HAT0Y Up Down \
    --axes_thr  X -0.5 0.5  Y -0x4000 0x4000
```

