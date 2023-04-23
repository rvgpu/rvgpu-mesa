# about this project

This is a vulkan driver based on mesa for rvgpu


# build with rvgpu
```
meson build -Dprefix=<path to install> -Dgallium-drivers= -Dvulkan-drivers=rvgpu -Dplatforms=x11 -Dglx=disabled

```
