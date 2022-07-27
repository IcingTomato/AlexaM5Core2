# Amazon Alexa for M5Core2

## 1. Introduction

## 2. Development Setup

This sections talks about setting up your development host, fetching the git repositories, and instructions for build and flash.

### 2.1 Host Setup

You should install drivers and support packages for your development host. Windows, Linux and Mac OS-X, are supported development hosts. Please see Get Started for the host setup instructions.

### 2.2 Getting the Repositories

```shell
git clone --recursive https://github.com/espressif/esp-idf.git

cd esp-idf; git checkout release/v4.2; git submodule init; git submodule update --init --recursive;

./install.sh

cd ..

git clone https://github.com/IcingTomato/AlexaM5Core2.git
```

### 2.3 Building the Firmware

```shell
cd AlexaM5Core2/examples/amazon_alexa/ 

export ESPPORT=/dev/ttyUSB0 (or /dev/ttycu.SLAB_USBtoUART macOS or COMxx on MinGW)

export IDF_PATH=/path/to/esp-idf

. $IDF_PATH/export.sh
```

Set audio_board path for M5Core2 and AWS_EDUKIT_PATH:

```shell
export AUDIO_BOARD_PATH=/path/to/AlexaM5Core2/components/audio_hal/audio_board/audio_board_m5_core2_aws
export AWS_EDUKIT_PATH=/path/to/AlexaM5Core2/components/core2forAWS
```

Menuconfig is avaliable, also you can change some components:

```shell
idf.py menuconfig
```

