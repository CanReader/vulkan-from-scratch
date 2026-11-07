# Vulkan from Scratch

This is the code for my Vulkan from Scratch series on [my blog](https://canberkpitirli.com/blog/). In the posts I explain the code piece by piece. Here you get the whole `main.cpp` for each part, so you don't have to put it together from the snippets yourself.

Every part has its own folder, and every folder is a complete program that builds and runs on its own. The code is exactly what you have at the end of that part, nothing more.

| Folder | Post | What it does |
|---|---|---|
| [`01-instance-and-devices`](01-instance-and-devices) | [Instance, Devices and Queues](https://canberkpitirli.com/blog/vulkan-instance-and-devices/) | Creates the instance with the validation layers and the window surface, picks a GPU, and creates the logical device and its queues. You get an empty window and the name of your GPU. |
| [`02-swapchain-and-presentation`](02-swapchain-and-presentation) | [Swapchains and Presentation](https://canberkpitirli.com/blog/vulkan-swapchain-and-presentation/) | Adds the swapchain, a command buffer, a fence and semaphores. The window clears itself to a new color every frame, survives resizing, and shows the frame rate in its title. |

## What you need

- The [Vulkan SDK](https://vulkan.lunarg.com/), because it comes with the validation layers. On Linux, your distro's Vulkan headers and validation layers packages work too.
- [SDL3](https://libsdl.org/). It has to be SDL3, not SDL2. I'm using 3.4.18.
- CMake 3.25 or newer.
- A compiler with C++26 support: GCC 16 or Clang 23.

GCC 16 is the only compiler with C++26 reflection right now, so with GCC the programs print enum names like `VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU`. Clang 23 doesn't have reflection yet, so you get the number instead, like `2`. Everything else works the same.

I haven't tried Visual Studio, and its C++26 support is behind, so on Windows I'd use GCC or Clang. On macOS you also need `VK_KHR_portability_enumeration` and its flag, which I mention in part 1 but didn't put in the code.

## Building and running

Pick a part and build it like any other CMake project:

```bash
cd 02-swapchain-and-presentation
cmake -B build
cmake --build build
./build/app
```

If you want Clang instead of your default compiler, configure with this instead:

```bash
cmake -B build -DCMAKE_CXX_COMPILER=clang++
```

## What you should see

Part 1 opens an empty window and prints one line:

```
Using NVIDIA GeForce RTX 4050 Laptop GPU (VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
```

Part 2 opens a window that slowly goes through all the colors, and prints:

```
Using NVIDIA GeForce RTX 4050 Laptop GPU (VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
Swapchain: 1280x720, 3 images, VK_PRESENT_MODE_FIFO_KHR
```

You'll see your own GPU there, of course. The validation layer is always on in these programs, so if nothing else shows up in the terminal, it didn't find anything wrong. If it does complain on your machine, I'd like to know, so feel free to open an issue with your GPU, driver and OS.

I tested both parts on my laptop with an RTX 4050, on Linux with KDE Plasma on Wayland, using GCC 16.2 and Clang 23.1.
