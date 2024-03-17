#include <iostream>
#include <daqhats/mcc128.h>
#include <daqhats/daqhats.h>
#include "mccdaq.h"
#include <vector>

/*
 * This file is entirely for testing out individual functions, and not for anything production-level
 * */

int main(){
	std::vector<int> list_of_daqs = initialize_daqs(); 
	for (int i = 0; i<(int)list_of_daqs.capacity(); i++){
		std::cout<<"DAQ no. " <<i+1<<": " << list_of_daqs[i]<<std::endl; 
	} 
	
	
	return 0; 
}
