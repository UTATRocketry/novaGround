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
	 
	close_daqs(list_of_daqs); 

	
	return 0; 
}
