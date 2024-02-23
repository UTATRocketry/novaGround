#include <iostream>
#include <thread>
#include <chrono>

#include <daqhats/daqhats.h>
#include <daqhats/mcc128.h>

#include "src/mccdaq.h"

int main() {
    int loop_upper_bound = 100;

    std::cout << hat_list(HAT_ID_MCC_128, NULL) << std::endl;
    mcc128_open(0);

    for (int i = 1; i < loop_upper_bound; i++){

        std::cout<<"Sample "<< i <<std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        for (int i = 0; i < 4; i++ ){
            std::cout << "Channel " << i << ": " << getDaqValue(0, i) << std::endl;
        }
    }

    mcc128_close(0);
    return 0;
}
