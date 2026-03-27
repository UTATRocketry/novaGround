# Nova Ground

## Build and Formatting tools
Note: novaGround must be installed and run on a raspberry pi 4 running the Raspberry Pi OS (Legacy, 64-bit) as the MCC DAQHat library does not work with Debian Trixie

### Installing and using meson

```
    sudo apt install build-essential clang
```

We use meson as our build tool in this project. It can be installed with pip:
```
    sudo apt install meson
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
wget https://archives.boost.io/release/1.81.0/source/boost_1_81_0.tar.bz2
tar xf boost_1_81_0.tar.bz2
cd boost_1_81_0
./bootstrap.sh --prefix=/usr/local
./b2
sudo ./b2 install

echo 'export BOOST_ROOT=/usr/local' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
echo 'export CPLUS_INCLUDE_PATH=/usr/local/include:$CPLUS_INCLUDE_PATH' >> ~/.bashrc

```

also:
```
    sudo apt-get install libpaho-mqtt-dev
    sudo apt install libgpiod-dev
    sudo apt-get install libcurl4-openssl-dev
```


Install the daqhats library
```
    git clone https://github.com/mccdaq/daqhats.git
    cd /daqhats
    sudo ./install.sh
```
[documentation](https://mccdaq.github.io/daqhats/install.html#installation)

Install WiringPI for the servo drivers
```
# fetch the source
git clone https://github.com/WiringPi/WiringPi.git
cd WiringPi

# build the package
./build debian
mv debian-template/wiringpi_3.18_arm64.deb .

# install it
sudo apt install ./wiringpi-3.x.deb
```
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

## Hardware Setup
When installing multiple hats, you must install the appropriate address jumpers onto address header locations A0-A2 of the new HAT board. The recommended addressing method is to have the addresses increment from 0 as the boards are installed, i.e. 0, 1, 2, and so forth. **There must always be a board at address 0.**

If you change the board stackup and have more than one HAT board attached, you must update the saved EEPROM images for the library to have the correct board information. You can use the DAQ HAT Manager or the command:
```
    sudo daqhats_read_eeproms
```

