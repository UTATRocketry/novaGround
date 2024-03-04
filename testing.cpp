#include <daqhats/daqhats.h>
#include <daqhats/mcc128.h>
#include "src/mccdaq.h"
#include "src/dummysensor.h"
#include <iostream>
#include

int main()
{
    do{
        std::cout<< "This is an ultra-important message!\n" <<"Press a key to continue . . . ";
    } while(std::cin.get() != '\n');
        ;
    return 0;

}
