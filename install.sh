#!/bin/bash
if [ "$(id -u)" != "0" ]; then
   echo "This script must be run as root, i.e 'sudo ./install.sh'" 1>&2
   exit 1
fi

# Update and upgrade the system
read -p "Would you like to update system first? (y/n): " update_sys

if [ "$update_sys" == "y" ]; then
    echo "Updating system..."
    sudo apt update && sudo apt upgrade -y
else
    echo "Skipping system updates..."
fi

# Install required packages

# build tools
echo "Installing build tools..."
sudo apt install build-essential clang meson python3 libbz2-dev libz-dev libicu-dev

# daq hats library
echo "Installing MCC DAQ..."
cd ~/Desktop
git clone https://github.com/mccdaq/daqhats.git
cd daqhats
sudo ./install.sh
cd ~

# boost library
echo "Installing boost..."
wget https://archives.boost.io/release/1.81.0/source/boost_1_81_0.tar.bz2
tar xf boost_1_81_0.tar.bz2
cd boost_1_81_0
./bootstrap.sh --prefix=/usr/local
./b2
sudo ./b2 install

echo 'export BOOST_ROOT=/usr/local' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
echo 'export CPLUS_INCLUDE_PATH=/usr/local/include:$CPLUS_INCLUDE_PATH' >> ~/.bashrc
cd ~

# WiringPi library
cd Desktop
echo "Installing WiringPi..."
# fetch the source
git clone https://github.com/WiringPi/WiringPi.git
cd WiringPi

# build the package
./build debian
mv debian-template/wiringpi_3.18_arm64.deb .

# install it
sudo apt install ./wiringpi-3.x.deb
cd ~

# others libraries
echo "Installing libraries..."
sudo apt-get install libpaho-mqtt-dev
sudo apt install libgpiod-dev
sudo apt-get install libcurl4-openssl-dev

echo "Installing utilities..."
sudo apt install tmux nmap 

# edit run script
# ./build/novaThermo --sample-ms 50 --broker 192.168.0.1 --backend 192.168.0.1:8000

# create service file
mv novaGround.service /etc/systemd/system
sudo systemctl daemon-reload
sudo systemctl enable novaGround.service
sudo systemctl start novaGround.service