# Nova Ground

## Build and Formatting tools
### Installing and using meson
We use meson as our build tool in this project. It can be installed with pip:
```
    pip3 install --user meson
```
Meson does out of src builds therefore we will use `novaGround/build` directory as standard. I think we will likely add more build directories in the future for testing and release builds. But for development use `build`. To set up the build directory and use clang for compilation, run the following:
```
    cd novaGround
    CC=clang CXX=clang++ meson setup build
```
This will set up the build directory `build`. To compile, run the following:
```
    meson compile -C build
```
The `-C` flag specifies which build directory to use.