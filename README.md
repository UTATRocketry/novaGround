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
## Build Targets
This repository builds three executables, each with its own build ID and default sampling/publish rates:

1. `novaGround` (build ID `novaGround`, sample `1ms`, publish `50ms`)
2. `novaThermo` (build ID `novaThermo`, sample `100ms`, publish `250ms`)
3. `novaMock` (build ID `novaMock`, sample `100ms`, publish `250ms`, hardware init disabled)

All targets are built by running:
```
    meson compile -C build
```

## Running
In order to run the program, an MQTT broker should be available (defaults to `localhost:1883`).

Examples:
```
    ./build/novaGround
    ./build/novaThermo
    ./build/novaMock
```

## Runtime Options
You can override broker/backend endpoints and sampling/publish rates at runtime:

```
    ./build/novaGround --publish-ms 250 --verbosity 1
    ./build/novaThermo --sample-ms 50 --broker 192.168.0.1 --backend 192.168.0.1:8000
```

Options:
1. `--broker <host[:port]|mqtt://...>`: MQTT broker address (default `localhost:1883`)
2. `--backend <host[:port]|http://...>`: Backend base URL for data-file uploads (default `http://localhost:8000`)
3. `--verbosity <0|1|2>`: 0=quiet, 1=info, 2=debug
4. `--sample-ms <ms>`: DAQ sampling interval
5. `--publish-ms <ms>`: Telemetry publish interval

## Data Logging
Data files are automatically prefixed with the build ID. Example:
`novaGround_someName_sensors.csv` and `novaGround_someName_actuators.csv`.

## Hardware Setup
When installing multiple hats, you must install the appropriate address jumpers onto address header locations A0-A2 of the new HAT board. The recommended addressing method is to have the addresses increment from 0 as the boards are installed, i.e. 0, 1, 2, and so forth. **There must always be a board at address 0.**

If you change the board stackup and have more than one HAT board attached, you must update the saved EEPROM images for the library to have the correct board information. You can use the DAQ HAT Manager or the command:
```
    sudo daqhats_read_eeproms
```

novaGround should only accept commands targeted to it's build ID

IO Expander I2C Addresses: 0×20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27

PWM Driver I2C Addresses: 0x40-0x7F

Raspberry Pi pins

MCC128:
GPIO 8, 9, 10, 11  (SPI interface)
ID_SD, ID_SC (ID EEPROM)
GPIO 12, 13, 26 (Board address)
GPIO 16, 20 (Reset, IRQ)

MCC134:
GPIO 8, GPIO 9, GPIO 10, GPIO 11 (SPI interface)
ID_SD, ID_SC (ID EEPROM)
GPIO 12, GPIO 13, GPIO 26, (Board address)

TCA9535:
GPIO 2, 3 (I2C interface)

PCA9685:
GPIO 2, 3 (I2C interface)


make script to restart the pi (send ssh command) and then time how long untill the web server is started/reachable

I have someone whos going to write a full Next.js frontend for this api, can you write a detailed description of how a frontend web app would have to interact with the API? Write it as though this person won't have access to the code and so this will have to provide all the information required for this front end to interact with the API. write it in a markdown file


can you add role negotiation/control to the api?

when the server starts up, the first client to join the websocket should be offered the operator role (might change to pad later), regardless of whether or not they accept, all subsequent clients are set as viewers.

a client's role determines their access/level of control over the system. If a clients tries to do a operatation/send a command they do not have authority to access, the operation/command will be blocked. There is only allowed to be 1 admin, 1 operator, and 1 pad at a time, there is no limit on viewers

access levels:
- admin: highest level of access, can use unstable features/commands, etc (I want a way to easily mark endpoints, commands and other features as admin only like a decorator or something)
- operator: full access to stable features and control of normal operations, can do everything not marked admin only
- pad: can send “safety critical” commands (TBD on how a command/actuator will be marked as “safety critical” right now but it'll be a part of the config), cannot upload, update, or reload configs, can set calibration but not data saving flags
- viewer: can only view data, actuator states, and configs, cannot send commands, cannot upload, update, or reload configs, cannot set calibration or data saving flags

clients may request to change their role, if they are a requesting a lower access level they can immediatly switch (bumping any client that had that role to viewer). if a client is requesting to elevate their access level they must send a password ("UTAT" for pad, "NOVA" for operator, "ROCKET" for admin), if another client already has that role that client is notified that someone is requesting their role and they can block the role switch by sending a BLOCK message back (within a 20s timeout) or else the requesting client gets the role and the other get demoted to viewer.

can you also keep routes for 
can you also write intructions on what changes the frontend will need to make to interact with it

 anything that might be needed to use this to the ui




There should be a dialog for the 





[1/27] Compiling C++ object novaGround.p/src_core_data_logger.cpp.o
In file included from ../src/core/data_logger.cpp:1:
../src/core/data_logger.hpp: In constructor ‘DataLogger::DataLogger(std::string, std::vector<std::__cxx11::basic_string<char> >, std::vector<std::__cxx11::basic_string<char> >, std::vector<int>, std::string)’:
../src/core/data_logger.hpp:38:22: warning: ‘DataLogger::gpio_pins_’ will be initialized after [-Wreorder]
   38 |     std::vector<int> gpio_pins_;
      |                      ^~~~~~~~~~
../src/core/data_logger.hpp:35:17: warning:   ‘std::string DataLogger::file_prefix_’ [-Wreorder]
   35 |     std::string file_prefix_;
      |                 ^~~~~~~~~~~~
../src/core/data_logger.cpp:14:1: warning:   when initialized here [-Wreorder]
   14 | DataLogger::DataLogger(std::string data_dir,
      | ^~~~~~~~~~
[8/27] Compiling C++ object novaThermo.p/src_core_data_logger.cpp.o
In file included from ../src/core/data_logger.cpp:1:
../src/core/data_logger.hpp: In constructor ‘DataLogger::DataLogger(std::string, std::vector<std::__cxx11::basic_string<char> >, std::vector<std::__cxx11::basic_string<char> >, std::vector<int>, std::string)’:
../src/core/data_logger.hpp:38:22: warning: ‘DataLogger::gpio_pins_’ will be initialized after [-Wreorder]
   38 |     std::vector<int> gpio_pins_;
      |                      ^~~~~~~~~~
../src/core/data_logger.hpp:35:17: warning:   ‘std::string DataLogger::file_prefix_’ [-Wreorder]
   35 |     std::string file_prefix_;
      |                 ^~~~~~~~~~~~
../src/core/data_logger.cpp:14:1: warning:   when initialized here [-Wreorder]
   14 | DataLogger::DataLogger(std::string data_dir,
      | ^~~~~~~~~~
[17/27] Compiling C++ object novaMock.p/src_core_data_logger.cpp.o
In file included from ../src/core/data_logger.cpp:1:
../src/core/data_logger.hpp: In constructor ‘DataLogger::DataLogger(std::string, std::vector<std::__cxx11::basic_string<char> >, std::vector<std::__cxx11::basic_string<char> >, std::vector<int>, std::string)’:
../src/core/data_logger.hpp:38:22: warning: ‘DataLogger::gpio_pins_’ will be initialized after [-Wreorder]
   38 |     std::vector<int> gpio_pins_;
      |                      ^~~~~~~~~~
../src/core/data_logger.hpp:35:17: warning:   ‘std::string DataLogger::file_prefix_’ [-Wreorder]
   35 |     std::string file_prefix_;
      |                 ^~~~~~~~~~~~
../src/core/data_logger.cpp:14:1: warning:   when initialized here [-Wreorder]
   14 | DataLogger::DataLogger(std::string data_dir,
      | ^~~~~~~~~~
[27/27] Linking target novaMock




x=177, y=212 -> x=169, y=192
x=1002, y=980 -> x=994, y=960

