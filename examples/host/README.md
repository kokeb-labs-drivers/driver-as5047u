# AS5047U host example

The driver on your PC, with no hardware: the host port in
[`host_as5047u_port.hpp`](host_as5047u_port.hpp) connects the real driver to a simulated AS5047U (the register model
the host tests use), and [`main.cpp`](main.cpp) initialises it, turns the simulated shaft by 30° four times and
reads the angle and the AGC each time.

## Build and run

From the repository root, with CMake 3.16+ and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/as5047u_host_example
```

## Expected output

```text
AS5047U driver 0.1.0 on the host
as5047u: initialised
angle    0.00 deg  AGC 128
angle   29.99 deg  AGC 128
angle   59.99 deg  AGC 128
angle   90.00 deg  AGC 128
```

The angle is 14-bit, so 30° reads as the nearest count, 29.99°.

The host tests build this folder too and run the example (CTest `as5047u_host_example`), so CI fails if it stops
compiling or a call fails.

## Files

| File | What it is |
| --- | --- |
| [`main.cpp`](main.cpp) | The example: initialise, read angle and AGC, print |
| [`host_as5047u_port.hpp`](host_as5047u_port.hpp) | The host port: bus hooks over the simulated chip, `DelayUs()` and `Log()` |
| [`CMakeLists.txt`](CMakeLists.txt) | Standalone project; adds the driver with `add_subdirectory(../..)` |
