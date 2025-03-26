# Nova Ground

## Build and Formatting tools
### Installing and using meson

```
    sudo apt-install build-essential clang
```

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

## Dependencies
Note that boost libraries will also need to be installed.

```
    sudo apt-get install libboost-all-dev libpaho-mqttpp-dev
```

Install the daqhats library
Install WiringPI for the servo drivers

[documentation](https://mccdaq.github.io/daqhats/install.html#installation)

<!-- ### Using clang-tidy (note still trying to make this work)
There is a `.clang_tidy` file in the directory that will perform linting on our code. Meson will automatically run this if you have `clang-tidy` available on your system. On mac this can be done by first making sure `llvm` is installed:
```
    brew install llvm
```
You can determine the location of clang-tidy with
```
    brew list llvm | grep bin/clang-tidy
```
Then you can set up an alias in `~/.zshrc` or `~/.bashrc` depending on what shell you use. Make sure to change the directory if it's different to mine:
```
    # For zsh
    echo "alias clang-tidy=\"/usr/local/Cellar/llvm/17.0.6_1/bin/clang-tidy\"" >> ~/.zshrc

    # For bash
    echo "alias clang-tidy=\"/usr/local/Cellar/llvm/17.0.6_1/bin/clang-tidy\"" >> ~/.bashrc
``` -->
## Testing
In order to run the program, a mqtt broker must be set up and running on port `1883`. Execute
```
    ./build/novaGround
```