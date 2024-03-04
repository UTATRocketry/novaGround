#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

#include <daqhats/daqhats.h>
#include <daqhats/mcc128.h>

#include "src/mccdaq.h"
#include "src/dummysensor.h"

int main() {
    int loop_upper_bound = 100;

    // std::vector<int> open_boards = initialize_daqs(); 
    // for (int i = 0; i<8; i++)
    // {
        
    // }

    std::cout << initialize_daqs()[0];

    // do{std::cout << "Number of MCC128s available: " << hat_list(HAT_ID_MCC_128, NULL) << "\n Press enter to continue . . . "<< std::endl;}
    // while (std::cin.get() != '\n');
    // mcc128_open(0);

    // for (int i = 1; i < loop_upper_bound; i++){

    //     std::cout<<"Sample "<< i <<std::endl;
    //     std::this_thread::sleep_for(std::chrono::milliseconds(100));

    //     for (int i = 0; i < 4; i++ ){
    //         std::cout << "Channel " << i << ": " << getDaqValue(0, i) << std::endl;
    //     }

    //     std::cout << "Dummy Sensor: " << dummysensor(0, 10) << std::endl << std::endl;
    // }

    // mcc128_close(0);
    return 0;
}
